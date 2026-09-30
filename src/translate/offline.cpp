#include <winsock2.h>
#include <ws2tcpip.h>
#include "translate/offline.h"
#include "translate/http_client.h"
#include "update/http.h"
#include "update/manifest.h"
#include <windows.h>
#include <dxgi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace lumashot::translate::offline {
namespace fs = std::filesystem;
namespace {
constexpr std::array<Model, 3> ModelTable{{
    {"hy-mt1.5-1.8b", L"混元翻译 1.5 · 1.8B", L"腾讯混元", L"33 种语言互译，小巧快速，适合大多数电脑",
        "HY-MT1.5-1.8B-Q4_K_M.gguf", 1133080512ull, "4383ac0c3c8e476de98ff979c2a3f069f8c4fb385e7860cf2d28da896cc477c7",
        {"https://modelscope.cn/models/Tencent-Hunyuan/HY-MT1.5-1.8B-GGUF/resolve/master/HY-MT1.5-1.8B-Q4_K_M.gguf",
         "https://hf-mirror.com/tencent/HY-MT1.5-1.8B-GGUF/resolve/main/HY-MT1.5-1.8B-Q4_K_M.gguf",
         "https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF/resolve/main/HY-MT1.5-1.8B-Q4_K_M.gguf"},
        4.f, 1.5f, 33, Prompt::Hunyuan, L"腾讯混元社区许可协议", "https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF/blob/main/License.txt"},
    {"hy-mt1.5-7b", L"混元翻译 1.5 · 7B", L"腾讯混元", L"WMT25 冠军模型升级版，译文更地道，需要较高配置",
        "HY-MT1.5-7B-Q4_K_M.gguf", 4624649312ull, "fc87637e4dd29547811a28170770c2ac17725fb7690b7c4aafa4f463c3e77568",
        {"https://modelscope.cn/models/Tencent-Hunyuan/HY-MT1.5-7B-GGUF/resolve/master/HY-MT1.5-7B-Q4_K_M.gguf",
         "https://hf-mirror.com/tencent/HY-MT1.5-7B-GGUF/resolve/main/HY-MT1.5-7B-Q4_K_M.gguf",
         "https://huggingface.co/tencent/HY-MT1.5-7B-GGUF/resolve/main/HY-MT1.5-7B-Q4_K_M.gguf"},
        16.f, 5.5f, 33, Prompt::Hunyuan, L"腾讯混元社区许可协议", "https://huggingface.co/tencent/HY-MT1.5-7B-GGUF/blob/main/License.txt"},
    {"translategemma-4b", L"TranslateGemma · 4B", L"Google", L"55 种语言，小语种覆盖更广",
        "translategemma-4b-it.Q4_K_M.gguf", 2489909760ull, "81200d03e843d2ec1ece6eeafe7d13cb6e5211e1fcd336ade55790b683a08330",
        {"https://hf-mirror.com/mradermacher/translategemma-4b-it-GGUF/resolve/main/translategemma-4b-it.Q4_K_M.gguf",
         "https://huggingface.co/mradermacher/translategemma-4b-it-GGUF/resolve/main/translategemma-4b-it.Q4_K_M.gguf", ""},
        8.f, 3.5f, 55, Prompt::Gemma, L"Gemma 使用条款", "https://ai.google.dev/gemma/terms"},
}};

constexpr Runtime RuntimeInfo{"b11272", "llama-b11272-bin-win-vulkan-x64.zip", 33093350ull,
    "55d4946939147fb3c2403d06637d701d4f0a98acf0ee3616d62530e71d9a00bf",
    "https://github.com/ggml-org/llama.cpp/releases/download/b11272/llama-b11272-bin-win-vulkan-x64.zip"};

constexpr std::array<std::wstring_view, 7> Members{L"llama-server.exe", L"llama-server-impl.dll", L"llama-common.dll", L"llama.dll", L"mtmd.dll", L"libomp.dll", L"ggml*.dll"};

struct Handle {
    HANDLE value{};
    Handle() = default;
    explicit Handle(HANDLE h) : value(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
    ~Handle() { Close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void Close() { if (value) CloseHandle(value); value = nullptr; }
};

std::wstring Gb(std::uint64_t bytes) {
    wchar_t text[32]{};
    const double gb = double(bytes) / double(1ull << 30);
    if (gb >= 1) swprintf_s(text, L"%.1f GB", gb);
    else swprintf_s(text, L"%.0f MB", double(bytes) / double(1ull << 20));
    return text;
}

std::string Hex(const std::array<std::uint8_t, 32>& digest) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const auto b : digest) { out.push_back(digits[b >> 4]); out.push_back(digits[b & 15]); }
    return out;
}

std::string ReadSmall(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text.size() > 256 ? std::string() : text;
}
bool WriteSmall(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), std::streamsize(text.size()));
    return bool(out);
}

std::uint64_t FreeAt(const fs::path& folder) {
    fs::path probe = folder;
    std::error_code error;
    while (!probe.empty() && !fs::exists(probe, error)) probe = probe.parent_path();
    ULARGE_INTEGER free{};
    if (probe.empty() || !GetDiskFreeSpaceExW(probe.c_str(), &free, nullptr, nullptr)) return 0;
    return free.QuadPart;
}

bool HashPrefix(const fs::path& file, std::uint64_t bytes, update::Sha256& hash, std::stop_token stop) {
    Handle in(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!in.value) return false;
    std::vector<std::uint8_t> buffer(1u << 20);
    std::uint64_t left = bytes;
    while (left) {
        if (stop.stop_requested()) return false;
        DWORD read = 0;
        const DWORD want = DWORD(std::min<std::uint64_t>(left, buffer.size()));
        if (!ReadFile(in.value, buffer.data(), want, &read, nullptr) || read != want) return false;
        hash.Update(std::span<const std::uint8_t>(buffer.data(), read));
        left -= read;
    }
    return true;
}

std::wstring Problem(const update::http::Result& r) {
    using update::http::Status;
    switch (r.status) {
    case Status::HttpStatus: return L"HTTP " + std::to_wstring(r.http_status);
    case Status::Slow: return L"速度过慢";
    case Status::TooLarge: return L"文件大小不符";
    case Status::Sink: return L"文件大小不符";
    case Status::BadUrl: return L"地址无效";
    case Status::Network: return L"网络错误 " + std::to_wstring(r.error);
    default: return L"未知错误";
    }
}

std::wstring Utf16(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
    return out;
}

// Last meaningful line of the server log (prefers lines mentioning an error).
std::wstring LogTail(const fs::path& log) {
    std::ifstream in(log, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto size = std::streamoff(in.tellg());
    in.seekg(std::max<std::streamoff>(0, size - 8192));
    std::string text((std::istreambuf_iterator<char>(in)), {});
    std::string best, last;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) {
            last = line;
            std::string lower = line;
            for (auto& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find("error") != std::string::npos || lower.find("failed") != std::string::npos) best = line;
        }
        start = end + 1;
    }
    auto pick = Utf16(best.empty() ? last : best);
    if (pick.size() > 160) pick = pick.substr(0, 157) + L"…";
    return pick;
}

int FreePort() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) return 0;
    int port = 0;
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int length = sizeof(address);
        if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && getsockname(s, reinterpret_cast<sockaddr*>(&address), &length) == 0) port = ntohs(address.sin_port);
        closesocket(s);
    }
    WSACleanup();
    return port;
}

Result Fail(Result::Kind kind, std::wstring message) { Result r; r.kind = kind; r.message = std::move(message); return r; }

Result Extract(const fs::path& zip, const fs::path& destination, std::stop_token stop) {
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    const fs::path tar = fs::path(system) / L"tar.exe";
    std::error_code error;
    if (!fs::exists(tar, error)) return Fail(Result::Kind::Extract, L"系统缺少 tar.exe（需要 Windows 10 1803 或更高版本）");
    const fs::path staging = destination.native() + L".tmp";
    fs::remove_all(staging, error);
    fs::create_directories(staging, error);
    if (error) return Fail(Result::Kind::Disk, L"无法创建目录：" + staging.wstring());
    std::wstring command = L"\"" + tar.wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + staging.wstring() + L"\"";
    for (const auto member : Members) command += L" \"" + std::wstring(member) + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, staging.c_str(), &startup, &info))
        return Fail(Result::Kind::Extract, L"无法启动解压（" + std::to_wstring(GetLastError()) + L"）");
    Handle process(info.hProcess), thread(info.hThread);
    for (;;) {
        if (WaitForSingleObject(process.value, 200) == WAIT_OBJECT_0) break;
        if (stop.stop_requested()) { TerminateProcess(process.value, 1); WaitForSingleObject(process.value, 2000); fs::remove_all(staging, error); return Fail(Result::Kind::Canceled, L"已取消"); }
    }
    DWORD code = 1;
    GetExitCodeProcess(process.value, &code);
    if (code != 0 || !fs::exists(staging / L"llama-server.exe", error) || !fs::exists(staging / L"ggml-base.dll", error)) {
        fs::remove_all(staging, error);
        return Fail(Result::Kind::Extract, L"解压推理组件失败（tar 返回 " + std::to_wstring(code) + L"）");
    }
    fs::remove_all(destination, error);
    fs::rename(staging, destination, error);
    if (error) { fs::remove_all(staging, error); return Fail(Result::Kind::Disk, L"无法写入推理组件目录"); }
    WriteSmall(destination / L".verified", RuntimeInfo.build);
    return Fail(Result::Kind::Ok, {});
}

// ---------- server state ----------
struct ServerState {
    std::mutex mutex;
    HANDLE job{}, process{};
    std::string model, base;
    int active{};
    bool cpu_only{};
    PTP_TIMER timer{};
    void StopLocked() {
        if (job) TerminateJobObject(job, 0);
        if (process) { WaitForSingleObject(process, 3000); CloseHandle(process); }
        if (job) CloseHandle(job);
        job = process = nullptr;
        model.clear();
        base.clear();
    }
    ~ServerState() {
        if (timer) { SetThreadpoolTimer(timer, nullptr, 0, 0); WaitForThreadpoolTimerCallbacks(timer, TRUE); CloseThreadpoolTimer(timer); }
        StopLocked();
    }
};
ServerState& State() { static ServerState state; return state; }

void CALLBACK IdleTick(PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) {
    auto& s = State();
    std::lock_guard lock(s.mutex);
    if (s.active == 0 && s.process) s.StopLocked();
}

bool Healthy(const std::string& base, std::stop_token stop) {
    http::Request request;
    request.method = "GET";
    request.url = base + "/health";
    http::Limits limits;
    limits.connect = std::chrono::milliseconds(800);
    limits.receive = std::chrono::milliseconds(1500);
    limits.max_bytes = 64 * 1024;
    return http::Send(request, stop, limits).Success();
}
}

std::span<const Model> Models() { return ModelTable; }
const Model* FindModel(std::string_view id) {
    for (const auto& model : ModelTable) if (model.id == id) return &model;
    return nullptr;
}
const Runtime& RuntimePackage() { return RuntimeInfo; }
std::span<const std::wstring_view> RuntimeMembers() { return Members; }

fs::path Root() {
    wchar_t overridden[MAX_PATH]{};
    if (const DWORD n = GetEnvironmentVariableW(L"LUMASHOT_OFFLINE_ROOT", overridden, MAX_PATH); n && n < MAX_PATH) return fs::path(overridden);
    PWSTR folder = nullptr;
    fs::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) path = fs::path(folder) / L"LumaShot" / L"offline";
    CoTaskMemFree(folder);
    return path;
}
fs::path ModelPath(const Model& model) { return Root() / L"models" / fs::path(std::string(model.file)); }
fs::path RuntimeDirectory() { return Root() / L"runtime" / fs::path(std::string(RuntimeInfo.build)); }
fs::path ServerPath() { return RuntimeDirectory() / L"llama-server.exe"; }

bool ModelInstalled(const Model& model) {
    std::error_code error;
    const auto path = ModelPath(model);
    if (path.empty() || fs::file_size(path, error) != model.size || error) return false;
    return ReadSmall(path.native() + L".verified") == model.sha256;
}
bool RuntimeInstalled() {
    std::error_code error;
    const auto dir = RuntimeDirectory();
    return !dir.empty() && fs::exists(ServerPath(), error) && fs::exists(dir / L"ggml-base.dll", error) && ReadSmall(dir / L".verified") == RuntimeInfo.build;
}
std::uint64_t PartialBytes(const Model& model) {
    std::error_code error;
    const auto size = fs::file_size(ModelPath(model).native() + L".partial", error);
    return error || size >= model.size ? 0 : size;
}
std::uint64_t FreeBytes() { return FreeAt(Root()); }

Hardware Probe() {
    Hardware hw;
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) hw.ram = memory.ullTotalPhys;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            // Integrated GPUs report a small carve-out; they share system memory.
            const std::uint64_t dedicated = desc.DedicatedVideoMemory;
            if (dedicated >= (1ull << 30) && dedicated > hw.vram) { hw.vram = dedicated; hw.gpu = desc.Description; }
            else if (hw.gpu.empty() && !hw.vram) hw.gpu = desc.Description;
        }
    }
    return hw;
}

Fit Assess(const Model& model, const Hardware& hw) {
    const double gib = double(1ull << 30);
    const double ram = double(hw.ram) / gib, vram = double(hw.vram) / gib;
    if (vram + 0.05 >= model.vram_gb && ram + 0.5 >= std::min(model.ram_gb, 8.f)) return Fit::Fast;
    if (ram + 0.5 >= model.ram_gb) return Fit::Ok;
    // Partial GPU offload keeps a model usable below the CPU-only memory figure.
    if (ram + vram + 0.5 >= model.ram_gb * 0.75) return Fit::Slow;
    return Fit::TooLarge;
}

const Model& Recommend(const Hardware& hw) {
    if (Assess(ModelTable[1], hw) == Fit::Fast) return ModelTable[1];
    return ModelTable[0];
}

std::optional<std::array<std::uint8_t, 32>> ParseSha256(std::string_view hex) {
    if (hex.size() != 64) return std::nullopt;
    std::array<std::uint8_t, 32> out{};
    const auto nibble = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
    for (size_t i = 0; i < 32; ++i) {
        const int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = std::uint8_t(hi << 4 | lo);
    }
    return out;
}

Result Fetch(const Source& source, const fs::path& target, std::stop_token stop, const ByteProgress& progress, const FetchOptions& options) {
    std::error_code error;
    if (source.urls.empty() || !source.size) return Fail(Result::Kind::Network, L"没有可用的下载地址");
    fs::create_directories(target.parent_path(), error);
    if (error) return Fail(Result::Kind::Disk, L"无法创建目录：" + target.parent_path().wstring());
    const fs::path partial = target.native() + L".partial";
    std::uint64_t have = 0;
    if (fs::exists(partial, error)) {
        have = fs::file_size(partial, error);
        if (error || have >= source.size) { fs::remove(partial, error); have = 0; }
    }
    if (const auto free = FreeAt(target.parent_path()); free && free < source.size - have + (64ull << 20))
        return Fail(Result::Kind::Space, L"磁盘空间不足：还需要 " + Gb(source.size - have) + L"，" + target.root_name().wstring() + L" 仅剩 " + Gb(free));
    std::optional<update::Sha256> hash;
    try { hash.emplace(); } catch (const std::exception&) { return Fail(Result::Kind::Verify, L"系统无法计算 SHA-256"); }
    if (have && !HashPrefix(partial, have, *hash, stop)) {
        if (stop.stop_requested()) return Fail(Result::Kind::Canceled, L"已暂停");
        fs::remove(partial, error);
        have = 0;
        hash.emplace();
    }
    if (progress) progress(have, source.size);
    std::wstring last = L"下载失败";
    Result::Kind last_kind = Result::Kind::Network;
    for (size_t index = 0; index < source.urls.size(); ++index) {
        if (stop.stop_requested()) return Fail(Result::Kind::Canceled, L"已暂停");
        Handle file(CreateFileW(partial.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file.value) return Fail(Result::Kind::Disk, L"无法写入下载文件（" + std::to_wstring(GetLastError()) + L"）");
        LARGE_INTEGER offset{};
        offset.QuadPart = LONGLONG(have);
        if (!SetFilePointerEx(file.value, offset, nullptr, FILE_BEGIN) || !SetEndOfFile(file.value)) return Fail(Result::Kind::Disk, L"无法写入下载文件");
        bool disk_error = false;
        update::http::Options http_options;
        http_options.connect_timeout = options.connect;
        http_options.receive_timeout = std::chrono::seconds(30);
        http_options.min_bytes_per_second = index + 1 < source.urls.size() ? options.min_rate : 0;
        http_options.throughput_window = options.window;
        http_options.allow_loopback_http = options.allow_loopback_http;
        http_options.range_start = have;
        http_options.on_response = [&](bool resumed, std::uint64_t total) {
            if (!resumed && have) {
                // The mirror ignored Range: start over from byte zero.
                have = 0;
                hash.emplace();
                LARGE_INTEGER zero{};
                if (!SetFilePointerEx(file.value, zero, nullptr, FILE_BEGIN) || !SetEndOfFile(file.value)) { disk_error = true; return false; }
            }
            return !total || total == source.size;
        };
        const auto fetched = update::http::Get(source.urls[index], stop, source.size, [&](std::span<const std::uint8_t> chunk) {
            if (have + chunk.size() > source.size) return false;
            DWORD written = 0;
            if (!WriteFile(file.value, chunk.data(), DWORD(chunk.size()), &written, nullptr) || written != chunk.size()) { disk_error = true; return false; }
            hash->Update(chunk);
            have += chunk.size();
            if (progress) progress(have, source.size);
            return true;
        }, {}, http_options);
        const bool flushed = FlushFileBuffers(file.value) != FALSE;
        file.Close();
        if (disk_error) return Fail(Result::Kind::Disk, L"写入磁盘失败，请检查剩余空间");
        if (fetched.status == update::http::Status::Canceled || stop.stop_requested()) return Fail(Result::Kind::Canceled, L"已暂停");
        if (have == source.size && flushed) {
            const auto digest = hash->Finish();
            if (digest == source.sha256) {
                if (!MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return Fail(Result::Kind::Disk, L"无法保存下载文件（" + std::to_wstring(GetLastError()) + L"）");
                return Fail(Result::Kind::Ok, {});
            }
            // Corrupt or tampered data: discard and try the next source from zero.
            fs::remove(partial, error);
            have = 0;
            hash.emplace();
            last = L"文件校验失败（SHA-256 不一致），已丢弃";
            last_kind = Result::Kind::Verify;
            continue;
        }
        last = L"下载中断（" + Problem(fetched) + L"）";
        last_kind = Result::Kind::Network;
    }
    return Fail(last_kind, last_kind == Result::Kind::Network ? last + L"，已下载部分会保留，稍后可继续" : last);
}

Result Install(const Model& model, std::stop_token stop, const std::function<void(const Progress&)>& progress) {
    const auto report = [&](Progress::Stage stage, std::uint64_t done, std::uint64_t total) { if (progress) progress({stage, done, total}); };
    if (Root().empty()) return Fail(Result::Kind::Disk, L"找不到本地应用数据目录");
    if (!RuntimeInstalled()) {
        Source source;
        source.urls.push_back(std::string(RuntimeInfo.url));
        for (const auto& mirror : update::DefaultMirrors()) source.urls.push_back(update::ViaMirror(mirror, RuntimeInfo.url));
        source.size = RuntimeInfo.size;
        source.sha256 = *ParseSha256(RuntimeInfo.sha256);
        const auto zip = Root() / L"downloads" / fs::path(std::string(RuntimeInfo.file));
        report(Progress::Stage::Runtime, 0, RuntimeInfo.size);
        auto result = Fetch(source, zip, stop, [&](std::uint64_t done, std::uint64_t total) { report(Progress::Stage::Runtime, done, total); });
        if (!result.ok()) { if (result.kind != Result::Kind::Canceled) result.message = L"推理组件：" + result.message; return result; }
        report(Progress::Stage::Extract, 0, 0);
        result = Extract(zip, RuntimeDirectory(), stop);
        std::error_code error;
        fs::remove(zip, error);
        if (!result.ok()) return result;
    }
    if (!ModelInstalled(model)) {
        Source source;
        for (const auto url : model.urls) if (!url.empty()) source.urls.push_back(std::string(url));
        source.size = model.size;
        source.sha256 = *ParseSha256(model.sha256);
        report(Progress::Stage::Verify, 0, model.size);
        const auto path = ModelPath(model);
        auto result = Fetch(source, path, stop, [&](std::uint64_t done, std::uint64_t total) { report(Progress::Stage::Model, done, total); });
        if (!result.ok()) return result;
        if (!WriteSmall(path.native() + L".verified", model.sha256)) return Fail(Result::Kind::Disk, L"无法写入校验标记");
    }
    return Fail(Result::Kind::Ok, {});
}

bool Remove(const Model& model) {
    {
        auto& s = State();
        std::lock_guard lock(s.mutex);
        if (s.model == model.id) s.StopLocked();
    }
    std::error_code error;
    const auto path = ModelPath(model);
    fs::remove(path.native() + L".verified", error);
    fs::remove(path.native() + L".partial", error);
    fs::remove(path, error);
    return !fs::exists(path, error);
}

std::wstring ServerCommandLine(const fs::path& server, const fs::path& model, int port, bool cpu_only, const fs::path& log) {
    std::wstring command = L"\"" + server.wstring() + L"\" -m \"" + model.wstring() + L"\" --host 127.0.0.1 --port " + std::to_wstring(port)
        + L" -c 4096 -np 1 --no-webui --log-file \"" + log.wstring() + L"\"";
    if (cpu_only) command += L" -dev none";
    return command;
}

std::optional<std::string> Acquire(const Model& model, std::stop_token stop, std::wstring* reason) {
    const auto fail = [&](std::wstring text) -> std::optional<std::string> { if (reason) *reason = std::move(text); return std::nullopt; };
    auto& s = State();
    std::lock_guard lock(s.mutex);
    if (s.process && WaitForSingleObject(s.process, 0) == WAIT_TIMEOUT && s.model == model.id) { ++s.active; return s.base; }
    s.StopLocked();
    if (!RuntimeInstalled()) return fail(L"离线翻译组件还没有下载，请打开引擎设置下载");
    if (!ModelInstalled(model)) return fail(L"离线模型「" + std::wstring(model.name) + L"」还没有下载，请打开引擎设置下载");
    const auto log = Root() / L"server.log";
    std::wstring last;
    // A broken GPU driver can crash the Vulkan backend; retry once on the CPU.
    for (int attempt = s.cpu_only ? 1 : 0; attempt < 2; ++attempt) {
        const bool cpu = attempt == 1;
        const int port = FreePort();
        if (!port) return fail(L"找不到可用的本机端口");
        std::error_code error;
        fs::remove(log, error);
        auto command = ServerCommandLine(ServerPath(), ModelPath(model), port, cpu, log);
        Handle job(CreateJobObjectW(nullptr, nullptr));
        if (!job.value) return fail(L"无法创建进程作业");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION info{};
        const auto folder = RuntimeDirectory();
        if (!CreateProcessW(ServerPath().c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, folder.c_str(), &startup, &info))
            return fail(L"无法启动离线引擎（" + std::to_wstring(GetLastError()) + L"）");
        Handle thread(info.hThread);
        if (!AssignProcessToJobObject(job.value, info.hProcess)) { TerminateProcess(info.hProcess, 1); CloseHandle(info.hProcess); return fail(L"无法管理离线引擎进程"); }
        ResumeThread(info.hThread);
        s.job = job.value; job.value = nullptr;
        s.process = info.hProcess;
        s.base = "http://127.0.0.1:" + std::to_string(port);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
        bool ready = false, exited = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (stop.stop_requested()) { s.StopLocked(); return fail(L"已取消"); }
            if (Healthy(s.base, stop)) { ready = true; break; }
            if (WaitForSingleObject(s.process, 250) == WAIT_OBJECT_0) { exited = true; break; }
        }
        if (ready) { s.model = std::string(model.id); s.cpu_only = cpu; ++s.active; return s.base; }
        s.StopLocked();
        last = LogTail(log);
        if (!exited) return fail(L"离线引擎启动超时（3 分钟），请关闭占用内存的程序后重试");
    }
    return fail(L"离线引擎无法启动" + (last.empty() ? std::wstring() : L"：" + last));
}

void Release() {
    auto& s = State();
    std::lock_guard lock(s.mutex);
    if (s.active > 0) --s.active;
    if (!s.timer) s.timer = CreateThreadpoolTimer(IdleTick, nullptr, nullptr);
    if (!s.timer) return;
    ULARGE_INTEGER due{};
    due.QuadPart = ULONGLONG(-LONGLONG(std::chrono::duration_cast<std::chrono::milliseconds>(IdleShutdown).count()) * 10000);
    FILETIME time{due.LowPart, due.HighPart};
    SetThreadpoolTimer(s.timer, &time, 0, 1000);
}

void Shutdown() {
    auto& s = State();
    std::lock_guard lock(s.mutex);
    s.StopLocked();
}

bool ServerRunning() {
    auto& s = State();
    std::lock_guard lock(s.mutex);
    return s.process && WaitForSingleObject(s.process, 0) == WAIT_TIMEOUT;
}
}
