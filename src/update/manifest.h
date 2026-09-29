#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Signed release manifest ("lumashot-update.txt", one per GitHub release).
// Proxies and mirrors are untrusted transport: every field is accepted only
// after the ECDSA P-256 signature over the preceding bytes verifies against the
// embedded release key, and the installer is accepted only by size + SHA-256.
namespace lumashot::update {
struct Version {
    unsigned major{}, minor{}, patch{};
    auto operator<=>(const Version&) const = default;
};
std::optional<Version> ParseVersion(std::string_view text);
std::wstring VersionText(const Version& version);
Version CurrentVersion();

using Digest = std::array<std::uint8_t, 32>;
struct Manifest {
    Version version;
    std::string tag, file, published;
    std::uint64_t size{};
    Digest sha256{};
    std::wstring notes;
    std::vector<std::string> mirrors;  // validated https prefixes ending in '/'
};

enum class ManifestError { None, Format, Signature };
using PublicKey = std::span<const std::uint8_t>;  // BCRYPT_ECCPUBLIC_BLOB (P-256)
PublicKey ReleasePublicKey();
constexpr std::size_t kMaxManifestBytes = 64 * 1024;
constexpr std::uint64_t kMaxInstallerBytes = 512ull * 1024 * 1024;
std::optional<Manifest> ParseSignedManifest(std::span<const std::uint8_t> bytes, PublicKey key, ManifestError* error = nullptr);

constexpr std::string_view kManifestName = "lumashot-update.txt";
std::string LatestManifestUrl();
std::string ReleaseAssetUrl(const Manifest& manifest);
std::vector<std::string> DefaultMirrors();
bool ValidMirror(std::string_view prefix);
// Empty prefix means the direct URL.
std::string ViaMirror(std::string_view prefix, std::string_view url);

// Streaming SHA-256 (CNG). Throws std::runtime_error when CNG is unavailable.
class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void Update(std::span<const std::uint8_t> bytes);
    Digest Finish();
private:
    void* algorithm_{};
    void* hash_{};
};
bool VerifySignature(PublicKey key, std::span<const std::uint8_t> data, std::span<const std::uint8_t> signature);
}
