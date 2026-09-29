#pragma once
#include "update/http.h"
#include "update/manifest.h"
#include <windows.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>

namespace lumashot::update {
struct Endpoints {
    std::string manifest_url = LatestManifestUrl();
    std::function<std::string(const Manifest&)> asset_url = ReleaseAssetUrl;
    std::vector<std::string> mirrors;  // proxy prefixes; the direct URL is always tried too
    std::vector<std::uint8_t> key;
    Version current = CurrentVersion();
    http::Options manifest_http{};
    http::Options download_http{std::chrono::milliseconds(10000), std::chrono::milliseconds(30000)};
    std::chrono::milliseconds check_deadline{15000};
    std::chrono::milliseconds settle{3000};  // extra wait for a newer manifest after the first valid one
    std::uint64_t min_download_rate{24 * 1024};
};
// Built-in proxies, then any extra ones remembered from the last verified manifest.
Endpoints ReleaseEndpoints(std::span<const std::string> remembered_mirrors = {});

struct CheckResult {
    enum class Kind { UpToDate, Available, Failed, Canceled } kind{Kind::Failed};
    std::optional<Manifest> manifest;   // newest valid manifest seen
    std::vector<std::string> sources;   // prefixes that served a valid manifest, fastest first ("" = direct)
    unsigned attempted{}, reachable{}, rejected{};
};
CheckResult Check(const Endpoints& endpoints, std::stop_token stop);

struct DownloadResult {
    enum class Kind { Ok, Failed, Canceled, Disk } kind{Kind::Failed};
    std::filesystem::path file;
    std::string source;
    unsigned attempted{}, corrupt{};
};
using Progress = std::function<void(std::uint64_t received, std::uint64_t total)>;
DownloadResult Download(const Endpoints& endpoints, const Manifest& manifest, std::span<const std::string> preferred,
    const std::filesystem::path& directory, std::stop_token stop, const Progress& progress = {});
bool VerifyInstaller(const std::filesystem::path& file, const Manifest& manifest, std::stop_token stop = {});
std::filesystem::path InstallerPath(const std::filesystem::path& directory, const Manifest& manifest);
// Removes partial downloads and installers not newer than `current`.
void PruneDownloads(const std::filesystem::path& directory, const Version& current);
std::filesystem::path DownloadDirectory();
// Runs the Inno Setup installer silently; it closes LumaShot and restarts it afterwards.
bool LaunchInstaller(const std::filesystem::path& installer);

// UI-thread front end: one worker at a time, results posted as `message` with
// WPARAM = Notification. Destruction cancels and joins promptly.
class Updater {
public:
    enum class Notification : WPARAM { CheckDone = 1, Progress = 2, DownloadDone = 3, VerifyDone = 4 };
    enum class Phase { Idle, Checking, Downloading, Verifying };
    Updater(HWND window, UINT message);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;
    Phase phase() const { return phase_; }
    bool Check(Endpoints endpoints);
    bool Download(Endpoints endpoints, Manifest manifest, std::vector<std::string> preferred);
    bool Verify(std::filesystem::path file, Manifest manifest);
    void Cancel();
    // Called on the UI thread after the matching notification arrives.
    CheckResult TakeCheck();
    DownloadResult TakeDownload();
    bool TakeVerify();
    std::pair<std::uint64_t, std::uint64_t> DownloadProgress() const;
private:
    void Run(Phase phase, std::function<void(std::stop_token)> work);
    HWND window_{};
    UINT message_{};
    Phase phase_{Phase::Idle};
    mutable std::mutex mutex_;
    CheckResult check_;
    DownloadResult download_;
    bool verified_{};
    std::uint64_t received_{}, total_{};
    unsigned last_percent_{101};
    std::jthread worker_;
};
}
