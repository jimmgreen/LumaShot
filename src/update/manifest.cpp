#include "update/manifest.h"
#include "lumashot_version.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace lumashot::update {
namespace {
// Release signing key; the private half lives outside the repository
// (%USERPROFILE%\.lumashot\release-signing-key, see scripts/update-signing.ps1).
constexpr std::uint8_t kReleaseKey[] = {
    0x45,0x43,0x53,0x31,0x20,0x00,0x00,0x00,0x31,0xaf,0xef,0x6b,
    0x32,0xf6,0xb9,0x7e,0xc2,0xd8,0x87,0x7b,0x4b,0x32,0x16,0x2b,
    0xb2,0x00,0xaf,0xbc,0x57,0xd7,0xe2,0xbb,0xea,0x7e,0xd1,0x34,
    0x5d,0x6c,0xf9,0xeb,0x2a,0x72,0x4b,0x62,0x65,0xd6,0x3f,0x8c,
    0x35,0xae,0xd8,0x7f,0x74,0x9e,0x26,0x45,0x48,0xb9,0xa9,0x49,
    0xf7,0xdb,0x8b,0x45,0x9c,0x95,0xbd,0x22,0x9f,0xc0,0xd3,0x2d};
constexpr std::string_view kRepository = "https://github.com/jimmgreen/LumaShot/releases/";
constexpr std::string_view kMagic = "LumaShot-Update 1";
constexpr std::size_t kMaxMirrors = 16;

bool Ok(NTSTATUS status) { return status >= 0; }

struct Algorithm {
    BCRYPT_ALG_HANDLE value{};
    explicit Algorithm(LPCWSTR id) { if (!Ok(BCryptOpenAlgorithmProvider(&value, id, nullptr, 0))) value = nullptr; }
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;
};

bool Name(std::string_view text) {
    return !text.empty() && text.size() <= 64 && text.front() != '.' &&
        std::all_of(text.begin(), text.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'; });
}

int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Strict canonical base64 (padding required, no whitespace).
std::optional<std::vector<std::uint8_t>> Base64(std::string_view text) {
    if (text.empty() || text.size() % 4) return std::nullopt;
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        int v[4]{};
        int padding = 0;
        for (int j = 0; j < 4; ++j) {
            const char c = text[i + j];
            if (c == '=' && i + 4 == text.size() && j >= 2) { ++padding; v[j] = 0; continue; }
            if (padding) return std::nullopt;
            v[j] = Base64Value(c);
            if (v[j] < 0) return std::nullopt;
        }
        const unsigned triple = (unsigned(v[0]) << 18) | (unsigned(v[1]) << 12) | (unsigned(v[2]) << 6) | unsigned(v[3]);
        out.push_back(static_cast<std::uint8_t>(triple >> 16));
        if (padding < 2) out.push_back(static_cast<std::uint8_t>(triple >> 8));
        if (padding < 1) out.push_back(static_cast<std::uint8_t>(triple));
        if ((padding == 1 && (v[2] & 3)) || (padding == 2 && (v[1] & 15))) return std::nullopt;
    }
    return out;
}

std::optional<std::wstring> Utf8(std::string_view text) {
    if (text.empty()) return std::wstring{};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), count);
    return wide;
}

std::optional<std::wstring> Notes(std::string_view escaped) {
    std::string plain;
    for (std::size_t i = 0; i < escaped.size(); ++i) {
        if (escaped[i] != '\\') { plain.push_back(escaped[i]); continue; }
        if (++i == escaped.size()) return std::nullopt;
        if (escaped[i] == 'n') plain.push_back('\n');
        else if (escaped[i] == '\\') plain.push_back('\\');
        else return std::nullopt;
    }
    auto wide = Utf8(plain);
    if (!wide || wide->size() > 4000) return std::nullopt;
    return wide;
}
}

std::optional<Version> ParseVersion(std::string_view text) {
    Version value;
    unsigned* parts[] = {&value.major, &value.minor, &value.patch};
    for (int i = 0; i < 3; ++i) {
        const auto dot = i < 2 ? text.find('.') : text.size();
        if (dot == std::string_view::npos || dot == 0 || dot > 5) return std::nullopt;
        const auto part = text.substr(0, dot);
        if (part.size() > 1 && part.front() == '0') return std::nullopt;
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), *parts[i]);
        if (error != std::errc{} || end != part.data() + part.size()) return std::nullopt;
        text.remove_prefix(i < 2 ? dot + 1 : dot);
    }
    return text.empty() ? std::optional<Version>(value) : std::nullopt;
}

std::wstring VersionText(const Version& v) {
    return std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." + std::to_wstring(v.patch);
}

Version CurrentVersion() { return {LUMASHOT_VERSION_MAJOR, LUMASHOT_VERSION_MINOR, LUMASHOT_VERSION_PATCH}; }

PublicKey ReleasePublicKey() { return kReleaseKey; }

std::string LatestManifestUrl() { return std::string(kRepository) + "latest/download/" + std::string(kManifestName); }

std::string ReleaseAssetUrl(const Manifest& manifest) {
    return std::string(kRepository) + "download/" + manifest.tag + "/" + manifest.file;
}

std::vector<std::string> DefaultMirrors() {
    // Prefix-style GitHub file proxies for networks where github.com is unreachable.
    return {"https://gh-proxy.com/", "https://ghfast.top/", "https://ghproxy.net/", "https://gh.llkk.cc/", "https://gh.zwy.one/"};
}

bool ValidMirror(std::string_view prefix) {
    constexpr std::string_view scheme = "https://";
    if (prefix.size() > 200 || !prefix.starts_with(scheme) || !prefix.ends_with('/')) return false;
    const auto rest = prefix.substr(scheme.size());
    const auto slash = rest.find('/');
    const auto host = rest.substr(0, slash);
    if (host.empty() || host.front() == '.' || host.front() == '-' || host.find('.') == std::string_view::npos) return false;
    if (!std::all_of(host.begin(), host.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'; })) return false;
    const auto path = rest.substr(slash);
    return std::all_of(path.begin(), path.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '~' || c == '/' || c == '-'; });
}

std::string ViaMirror(std::string_view prefix, std::string_view url) { return std::string(prefix) + std::string(url); }

Sha256::Sha256() {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (!Ok(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) throw std::runtime_error("SHA-256 unavailable");
    if (!Ok(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("SHA-256 unavailable");
    }
    algorithm_ = algorithm;
    hash_ = hash;
}
Sha256::~Sha256() {
    if (hash_) BCryptDestroyHash(hash_);
    if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
}
void Sha256::Update(std::span<const std::uint8_t> bytes) {
    while (!bytes.empty()) {
        const auto chunk = static_cast<ULONG>(std::min<std::size_t>(bytes.size(), 1u << 30));
        if (!Ok(BCryptHashData(hash_, const_cast<PUCHAR>(bytes.data()), chunk, 0))) throw std::runtime_error("SHA-256 failed");
        bytes = bytes.subspan(chunk);
    }
}
Digest Sha256::Finish() {
    Digest digest{};
    if (!Ok(BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0))) throw std::runtime_error("SHA-256 failed");
    return digest;
}

bool VerifySignature(PublicKey key, std::span<const std::uint8_t> data, std::span<const std::uint8_t> signature) {
    if (signature.size() != 64 || key.size() != sizeof(BCRYPT_ECCKEY_BLOB) + 64) return false;
    const auto* header = reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(key.data());
    if (header->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC || header->cbKey != 32) return false;
    Digest digest;
    try {
        Sha256 hash;
        hash.Update(data);
        digest = hash.Finish();
    } catch (const std::exception&) {
        return false;
    }
    Algorithm ecdsa(BCRYPT_ECDSA_P256_ALGORITHM);
    if (!ecdsa.value) return false;
    BCRYPT_KEY_HANDLE handle{};
    if (!Ok(BCryptImportKeyPair(ecdsa.value, nullptr, BCRYPT_ECCPUBLIC_BLOB, &handle, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0))) return false;
    const bool valid = Ok(BCryptVerifySignature(handle, nullptr, digest.data(), static_cast<ULONG>(digest.size()), const_cast<PUCHAR>(signature.data()), static_cast<ULONG>(signature.size()), 0));
    BCryptDestroyKey(handle);
    return valid;
}

std::optional<Manifest> ParseSignedManifest(std::span<const std::uint8_t> bytes, PublicKey key, ManifestError* error) {
    const auto fail = [&](ManifestError kind) { if (error) *error = kind; return std::optional<Manifest>{}; };
    if (error) *error = ManifestError::None;
    if (bytes.empty() || bytes.size() > kMaxManifestBytes) return fail(ManifestError::Format);
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.find('\r') != std::string_view::npos || text.find('\0') != std::string_view::npos) return fail(ManifestError::Format);
    // The signature line is last; it signs every byte before it.
    auto body_end = text.rfind("\nsignature=");
    if (body_end == std::string_view::npos) return fail(ManifestError::Format);
    ++body_end;
    auto signature_text = text.substr(body_end + 10);
    if (signature_text.ends_with('\n')) signature_text.remove_suffix(1);
    if (signature_text.find('\n') != std::string_view::npos) return fail(ManifestError::Format);
    const auto signature = Base64(signature_text);
    if (!signature || signature->size() != 64) return fail(ManifestError::Format);
    if (!VerifySignature(key, bytes.first(body_end), *signature)) return fail(ManifestError::Signature);

    // Authenticated from here on; still validate every field strictly.
    Manifest manifest;
    std::string_view body = text.substr(0, body_end);
    bool first = true, has_version = false, has_size = false, has_hash = false, has_notes = false;
    std::vector<std::string_view> seen;
    while (!body.empty()) {
        const auto end = body.find('\n');
        const auto line = body.substr(0, end);
        body.remove_prefix(end + 1);
        if (first) { if (line != kMagic) return fail(ManifestError::Format); first = false; continue; }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || equals == 0) return fail(ManifestError::Format);
        const auto name = line.substr(0, equals), value = line.substr(equals + 1);
        if (name == "mirror") {
            if (manifest.mirrors.size() < kMaxMirrors && ValidMirror(value) &&
                std::find(manifest.mirrors.begin(), manifest.mirrors.end(), value) == manifest.mirrors.end()) manifest.mirrors.emplace_back(value);
            continue;
        }
        if (std::find(seen.begin(), seen.end(), name) != seen.end()) return fail(ManifestError::Format);
        seen.push_back(name);
        if (name == "version") {
            const auto version = ParseVersion(value);
            if (!version) return fail(ManifestError::Format);
            manifest.version = *version;
            has_version = true;
        } else if (name == "tag") {
            if (!Name(value)) return fail(ManifestError::Format);
            manifest.tag = value;
        } else if (name == "file") {
            if (!Name(value) || !value.ends_with(".exe")) return fail(ManifestError::Format);
            manifest.file = value;
        } else if (name == "size") {
            const auto [end_size, parse] = std::from_chars(value.data(), value.data() + value.size(), manifest.size);
            if (parse != std::errc{} || end_size != value.data() + value.size() || manifest.size == 0 || manifest.size > kMaxInstallerBytes) return fail(ManifestError::Format);
            has_size = true;
        } else if (name == "sha256") {
            if (value.size() != 64) return fail(ManifestError::Format);
            for (std::size_t i = 0; i < 32; ++i) {
                const auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
                const int high = hex(value[i * 2]), low = hex(value[i * 2 + 1]);
                if (high < 0 || low < 0) return fail(ManifestError::Format);
                manifest.sha256[i] = static_cast<std::uint8_t>(high * 16 + low);
            }
            has_hash = true;
        } else if (name == "published") {
            if (value.size() > 32) return fail(ManifestError::Format);
            manifest.published = value;
        } else if (name == "notes") {
            auto notes = Notes(value);
            if (!notes) return fail(ManifestError::Format);
            manifest.notes = std::move(*notes);
            has_notes = true;
        }
        // Unknown signed keys are ignored so later clients can add fields.
    }
    if (first || !has_version || manifest.tag.empty() || manifest.file.empty() || !has_size || !has_hash || !has_notes) return fail(ManifestError::Format);
    return manifest;
}
}
