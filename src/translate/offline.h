#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

// Built-in offline translation: open translation models (GGUF) served by a
// pinned llama.cpp build (llama-server, Vulkan + CPU backends). Everything is
// downloaded on demand into %LOCALAPPDATA%\LumaShot\offline, verified by size
// and SHA-256 before use, and the server process only runs while translating
// (started on first request, stopped after an idle period, killed with LumaShot).
namespace lumashot::translate::offline {
enum class Prompt : std::uint8_t { Hunyuan, Gemma };

struct Model {
    std::string_view id;             // stored as the provider model (translation.ini)
    std::wstring_view name;          // 混元翻译 1.5 · 1.8B
    std::wstring_view vendor;        // 腾讯混元
    std::wstring_view summary;       // one-line pitch shown on the card
    std::string_view file;           // GGUF file name on disk
    std::uint64_t size;              // exact bytes
    std::string_view sha256;         // lowercase hex
    std::array<std::string_view, 3> urls;  // tried in order (CN-friendly mirror first)
    float ram_gb;                    // system memory needed to run on the CPU
    float vram_gb;                   // video memory for full GPU offload
    int languages;
    Prompt prompt;
    std::wstring_view license;
    std::string_view license_url;
};
inline constexpr std::string_view DefaultModel = "hy-mt1.5-1.8b";
std::span<const Model> Models();
const Model* FindModel(std::string_view id);

struct Runtime {
    std::string_view build;          // llama.cpp release tag
    std::string_view file;           // release asset (zip)
    std::uint64_t size;
    std::string_view sha256;
    std::string_view url;            // GitHub release asset; prefix proxies are tried too
};
const Runtime& RuntimePackage();
// Files extracted from the runtime zip (llama-server and its DLLs only).
std::span<const std::wstring_view> RuntimeMembers();

std::filesystem::path Root();        // LUMASHOT_OFFLINE_ROOT or %LOCALAPPDATA%\LumaShot\offline
std::filesystem::path ModelPath(const Model& model);
std::filesystem::path RuntimeDirectory();
std::filesystem::path ServerPath();
bool ModelInstalled(const Model& model);
bool RuntimeInstalled();
std::uint64_t PartialBytes(const Model& model);   // resumable bytes of an interrupted download
std::uint64_t FreeBytes();                        // free space on the volume holding Root()

struct Hardware {
    std::uint64_t ram{};             // physical memory
    std::uint64_t vram{};            // largest dedicated video memory (0 when only integrated)
    std::wstring gpu;                // adapter name for that memory
};
Hardware Probe();
enum class Fit { Fast, Ok, Slow, TooLarge };
Fit Assess(const Model& model, const Hardware& hardware);
const Model& Recommend(const Hardware& hardware);

// ---------- downloads ----------
struct Source {
    std::vector<std::string> urls;
    std::uint64_t size{};
    std::array<std::uint8_t, 32> sha256{};
};
struct FetchOptions {
    bool allow_loopback_http{};                 // tests only
    std::uint64_t min_rate{96 * 1024};          // bytes/s before leaving a crawling mirror (not for the last one)
    std::chrono::milliseconds window{std::chrono::seconds(20)};
    std::chrono::milliseconds connect{std::chrono::seconds(8)};
};
struct Result {
    enum class Kind { Ok, Canceled, Disk, Space, Network, Verify, Extract } kind{Kind::Network};
    std::wstring message;
    bool ok() const { return kind == Kind::Ok; }
};
using ByteProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;
// Resumable download (<target>.partial + HTTP Range) with mirror fallback. The
// file is renamed into place only after size and SHA-256 match.
Result Fetch(const Source& source, const std::filesystem::path& target, std::stop_token stop, const ByteProgress& progress, const FetchOptions& options = {});
std::optional<std::array<std::uint8_t, 32>> ParseSha256(std::string_view hex);

struct Progress {
    enum class Stage { Runtime, Extract, Model, Verify } stage{Stage::Model};
    std::uint64_t done{}, total{};
};
Result Install(const Model& model, std::stop_token stop, const std::function<void(const Progress&)>& progress);
bool Remove(const Model& model);

// ---------- server ----------
// Process-wide llama-server. Acquire() starts (or reuses) the server for `model`
// and returns its base URL (http://127.0.0.1:<port>); every successful Acquire
// must be paired with Release(), which arms the idle shutdown.
std::optional<std::string> Acquire(const Model& model, std::stop_token stop, std::wstring* reason);
void Release();
void Shutdown();
bool ServerRunning();
inline constexpr std::chrono::minutes IdleShutdown{5};
std::wstring ServerCommandLine(const std::filesystem::path& server, const std::filesystem::path& model, int port, bool cpu_only, const std::filesystem::path& log);
}
