#pragma once
#include "translate/http_client.h"
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

// Screenshot translation engines. Only OCR text is ever sent. Pure request
// builders and response parsers are separated from the blocking Run() so the
// wire formats can be verified against a loopback fake without real services.
namespace lumashot::translate {
enum class Language : uint8_t { Auto, ChineseSimplified, ChineseTraditional, English, Japanese, Korean, French, German, Spanish, Russian };
inline constexpr int LanguageCount = 10;
std::wstring_view LanguageLabel(Language language);   // 自动 / 简体中文 / English ...
std::string_view LanguageKey(Language language);      // stable ini token
Language LanguageFromKey(std::string_view key);
// Script-based guess over OCR text: Japanese (kana), Korean (Hangul), Chinese
// (Han), Russian (Cyrillic), otherwise English/Latin. Auto when no letters.
Language Detect(std::wstring_view text);
// Auto: Chinese sources go to English, everything else to Simplified Chinese.
// An explicit target equal to the source falls back to the automatic rule.
Language ResolveTarget(Language configured, Language source);

enum class Engine : uint8_t { OpenAi, DeepL, Baidu, Youdao, Tencent };
struct Preset {
    std::string_view id;
    std::wstring_view label;
    Engine engine;
    std::string_view endpoint;   // OpenAI-compatible base URL (…/v1) or empty for fixed APIs
    std::string_view model;
    bool local{};                // runs on this machine; no key needed
    std::wstring_view id_label;  // non-empty when the engine needs an app id (百度 APP ID, 有道 应用ID, 腾讯 SecretId)
    std::wstring_view key_label; // API Key / 密钥 / SecretKey
    std::wstring_view signup;    // where to get a key (shown as hint)
};
std::span<const Preset> Presets();
const Preset* FindPreset(std::string_view id);

struct ProviderSettings {
    std::string endpoint, model, app_id, region;
    std::string protected_key;   // DPAPI + base64; never plaintext on disk
};

struct Config {
    std::string provider;        // preset id; empty = not configured yet
    Language target{Language::Auto};
    std::map<std::string, ProviderSettings> providers;
    const ProviderSettings* Settings(std::string_view id) const;
    ProviderSettings& Edit(const std::string& id);
};
std::filesystem::path DefaultConfigPath(); // %LOCALAPPDATA%\LumaShot\translation.ini
Config LoadConfig(const std::filesystem::path& path);
bool SaveConfig(const std::filesystem::path& path, const Config& config);

// Plaintext working copy for one translation job; lives only in memory.
struct Credentials {
    const Preset* preset{};
    std::string endpoint, model, app_id, region, key;
};
// Missing preset or required credential → nullopt with a user-facing reason.
std::optional<Credentials> Resolve(const Config& config, std::wstring* reason = nullptr);
bool Ready(const Config& config);

struct Job {
    std::vector<std::wstring> texts;
    Language source{Language::Auto};
    Language target{Language::ChineseSimplified};
};
struct Outcome {
    bool ok{};
    bool format_mismatch{};      // parse succeeded structurally but the item count differs
    std::vector<std::wstring> texts;
    std::wstring error;
    std::string detected;        // provider-reported source language, if any
};
struct Stamp { std::time_t now{}; std::string salt; };

// Maximum characters per request for the engine (split before building).
size_t ChunkLimit(const Credentials& credentials);
http::Request BuildRequest(const Credentials& credentials, const Job& job, const Stamp& stamp);
Outcome ParseResponse(const Credentials& credentials, const Job& job, const http::Response& response);
// Chunks, sends, parses; LLM chunks that come back misaligned are retried per item.
Outcome Run(const Credentials& credentials, const Job& job, std::stop_token stop, const http::Limits& limits = {});

// OpenAI-compatible model discovery: GET {endpoint}/models → data[].id.
http::Request ModelsRequest(const Credentials& credentials);
std::vector<std::string> ParseModels(std::string_view body);
std::vector<std::string> ListModels(const Credentials& credentials, std::stop_token stop, std::chrono::milliseconds timeout);

// Youdao v3 sign input truncation (UTF-16 code units).
std::wstring YoudaoTruncate(std::wstring_view text);
std::string TencentAuthorization(std::string_view secret_id, std::string_view secret_key, std::string_view host,
    std::string_view payload, std::time_t timestamp, std::string_view service = "tmt");
}
