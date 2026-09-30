#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Minimal JSON DOM for translation provider traffic. Strings are UTF-8.
// Parsing is strict (RFC 8259) with a depth limit so untrusted responses
// cannot exhaust the stack.
namespace lumashot::translate::json {
struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind{Kind::Null};
    bool boolean{};
    double number{};
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    const Value* Find(std::string_view key) const;
    const Value* At(size_t index) const { return kind == Kind::Array && index < array.size() ? &array[index] : nullptr; }
    bool IsString() const { return kind == Kind::String; }
    bool IsArray() const { return kind == Kind::Array; }
    bool IsObject() const { return kind == Kind::Object; }
    // Convenience accessors return empty/0 when the shape does not match.
    std::string Text(std::string_view key) const;
    double Number(std::string_view key, double fallback = 0) const;
};

std::optional<Value> Parse(std::string_view text);
// Appends a quoted, escaped JSON string (control characters as \uXXXX; UTF-8 passes through).
void Quote(std::string& out, std::string_view text);
std::string Quote(std::string_view text);
std::string Serialize(const Value& value);

std::string Utf8(std::wstring_view text);
std::wstring Utf16(std::string_view text);
}
