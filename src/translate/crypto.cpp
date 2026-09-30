#include "translate/crypto.h"
#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <wincrypt.h>

namespace lumashot::translate::crypto {
namespace {
struct Algorithm {
    BCRYPT_ALG_HANDLE handle{};
    Algorithm(const wchar_t* id, ULONG flags) {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&handle, id, nullptr, flags))) handle = nullptr;
    }
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;
};

std::string Digest(const wchar_t* id, std::string_view key, std::string_view data, bool hmac) {
    Algorithm algorithm(id, hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0);
    if (!algorithm.handle) return {};
    DWORD length = 0, written = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm.handle, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &written, 0))) return {};
    BCRYPT_HASH_HANDLE hash{};
    auto* secret = hmac ? reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())) : nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm.handle, &hash, nullptr, 0, secret, hmac ? static_cast<ULONG>(key.size()) : 0, 0))) return {};
    std::string out(length, '\0');
    const bool ok = BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), 0))
        && BCRYPT_SUCCESS(BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(out.data()), length, 0));
    BCryptDestroyHash(hash);
    return ok ? out : std::string{};
}
}

std::string Md5(std::string_view data) { return Digest(BCRYPT_MD5_ALGORITHM, {}, data, false); }
std::string Sha256(std::string_view data) { return Digest(BCRYPT_SHA256_ALGORITHM, {}, data, false); }
std::string HmacSha256(std::string_view key, std::string_view data) { return Digest(BCRYPT_SHA256_ALGORITHM, key, data, true); }

std::string Hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

std::string Base64(std::string_view bytes) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8) | static_cast<uint8_t>(bytes[i + 2]);
        out.push_back(table[v >> 18]); out.push_back(table[(v >> 12) & 63]); out.push_back(table[(v >> 6) & 63]); out.push_back(table[v & 63]);
    }
    if (i < bytes.size()) {
        uint32_t v = static_cast<uint8_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) v |= static_cast<uint8_t>(bytes[i + 1]) << 8;
        out.push_back(table[v >> 18]); out.push_back(table[(v >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? table[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

std::optional<std::string> Unbase64(std::string_view text) {
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    size_t padding = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') { ++padding; continue; }
        else if (c == '\r' || c == '\n' || c == ' ') continue;
        else return std::nullopt;
        if (padding) return std::nullopt;
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((buffer >> bits) & 0xff)); }
    }
    if (padding > 2) return std::nullopt;
    return out;
}

std::string Uuid() {
    unsigned char bytes[16]{};
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return {};
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
    const auto hex = Hex(std::string_view(reinterpret_cast<const char*>(bytes), sizeof(bytes)));
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" + hex.substr(20);
}

std::string Protect(std::string_view secret) {
    if (secret.empty()) return {};
    DATA_BLOB input{static_cast<DWORD>(secret.size()), reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()))}, output{};
    if (!CryptProtectData(&input, L"LumaShot translation", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    std::string raw(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return Base64(raw);
}

std::optional<std::string> Unprotect(std::string_view protected_text) {
    if (protected_text.empty()) return std::string{};
    auto raw = Unbase64(protected_text);
    if (!raw || raw->empty()) return std::nullopt;
    DATA_BLOB input{static_cast<DWORD>(raw->size()), reinterpret_cast<BYTE*>(raw->data())}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return std::nullopt;
    std::string secret(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return secret;
}

std::string FormEncode(std::string_view text) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const char c : text) {
        const auto b = static_cast<unsigned char>(c);
        if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '-' || b == '_' || b == '.' || b == '~') out.push_back(c);
        else { out.push_back('%'); out.push_back(digits[b >> 4]); out.push_back(digits[b & 15]); }
    }
    return out;
}
}
