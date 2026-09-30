#include "translate/engine.h"
#include "translate/crypto.h"
#include "translate/json.h"
#include "translate/offline.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <thread>

namespace lumashot::translate {
namespace {
struct LanguageInfo {
    Language language;
    std::wstring_view label;
    std::string_view key, deepl, baidu, youdao, tencent, english, qwen;
    std::wstring_view chinese;
};
constexpr std::array<LanguageInfo, LanguageCount> Languages{{
    {Language::Auto, L"自动", "auto", "", "auto", "auto", "auto", "", "auto", L""},
    {Language::ChineseSimplified, L"简体中文", "zh-Hans", "ZH-HANS", "zh", "zh-CHS", "zh", "Simplified Chinese", "Chinese", L"中文"},
    {Language::ChineseTraditional, L"繁體中文", "zh-Hant", "ZH-HANT", "cht", "zh-CHT", "zh-TW", "Traditional Chinese", "Traditional Chinese", L"繁体中文"},
    {Language::English, L"English", "en", "EN-US", "en", "en", "en", "English", "English", L"英语"},
    {Language::Japanese, L"日本語", "ja", "JA", "jp", "ja", "ja", "Japanese", "Japanese", L"日语"},
    {Language::Korean, L"한국어", "ko", "KO", "kor", "ko", "ko", "Korean", "Korean", L"韩语"},
    {Language::French, L"Français", "fr", "FR", "fra", "fr", "fr", "French", "French", L"法语"},
    {Language::German, L"Deutsch", "de", "DE", "de", "de", "de", "German", "German", L"德语"},
    {Language::Spanish, L"Español", "es", "ES", "spa", "es", "es", "Spanish", "Spanish", L"西班牙语"},
    {Language::Russian, L"Русский", "ru", "RU", "ru", "ru", "ru", "Russian", "Russian", L"俄语"},
}};
const LanguageInfo& Info(Language language) { return Languages[std::min<size_t>(static_cast<size_t>(language), Languages.size() - 1)]; }

constexpr std::array<Preset, 14> PresetTable{{
    {"offline", L"离线翻译", Engine::OpenAi, "", "hy-mt1.5-1.8b", true, L"", L"", L"在本机运行开源翻译模型：不联网、不要密钥，文字不离开电脑"},
    {"ollama", L"本地 Ollama", Engine::OpenAi, "http://127.0.0.1:11434/v1", "", true, L"", L"", L"安装 Ollama 并拉取模型，例如 ollama pull qwen3:8b"},
    {"lmstudio", L"本地 LM Studio", Engine::OpenAi, "http://127.0.0.1:1234/v1", "", true, L"", L"", L"在 LM Studio 中加载模型并启动本地服务器"},
    {"deepseek", L"DeepSeek", Engine::OpenAi, "https://api.deepseek.com", "deepseek-v4-flash", false, L"", L"API Key", L"platform.deepseek.com → API Keys"},
    {"qwen", L"通义千问（阿里云百炼）", Engine::OpenAi, "https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-mt-turbo", false, L"", L"API Key", L"bailian.console.aliyun.com → API-KEY"},
    {"siliconflow", L"硅基流动", Engine::OpenAi, "https://api.siliconflow.cn/v1", "tencent/Hunyuan-MT-7B", false, L"", L"API Key", L"cloud.siliconflow.cn → API 密钥（混元翻译模型免费）"},
    {"zhipu", L"智谱 GLM", Engine::OpenAi, "https://open.bigmodel.cn/api/paas/v4", "glm-4-flash", false, L"", L"API Key", L"open.bigmodel.cn → API Keys"},
    {"kimi", L"Kimi（月之暗面）", Engine::OpenAi, "https://api.moonshot.cn/v1", "moonshot-v1-8k", false, L"", L"API Key", L"platform.moonshot.cn → API Key 管理"},
    {"openai", L"OpenAI", Engine::OpenAi, "https://api.openai.com/v1", "gpt-5-mini", false, L"", L"API Key", L"platform.openai.com → API keys"},
    {"custom", L"自定义（OpenAI 兼容）", Engine::OpenAi, "", "", false, L"", L"API Key（可选）", L"填写 …/v1 形式的接口地址，vLLM、One API 等均可"},
    {"deepl", L"DeepL", Engine::DeepL, "", "", false, L"", L"Authentication Key", L"deepl.com → 账户 → API Keys（免费版密钥以 :fx 结尾）"},
    {"baidu", L"百度翻译", Engine::Baidu, "", "", false, L"APP ID", L"密钥", L"fanyi-api.baidu.com → 管理控制台 → 开发者信息"},
    {"youdao", L"有道智云", Engine::Youdao, "", "", false, L"应用ID", L"应用密钥", L"ai.youdao.com → 应用总览（需绑定批量文本翻译）"},
    {"tencent", L"腾讯云机器翻译", Engine::Tencent, "", "", false, L"SecretId", L"SecretKey", L"console.cloud.tencent.com/cam/capi"},
}};

bool Kana(wchar_t c) { return (c >= 0x3040 && c <= 0x30ff) || (c >= 0x31f0 && c <= 0x31ff) || (c >= 0xff66 && c <= 0xff9d); }
bool Hangul(wchar_t c) { return (c >= 0xac00 && c <= 0xd7af) || (c >= 0x1100 && c <= 0x11ff) || (c >= 0x3130 && c <= 0x318f); }
bool Han(wchar_t c) { return (c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf) || (c >= 0xf900 && c <= 0xfaff); }
bool Cyrillic(wchar_t c) { return c >= 0x0400 && c <= 0x04ff; }
bool Latin(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= 0xc0 && c <= 0x24f && c != 0xd7 && c != 0xf7); }

std::string Lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string Trim(std::string_view text) {
    size_t a = 0, b = text.size();
    while (a < b && static_cast<unsigned char>(text[a]) <= ' ') ++a;
    while (b > a && static_cast<unsigned char>(text[b - 1]) <= ' ') --b;
    return std::string(text.substr(a, b - a));
}

std::wstring Flatten(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        if (c == L'\r' || c == L'\n' || c == L'\t') { if (!out.empty() && out.back() != L' ') out.push_back(L' '); }
        else out.push_back(c);
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

std::string Host(std::string_view url) {
    auto start = url.find("://");
    start = start == std::string_view::npos ? 0 : start + 3;
    auto end = url.find_first_of(":/?#", start);
    return Lower(url.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
}

std::string Base(std::string_view endpoint) {
    std::string base = Trim(endpoint);
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

std::string ChatUrl(std::string_view endpoint) {
    auto base = Base(endpoint);
    const auto lower = Lower(base);
    if (lower.size() >= 17 && lower.compare(lower.size() - 17, 17, "/chat/completions") == 0) return base;
    return base + "/chat/completions";
}

std::string ModelsUrl(std::string_view endpoint) {
    auto base = Base(endpoint);
    const auto lower = Lower(base);
    if (lower.size() >= 17 && lower.compare(lower.size() - 17, 17, "/chat/completions") == 0) base.resize(base.size() - 17);
    return base + "/models";
}

enum class Protocol { JsonArray, QwenMt, Hunyuan, Gemma };
Protocol ProtocolFor(const Credentials& c) {
    const auto model = Lower(c.model);
    if (model.find("qwen-mt") != std::string::npos) return Protocol::QwenMt;
    if (model.find("hunyuan-mt") != std::string::npos || model.find("hy-mt") != std::string::npos) return Protocol::Hunyuan;
    if (model.find("translategemma") != std::string::npos) return Protocol::Gemma;
    return Protocol::JsonArray;
}
bool Offline(const Credentials& c) { return c.preset && c.preset->id == "offline"; }
bool Chinese(Language l) { return l == Language::ChineseSimplified || l == Language::ChineseTraditional; }
// TranslateGemma language names/codes follow its model card (zh-Hans / zh-Hant are both "Chinese").
std::string GemmaName(Language l) { return Chinese(l) ? std::string("Chinese") : std::string(Info(l).english); }

std::string Joined(const Job& job) {
    std::string out;
    for (size_t i = 0; i < job.texts.size(); ++i) { if (i) out.push_back('\n'); out += json::Utf8(Flatten(job.texts[i])); }
    return out;
}

std::string FormPair(std::string_view name, std::string_view value) { return std::string(name) + "=" + crypto::FormEncode(value); }

std::wstring Wide(std::string_view text) { return json::Utf16(text); }

std::wstring HttpProblem(const http::Response& r) {
    switch (r.status) {
    case http::Status::BadUrl: return L"接口地址无效";
    case http::Status::Insecure: return L"非本机/局域网地址必须使用 https://";
    case http::Status::Timeout: return L"连接超时，请检查网络或服务是否在运行";
    case http::Status::Canceled: return L"已取消";
    case http::Status::TooLarge: return L"服务返回的数据过大";
    case http::Status::Network: {
        if (r.error == 12029 || r.error == 12007) return L"无法连接到翻译服务（" + std::to_wstring(r.error) + L"），请检查地址、网络或本地服务是否已启动";
        return L"网络错误（" + std::to_wstring(r.error) + L"）";
    }
    case http::Status::Ok: break;
    }
    if (r.http_status == 401 || r.http_status == 403) return L"密钥无效或没有权限（HTTP " + std::to_wstring(r.http_status) + L"）";
    if (r.http_status == 404) return L"接口或模型不存在（HTTP 404），请检查地址与模型名";
    if (r.http_status == 429) return L"请求过于频繁或额度已用完（HTTP 429）";
    if (r.http_status == 456) return L"DeepL 本月额度已用完（HTTP 456）";
    return L"服务返回 HTTP " + std::to_wstring(r.http_status);
}

std::wstring Detail(const std::wstring& base, std::string_view message) {
    const auto text = Trim(message);
    if (text.empty()) return base;
    auto wide = Wide(text.substr(0, 240));
    return base + L"：" + wide;
}

// Pulls the most specific message out of common error envelopes.
std::string ErrorMessage(const json::Value& root) {
    if (const auto* e = root.Find("error")) {
        if (e->IsString()) return e->string;
        if (e->IsObject()) { auto m = e->Text("message"); if (!m.empty()) return m; }
    }
    if (auto m = root.Text("message"); !m.empty()) return m;
    if (auto m = root.Text("error_msg"); !m.empty()) return m;
    if (const auto* response = root.Find("Response")) if (const auto* e = response->Find("Error")) return e->Text("Code") + " " + e->Text("Message");
    return {};
}

void StripThinking(std::string& content) {
    for (;;) {
        const auto open = content.find("<think>");
        if (open == std::string::npos) break;
        const auto close = content.find("</think>", open);
        if (close == std::string::npos) { content.erase(open); break; }
        content.erase(open, close + 8 - open);
    }
}

std::optional<std::vector<std::wstring>> StringArray(const json::Value& value) {
    const json::Value* array = &value;
    if (value.IsObject()) {
        array = nullptr;
        for (const auto& [name, item] : value.object) if (item.IsArray()) { array = &item; break; }
        if (!array) return std::nullopt;
    }
    if (!array->IsArray()) return std::nullopt;
    std::vector<std::wstring> out;
    for (const auto& item : array->array) {
        if (item.IsString()) out.push_back(Wide(Trim(item.string)));
        else if (item.kind == json::Value::Kind::Number) out.push_back(Wide(json::Serialize(item)));
        else if (item.IsObject() && item.Find("translation")) out.push_back(Wide(Trim(item.Text("translation"))));
        else return std::nullopt;
    }
    return out;
}

void ParseLlmContent(std::string content, const Job& job, Protocol protocol, Outcome& out) {
    StripThinking(content);
    content = Trim(content);
    if (content.rfind("```", 0) == 0) {
        const auto first = content.find('\n');
        const auto last = content.rfind("```");
        if (first != std::string::npos && last != std::string::npos && last > first) content = Trim(content.substr(first + 1, last - first - 1));
    }
    if (protocol == Protocol::JsonArray) {
        std::optional<std::vector<std::wstring>> items;
        if (auto whole = json::Parse(content)) items = StringArray(*whole);
        if (!items) {
            const auto open = content.find('['), close = content.rfind(']');
            if (open != std::string::npos && close != std::string::npos && close > open)
                if (auto part = json::Parse(content.substr(open, close - open + 1))) items = StringArray(*part);
        }
        if (items && items->size() == job.texts.size()) { out.ok = true; out.texts = std::move(*items); return; }
        // A single string may come back bare instead of wrapped in an array.
        if (job.texts.size() == 1 && !content.empty()) { out.ok = true; out.texts = {Wide(content)}; return; }
        out.format_mismatch = true;
        out.error = L"模型返回的译文条数与原文不一致";
        return;
    }
    if (job.texts.size() == 1) { out.ok = true; out.texts = {Wide(content)}; return; }
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= content.size()) {
        auto end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();
        auto line = Trim(std::string_view(content).substr(start, end - start));
        if (!line.empty()) lines.push_back(Wide(line));
        start = end + 1;
    }
    if (lines.size() == job.texts.size()) { out.ok = true; out.texts = std::move(lines); return; }
    out.format_mismatch = true;
    out.error = L"模型返回的译文行数与原文不一致";
}

std::string UtcDate(std::time_t timestamp) {
    std::tm tm{};
    gmtime_s(&tm, &timestamp);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buffer;
}

std::string_view BaiduError(std::string_view code) {
    if (code == "52001") return "请求超时，请重试";
    if (code == "52002") return "系统错误，请重试";
    if (code == "52003") return "未授权用户，请检查 APP ID 是否正确或服务是否开通";
    if (code == "54000") return "必填参数为空";
    if (code == "54001") return "签名错误，请检查密钥";
    if (code == "54003") return "访问频率受限";
    if (code == "54004") return "账户余额不足";
    if (code == "54005") return "长 query 请求频繁";
    if (code == "58000") return "客户端 IP 非法";
    if (code == "58001") return "译文语言方向不支持";
    if (code == "58002") return "服务当前已关闭";
    if (code == "90107") return "认证未通过或未生效";
    return {};
}

std::string_view YoudaoError(std::string_view code) {
    if (code == "101") return "缺少必填参数";
    if (code == "102") return "不支持的语言类型";
    if (code == "108") return "应用ID无效";
    if (code == "110") return "无相关服务的有效应用，请在控制台为应用绑定批量文本翻译服务";
    if (code == "111") return "开发者账号无效";
    if (code == "113") return "翻译文本不能为空";
    if (code == "202") return "签名检验失败，请检查应用ID与应用密钥";
    if (code == "206") return "时间戳无效，请校准系统时间";
    if (code == "401") return "账户已欠费";
    if (code == "411") return "访问频率受限";
    if (code == "412") return "长请求过于频繁";
    return {};
}
}

std::wstring_view LanguageLabel(Language language) { return Info(language).label; }
std::string_view LanguageKey(Language language) { return Info(language).key; }
Language LanguageFromKey(std::string_view key) {
    for (const auto& info : Languages) if (info.key == key) return info.language;
    return Language::Auto;
}

Language Detect(std::wstring_view text) {
    size_t kana = 0, hangul = 0, han = 0, cyrillic = 0, latin = 0;
    for (const wchar_t c : text) {
        if (Kana(c)) ++kana;
        else if (Hangul(c)) ++hangul;
        else if (Han(c)) ++han;
        else if (Cyrillic(c)) ++cyrillic;
        else if (Latin(c)) ++latin;
    }
    const size_t cjk = kana + hangul + han;
    // One CJK character carries roughly a word; weight it against Latin letters.
    if (cjk && cjk * 4 >= latin) {
        if (hangul >= std::max<size_t>(2, (kana + han) / 2)) return Language::Korean;
        if (kana >= 2 && kana * 10 >= cjk) return Language::Japanese;
        return han ? Language::ChineseSimplified : (kana ? Language::Japanese : Language::Korean);
    }
    if (cyrillic && cyrillic >= latin) return Language::Russian;
    if (latin || cyrillic) return Language::English;
    return Language::Auto;
}

Language ResolveTarget(Language configured, Language source) {
    const auto chinese = [](Language l) { return l == Language::ChineseSimplified || l == Language::ChineseTraditional; };
    if (configured != Language::Auto && !(configured == source || (chinese(configured) && chinese(source)))) return configured;
    return chinese(source) ? Language::English : Language::ChineseSimplified;
}

std::span<const Preset> Presets() { return PresetTable; }
const Preset* FindPreset(std::string_view id) {
    for (const auto& preset : PresetTable) if (preset.id == id) return &preset;
    return nullptr;
}

const ProviderSettings* Config::Settings(std::string_view id) const {
    const auto it = providers.find(std::string(id));
    return it == providers.end() ? nullptr : &it->second;
}
ProviderSettings& Config::Edit(const std::string& id) { return providers[id]; }

std::optional<Credentials> Resolve(const Config& config, std::wstring* reason) {
    const auto fail = [&](std::wstring text) { if (reason) *reason = std::move(text); return std::nullopt; };
    const auto* preset = FindPreset(config.provider);
    if (!preset) return fail(L"尚未选择翻译引擎");
    Credentials c;
    c.preset = preset;
    c.endpoint = std::string(preset->endpoint);
    c.model = std::string(preset->model);
    if (const auto* s = config.Settings(preset->id)) {
        if (!Trim(s->endpoint).empty()) c.endpoint = Trim(s->endpoint);
        if (!Trim(s->model).empty()) c.model = Trim(s->model);
        c.app_id = Trim(s->app_id);
        c.region = Trim(s->region);
        if (!s->protected_key.empty()) {
            auto key = crypto::Unprotect(s->protected_key);
            if (!key) return fail(L"无法解密已保存的密钥，请重新填写");
            c.key = Trim(*key);
        }
    }
    if (preset->id == "offline") {
        const auto* model = offline::FindModel(c.model);
        if (!model) return fail(L"未知的离线模型，请在引擎设置中重新选择");
        if (!offline::RuntimeInstalled() || !offline::ModelInstalled(*model)) return fail(L"离线模型「" + std::wstring(model->name) + L"」还没有下载");
        return c;
    }
    if (preset->engine == Engine::OpenAi) {
        if (c.endpoint.empty()) return fail(L"请填写接口地址");
        if (c.model.empty()) return fail(L"请选择或填写模型");
        if (!preset->local && preset->id != "custom" && c.key.empty()) return fail(L"请填写 API Key");
    } else {
        if (!preset->id_label.empty() && c.app_id.empty()) return fail(std::wstring(L"请填写 ") + std::wstring(preset->id_label));
        if (c.key.empty()) return fail(std::wstring(L"请填写 ") + std::wstring(preset->key_label));
    }
    if (preset->engine == Engine::Tencent && c.region.empty()) c.region = "ap-guangzhou";
    return c;
}

bool Ready(const Config& config) { return Resolve(config).has_value(); }

size_t ChunkLimit(const Credentials& c) {
    switch (c.preset ? c.preset->engine : Engine::OpenAi) {
    case Engine::DeepL: return 20000;
    case Engine::Baidu: return 1800;
    case Engine::Youdao: return 1800;
    case Engine::Tencent: return 1800;
    case Engine::OpenAi: return Offline(c) ? 1200 : c.preset && c.preset->local ? 1500 : 3000;
    }
    return 1500;
}

std::wstring YoudaoTruncate(std::wstring_view text) {
    if (text.size() <= 20) return std::wstring(text);
    return std::wstring(text.substr(0, 10)) + std::to_wstring(text.size()) + std::wstring(text.substr(text.size() - 10));
}

std::string TencentAuthorization(std::string_view secret_id, std::string_view secret_key, std::string_view host,
    std::string_view payload, std::time_t timestamp, std::string_view service) {
    const auto date = UtcDate(timestamp);
    const std::string canonical = "POST\n/\n\ncontent-type:application/json; charset=utf-8\nhost:" + std::string(host)
        + "\n\ncontent-type;host\n" + crypto::Hex(crypto::Sha256(payload));
    const std::string scope = date + "/" + std::string(service) + "/tc3_request";
    const std::string to_sign = "TC3-HMAC-SHA256\n" + std::to_string(timestamp) + "\n" + scope + "\n" + crypto::Hex(crypto::Sha256(canonical));
    const auto k_date = crypto::HmacSha256("TC3" + std::string(secret_key), date);
    const auto k_service = crypto::HmacSha256(k_date, service);
    const auto k_signing = crypto::HmacSha256(k_service, "tc3_request");
    const auto signature = crypto::Hex(crypto::HmacSha256(k_signing, to_sign));
    return "TC3-HMAC-SHA256 Credential=" + std::string(secret_id) + "/" + scope + ", SignedHeaders=content-type;host, Signature=" + signature;
}

http::Request BuildRequest(const Credentials& c, const Job& job, const Stamp& stamp) {
    http::Request r;
    const auto& target = Info(job.target);
    switch (c.preset->engine) {
    case Engine::OpenAi: {
        const auto protocol = ProtocolFor(c);
        if (protocol == Protocol::Gemma) {
            // TranslateGemma's chat template needs structured content, so the
            // prompt is rendered here and sent to llama-server's raw /completion.
            auto base = Base(c.endpoint);
            if (Lower(base).ends_with("/v1")) base.resize(base.size() - 3);
            r.url = base + "/completion";
            r.headers.emplace_back("Content-Type", "application/json");
            std::wstring joined;
            for (const auto& text : job.texts) { if (!joined.empty()) joined.push_back(L'\n'); joined += Flatten(text); }
            Language source = job.source != Language::Auto ? job.source : Detect(joined);
            if (source == Language::Auto) source = Language::English;
            const auto& from = Info(source);
            const std::string s_name = GemmaName(source), t_name = GemmaName(job.target);
            const std::string prompt = "<start_of_turn>user\nYou are a professional " + s_name + " (" + std::string(from.key) + ") to " + t_name + " (" + std::string(target.key)
                + ") translator. Your goal is to accurately convey the meaning and nuances of the original " + s_name + " text while adhering to " + t_name
                + " grammar, vocabulary, and cultural sensitivities.\nProduce only the " + t_name + " translation, without any additional explanations or commentary. Please translate the following "
                + s_name + " text into " + t_name + ":\n\n\n" + json::Utf8(joined) + "<end_of_turn>\n<start_of_turn>model\n";
            r.body = "{\"prompt\":" + json::Quote(prompt) + ",\"n_predict\":2048,\"temperature\":0,\"stream\":false,\"cache_prompt\":true}";
            break;
        }
        r.url = ChatUrl(c.endpoint);
        r.headers.emplace_back("Content-Type", "application/json");
        if (!c.key.empty()) r.headers.emplace_back("Authorization", "Bearer " + c.key);
        std::string body = "{\"model\":" + json::Quote(c.model) + ",\"stream\":false,\"messages\":[";
        if (protocol == Protocol::QwenMt) {
            body += "{\"role\":\"user\",\"content\":" + json::Quote(Joined(job)) + "}]";
            body += ",\"translation_options\":{\"source_lang\":\"auto\",\"target_lang\":" + json::Quote(target.qwen) + "}";
        } else if (protocol == Protocol::Hunyuan) {
            // Official Hunyuan-MT templates: Chinese instruction when either side is Chinese.
            const bool chinese = Chinese(job.target) || Chinese(job.source);
            const std::string prompt = chinese
                ? "将以下文本翻译为" + json::Utf8(target.chinese) + "，注意只需要输出翻译后的结果，不要额外解释：\n\n"
                : "Translate the following segment into " + std::string(target.english) + ", without additional explanation.\n\n";
            body += "{\"role\":\"user\",\"content\":" + json::Quote(prompt + Joined(job)) + "}]";
            // Sampling recommended by the model card (llama-server field names).
            if (Offline(c)) body += ",\"temperature\":0.7,\"top_k\":20,\"top_p\":0.6,\"repeat_penalty\":1.05";
        } else {
            const std::string system =
                "You are a professional translation engine. Translate every string in the user's JSON array into "
                + std::string(target.english)
                + ". The strings are OCR text from a screenshot: silently repair obvious OCR noise, keep numbers, URLs, code, file paths, "
                  "product names and placeholders unchanged, and keep a string unchanged if it is already in the target language. "
                  "Reply with only a JSON array of translated strings, with exactly the same number of items in the same order, and no explanations.";
            std::string items = "[";
            for (size_t i = 0; i < job.texts.size(); ++i) { if (i) items.push_back(','); json::Quote(items, json::Utf8(job.texts[i])); }
            items.push_back(']');
            body += "{\"role\":\"system\",\"content\":" + json::Quote(system) + "},{\"role\":\"user\",\"content\":" + json::Quote(items) + "}]";
        }
        if (Host(c.endpoint).find("deepseek.com") != std::string::npos) body += ",\"thinking\":{\"type\":\"disabled\"}";
        body.push_back('}');
        r.body = std::move(body);
        break;
    }
    case Engine::DeepL: {
        const bool free = c.key.size() > 3 && c.key.compare(c.key.size() - 3, 3, ":fx") == 0;
        r.url = !c.endpoint.empty() ? Base(c.endpoint) + "/v2/translate" : (free ? "https://api-free.deepl.com/v2/translate" : "https://api.deepl.com/v2/translate");
        r.headers.emplace_back("Content-Type", "application/json");
        r.headers.emplace_back("Authorization", "DeepL-Auth-Key " + c.key);
        std::string body = "{\"text\":[";
        for (size_t i = 0; i < job.texts.size(); ++i) { if (i) body.push_back(','); json::Quote(body, json::Utf8(job.texts[i])); }
        body += "],\"target_lang\":" + json::Quote(target.deepl) + "}";
        r.body = std::move(body);
        break;
    }
    case Engine::Baidu: {
        r.url = !c.endpoint.empty() ? Base(c.endpoint) + "/api/trans/vip/translate" : "https://fanyi-api.baidu.com/api/trans/vip/translate";
        r.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
        const auto q = Joined(job);
        const auto sign = crypto::Hex(crypto::Md5(c.app_id + q + stamp.salt + c.key));
        r.body = FormPair("q", q) + "&" + FormPair("from", "auto") + "&" + FormPair("to", target.baidu) + "&" + FormPair("appid", c.app_id)
            + "&" + FormPair("salt", stamp.salt) + "&" + FormPair("sign", sign);
        break;
    }
    case Engine::Youdao: {
        r.url = !c.endpoint.empty() ? Base(c.endpoint) + "/v2/api" : "https://openapi.youdao.com/v2/api";
        r.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
        std::wstring all;
        std::string body;
        for (const auto& text : job.texts) {
            const auto flat = Flatten(text);
            all += flat;
            body += FormPair("q", json::Utf8(flat)) + "&";
        }
        const auto curtime = std::to_string(stamp.now);
        const auto sign = crypto::Hex(crypto::Sha256(c.app_id + json::Utf8(YoudaoTruncate(all)) + stamp.salt + curtime + c.key));
        body += FormPair("from", "auto") + "&" + FormPair("to", target.youdao) + "&" + FormPair("appKey", c.app_id) + "&" + FormPair("salt", stamp.salt)
            + "&" + FormPair("sign", sign) + "&" + FormPair("signType", "v3") + "&" + FormPair("curtime", curtime);
        r.body = std::move(body);
        break;
    }
    case Engine::Tencent: {
        r.url = !c.endpoint.empty() ? Base(c.endpoint) + "/" : "https://tmt.tencentcloudapi.com/";
        std::string body = "{\"Source\":\"auto\",\"Target\":" + json::Quote(target.tencent) + ",\"ProjectId\":0,\"SourceTextList\":[";
        for (size_t i = 0; i < job.texts.size(); ++i) { if (i) body.push_back(','); json::Quote(body, json::Utf8(Flatten(job.texts[i]))); }
        body += "]}";
        const auto host = c.endpoint.empty() ? std::string("tmt.tencentcloudapi.com") : Host(c.endpoint);
        r.headers.emplace_back("Content-Type", "application/json; charset=utf-8");
        r.headers.emplace_back("X-TC-Action", "TextTranslateBatch");
        r.headers.emplace_back("X-TC-Version", "2018-03-21");
        r.headers.emplace_back("X-TC-Region", c.region.empty() ? "ap-guangzhou" : c.region);
        r.headers.emplace_back("X-TC-Timestamp", std::to_string(stamp.now));
        r.headers.emplace_back("Authorization", TencentAuthorization(c.app_id, c.key, host, body, stamp.now));
        r.body = std::move(body);
        break;
    }
    }
    return r;
}

Outcome ParseResponse(const Credentials& c, const Job& job, const http::Response& response) {
    Outcome out;
    const auto root = response.status == http::Status::Ok ? json::Parse(response.body) : std::nullopt;
    if (!response.Success()) {
        out.error = HttpProblem(response);
        if (root) out.error = Detail(out.error, ErrorMessage(*root));
        return out;
    }
    if (!root) { out.error = L"服务返回的内容不是有效的 JSON"; return out; }
    switch (c.preset->engine) {
    case Engine::OpenAi: {
        if (const auto* e = root->Find("error"); e && e->kind != json::Value::Kind::Null) { out.error = Detail(L"模型服务报错", ErrorMessage(*root)); return out; }
        if (ProtocolFor(c) == Protocol::Gemma) {
            const auto* content = root->Find("content");
            if (!content || !content->IsString()) { out.error = L"模型没有返回译文"; return out; }
            auto text = content->string;
            if (const auto end = text.find("<end_of_turn>"); end != std::string::npos) text.resize(end);
            ParseLlmContent(std::move(text), job, Protocol::Gemma, out);
            return out;
        }
        const auto* choices = root->Find("choices");
        const auto* first = choices ? choices->At(0) : nullptr;
        const auto* message = first ? first->Find("message") : nullptr;
        const auto* content = message ? message->Find("content") : nullptr;
        if (!content || !content->IsString()) { out.error = L"模型没有返回译文"; return out; }
        ParseLlmContent(content->string, job, ProtocolFor(c), out);
        return out;
    }
    case Engine::DeepL: {
        const auto* list = root->Find("translations");
        if (!list || !list->IsArray() || list->array.size() != job.texts.size()) { out.error = Detail(L"DeepL 返回格式异常", ErrorMessage(*root)); return out; }
        for (const auto& item : list->array) out.texts.push_back(Wide(item.Text("text")));
        out.detected = list->array.empty() ? std::string{} : list->array.front().Text("detected_source_language");
        out.ok = true;
        return out;
    }
    case Engine::Baidu: {
        if (auto code = root->Text("error_code"); !code.empty() && code != "52000") {
            const auto known = BaiduError(code);
            out.error = L"百度翻译错误 " + Wide(code) + L"：" + Wide(known.empty() ? root->Text("error_msg") : std::string(known));
            return out;
        }
        const auto* list = root->Find("trans_result");
        if (!list || !list->IsArray()) { out.error = L"百度翻译返回格式异常"; return out; }
        out.detected = root->Text("from");
        if (list->array.size() == job.texts.size()) {
            for (const auto& item : list->array) out.texts.push_back(Wide(item.Text("dst")));
        } else {
            // Rare: the service merged or dropped lines. Match by source text, keep originals otherwise.
            for (const auto& text : job.texts) {
                const auto flat = json::Utf8(Flatten(text));
                std::wstring found = text;
                for (const auto& item : list->array) if (Trim(item.Text("src")) == Trim(flat)) { found = Wide(item.Text("dst")); break; }
                out.texts.push_back(std::move(found));
            }
        }
        out.ok = true;
        return out;
    }
    case Engine::Youdao: {
        const auto code = root->Text("errorCode");
        if (code != "0") {
            const auto known = YoudaoError(code);
            out.error = L"有道翻译错误 " + Wide(code) + (known.empty() ? std::wstring{} : L"：" + Wide(known));
            return out;
        }
        const auto* list = root->Find("translateResults");
        std::vector<bool> failed(job.texts.size());
        if (const auto* errors = root->Find("errorIndex"); errors && errors->IsArray())
            for (const auto& e : errors->array) if (e.kind == json::Value::Kind::Number && e.number >= 0 && e.number < static_cast<double>(failed.size())) failed[static_cast<size_t>(e.number)] = true;
        size_t next = 0;
        for (size_t i = 0; i < job.texts.size(); ++i) {
            const auto* item = !failed[i] && list ? list->At(next) : nullptr;
            if (item) { out.texts.push_back(Wide(item->Text("translation"))); ++next; if (out.detected.empty()) out.detected = item->Text("type"); }
            else out.texts.push_back(job.texts[i]);
        }
        if (!list || next == 0) { out.texts.clear(); out.error = L"有道翻译没有返回译文"; return out; }
        out.ok = true;
        return out;
    }
    case Engine::Tencent: {
        const auto* body = root->Find("Response");
        if (!body) { out.error = L"腾讯翻译返回格式异常"; return out; }
        if (const auto* e = body->Find("Error")) { out.error = L"腾讯翻译错误：" + Wide(e->Text("Code") + " " + e->Text("Message")); return out; }
        const auto* list = body->Find("TargetTextList");
        if (!list || !list->IsArray() || list->array.size() != job.texts.size()) { out.error = L"腾讯翻译返回条数异常"; return out; }
        for (const auto& item : list->array) out.texts.push_back(Wide(item.IsString() ? item.string : std::string{}));
        out.detected = body->Text("Source");
        out.ok = true;
        return out;
    }
    }
    return out;
}

namespace {
Outcome RunOne(const Credentials& c, const Job& job, std::stop_token stop, const http::Limits& limits) {
    const Stamp stamp{std::time(nullptr), c.preset->engine == Engine::Youdao ? crypto::Uuid() : std::to_string(GetTickCount64() % 1000000000ull + 10000)};
    return ParseResponse(c, job, http::Send(BuildRequest(c, job, stamp), stop, limits));
}

Outcome RunChunks(const Credentials& c, const Job& job, std::stop_token stop, const http::Limits& limits) {
    Outcome total;
    const size_t limit = ChunkLimit(c);
    const size_t max_items = c.preset->engine == Engine::DeepL ? 50 : Offline(c) ? 16 : (c.preset->engine == Engine::OpenAi ? 40 : 100);
    size_t index = 0;
    bool first_request = true;
    while (index < job.texts.size()) {
        Job chunk{{}, job.source, job.target};
        size_t chars = 0;
        while (index < job.texts.size() && chunk.texts.size() < max_items && (chunk.texts.empty() || chars + job.texts[index].size() <= limit)) {
            chars += job.texts[index].size();
            chunk.texts.push_back(job.texts[index++]);
        }
        if (stop.stop_requested()) { total.error = L"已取消"; return total; }
        // Baidu's standard tier allows one request per second.
        if (!first_request && c.preset->engine == Engine::Baidu) std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        first_request = false;
        auto part = RunOne(c, chunk, stop, limits);
        if (!part.ok && part.format_mismatch && chunk.texts.size() > 1) {
            part = {};
            part.ok = true;
            for (const auto& text : chunk.texts) {
                auto single = RunOne(c, Job{{text}, job.source, job.target}, stop, limits);
                if (!single.ok) { part = std::move(single); break; }
                part.texts.push_back(single.texts.front());
            }
        }
        if (!part.ok) { total.error = part.error; total.texts.clear(); return total; }
        if (total.detected.empty()) total.detected = part.detected;
        total.texts.insert(total.texts.end(), part.texts.begin(), part.texts.end());
    }
    total.ok = true;
    return total;
}
}

Outcome Run(const Credentials& c, const Job& job, std::stop_token stop, const http::Limits& limits) {
    Outcome total;
    if (!c.preset) { total.error = L"尚未选择翻译引擎"; return total; }
    if (!Offline(c)) return RunChunks(c, job, stop, limits);
    const auto* model = offline::FindModel(c.model);
    if (!model) { total.error = L"未知的离线模型"; return total; }
    std::wstring reason;
    const auto base = offline::Acquire(*model, stop, &reason);
    if (!base) { total.error = reason; return total; }
    Credentials local = c;
    local.endpoint = *base + "/v1";
    // First tokens on a cold CPU can take a while; never cut a local job short.
    http::Limits relaxed = limits;
    relaxed.receive = std::max(relaxed.receive, std::chrono::milliseconds(std::chrono::minutes(3)));
    total = RunChunks(local, job, stop, relaxed);
    offline::Release();
    return total;
}

http::Request ModelsRequest(const Credentials& c) {
    http::Request r;
    r.method = "GET";
    r.url = ModelsUrl(c.endpoint);
    if (!c.key.empty()) r.headers.emplace_back("Authorization", "Bearer " + c.key);
    return r;
}

std::vector<std::string> ParseModels(std::string_view body) {
    std::vector<std::string> out;
    const auto root = json::Parse(body);
    if (!root) return out;
    const json::Value* list = root->Find("data");
    if (!list) list = root->Find("models");
    if (!list || !list->IsArray()) return out;
    for (const auto& item : list->array) {
        auto id = item.Text("id");
        if (id.empty()) id = item.Text("name");
        if (id.empty() || std::find(out.begin(), out.end(), id) != out.end()) continue;
        // Embedding / speech models cannot translate.
        const auto lower = Lower(id);
        if (lower.find("embed") != std::string::npos || lower.find("whisper") != std::string::npos || lower.find("tts") != std::string::npos || lower.find("rerank") != std::string::npos) continue;
        out.push_back(std::move(id));
    }
    return out;
}

std::vector<std::string> ListModels(const Credentials& c, std::stop_token stop, std::chrono::milliseconds timeout) {
    http::Limits limits;
    limits.connect = timeout;
    limits.receive = timeout;
    limits.max_bytes = 2u << 20;
    const auto response = http::Send(ModelsRequest(c), stop, limits);
    if (!response.Success()) return {};
    return ParseModels(response.body);
}
}
