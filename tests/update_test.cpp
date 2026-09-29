// Updater rules: manifest signature/format/version checks with an in-test key,
// and source fallback against a loopback HTTP server (no internet, no real key).
#include <winsock2.h>
#include <ws2tcpip.h>
#include "update/updater.h"
#include "lumashot_version.h"
#include <bcrypt.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <thread>

using namespace lumashot;
using namespace lumashot::update;
namespace {
int failures{};
void Expect(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl; failures += !ok; }
std::span<const std::uint8_t> Bytes(const std::string& text) { return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}; }

struct TestKey {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_KEY_HANDLE key{};
    std::vector<std::uint8_t> public_blob;
    TestKey() {
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
        BCryptGenerateKeyPair(algorithm, &key, 256, 0);
        BCryptFinalizeKeyPair(key, 0);
        ULONG size = 0;
        BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &size, 0);
        public_blob.resize(size);
        BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, public_blob.data(), size, &size, 0);
    }
    ~TestKey() { if (key) BCryptDestroyKey(key); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    std::string Base64Signature(const std::string& body) const {
        Sha256 hash;
        hash.Update(Bytes(body));
        auto digest = hash.Finish();
        std::uint8_t signature[64]{};
        ULONG size = 0;
        BCryptSignHash(key, nullptr, digest.data(), 32, signature, 64, &size, 0);
        static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (int i = 0; i < 64; i += 3) {
            const unsigned a = signature[i], b = i + 1 < 64 ? signature[i + 1] : 0, c = i + 2 < 64 ? signature[i + 2] : 0;
            const unsigned triple = (a << 16) | (b << 8) | c;
            out += table[(triple >> 18) & 63];
            out += table[(triple >> 12) & 63];
            out += i + 1 < 64 ? table[(triple >> 6) & 63] : '=';
            out += i + 2 < 64 ? table[triple & 63] : '=';
        }
        return out;
    }
    std::string Sign(const std::string& body) const { return body + "signature=" + Base64Signature(body) + "\n"; }
};

std::string HexDigest(const std::string& payload) {
    Sha256 hash;
    hash.Update(Bytes(payload));
    const auto digest = hash.Finish();
    std::string out;
    for (auto b : digest) { const char* hex = "0123456789abcdef"; out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}

std::string Body(const std::string& version, const std::string& payload, const std::string& extra = "") {
    return "LumaShot-Update 1\nversion=" + version + "\ntag=v" + version + "\nfile=LumaShot-Setup.exe\nsize=" + std::to_string(payload.size()) +
        "\nsha256=" + HexDigest(payload) + "\npublished=2026-09-29T00:00:00Z\nnotes=第一行亮点\\n第二行 \\\\ 反斜杠\n" + extra;
}

// Tiny HTTP/1.1 server: route(path) -> {status, body}; status 0 = hang until shutdown.
class Server {
public:
    using Route = std::function<std::pair<int, std::string>(const std::string&)>;
    explicit Server(Route route) : route_(std::move(route)) {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        int length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        listen(listener_, 32);
        acceptor_ = std::thread([this] {
            for (;;) {
                SOCKET client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET) break;
                std::lock_guard lock(mutex_);
                clients_.emplace_back([this, client] { Serve(client); });
            }
        });
    }
    ~Server() {
        stopping_ = true;
        closesocket(listener_);
        acceptor_.join();
        std::lock_guard lock(mutex_);
        for (auto& client : clients_) client.join();
        WSACleanup();
    }
    std::string Base() const { return "http://127.0.0.1:" + std::to_string(port_); }
private:
    void Serve(SOCKET client) {
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const int got = recv(client, buffer, sizeof(buffer), 0);
            if (got <= 0) { closesocket(client); return; }
            request.append(buffer, got);
        }
        const auto start = request.find(' ') + 1;
        const auto path = request.substr(start, request.find(' ', start) - start);
        const auto [status, body] = route_(path);
        if (status == 0) {
            while (!stopping_) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } else {
            const std::string head = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK" : " Error") +
                "\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
            const std::string all = head + body;
            send(client, all.data(), static_cast<int>(all.size()), 0);
            shutdown(client, SD_SEND);
        }
        closesocket(client);
    }
    Route route_;
    SOCKET listener_{INVALID_SOCKET};
    unsigned short port_{};
    std::atomic_bool stopping_{};
    std::thread acceptor_;
    std::mutex mutex_;
    std::vector<std::thread> clients_;
};

void ManifestRules(const TestKey& key) {
    const std::string payload(4096, 'x');
    const auto good = key.Sign(Body("0.3.0", payload, "mirror=https://gh-proxy.com/\nmirror=http://insecure.example/\nmirror=https://ghfast.top/\nfuture=ignored\n"));
    ManifestError error{};
    const auto manifest = ParseSignedManifest(Bytes(good), key.public_blob, &error);
    Expect(manifest && error == ManifestError::None, "valid signed manifest parses");
    if (manifest) {
        Expect(manifest->version == Version{0, 3, 0} && manifest->tag == "v0.3.0" && manifest->file == "LumaShot-Setup.exe" && manifest->size == 4096, "manifest fields parsed");
        Expect(manifest->notes == L"第一行亮点\n第二行 \\ 反斜杠", "notes unescape newline and backslash");
        Expect(manifest->mirrors == std::vector<std::string>{"https://gh-proxy.com/", "https://ghfast.top/"}, "only https mirror prefixes are kept");
        Expect(ReleaseAssetUrl(*manifest) == "https://github.com/jimmgreen/LumaShot/releases/download/v0.3.0/LumaShot-Setup.exe", "asset URL is fixed to the release repository");
        Expect(ViaMirror("https://gh-proxy.com/", ReleaseAssetUrl(*manifest)) == "https://gh-proxy.com/https://github.com/jimmgreen/LumaShot/releases/download/v0.3.0/LumaShot-Setup.exe", "prefix proxy URL");
    }
    auto tampered = good;
    tampered[tampered.find("size=") + 5] = '5';
    Expect(!ParseSignedManifest(Bytes(tampered), key.public_blob, &error) && error == ManifestError::Signature, "tampered body fails signature");
    const TestKey other;
    Expect(!ParseSignedManifest(Bytes(good), other.public_blob, &error) && error == ManifestError::Signature, "wrong key fails signature");
    Expect(!ParseSignedManifest(Bytes(good), ReleasePublicKey(), &error) && error == ManifestError::Signature, "embedded release key rejects test-signed manifest");
    auto crlf = good;
    crlf.insert(crlf.find('\n'), "\r");
    Expect(!ParseSignedManifest(Bytes(crlf), key.public_blob, &error) && error == ManifestError::Format, "CR line endings rejected");
    Expect(!ParseSignedManifest(Bytes(Body("0.3.0", payload)), key.public_blob, &error) && error == ManifestError::Format, "unsigned manifest rejected");
    auto trailing = good + "version=9.9.9\n";
    Expect(!ParseSignedManifest(Bytes(trailing), key.public_blob), "data after signature rejected");
    Expect(!ParseSignedManifest(Bytes(key.Sign(Body("0.3.0", payload, "version=9.9.9\n"))), key.public_blob), "duplicate field rejected even when signed");
    Expect(!ParseSignedManifest(Bytes(key.Sign(Body("01.3.0", payload))), key.public_blob), "non-canonical version rejected");
    auto bad_file = Body("0.3.0", payload);
    bad_file.replace(bad_file.find("LumaShot-Setup.exe"), 18, "../../evil.exe");
    Expect(!ParseSignedManifest(Bytes(key.Sign(bad_file)), key.public_blob), "path traversal in file name rejected");
    auto no_hash = Body("0.3.0", payload);
    no_hash.erase(no_hash.find("sha256="), 72);
    Expect(!ParseSignedManifest(Bytes(key.Sign(no_hash)), key.public_blob), "missing sha256 rejected");
    Expect(!ParseSignedManifest(Bytes(std::string(kMaxManifestBytes + 1, 'a')), key.public_blob), "oversized manifest rejected");
    Expect(!ParseSignedManifest(Bytes(key.Sign("Other-Format 1\n" + Body("0.3.0", payload).substr(18))), key.public_blob), "wrong magic rejected");

    Expect(ParseVersion("0.10.0") > ParseVersion("0.9.9") && ParseVersion("1.0.0") > ParseVersion("0.99.99"), "numeric version comparison");
    Expect(!ParseVersion("1.2") && !ParseVersion("1.2.3.4") && !ParseVersion("1.2.x") && !ParseVersion("") && !ParseVersion("1..2"), "malformed versions rejected");
    Expect(CurrentVersion() == ParseVersion(LUMASHOT_VERSION_TEXT) && VersionText(CurrentVersion()) == LUMASHOT_VERSION_WTEXT, "current version from CMake");
    Expect(ValidMirror("https://gh-proxy.com/") && ValidMirror("https://example.com/gh/") && !ValidMirror("http://gh-proxy.com/") &&
        !ValidMirror("https://gh-proxy.com") && !ValidMirror("https://evil.com/?u=/") && !ValidMirror("https://user@evil.com/") && !ValidMirror("https://localhost/"), "mirror prefix validation");
    const auto release = ReleasePublicKey();
    Expect(release.size() == 72 && release[0] == 'E' && release[1] == 'C' && release[2] == 'S' && release[3] == '1', "embedded release key is a P-256 public blob");
    for (const auto& mirror : DefaultMirrors()) Expect(ValidMirror(mirror), "built-in mirror is valid");
}

void Fallback(const TestKey& key) {
    const std::string payload = [] { std::string p(300 * 1024, '\0'); for (size_t i = 0; i < p.size(); ++i) p[i] = static_cast<char>(i * 131 + 7); return p; }();
    auto corrupt = payload;
    corrupt[1000] = static_cast<char>(corrupt[1000] ^ 1);
    const auto newest = key.Sign(Body("9.9.9", payload));
    const auto older = key.Sign(Body("0.3.0", payload));
    auto tampered = newest;
    tampered[tampered.find("tag=") + 5] = '8';
    std::atomic_int direct_hits{};
    Server server([&](const std::string& path) -> std::pair<int, std::string> {
        const bool manifest = path.ends_with("/direct/manifest");
        if (path.starts_with("/direct/")) { ++direct_hits; return {404, "blocked"}; }
        if (path.starts_with("/m1/")) return {0, {}};                                       // hangs
        if (path.starts_with("/m2/")) return manifest ? std::pair{200, newest} : std::pair{200, corrupt};
        if (path.starts_with("/m3/")) return manifest ? std::pair{200, tampered} : std::pair{200, payload};
        if (path.starts_with("/m4/")) return manifest ? std::pair{200, older} : std::pair{404, std::string{}};
        return {404, {}};
    });
    Endpoints endpoints;
    endpoints.manifest_url = server.Base() + "/direct/manifest";
    endpoints.asset_url = [&](const Manifest& m) { return server.Base() + "/direct/" + m.tag + "/" + m.file; };
    for (const char* name : {"/m1/", "/m2/", "/m3/", "/m4/"}) endpoints.mirrors.push_back(server.Base() + name);
    endpoints.key = key.public_blob;
    endpoints.current = {0, 2, 0};
    endpoints.manifest_http.allow_loopback_http = true;
    endpoints.manifest_http.receive_timeout = std::chrono::milliseconds(10000);
    endpoints.download_http.allow_loopback_http = true;
    endpoints.download_http.receive_timeout = std::chrono::milliseconds(1500);
    endpoints.check_deadline = std::chrono::milliseconds(8000);
    endpoints.settle = std::chrono::milliseconds(600);

    const auto started = std::chrono::steady_clock::now();
    const auto check = Check(endpoints, {});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Expect(check.kind == CheckResult::Kind::Available && check.manifest && check.manifest->version == Version{9, 9, 9}, "newest valid manifest wins across sources");
    Expect(check.attempted == 5 && check.reachable == 3 && check.rejected == 1, "direct blocked, one hang, one tampered manifest rejected");
    Expect(check.sources.size() == 2 && std::find(check.sources.begin(), check.sources.end(), server.Base() + "/m3/") == check.sources.end(), "only verified sources are ranked");
    Expect(elapsed < std::chrono::milliseconds(4000), "hanging proxy does not hold the check beyond the settle window");

    auto up_to_date = endpoints;
    up_to_date.current = {9, 9, 9};
    Expect(Check(up_to_date, {}).kind == CheckResult::Kind::UpToDate, "same version is up to date");

    const auto directory = std::filesystem::temp_directory_path() / (L"lumashot-update-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(directory);
    int progress_calls = 0;
    bool progress_total = true;
    const auto download = Download(endpoints, *check.manifest, check.sources, directory, {}, [&](std::uint64_t, std::uint64_t total) { ++progress_calls; progress_total = progress_total && total == payload.size(); });
    Expect(progress_total, "progress total is manifest size");
    Expect(download.kind == DownloadResult::Kind::Ok && download.source == server.Base() + "/m3/", "falls back past corrupt, missing and hanging sources");
    Expect(download.corrupt == 1 && progress_calls > 0, "corrupt installer from a proxy is detected");
    Expect(download.file == InstallerPath(directory, *check.manifest) && VerifyInstaller(download.file, *check.manifest), "installer stored under versioned name and verifies");
    Expect(!std::filesystem::exists(std::filesystem::path(download.file.native() + L".partial")), "partial file removed");
    const auto again = Download(endpoints, *check.manifest, check.sources, directory, {});
    Expect(again.kind == DownloadResult::Kind::Ok && again.attempted == 0, "verified existing download is reused without network");
    {
        std::fstream file(download.file, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(10);
        file.put('!');
    }
    Expect(!VerifyInstaller(download.file, *check.manifest), "modified installer on disk fails verification");

    // Cancellation reaches a request blocked on a hanging server promptly.
    auto hanging = endpoints;
    hanging.mirrors = {server.Base() + "/m1/"};
    hanging.download_http.receive_timeout = std::chrono::milliseconds(30000);
    hanging.min_download_rate = 0;
    std::stop_source stop;
    std::jthread canceler([&] { std::this_thread::sleep_for(std::chrono::milliseconds(400)); stop.request_stop(); });
    auto hanging_manifest = *check.manifest;
    hanging_manifest.version = {9, 9, 8};
    const auto cancel_started = std::chrono::steady_clock::now();
    const auto canceled = Download(hanging, hanging_manifest, std::vector<std::string>{server.Base() + "/m1/"}, directory, stop.get_token());
    Expect(canceled.kind == DownloadResult::Kind::Canceled && std::chrono::steady_clock::now() - cancel_started < std::chrono::milliseconds(3000), "cancel interrupts a blocked download");

    std::vector<std::uint8_t> bytes;
    http::Options loopback;
    loopback.allow_loopback_http = true;
    Expect(http::GetBytes(server.Base() + "/m2/x", {}, 1024, bytes, loopback).status == http::Status::TooLarge, "body larger than the cap is rejected");
    Expect(http::GetBytes(server.Base() + "/m2/x", {}, 1 << 20, bytes).status == http::Status::BadUrl, "plain http rejected unless loopback test opt-in");

    for (const auto* name : {L"LumaShot-Setup-0.1.0.exe", L"LumaShot-Setup-0.2.0.exe", L"LumaShot-Setup-9.9.9.exe.partial", L"LumaShot-Setup-10.0.0.exe"})
        std::ofstream(directory / name) << "x";
    PruneDownloads(directory, {0, 2, 0});
    Expect(!std::filesystem::exists(directory / L"LumaShot-Setup-0.1.0.exe") && !std::filesystem::exists(directory / L"LumaShot-Setup-0.2.0.exe") &&
        !std::filesystem::exists(directory / L"LumaShot-Setup-9.9.9.exe.partial") && std::filesystem::exists(directory / L"LumaShot-Setup-10.0.0.exe") &&
        std::filesystem::exists(directory / L"LumaShot-Setup-9.9.9.exe"), "prune keeps only newer installers");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    Expect(direct_hits > 0, "direct GitHub URL is always attempted");
}

// Release diagnostics (not part of ctest):
//   --verify <file>  check a manifest signed by scripts/update-signing.ps1 against the embedded key
//   --live           fetch the published manifest from GitHub and every proxy (needs internet)
int Diagnose(int argc, char** argv) {
    const std::string mode = argv[1];
    if (mode == "--verify" && argc == 3) {
        std::ifstream file(argv[2], std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        ManifestError error{};
        const auto manifest = ParseSignedManifest(Bytes(bytes), ReleasePublicKey(), &error);
        if (!manifest) { std::cout << "INVALID error=" << static_cast<int>(error) << std::endl; return 1; }
        std::wcout << L"VALID version=" << VersionText(manifest->version) << L" size=" << manifest->size << L" mirrors=" << manifest->mirrors.size() << std::endl;
        std::cout << "asset=" << ReleaseAssetUrl(*manifest) << std::endl;
        return 0;
    }
    if (mode == "--live" && argc == 2) {
        int valid = 0;
        auto endpoints = ReleaseEndpoints();
        std::vector<std::string> sources{""};
        sources.insert(sources.end(), endpoints.mirrors.begin(), endpoints.mirrors.end());
        for (const auto& source : sources) {
            std::vector<std::uint8_t> bytes;
            const auto started = std::chrono::steady_clock::now();
            const auto result = http::GetBytes(ViaMirror(source, endpoints.manifest_url), {}, kMaxManifestBytes, bytes, endpoints.manifest_http);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            ManifestError error{};
            const auto manifest = result.status == http::Status::Ok ? ParseSignedManifest(bytes, ReleasePublicKey(), &error) : std::nullopt;
            valid += manifest.has_value();
            std::cout << (source.empty() ? std::string("direct") : source) << " status=" << static_cast<int>(result.status) << " http=" << result.http_status
                      << " err=" << result.error << " ms=" << ms << " manifest=" << (manifest ? "valid" : "none");
            if (manifest) std::wcout << L" version=" << VersionText(manifest->version);
            std::cout << std::endl;
        }
        const auto check = Check(endpoints, {});
        std::cout << "CHECK kind=" << static_cast<int>(check.kind) << " reachable=" << check.reachable << " rejected=" << check.rejected << " ranked=" << check.sources.size() << std::endl;
        return valid ? 0 : 1;
    }
    std::cout << "usage: lumashot_update_test [--verify manifest | --live]" << std::endl;
    return 2;
}
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc > 1) return Diagnose(argc, argv);
    const TestKey key;
    ManifestRules(key);
    Fallback(key);
    std::cout << (failures ? "FAILED " : "ALL PASSED ") << failures << std::endl;
    return failures ? 1 : 0;
}
