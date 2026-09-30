#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Hashing/signing primitives for provider request signatures (BCrypt) and
// DPAPI protection for stored API secrets. Everything operates on bytes held
// in std::string.
namespace lumashot::translate::crypto {
std::string Md5(std::string_view data);
std::string Sha256(std::string_view data);
std::string HmacSha256(std::string_view key, std::string_view data);
std::string Hex(std::string_view bytes);
std::string Base64(std::string_view bytes);
std::optional<std::string> Unbase64(std::string_view text);
// Random lowercase-hex UUID v4 text (8-4-4-4-12).
std::string Uuid();
// CryptProtectData bound to the current user, encoded as base64 text.
std::string Protect(std::string_view secret);
std::optional<std::string> Unprotect(std::string_view protected_text);
// RFC 3986 percent-encoding for application/x-www-form-urlencoded bodies.
std::string FormEncode(std::string_view text);
}
