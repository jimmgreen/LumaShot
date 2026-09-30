// Focused tests for screenshot translation core: JSON, signing, languages,
// config/DPAPI, provider wire formats against a loopback fake, cancellation,
// the async service and OCR paragraph grouping. Never contacts real services.
#include "translate/blocks.h"
#include "translate/crypto.h"
#include "translate/engine.h"
#include "translate/json.h"
#include "translate/service.h"
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

using namespace lumashot;
using namespace lumashot::translate;

namespace {
int failures = 0, checks = 0;
void Expect(bool value, const char* what) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}
std::string U8(const std::wstring& w) { return json::Utf8(w); }

struct Captured { std::string method, path, headers, body; };

// Loopback HTTP/1.1 server capturing full requests. Handler returns {status, body};
// status 0 hangs until shutdown (for cancellation tests).
class Server {
public:
    using Handler = std::function<std::pair<int, std::string>(const Captured&)>;
    explicit Server(Handler handler) : handler_(std::move(handler)) {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        int length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        listen(listener_, 16);
        acceptor_ = std::thread([this] {
            for (;;) {
                SOCKET client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET) break;
                std::lock_guard lock(mutex_);
                clients_.emplace_back([this, client] { Serve(client); });
            }
        });
    }
    ~Server() {
        stopping_ = true;
        closesocket(listener_);
        acceptor_.join();
        std::lock_guard lock(mutex_);
        for (auto& client : clients_) client.join();
        WSACleanup();
    }
    std::string Base() const { return "http://127.0.0.1:" + std::to_string(port_); }
    std::vector<Captured> Requests() { std::lock_guard lock(mutex_); return requests_; }
private:
    void Serve(SOCKET client) {
        std::string data;
        char buffer[8192];
        size_t header_end = std::string::npos;
        while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
            const int got = recv(client, buffer, sizeof(buffer), 0);
            if (got <= 0) { closesocket(client); return; }
            data.append(buffer, got);
        }
        Captured request;
        request.headers = data.substr(0, header_end);
        size_t length = 0;
        std::string lower = request.headers;
        for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (const auto at = lower.find("content-length:"); at != std::string::npos) length = std::stoul(lower.substr(at + 15));
        while (data.size() < header_end + 4 + length) {
            const int got = recv(client, buffer, sizeof(buffer), 0);
            if (got <= 0) break;
            data.append(buffer, got);
        }
        request.body = data.substr(header_end + 4);
        const auto first = request.headers.find(' ');
        request.method = request.headers.substr(0, first);
        request.path = request.headers.substr(first + 1, request.headers.find(' ', first + 1) - first - 1);
        { std::lock_guard lock(mutex_); requests_.push_back(request); }
        const auto [status, body] = handler_(request);
        if (status == 0) {
            while (!stopping_) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            const std::string all = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(client, all.data(), static_cast<int>(all.size()), 0);
            shutdown(client, SD_SEND);
        }
        closesocket(client);
    }
    Handler handler_;
    SOCKET listener_{INVALID_SOCKET};
    unsigned short port_{};
    std::atomic_bool stopping_{};
    std::thread acceptor_;
    std::mutex mutex_;
    std::vector<std::thread> clients_;
    std::vector<Captured> requests_;
};

Credentials Make(std::string_view preset, std::string endpoint, std::string key = "k", std::string app_id = "", std::string model = "") {
    Credentials c;
    c.preset = FindPreset(preset);
    c.endpoint = std::move(endpoint);
    c.key = std::move(key);
    c.app_id = std::move(app_id);
    c.model = model.empty() ? std::string(c.preset->model) : std::move(model);
    if (c.model.empty()) c.model = "test-model";
    return c;
}

std::string Chat(const std::string& content) { return "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":" + json::Quote(content) + "}}]}"; }

void JsonTests() {
    auto v = json::Parse(R"({"a":[1,2.5e1,true,false,null],"s":"x\"\\\/\b\f\n\r\t\u4e2d\ud83d\ude00","o":{}} )");
    Expect(v && v->Find("a") && v->Find("a")->array.size() == 5, "json nested array");
    Expect(v && v->Find("a")->array[1].number == 25, "json exponent");
    Expect(v && v->Text("s") == "x\"\\/\b\f\n\r\t\xe4\xb8\xad\xf0\x9f\x98\x80", "json escapes and surrogate pair");
    Expect(!json::Parse("{\"a\":1,}") && !json::Parse("[1 2]") && !json::Parse("\"\x01\"") && !json::Parse("01") && !json::Parse("{} x"), "json rejects invalid input");
    Expect(!json::Parse(std::string(200, '[') + std::string(200, ']')), "json depth limit");
    Expect(json::Quote("a\"b\n\x01") == "\"a\\\"b\\n\\u0001\"", "json quote escapes");
    Expect(json::Utf16("\xe4\xb8\xad") == L"中" && json::Utf8(L"中") == "\xe4\xb8\xad", "utf conversions");
    auto round = json::Parse(json::Serialize(*v));
    Expect(round && round->Text("s") == v->Text("s"), "json serialize round trip");
}

void CryptoTests() {
    Expect(crypto::Hex(crypto::Sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 vector");
    Expect(crypto::Hex(crypto::Md5("")) == "d41d8cd98f00b204e9800998ecf8427e", "md5 vector");
    Expect(crypto::Hex(crypto::HmacSha256("key", "The quick brown fox jumps over the lazy dog")) == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8", "hmac-sha256 vector");
    Expect(crypto::Base64("hello!?") == "aGVsbG8hPw==" && crypto::Unbase64("aGVsbG8hPw==") == std::optional<std::string>("hello!?"), "base64 round trip");
    Expect(!crypto::Unbase64("ab$c"), "base64 rejects garbage");
    Expect(crypto::FormEncode("a b&中~") == "a%20b%26%E4%B8%AD~", "form encoding");
    const auto uuid = crypto::Uuid();
    Expect(uuid.size() == 36 && uuid[14] == '4', "uuid v4 shape");
    const auto sealed = crypto::Protect("sk-secret-123");
    Expect(!sealed.empty() && sealed.find("sk-secret") == std::string::npos, "dpapi output is not plaintext");
    Expect(crypto::Unprotect(sealed) == std::optional<std::string>("sk-secret-123"), "dpapi round trip");
    Expect(!crypto::Unprotect("aGVsbG8="), "dpapi rejects foreign blob");
}

void LanguageTests() {
    Expect(Detect(L"Hello world, how are you?") == Language::English, "detect english");
    Expect(Detect(L"今天天气很好，我们去公园吧") == Language::ChineseSimplified, "detect chinese");
    Expect(Detect(L"今日はいい天気ですね") == Language::Japanese, "detect japanese via kana");
    Expect(Detect(L"안녕하세요 반갑습니다") == Language::Korean, "detect korean");
    Expect(Detect(L"Привет, как дела?") == Language::Russian, "detect russian");
    Expect(Detect(L"设置 Settings") == Language::ChineseSimplified, "short mixed text with CJK counts as chinese");
    Expect(Detect(L"1234 ...") == Language::Auto, "no letters -> auto");
    Expect(ResolveTarget(Language::Auto, Language::English) == Language::ChineseSimplified, "auto: foreign -> zh");
    Expect(ResolveTarget(Language::Auto, Language::ChineseSimplified) == Language::English, "auto: zh -> en");
    Expect(ResolveTarget(Language::Japanese, Language::English) == Language::Japanese, "explicit target honored");
    Expect(ResolveTarget(Language::ChineseSimplified, Language::ChineseSimplified) == Language::English, "explicit target equal to source falls back");
    Expect(LanguageFromKey(LanguageKey(Language::Korean)) == Language::Korean && LanguageFromKey("bogus") == Language::Auto, "language key round trip");
}

void HostTests() {
    Expect(http::PrivateHost(L"127.0.0.1") && http::PrivateHost(L"localhost") && http::PrivateHost(L"192.168.1.20") && http::PrivateHost(L"10.0.0.5") && http::PrivateHost(L"172.20.1.1"), "private hosts allowed");
    Expect(!http::PrivateHost(L"172.32.0.1") && !http::PrivateHost(L"8.8.8.8") && !http::PrivateHost(L"example.com") && !http::PrivateHost(L"127.0.0.1.evil.com"), "public hosts rejected");
    http::Request r;
    r.url = "http://example.com/v1/chat/completions";
    const auto response = http::Send(r, {});
    Expect(response.status == http::Status::Insecure, "plain http to public host is refused before connecting");
    r.url = "ftp://127.0.0.1/";
    Expect(http::Send(r, {}).status == http::Status::BadUrl, "non-http scheme rejected");
}

void ConfigTests(const std::filesystem::path& dir) {
    const auto path = dir / "translation.ini";
    std::filesystem::remove(path);
    Config empty = LoadConfig(path);
    Expect(empty.provider.empty() && !Ready(empty), "missing file -> unconfigured");
    std::wstring reason;
    Expect(!Resolve(empty, &reason) && !reason.empty(), "unconfigured reason");
    Config config;
    config.provider = "deepseek";
    config.target = Language::Japanese;
    config.Edit("deepseek").protected_key = crypto::Protect("sk-deep-777");
    config.Edit("baidu").app_id = "2015063000000001";
    config.Edit("baidu").protected_key = crypto::Protect("12345678");
    config.Edit("ollama").model = "qwen3:8b";
    Expect(SaveConfig(path, config), "save config");
    std::ifstream file(path, std::ios::binary);
    std::stringstream raw;
    raw << file.rdbuf();
    Expect(raw.str().find("sk-deep-777") == std::string::npos && raw.str().find("12345678") == std::string::npos, "secrets never written in plaintext");
    const auto loaded = LoadConfig(path);
    Expect(loaded.provider == "deepseek" && loaded.target == Language::Japanese, "provider and target persist");
    auto resolved = Resolve(loaded);
    Expect(resolved && resolved->key == "sk-deep-777" && resolved->model == "deepseek-v4-flash" && resolved->endpoint == "https://api.deepseek.com", "resolve decrypts key and applies preset defaults");
    Config baidu = loaded;
    baidu.provider = "baidu";
    auto b = Resolve(baidu);
    Expect(b && b->app_id == "2015063000000001" && b->key == "12345678", "baidu credentials resolve");
    Config ollama = loaded;
    ollama.provider = "ollama";
    Expect(Resolve(ollama) && Resolve(ollama)->key.empty(), "local engine needs no key");
    Config missing;
    missing.provider = "tencent";
    Expect(!Resolve(missing, &reason) && reason.find(L"SecretId") != std::wstring::npos, "tencent requires SecretId");
    Config broken = loaded;
    broken.Edit("deepseek").protected_key = "aGVsbG8=";
    Expect(!Resolve(broken, &reason) && reason.find(L"解密") != std::wstring::npos, "undecryptable key reported");
}

void BuilderTests() {
    Job job{{L"Hello", L"World \"quoted\""}, Language::English, Language::ChineseSimplified};
    auto deepseek = Make("deepseek", "https://api.deepseek.com", "sk-1");
    auto r = BuildRequest(deepseek, job, {});
    Expect(r.url == "https://api.deepseek.com/chat/completions", "deepseek chat url");
    auto body = json::Parse(r.body);
    Expect(body && body->Text("model") == "deepseek-v4-flash" && body->Find("thinking") && body->Find("thinking")->Text("type") == "disabled", "deepseek disables thinking");
    const auto* messages = body ? body->Find("messages") : nullptr;
    Expect(messages && messages->array.size() == 2 && messages->array[0].Text("role") == "system" && messages->array[0].Text("content").find("Simplified Chinese") != std::string::npos, "system prompt names target language");
    auto items = messages ? json::Parse(messages->array[1].Text("content")) : std::nullopt;
    Expect(items && items->array.size() == 2 && items->array[1].string == "World \"quoted\"", "user content is JSON array of sources");
    bool bearer = false;
    for (const auto& [k, v] : r.headers) bearer |= k == "Authorization" && v == "Bearer sk-1";
    Expect(bearer, "bearer auth header");

    auto local = Make("ollama", "http://127.0.0.1:11434/v1/", "", "", "qwen3:8b");
    r = BuildRequest(local, job, {});
    bool auth = false;
    for (const auto& h : r.headers) auth |= h.first == "Authorization";
    Expect(r.url == "http://127.0.0.1:11434/v1/chat/completions" && !auth && r.body.find("thinking") == std::string::npos, "local model: trailing slash, no auth, no vendor fields");
    auto full = Make("custom", "https://x.example/v1/chat/completions");
    Expect(BuildRequest(full, job, {}).url == "https://x.example/v1/chat/completions", "full chat url is not doubled");

    auto qwen = Make("qwen", "https://dashscope.aliyuncs.com/compatible-mode/v1");
    body = json::Parse(BuildRequest(qwen, job, {}).body);
    Expect(body && body->Find("translation_options") && body->Find("translation_options")->Text("target_lang") == "Chinese" && body->Find("messages")->array.size() == 1
        && body->Find("messages")->array[0].Text("content") == "Hello\nWorld \"quoted\"", "qwen-mt uses translation_options and newline protocol");
    auto hunyuan = Make("siliconflow", "https://api.siliconflow.cn/v1");
    body = json::Parse(BuildRequest(hunyuan, job, {}).body);
    Expect(body && body->Find("messages")->array[0].Text("content").find("将以下文本翻译为中文") != std::string::npos, "hunyuan-mt prompt template");
    Expect(BuildRequest(hunyuan, job, {}).body.find("top_k") == std::string::npos, "cloud hunyuan keeps provider sampling");

    auto deepl_free = Make("deepl", "", "abc:fx");
    r = BuildRequest(deepl_free, job, {});
    body = json::Parse(r.body);
    Expect(r.url == "https://api-free.deepl.com/v2/translate" && body && body->Text("target_lang") == "ZH-HANS" && body->Find("text")->array.size() == 2, "deepl free endpoint and body");
    Expect(BuildRequest(Make("deepl", "", "abc"), job, {}).url == "https://api.deepl.com/v2/translate", "deepl pro endpoint");

    auto baidu = Make("baidu", "", "12345678", "2015063000000001");
    r = BuildRequest(baidu, Job{{L"apple"}, Language::English, Language::ChineseSimplified}, {0, "1435660288"});
    Expect(r.url == "https://fanyi-api.baidu.com/api/trans/vip/translate" && r.body.find("sign=f89f9594663708c1605f3d736d01d2d4") != std::string::npos && r.body.find("to=zh") != std::string::npos, "baidu md5 sign matches documented example");

    auto youdao = Make("youdao", "", "SECRET", "APPKEY");
    Expect(YoudaoTruncate(L"Hello world, this is LumaShot.第二行文字") == L"Hello worl35Shot.第二行文字", "youdao truncate");
    r = BuildRequest(youdao, Job{{L"Hello world, this is LumaShot.", L"第二行文字"}, Language::English, Language::ChineseSimplified}, {1700000000, "SALT-1"});
    Expect(r.url == "https://openapi.youdao.com/v2/api" && r.body.find("sign=0124ae4eed73e6fa447b5b7ad5625ac5429972bb2e8efdab6d81155a8489e89d") != std::string::npos
        && r.body.find("q=Hello%20world") != std::string::npos && r.body.find("to=zh-CHS") != std::string::npos && r.body.find("signType=v3") != std::string::npos, "youdao v3 sign and repeated q");

    Expect(TencentAuthorization("AKIDexample", "SKEYexample", "tmt.tencentcloudapi.com", R"({"Source":"auto","Target":"zh","ProjectId":0,"SourceTextList":["Hello"]})", 1700000000)
        == "TC3-HMAC-SHA256 Credential=AKIDexample/2023-11-14/tmt/tc3_request, SignedHeaders=content-type;host, Signature=2df81a9299707de82644659968e0e51f45507c7000368a13344dda269708cec9", "tencent TC3 signature matches reference");
    auto tencent = Make("tencent", "", "SKEYexample", "AKIDexample");
    r = BuildRequest(tencent, Job{{L"Hello"}, Language::English, Language::ChineseSimplified}, {1700000000, ""});
    std::string action, authorization;
    for (const auto& [k, v] : r.headers) { if (k == "X-TC-Action") action = v; if (k == "Authorization") authorization = v; }
    Expect(r.body == R"({"Source":"auto","Target":"zh","ProjectId":0,"SourceTextList":["Hello"]})" && action == "TextTranslateBatch" && authorization.find("Signature=2df81a92") != std::string::npos, "tencent request body and headers");
}

void ParserTests() {
    auto c = Make("deepseek", "https://api.deepseek.com");
    Job two{{L"a", L"b"}, Language::English, Language::ChineseSimplified};
    http::Response ok{http::Status::Ok, 0, 200, Chat("<think>hmm</think>```json\n[\"甲\",\"乙\"]\n```")};
    auto out = ParseResponse(c, two, ok);
    Expect(out.ok && out.texts == std::vector<std::wstring>{L"甲", L"乙"}, "llm: strips think block and code fence");
    out = ParseResponse(c, two, {http::Status::Ok, 0, 200, Chat("Here you go: [\"甲\", \"乙\"] hope it helps")});
    Expect(out.ok && out.texts.size() == 2, "llm: extracts embedded array");
    out = ParseResponse(c, two, {http::Status::Ok, 0, 200, Chat("{\"translations\":[\"甲\",\"乙\"]}")});
    Expect(out.ok && out.texts.size() == 2, "llm: object-wrapped array");
    out = ParseResponse(c, two, {http::Status::Ok, 0, 200, Chat("[\"甲\"]")});
    Expect(!out.ok && out.format_mismatch, "llm: count mismatch flagged");
    out = ParseResponse(c, Job{{L"a"}, Language::English, Language::ChineseSimplified}, {http::Status::Ok, 0, 200, Chat("甲乙丙")});
    Expect(out.ok && out.texts == std::vector<std::wstring>{L"甲乙丙"}, "llm: bare single translation");
    out = ParseResponse(c, two, {http::Status::Ok, 0, 401, R"({"error":{"message":"Authentication Fails","type":"auth"}})"});
    Expect(!out.ok && out.error.find(L"密钥无效") != std::wstring::npos && out.error.find(L"Authentication Fails") != std::wstring::npos, "llm: 401 message");
    out = ParseResponse(c, two, {http::Status::Ok, 0, 404, R"({"error":"model 'x' not found"})"});
    Expect(!out.ok && out.error.find(L"not found") != std::wstring::npos, "ollama string error");
    out = ParseResponse(Make("baidu", "", "k", "id"), two, {http::Status::Ok, 0, 200, R"({"error_code":"54001","error_msg":"Invalid Sign"})"});
    Expect(!out.ok && out.error.find(L"签名错误") != std::wstring::npos, "baidu error mapped");
    out = ParseResponse(Make("youdao", "", "k", "id"), two, {http::Status::Ok, 0, 200, R"({"errorCode":"0","errorIndex":[1],"translateResults":[{"query":"a","translation":"甲","type":"en2zh-CHS"}]})"});
    Expect(out.ok && out.texts == std::vector<std::wstring>{L"甲", L"b"}, "youdao partial failure keeps original");
    out = ParseResponse(Make("tencent", "", "k", "id"), two, {http::Status::Ok, 0, 200, R"({"Response":{"Error":{"Code":"AuthFailure.SignatureFailure","Message":"bad"},"RequestId":"x"}})"});
    Expect(!out.ok && out.error.find(L"AuthFailure") != std::wstring::npos, "tencent error surfaced");
    out = ParseResponse(c, two, {http::Status::Network, 12029, 0, ""});
    Expect(!out.ok && out.error.find(L"无法连接") != std::wstring::npos, "connection refused message");
    Expect(ParseModels(R"({"object":"list","data":[{"id":"qwen3:8b"},{"id":"nomic-embed-text"},{"id":"gemma3:4b"}]})") == std::vector<std::string>{"qwen3:8b", "gemma3:4b"}, "model list skips embeddings");
}

void EndToEnd() {
    Server server([](const Captured& r) -> std::pair<int, std::string> {
        if (r.path == "/v1/models") return {200, R"({"data":[{"id":"qwen3:8b"},{"id":"llama3.2"}]})"};
        if (r.path == "/v1/chat/completions") {
            auto body = json::Parse(r.body);
            if (!body) return {400, "{}"};
            const auto model = body->Text("model");
            if (model == "hang") return {0, ""};
            if (model == "bad-key") return {401, R"({"error":{"message":"Incorrect API key provided"}})"};
            const auto& messages = body->Find("messages")->array;
            auto items = json::Parse(messages.back().Text("content"));
            std::string out = "[";
            const size_t n = items && items->IsArray() ? items->array.size() : 1;
            // "sloppy" answers a batch with a single merged string to force the per-item retry.
            if (model == "sloppy" && n > 1) return {200, Chat("[\"merged\"]")};
            for (size_t i = 0; i < n; ++i) { if (i) out += ","; out += json::Quote("译" + (items && items->IsArray() ? items->array[i].string : std::string("x"))); }
            return {200, Chat(out + "]")};
        }
        if (r.path == "/v2/translate") return {200, R"({"translations":[{"detected_source_language":"EN","text":"你好"},{"detected_source_language":"EN","text":"世界"}]})"};
        if (r.path == "/api/trans/vip/translate") return {200, R"({"from":"en","to":"zh","trans_result":[{"src":"Hello","dst":"你好"},{"src":"World","dst":"世界"}]})"};
        if (r.path == "/v2/api") return {200, R"({"errorCode":"0","translateResults":[{"query":"Hello","translation":"你好","type":"en2zh-CHS"},{"query":"World","translation":"世界","type":"en2zh-CHS"}]})"};
        if (r.path == "/") return {200, R"({"Response":{"Source":"en","Target":"zh","TargetTextList":["你好","世界"],"RequestId":"r"}})"};
        return {404, "{}"};
    });
    const auto base = server.Base();
    Job job{{L"Hello", L"World"}, Language::English, Language::ChineseSimplified};
    auto out = Run(Make("ollama", base + "/v1", "", "", "qwen3:8b"), job, {});
    Expect(out.ok && out.texts == std::vector<std::wstring>{L"译Hello", L"译World"}, "e2e local openai-compatible");
    out = Run(Make("custom", base + "/v1", "", "", "sloppy"), job, {});
    Expect(out.ok && out.texts == std::vector<std::wstring>{L"译Hello", L"译World"}, "e2e misaligned batch retried per item");
    out = Run(Make("custom", base + "/v1", "sk", "", "bad-key"), job, {});
    Expect(!out.ok && out.error.find(L"Incorrect API key") != std::wstring::npos, "e2e auth failure surfaces provider message");
    out = Run(Make("deepl", base, "k:fx"), job, {});
    Expect(out.ok && out.texts[1] == L"世界" && out.detected == "EN", "e2e deepl");
    out = Run(Make("baidu", base, "sec", "app"), job, {});
    Expect(out.ok && out.texts[0] == L"你好" && out.detected == "en", "e2e baidu");
    out = Run(Make("youdao", base, "sec", "app"), job, {});
    Expect(out.ok && out.texts[1] == L"世界", "e2e youdao");
    out = Run(Make("tencent", base, "sec", "id"), job, {});
    Expect(out.ok && out.texts[0] == L"你好", "e2e tencent");
    const auto requests = server.Requests();
    bool tencent_signed = false, youdao_form = false, deepl_auth = false;
    for (const auto& r : requests) {
        if (r.path == "/" && r.headers.find("X-TC-Action: TextTranslateBatch") != std::string::npos && r.headers.find("TC3-HMAC-SHA256 Credential=id/") != std::string::npos) tencent_signed = true;
        if (r.path == "/v2/api" && r.body.find("q=Hello&q=World&") != std::string::npos) youdao_form = true;
        if (r.path == "/v2/translate" && r.headers.find("Authorization: DeepL-Auth-Key k:fx") != std::string::npos) deepl_auth = true;
    }
    Expect(tencent_signed && youdao_form && deepl_auth, "e2e wire formats observed by server");
    const auto models = ListModels(Make("ollama", base + "/v1", ""), {}, std::chrono::milliseconds(2000));
    Expect(models == std::vector<std::string>{"qwen3:8b", "llama3.2"}, "model discovery");

    // Many items are split into several requests and reassembled in order.
    Job big{{}, Language::English, Language::ChineseSimplified};
    for (int i = 0; i < 95; ++i) big.texts.push_back(L"line" + std::to_wstring(i));
    const auto before = server.Requests().size();
    out = Run(Make("custom", base + "/v1", "", "", "m"), big, {});
    Expect(out.ok && out.texts.size() == 95 && out.texts[94] == L"译line94" && server.Requests().size() - before == 3, "chunking by item count keeps order");

    // Cancellation of a hanging request returns promptly.
    std::stop_source stop;
    const auto start = std::chrono::steady_clock::now();
    std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); stop.request_stop(); });
    out = Run(Make("custom", base + "/v1", "", "", "hang"), job, stop.get_token());
    canceller.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    Expect(!out.ok && out.error == L"已取消" && elapsed < 3000, "cancel aborts a hanging request quickly");

    // Refused connection (nothing listening) produces a friendly message.
    out = Run(Make("lmstudio", "http://127.0.0.1:9/v1", "", "", "m"), job, {}, http::Limits{std::chrono::milliseconds(1500), std::chrono::milliseconds(1500), 1 << 20});
    Expect(!out.ok && !out.error.empty(), "refused connection reported");

    // Async service posts completion to the owner window.
    const HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    constexpr UINT Done = WM_APP + 7;
    {
        Service service(window, Done);
        const auto id = service.Submit(Make("ollama", base + "/v1", "", "", "qwen3:8b"), job);
        const auto hang = service.Submit(Make("custom", base + "/v1", "", "", "hang"), job);
        Expect(service.Pending() == 2, "two jobs pending");
        std::optional<Outcome> result;
        const auto deadline = GetTickCount64() + 10000;
        while (!result && GetTickCount64() < deadline) {
            MSG msg{};
            if (PeekMessageW(&msg, window, Done, Done, PM_REMOVE)) { if (msg.wParam == id) result = service.Take(id); }
            else Sleep(5);
        }
        Expect(result && result->ok && result->texts[0] == L"译Hello", "service completion delivered via message");
        const auto cancel_start = GetTickCount64();
        service.Cancel(hang);
        Expect(GetTickCount64() - cancel_start < 500 && service.Pending() == 0 && !service.Take(hang), "cancel is non-blocking and drops the job");
    }
    DestroyWindow(window);
}

ocr::Line L(std::wstring text, float left, float top, float right, float bottom) {
    ocr::Line line;
    for (const wchar_t c : text) line.glyphs.push_back({std::wstring(1, c), {}, 1});
    line.box = {left, top, right, bottom};
    return line;
}

void BlockTests() {
    ocr::Text text;
    text.lines.push_back(L(L"Getting Started", 20, 10, 260, 40));                          // title (taller)
    text.lines.push_back(L(L"LumaShot captures the screen and trans-", 20, 60, 420, 80));  // paragraph
    text.lines.push_back(L(L"lates what you select into your own", 20, 86, 400, 106));
    text.lines.push_back(L(L"language.", 20, 112, 110, 132));
    text.lines.push_back(L(L"• First bullet", 20, 160, 200, 180));                          // list
    text.lines.push_back(L(L"• Second bullet", 20, 186, 210, 206));
    text.lines.push_back(L(L"右侧栏的中文说明文字", 600, 60, 800, 80));                      // other column
    text.lines.push_back(L(L"会被合并成一段", 600, 86, 740, 106));
    const auto blocks = BuildBlocks(text);
    Expect(blocks.size() == 5, "blocks: title, paragraph, two bullets, column");
    if (blocks.size() == 5) {
        Expect(blocks[0].text == L"Getting Started", "title separate");
        Expect(blocks[1].text == L"LumaShot captures the screen and translates what you select into your own language." && blocks[1].lines.size() == 3, "paragraph joined with de-hyphenation");
        Expect(blocks[1].box.left == 20 && blocks[1].box.top == 60 && blocks[1].box.right == 420 && blocks[1].box.bottom == 132, "paragraph box is union");
        Expect(blocks[2].text == L"• First bullet" && blocks[3].text == L"• Second bullet", "bullets separate");
        Expect(blocks[4].text == L"右侧栏的中文说明文字会被合并成一段", "CJK joined without spaces");
        Expect(blocks[1].line_height == 20, "median line height");
    }
    text.table = ocr::Table{};
    Expect(BuildBlocks(text).size() == text.lines.size(), "tables are never merged");
}
}

int main() {
    const auto dir = std::filesystem::current_path();
    JsonTests();
    CryptoTests();
    LanguageTests();
    HostTests();
    ConfigTests(dir);
    BuilderTests();
    ParserTests();
    EndToEnd();
    BlockTests();
    std::cout << "translate: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
