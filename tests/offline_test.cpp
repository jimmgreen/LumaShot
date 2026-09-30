// Offline translation: catalog, hardware fit, command line, install markers
// and the resumable, verified downloader against a loopback HTTP server.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include "translate/engine.h"
#include "translate/offline.h"
#include "update/manifest.h"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace lumashot::translate;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void Expect(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++failures;
}

// Minimal HTTP/1.1 file server: honours "Range: bytes=N-" unless told not to.
class Server {
public:
    std::map<std::string, std::string> files;
    std::atomic<bool> honor_range{true};
    Server() {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        int size = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
        port_ = ntohs(address.sin_port);
        listen(listener_, 8);
        thread_ = std::jthread([this](std::stop_token stop) { Loop(stop); });
    }
    ~Server() { thread_.request_stop(); thread_.join(); closesocket(listener_); WSACleanup(); }
    std::string Url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
    std::vector<std::string> Ranges() { std::lock_guard lock(mutex_); return ranges_; }
    void ClearRanges() { std::lock_guard lock(mutex_); ranges_.clear(); }
private:
    SOCKET listener_{INVALID_SOCKET};
    int port_{};
    std::jthread thread_;
    std::mutex mutex_;
    std::vector<std::string> ranges_;
    void Loop(std::stop_token stop) {
        while (!stop.stop_requested()) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(listener_, &set);
            timeval wait{0, 50000};
            if (select(0, &set, nullptr, nullptr, &wait) <= 0) continue;
            const SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            Serve(client);
            shutdown(client, SD_SEND);
            closesocket(client);
        }
    }
    static void SendAll(SOCKET s, const char* data, size_t size) {
        while (size) { const int n = send(s, data, int(std::min<size_t>(size, 1 << 16)), 0); if (n <= 0) return; data += n; size -= size_t(n); }
    }
    void Serve(SOCKET client) {
        std::string request;
        char buffer[2048];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const int n = recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0) return;
            request.append(buffer, size_t(n));
        }
        const auto path_start = request.find(' ') + 1;
        const std::string path = request.substr(path_start, request.find(' ', path_start) - path_start);
        std::string range;
        if (const auto at = request.find("Range: bytes="); at != std::string::npos) range = request.substr(at + 13, request.find("\r\n", at) - at - 13);
        { std::lock_guard lock(mutex_); ranges_.push_back(range); }
        const auto it = files.find(path);
        if (it == files.end()) { const std::string r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; SendAll(client, r.data(), r.size()); return; }
        const std::string& body = it->second;
        size_t start = 0;
        std::string head;
        if (!range.empty() && honor_range) {
            start = size_t(std::stoull(range));
            head = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + std::to_string(start) + "-" + std::to_string(body.size() - 1) + "/" + std::to_string(body.size()) + "\r\n";
        } else head = "HTTP/1.1 200 OK\r\n";
        head += "Content-Length: " + std::to_string(body.size() - start) + "\r\nConnection: close\r\n\r\n";
        SendAll(client, head.data(), head.size());
        SendAll(client, body.data() + start, body.size() - start);
    }
};

std::string Payload(size_t size, unsigned seed) {
    std::string out(size, '\0');
    for (size_t i = 0; i < size; ++i) out[i] = char((i * 31 + seed * 7 + (i >> 11)) & 255);
    return out;
}
std::array<std::uint8_t, 32> Digest(const std::string& data) {
    lumashot::update::Sha256 hash;
    hash.Update(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
    return hash.Finish();
}
std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void Write(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(data.data(), std::streamsize(data.size()));
}
// A file of the model's exact size without writing gigabytes.
bool Sparse(const fs::path& path, std::uint64_t size) {
    fs::create_directories(path.parent_path());
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0;
    DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
    LARGE_INTEGER end{};
    end.QuadPart = LONGLONG(size);
    const bool ok = SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file);
    CloseHandle(file);
    return ok;
}

void Catalog() {
    const auto models = offline::Models();
    Expect(models.size() == 3, "three offline models");
    std::set<std::string_view> ids;
    for (const auto& m : models) {
        ids.insert(m.id);
        Expect(m.sha256.size() == 64 && offline::ParseSha256(m.sha256).has_value(), "model sha256 is 64 hex digits");
        Expect(m.size > (1ull << 30) && !m.urls[0].empty() && m.urls[0].starts_with("https://") && m.file.ends_with(".gguf"), "model has size, https source and gguf file");
        Expect(m.ram_gb > m.vram_gb && m.languages >= 33 && !m.license.empty(), "model requirements and license present");
    }
    Expect(ids.size() == models.size(), "model ids unique");
    Expect(offline::FindModel(offline::DefaultModel) == &models[0] && !offline::FindModel("nope"), "default model is first; unknown id rejected");
    Expect(offline::FindModel("translategemma-4b") && offline::FindModel("translategemma-4b")->prompt == offline::Prompt::Gemma, "translategemma uses the gemma prompt");
    const auto& runtime = offline::RuntimePackage();
    Expect(runtime.url.starts_with("https://github.com/ggml-org/llama.cpp/releases/download/" + std::string(runtime.build) + "/") && offline::ParseSha256(runtime.sha256), "runtime package pinned to a llama.cpp release");
    const auto* preset = FindPreset("offline");
    Expect(preset && preset->local && preset->model == offline::DefaultModel && Presets().front().id == "offline", "offline preset listed first with the default model");
}

void Sha() {
    const auto parsed = offline::ParseSha256("4383AC0C3C8E476DE98FF979C2A3F069F8C4FB385E7860CF2D28DA896CC477C7");
    Expect(parsed && (*parsed)[0] == 0x43 && (*parsed)[31] == 0xc7, "sha256 parses upper case");
    Expect(!offline::ParseSha256("abc") && !offline::ParseSha256(std::string(63, 'a') + "g"), "bad sha256 rejected");
}

void Fit() {
    const auto gib = 1ull << 30;
    const auto& small_model = *offline::FindModel("hy-mt1.5-1.8b");
    const auto& big = *offline::FindModel("hy-mt1.5-7b");
    const auto& gemma = *offline::FindModel("translategemma-4b");
    const offline::Hardware gamer{16 * gib, 8 * gib, L"GPU"};
    Expect(offline::Assess(big, gamer) == offline::Fit::Fast && &offline::Recommend(gamer) == &big, "8 GB GPU: 7B runs fast and is recommended");
    const offline::Hardware office{8 * gib, 0, L""};
    Expect(offline::Assess(small_model, office) == offline::Fit::Ok && offline::Assess(gemma, office) == offline::Fit::Ok, "8 GB RAM, no GPU: 1.8B and 4B run on the CPU");
    Expect(offline::Assess(big, office) == offline::Fit::TooLarge && &offline::Recommend(office) == &small_model, "8 GB RAM: 7B too large, 1.8B recommended");
    const offline::Hardware laptop{16 * gib, 3 * gib + gib / 10, L"GTX 1060 3GB"};
    Expect(offline::Assess(small_model, laptop) == offline::Fit::Fast && offline::Assess(big, laptop) == offline::Fit::Ok, "3 GB GPU: 1.8B fast, 7B on CPU");
    const offline::Hardware tight{6 * gib, 0, L""};
    Expect(offline::Assess(gemma, tight) == offline::Fit::Slow, "6 GB RAM: 4B slow");
}

void CommandLine() {
    const auto gpu = offline::ServerCommandLine(L"C:\\r\\llama-server.exe", L"C:\\m\\a b.gguf", 5123, false, L"C:\\o\\server.log");
    Expect(gpu.find(L"-m \"C:\\m\\a b.gguf\"") != std::wstring::npos && gpu.find(L"--host 127.0.0.1 --port 5123") != std::wstring::npos && gpu.find(L"--no-webui") != std::wstring::npos && gpu.find(L"-dev none") == std::wstring::npos, "server binds loopback, quotes paths");
    Expect(offline::ServerCommandLine(L"s", L"m", 1, true, L"l").ends_with(L" -dev none"), "cpu fallback disables devices");
}

void Markers(const fs::path& root) {
    SetEnvironmentVariableW(L"LUMASHOT_OFFLINE_ROOT", root.c_str());
    Expect(offline::Root() == root, "LUMASHOT_OFFLINE_ROOT overrides the folder");
    const auto& m = *offline::FindModel(offline::DefaultModel);
    Expect(!offline::ModelInstalled(m) && !offline::RuntimeInstalled(), "nothing installed in an empty folder");
    Config config;
    config.provider = "offline";
    std::wstring reason;
    Expect(!Resolve(config, &reason) && reason.find(L"还没有下载") != std::wstring::npos, "resolve explains the missing download");
    Expect(Sparse(offline::ModelPath(m), m.size), "sparse model file");
    Expect(!offline::ModelInstalled(m), "size alone is not enough");
    Write(offline::ModelPath(m).native() + L".verified", std::string(m.sha256));
    Expect(offline::ModelInstalled(m), "size + verified marker = installed");
    Write(offline::ServerPath(), "stub");
    Write(offline::RuntimeDirectory() / L"ggml-base.dll", "stub");
    Write(offline::RuntimeDirectory() / L".verified", std::string(offline::RuntimePackage().build));
    Expect(offline::RuntimeInstalled(), "runtime marker");
    auto resolved = Resolve(config, &reason);
    Expect(resolved && resolved->model == offline::DefaultModel && resolved->key.empty(), "installed offline engine resolves without a key");
    config.Edit("offline").model = "translategemma-4b";
    Expect(!Resolve(config, &reason) && reason.find(L"TranslateGemma") != std::wstring::npos, "each model checked separately");
    Write(offline::ModelPath(m).native() + L".partial", "12345");
    Expect(offline::Remove(m) && !offline::ModelInstalled(m) && !fs::exists(offline::ModelPath(m).native() + L".partial"), "remove deletes model, marker and partial");
}

void Downloads(const fs::path& root) {
    Server server;
    const std::string good = Payload(700 * 1024 + 123, 1), bad = Payload(good.size(), 2);
    server.files["/good"] = good;
    server.files["/good2"] = good;
    server.files["/bad"] = bad;
    offline::Source source;
    source.size = good.size();
    source.sha256 = Digest(good);
    offline::FetchOptions options;
    options.allow_loopback_http = true;
    options.min_rate = 0;
    options.connect = std::chrono::seconds(3);
    const fs::path target = root / L"dl" / L"file.bin";
    const fs::path partial = target.native() + L".partial";
    std::stop_source never;
    std::uint64_t last = 0, reports = 0;
    bool monotonic = true;
    const auto progress = [&](std::uint64_t done, std::uint64_t total) { monotonic &= done >= last && total == good.size(); last = done; ++reports; };

    source.urls = {server.Url("/good")};
    auto result = offline::Fetch(source, target, never.get_token(), progress, options);
    Expect(result.ok() && Read(target) == good && !fs::exists(partial), "full download verified and moved into place");
    Expect(monotonic && last == good.size() && reports > 1, "progress is monotonic and reaches the total");

    fs::remove(target);
    Write(partial, good.substr(0, 300000));
    server.ClearRanges();
    result = offline::Fetch(source, target, never.get_token(), {}, options);
    const auto ranges = server.Ranges();
    Expect(result.ok() && Read(target) == good && ranges.size() == 1 && ranges[0] == "300000-", "resume continues from the partial file");

    fs::remove(target);
    Write(partial, good.substr(0, 300000));
    server.honor_range = false;
    result = offline::Fetch(source, target, never.get_token(), {}, options);
    server.honor_range = true;
    Expect(result.ok() && Read(target) == good, "server ignoring Range restarts cleanly");

    fs::remove(target);
    Write(partial, bad.substr(0, 300000));
    source.urls = {server.Url("/good"), server.Url("/good2")};
    result = offline::Fetch(source, target, never.get_token(), {}, options);
    Expect(result.ok() && Read(target) == good, "corrupt partial fails verification, next source restarts from zero");

    fs::remove(target);
    source.urls = {server.Url("/missing"), server.Url("/bad"), server.Url("/good")};
    result = offline::Fetch(source, target, never.get_token(), {}, options);
    Expect(result.ok() && Read(target) == good, "404 and tampered mirrors fall through to a good one");

    fs::remove(target);
    source.urls = {server.Url("/bad")};
    result = offline::Fetch(source, target, never.get_token(), {}, options);
    Expect(result.kind == offline::Result::Kind::Verify && !fs::exists(target) && !fs::exists(partial), "tampered only source: verify error, nothing kept");

    source.urls = {server.Url("/good")};
    std::stop_source cancel;
    cancel.request_stop();
    result = offline::Fetch(source, target, cancel.get_token(), {}, options);
    Expect(result.kind == offline::Result::Kind::Canceled && !fs::exists(target), "cancel stops before writing");

    offline::Source wrong = source;
    wrong.size = good.size() + 10;
    result = offline::Fetch(wrong, target, never.get_token(), {}, options);
    Expect(!result.ok() && !fs::exists(target), "size mismatch rejected");
}

void Prompts() {
    Credentials c;
    c.preset = FindPreset("offline");
    c.endpoint = "http://127.0.0.1:5123/v1";
    c.model = "hy-mt1.5-1.8b";
    const Job to_chinese{{L"Hello", L"World"}, Language::English, Language::ChineseSimplified};
    auto body = BuildRequest(c, to_chinese, {}).body;
    Expect(body.find("将以下文本翻译为") != std::string::npos && body.find("\"top_k\":20") != std::string::npos && body.find("\"repeat_penalty\":1.05") != std::string::npos, "offline hunyuan: official prompt and sampling");
    body = BuildRequest(c, Job{{L"你好"}, Language::ChineseSimplified, Language::Japanese}, {}).body;
    Expect(body.find("将以下文本翻译为") != std::string::npos, "chinese source keeps the chinese template");
    body = BuildRequest(c, Job{{L"Bonjour"}, Language::French, Language::English}, {}).body;
    Expect(body.find("Translate the following segment into English, without additional explanation.") != std::string::npos, "non-chinese pair uses the english template");
    c.model = "translategemma-4b";
    const auto request = BuildRequest(c, to_chinese, {});
    Expect(request.url == "http://127.0.0.1:5123/completion", "translategemma posts to raw /completion");
    Expect(request.body.find("<start_of_turn>user\\nYou are a professional English (en) to Chinese (zh-Hans) translator.") != std::string::npos
        && request.body.find("Hello\\nWorld<end_of_turn>\\n<start_of_turn>model\\n") != std::string::npos && request.body.find("\"temperature\":0") != std::string::npos, "translategemma official prompt");
    http::Response response;
    response.status = http::Status::Ok;
    response.http_status = 200;
    response.body = "{\"content\":\"你好\\n世界<end_of_turn>\",\"stop\":true}";
    const auto outcome = ParseResponse(c, to_chinese, response);
    Expect(outcome.ok && outcome.texts.size() == 2 && outcome.texts[0] == L"你好" && outcome.texts[1] == L"世界", "translategemma completion parsed line by line");
}
}

int main() {
    const fs::path root = fs::absolute(L"offline-test-tmp");
    std::error_code error;
    fs::remove_all(root, error);
    Catalog();
    Sha();
    Fit();
    CommandLine();
    Markers(root / L"markers");
    Downloads(root);
    Prompts();
    fs::remove_all(root, error);
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
