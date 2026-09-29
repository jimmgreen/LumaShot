#include "update/updater.h"
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <condition_variable>
#include <utility>

namespace lumashot::update {
namespace {
struct File {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~File() { Close(); }
    void Close() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); value = INVALID_HANDLE_VALUE; }
};

void AddUnique(std::vector<std::string>& list, const std::string& value) {
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

std::optional<Version> InstallerVersion(const std::filesystem::path& file) {
    // LumaShot-Setup-<major.minor.patch>.exe
    const auto name = file.filename().wstring();
    constexpr std::wstring_view prefix = L"LumaShot-Setup-", suffix = L".exe";
    if (name.size() <= prefix.size() + suffix.size() || !name.starts_with(prefix) || !name.ends_with(suffix)) return std::nullopt;
    std::string text;
    for (const wchar_t c : name.substr(prefix.size(), name.size() - prefix.size() - suffix.size())) {
        if (c > 0x7f) return std::nullopt;
        text.push_back(static_cast<char>(c));
    }
    return ParseVersion(text);
}
}

Endpoints ReleaseEndpoints(std::span<const std::string> remembered_mirrors) {
    Endpoints endpoints;
    endpoints.key.assign(ReleasePublicKey().begin(), ReleasePublicKey().end());
    endpoints.mirrors = DefaultMirrors();
    for (const auto& mirror : remembered_mirrors) if (ValidMirror(mirror)) AddUnique(endpoints.mirrors, mirror);
    return endpoints;
}

CheckResult Check(const Endpoints& endpoints, std::stop_token stop) {
    struct Response { std::string source; std::chrono::steady_clock::duration latency{}; std::optional<Manifest> manifest; bool reached{}, rejected{}; bool done{}; };
    std::vector<std::string> sources{""};
    for (const auto& mirror : endpoints.mirrors) AddUnique(sources, mirror);
    std::vector<Response> responses(sources.size());
    std::mutex guard;
    std::condition_variable_any changed;
    std::stop_source cancel;
    std::stop_callback forward(stop, [&] { cancel.request_stop(); changed.notify_all(); });
    const auto started = std::chrono::steady_clock::now();
    {
        std::vector<std::jthread> workers;
        workers.reserve(sources.size());
        for (std::size_t i = 0; i < sources.size(); ++i) {
            workers.emplace_back([&, i] {
                Response response;
                response.source = sources[i];
                std::vector<std::uint8_t> bytes;
                const auto fetched = http::GetBytes(ViaMirror(sources[i], endpoints.manifest_url), cancel.get_token(), kMaxManifestBytes, bytes, endpoints.manifest_http);
                response.latency = std::chrono::steady_clock::now() - started;
                if (fetched.status == http::Status::Ok) {
                    response.reached = true;
                    ManifestError error{};
                    response.manifest = ParseSignedManifest(bytes, endpoints.key, &error);
                    response.rejected = !response.manifest;
                }
                response.done = true;
                std::lock_guard lock(guard);
                responses[i] = std::move(response);
                changed.notify_all();
            });
        }
        std::unique_lock lock(guard);
        std::optional<std::chrono::steady_clock::time_point> first_valid;
        for (;;) {
            if (cancel.stop_requested()) break;
            const bool all = std::all_of(responses.begin(), responses.end(), [](const Response& r) { return r.done; });
            if (all) break;
            if (!first_valid && std::any_of(responses.begin(), responses.end(), [](const Response& r) { return r.manifest.has_value(); }))
                first_valid = std::chrono::steady_clock::now();
            auto until = started + endpoints.check_deadline;
            if (first_valid) until = std::min(until, *first_valid + endpoints.settle);
            if (std::chrono::steady_clock::now() >= until) break;
            changed.wait_until(lock, until);
        }
        lock.unlock();
        cancel.request_stop();  // abandon slow sources; workers return promptly
    }
    CheckResult result;
    result.attempted = static_cast<unsigned>(sources.size());
    std::vector<const Response*> valid;
    for (const auto& response : responses) {
        result.reachable += response.reached;
        result.rejected += response.rejected;
        if (response.manifest) valid.push_back(&response);
    }
    std::sort(valid.begin(), valid.end(), [](const Response* a, const Response* b) { return a->latency < b->latency; });
    for (const auto* response : valid) {
        result.sources.push_back(response->source);
        if (!result.manifest || response->manifest->version > result.manifest->version) result.manifest = response->manifest;
    }
    if (!result.manifest) result.kind = stop.stop_requested() ? CheckResult::Kind::Canceled : CheckResult::Kind::Failed;
    else result.kind = result.manifest->version > endpoints.current ? CheckResult::Kind::Available : CheckResult::Kind::UpToDate;
    return result;
}

std::filesystem::path InstallerPath(const std::filesystem::path& directory, const Manifest& manifest) {
    return directory / (L"LumaShot-Setup-" + VersionText(manifest.version) + L".exe");
}

bool VerifyInstaller(const std::filesystem::path& file, const Manifest& manifest, std::stop_token stop) {
    File handle;
    handle.value = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle.value == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle.value, &size) || static_cast<std::uint64_t>(size.QuadPart) != manifest.size) return false;
    try {
        Sha256 hash;
        std::vector<std::uint8_t> buffer(1 << 20);
        for (;;) {
            if (stop.stop_requested()) return false;
            DWORD read = 0;
            if (!ReadFile(handle.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) return false;
            if (!read) break;
            hash.Update(std::span<const std::uint8_t>(buffer.data(), read));
        }
        return hash.Finish() == manifest.sha256;
    } catch (const std::exception&) {
        return false;
    }
}

DownloadResult Download(const Endpoints& endpoints, const Manifest& manifest, std::span<const std::string> preferred,
    const std::filesystem::path& directory, std::stop_token stop, const Progress& progress) {
    DownloadResult result;
    const auto target = InstallerPath(directory, manifest);
    if (VerifyInstaller(target, manifest, stop)) { result.kind = DownloadResult::Kind::Ok; result.file = target; return result; }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) { result.kind = DownloadResult::Kind::Disk; return result; }
    // Sources that just served a valid manifest go first (fastest first), then
    // the signed mirror list, the built-in list, and the direct URL last.
    std::vector<std::string> candidates(preferred.begin(), preferred.end());
    for (const auto& mirror : manifest.mirrors) AddUnique(candidates, mirror);
    for (const auto& mirror : endpoints.mirrors) AddUnique(candidates, mirror);
    AddUnique(candidates, "");
    const auto partial = std::filesystem::path(target.native() + L".partial");
    const auto url = endpoints.asset_url(manifest);
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (stop.stop_requested()) break;
        ++result.attempted;
        bool disk_error = false;
        {
            File file;
            file.value = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (file.value == INVALID_HANDLE_VALUE) { result.kind = DownloadResult::Kind::Disk; return result; }
            Sha256 hash;
            auto options = endpoints.download_http;
            // The last source is allowed to be slow; earlier ones yield to the next.
            options.min_bytes_per_second = index + 1 < candidates.size() ? endpoints.min_download_rate : 0;
            if (progress) progress(0, manifest.size);
            const auto fetched = http::Get(ViaMirror(candidates[index], url), stop, manifest.size, [&](std::span<const std::uint8_t> chunk) {
                DWORD written = 0;
                if (!WriteFile(file.value, chunk.data(), static_cast<DWORD>(chunk.size()), &written, nullptr) || written != chunk.size()) { disk_error = true; return false; }
                hash.Update(chunk);
                return true;
            }, [&](std::uint64_t received, std::uint64_t) { if (progress) progress(received, manifest.size); }, options);
            const bool flushed = fetched.status == http::Status::Ok && FlushFileBuffers(file.value);
            file.Close();
            if (fetched.status == http::Status::Canceled) break;
            if (disk_error) { std::filesystem::remove(partial, error); result.kind = DownloadResult::Kind::Disk; return result; }
            if (flushed && fetched.bytes == manifest.size) {
                if (hash.Finish() == manifest.sha256) {
                    if (!MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                        std::filesystem::remove(partial, error);
                        result.kind = DownloadResult::Kind::Disk;
                        return result;
                    }
                    result.kind = DownloadResult::Kind::Ok;
                    result.file = target;
                    result.source = candidates[index];
                    return result;
                }
                ++result.corrupt;  // a proxy served altered bytes; never trust it
            }
        }
        std::filesystem::remove(partial, error);
    }
    std::filesystem::remove(partial, error);
    result.kind = stop.stop_requested() ? DownloadResult::Kind::Canceled : DownloadResult::Kind::Failed;
    return result;
}

void PruneDownloads(const std::filesystem::path& directory, const Version& current) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        const auto& path = it->path();
        const auto version = InstallerVersion(path);
        const bool stale = path.extension() == L".partial" || (version && *version <= current);
        if (stale) { std::error_code ignored; std::filesystem::remove(path, ignored); }
    }
}

std::filesystem::path DownloadDirectory() {
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) return {};
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path / L"LumaShot" / L"Updates";
}

bool LaunchInstaller(const std::filesystem::path& installer) {
    // ShellExecuteEx (not CreateProcess) so a per-machine install can still elevate.
    const std::wstring parameters = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCANCEL";
    const auto directory = installer.parent_path();
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"open";
    info.lpFile = installer.c_str();
    info.lpParameters = parameters.c_str();
    info.lpDirectory = directory.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) return false;
    if (info.hProcess) {
        AllowSetForegroundWindow(GetProcessId(info.hProcess));
        CloseHandle(info.hProcess);
    }
    return true;
}

Updater::Updater(HWND window, UINT message) : window_(window), message_(message) {}
Updater::~Updater() {
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
}

void Updater::Run(Phase phase, std::function<void(std::stop_token)> work) {
    if (worker_.joinable()) worker_.join();  // previous job already posted its result
    phase_ = phase;
    worker_ = std::jthread([work = std::move(work)](std::stop_token stop) { work(stop); });
}

bool Updater::Check(Endpoints endpoints) {
    if (phase_ != Phase::Idle) return false;
    Run(Phase::Checking, [this, endpoints = std::move(endpoints)](std::stop_token stop) {
        CheckResult result;
        try { PruneDownloads(DownloadDirectory(), endpoints.current); result = update::Check(endpoints, stop); } catch (const std::exception&) { result.kind = CheckResult::Kind::Failed; }
        { std::lock_guard lock(mutex_); check_ = std::move(result); }
        PostMessageW(window_, message_, static_cast<WPARAM>(Notification::CheckDone), 0);
    });
    return true;
}

bool Updater::Download(Endpoints endpoints, Manifest manifest, std::vector<std::string> preferred) {
    if (phase_ != Phase::Idle) return false;
    { std::lock_guard lock(mutex_); received_ = 0; total_ = manifest.size; last_percent_ = 101; }
    Run(Phase::Downloading, [this, endpoints = std::move(endpoints), manifest = std::move(manifest), preferred = std::move(preferred)](std::stop_token stop) {
        const auto directory = DownloadDirectory();
        DownloadResult result;
        if (directory.empty()) result.kind = DownloadResult::Kind::Disk;
        else try { result = update::Download(endpoints, manifest, preferred, directory, stop, [this](std::uint64_t received, std::uint64_t total) {
            bool post = false;
            {
                std::lock_guard lock(mutex_);
                received_ = received;
                total_ = total;
                const auto percent = total ? static_cast<unsigned>(received * 100 / total) : 0u;
                if (percent != last_percent_) { last_percent_ = percent; post = true; }
            }
            if (post) PostMessageW(window_, message_, static_cast<WPARAM>(Notification::Progress), 0);
        }); } catch (const std::exception&) { result = {}; result.kind = stop.stop_requested() ? DownloadResult::Kind::Canceled : DownloadResult::Kind::Failed; }
        { std::lock_guard lock(mutex_); download_ = std::move(result); }
        PostMessageW(window_, message_, static_cast<WPARAM>(Notification::DownloadDone), 0);
    });
    return true;
}

bool Updater::Verify(std::filesystem::path file, Manifest manifest) {
    if (phase_ != Phase::Idle) return false;
    Run(Phase::Verifying, [this, file = std::move(file), manifest = std::move(manifest)](std::stop_token stop) {
        const bool ok = VerifyInstaller(file, manifest, stop);
        { std::lock_guard lock(mutex_); verified_ = ok; }
        PostMessageW(window_, message_, static_cast<WPARAM>(Notification::VerifyDone), 0);
    });
    return true;
}

void Updater::Cancel() { worker_.request_stop(); }

CheckResult Updater::TakeCheck() {
    std::lock_guard lock(mutex_);
    phase_ = Phase::Idle;
    return std::exchange(check_, {});
}
DownloadResult Updater::TakeDownload() {
    std::lock_guard lock(mutex_);
    phase_ = Phase::Idle;
    return std::exchange(download_, {});
}
bool Updater::TakeVerify() {
    std::lock_guard lock(mutex_);
    phase_ = Phase::Idle;
    return std::exchange(verified_, false);
}
std::pair<std::uint64_t, std::uint64_t> Updater::DownloadProgress() const {
    std::lock_guard lock(mutex_);
    return {received_, total_};
}
}
