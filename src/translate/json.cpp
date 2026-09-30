#include "translate/json.h"
#include <windows.h>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace lumashot::translate::json {
namespace {
constexpr int MaxDepth = 64;

struct Parser {
    std::string_view text;
    size_t at{};
    bool failed{};

    void Space() { while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) ++at; }
    bool Eat(char c) { Space(); if (at < text.size() && text[at] == c) { ++at; return true; } return false; }
    bool Literal(std::string_view word) {
        if (text.substr(at, word.size()) != word) return false;
        at += word.size();
        return true;
    }
    static void Append(std::string& out, uint32_t code) {
        if (code < 0x80) out.push_back(static_cast<char>(code));
        else if (code < 0x800) { out.push_back(static_cast<char>(0xc0 | (code >> 6))); out.push_back(static_cast<char>(0x80 | (code & 0x3f))); }
        else if (code < 0x10000) { out.push_back(static_cast<char>(0xe0 | (code >> 12))); out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (code & 0x3f))); }
        else { out.push_back(static_cast<char>(0xf0 | (code >> 18))); out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f))); out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (code & 0x3f))); }
    }
    bool Hex4(uint32_t& value) {
        if (at + 4 > text.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text[at++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool String(std::string& out) {
        if (at >= text.size() || text[at] != '"') return false;
        ++at;
        while (at < text.size()) {
            const char c = text[at++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') { out.push_back(c); continue; }
            if (at >= text.size()) return false;
            switch (text[at++]) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                uint32_t code{};
                if (!Hex4(code)) return false;
                if (code >= 0xd800 && code < 0xdc00) {
                    uint32_t low{};
                    if (text.substr(at, 2) == "\\u") {
                        at += 2;
                        if (!Hex4(low)) return false;
                        if (low >= 0xdc00 && low < 0xe000) code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                        else { Append(out, 0xfffd); code = low; }
                    } else code = 0xfffd;
                } else if (code >= 0xdc00 && code < 0xe000) code = 0xfffd;
                Append(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Number(double& out) {
        const size_t start = at;
        if (at < text.size() && text[at] == '-') ++at;
        if (at >= text.size() || !(text[at] >= '0' && text[at] <= '9')) return false;
        if (text[at] == '0') ++at; else while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        if (at < text.size() && text[at] == '.') {
            ++at;
            if (at >= text.size() || !(text[at] >= '0' && text[at] <= '9')) return false;
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        }
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
            ++at;
            if (at < text.size() && (text[at] == '+' || text[at] == '-')) ++at;
            if (at >= text.size() || !(text[at] >= '0' && text[at] <= '9')) return false;
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        }
        const auto result = std::from_chars(text.data() + start, text.data() + at, out);
        return result.ec == std::errc{} || result.ec == std::errc::result_out_of_range;
    }
    bool Parse(Value& value, int depth) {
        if (depth > MaxDepth) return false;
        Space();
        if (at >= text.size()) return false;
        const char c = text[at];
        if (c == '{') {
            ++at;
            value.kind = Value::Kind::Object;
            if (Eat('}')) return true;
            for (;;) {
                Space();
                std::string key;
                if (!String(key) || !Eat(':')) return false;
                Value item;
                if (!Parse(item, depth + 1)) return false;
                value.object.emplace_back(std::move(key), std::move(item));
                if (Eat(',')) continue;
                return Eat('}');
            }
        }
        if (c == '[') {
            ++at;
            value.kind = Value::Kind::Array;
            if (Eat(']')) return true;
            for (;;) {
                Value item;
                if (!Parse(item, depth + 1)) return false;
                value.array.push_back(std::move(item));
                if (Eat(',')) continue;
                return Eat(']');
            }
        }
        if (c == '"') { value.kind = Value::Kind::String; return String(value.string); }
        if (c == 't') { value.kind = Value::Kind::Bool; value.boolean = true; return Literal("true"); }
        if (c == 'f') { value.kind = Value::Kind::Bool; return Literal("false"); }
        if (c == 'n') return Literal("null");
        value.kind = Value::Kind::Number;
        return Number(value.number);
    }
};

void Write(std::string& out, const Value& value) {
    switch (value.kind) {
    case Value::Kind::Null: out += "null"; break;
    case Value::Kind::Bool: out += value.boolean ? "true" : "false"; break;
    case Value::Kind::Number: {
        if (!std::isfinite(value.number)) { out += "null"; break; }
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.number);
        out.append(buffer, result.ptr);
        break;
    }
    case Value::Kind::String: Quote(out, value.string); break;
    case Value::Kind::Array:
        out.push_back('[');
        for (size_t i = 0; i < value.array.size(); ++i) { if (i) out.push_back(','); Write(out, value.array[i]); }
        out.push_back(']');
        break;
    case Value::Kind::Object:
        out.push_back('{');
        for (size_t i = 0; i < value.object.size(); ++i) {
            if (i) out.push_back(',');
            Quote(out, value.object[i].first);
            out.push_back(':');
            Write(out, value.object[i].second);
        }
        out.push_back('}');
        break;
    }
}
}

const Value* Value::Find(std::string_view key) const {
    if (kind != Kind::Object) return nullptr;
    for (const auto& [name, item] : object) if (name == key) return &item;
    return nullptr;
}

std::string Value::Text(std::string_view key) const {
    const auto* item = Find(key);
    if (!item) return {};
    if (item->kind == Kind::String) return item->string;
    if (item->kind == Kind::Number) return Serialize(*item);
    return {};
}

double Value::Number(std::string_view key, double fallback) const {
    const auto* item = Find(key);
    if (item && item->kind == Kind::Number) return item->number;
    if (item && item->kind == Kind::String) {
        double parsed{};
        const auto& s = item->string;
        const auto result = std::from_chars(s.data(), s.data() + s.size(), parsed);
        if (result.ec == std::errc{} && result.ptr == s.data() + s.size()) return parsed;
    }
    return fallback;
}

std::optional<Value> Parse(std::string_view text) {
    if (text.size() >= 3 && text.substr(0, 3) == "\xef\xbb\xbf") text.remove_prefix(3);
    Parser parser{text};
    Value value;
    if (!parser.Parse(value, 0)) return std::nullopt;
    parser.Space();
    if (parser.at != text.size()) return std::nullopt;
    return value;
}

void Quote(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else out.push_back(c);
        }
    }
    out.push_back('"');
}

std::string Quote(std::string_view text) { std::string out; Quote(out, text); return out; }
std::string Serialize(const Value& value) { std::string out; Write(out, value); return out; }

std::string Utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Utf16(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}
}
