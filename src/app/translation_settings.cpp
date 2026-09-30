#include "app/translation_settings.h"
#include "app/resource.h"
#include "translate/crypto.h"
#include "translate/engine.h"
#include "translate/json.h"
#include "translate/offline.h"
#include "ui/text_renderer.h"
#include <commctrl.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <memory>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace lumashot {
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t ClassName[] = L"LumaShot.TranslationSettings";
// Compact layout (DIP): engine dropdown, only the fields the engine needs,
// default language dropdown and an inline test card above the footer.
// The offline engine swaps the fields for three model cards and is taller.
constexpr float W = 520.f, BaseH = 492.f, OfflineH = 606.f;
constexpr UINT TestDone = WM_APP + 1, ModelsDone = WM_APP + 2, ProbeDone = WM_APP + 3, InstallStep = WM_APP + 4, InstallDone = WM_APP + 5, HardwareDone = WM_APP + 6;
constexpr UINT_PTR ConfirmTimer = 1;
enum Id {
    None = -1, PresetBase = 100, LanguageBase = 200, FetchModels = 300, Test, Save, Cancel, ModelBase = 400, EngineBox = 500, LanguageBox, Reveal, Advanced,
    CardBase = 600, ActionBase = 610, DeleteBase = 620, Details = 630, DetailsClose, OpenFolder, LinkBase = 640,
};
constexpr float CardTop = 146.f, CardHeight = 70.f, CardGap = 8.f;
namespace offline = translate::offline;
std::wstring Bytes(std::uint64_t bytes) {
    wchar_t text[32]{};
    const double gb = double(bytes) / double(1ull << 30);
    if (gb >= 1) swprintf_s(text, L"%.1f GB", gb);
    else swprintf_s(text, L"%.0f MB", double(bytes) / double(1ull << 20));
    return text;
}
std::wstring Gb(float gb) { wchar_t text[16]{}; swprintf_s(text, gb == std::floor(gb) ? L"%.0f GB" : L"%.1f GB", double(gb)); return text; }
// "NVIDIA GeForce GTX 1060 3GB" → "GTX 1060 3GB": the vendor prefix only costs width.
std::wstring ShortGpu(std::wstring name) {
    for (const wchar_t* prefix : {L"NVIDIA GeForce ", L"NVIDIA ", L"AMD Radeon(TM) ", L"AMD ", L"Intel(R) "}) if (name.starts_with(prefix)) { name.erase(0, std::wcslen(prefix)); break; }
    return name;
}
constexpr int EditCount = 3;
constexpr int EditBase = 1001;
constexpr float FieldTop = 152.f, FieldPitch = 44.f, FieldLeft = 102.f;

struct Badge { std::string_view id; UINT32 color; const wchar_t* text; const wchar_t* tag; };
constexpr Badge Badges[] = {
    {"offline", 0x0e9f6e, L"离", L"免费"}, {"ollama", 0x2f3542, L"O", L"本地"}, {"lmstudio", 0x5b4bd6, L"L", L"本地"}, {"deepseek", 0x4d6bfe, L"D", L""},
    {"qwen", 0x6a4cf5, L"通", L""}, {"siliconflow", 0x7c3aed, L"硅", L"免费"}, {"zhipu", 0x1e4fd8, L"智", L"免费额度"},
    {"kimi", 0x2b3446, L"K", L""}, {"openai", 0x10a37f, L"AI", L""}, {"custom", 0x64748b, L"C", L""},
    {"deepl", 0x0f2b46, L"DL", L"免费额度"}, {"baidu", 0x2932e1, L"百", L"免费额度"}, {"youdao", 0xe5322d, L"有", L""}, {"tencent", 0x0052d9, L"腾", L"免费额度"},
};
const Badge& BadgeOf(std::string_view id) {
    for (const auto& b : Badges) if (b.id == id) return b;
    for (const auto& b : Badges) if (b.id == "custom") return b;
    return Badges[0];
}
const wchar_t* KindOf(const translate::Preset& preset) {
    if (preset.id == "offline") return L"离线";
    if (preset.local) return L"本地";
    if (preset.engine != translate::Engine::OpenAi) return L"翻译 API";
    return preset.id == "custom" ? L"兼容" : L"大模型";
}
bool HasAdvanced(const translate::Preset& preset) { return preset.engine == translate::Engine::OpenAi && !preset.local && preset.id != "custom"; }
struct TestResult { bool ok{}; std::wstring text; long long ms{}; };

struct Theme {
    bool dark{};
    UINT32 bg() const { return dark ? 0x1f2530 : 0xf6f8fb; }
    UINT32 card() const { return dark ? 0x2a313d : 0xffffff; }
    UINT32 field() const { return dark ? 0x1b212b : 0xffffff; }
    UINT32 warn() const { return dark ? 0xf0a53a : 0xc77700; }
    UINT32 hover() const { return dark ? 0x354257 : 0xe9eef6; }
    UINT32 ink() const { return dark ? 0xedf4ff : 0x243142; }
    UINT32 muted() const { return dark ? 0x8f9bb0 : 0x7d8a9c; }
    UINT32 border() const { return dark ? 0x3d4858 : 0xdbe2ec; }
    UINT32 accent() const { return dark ? 0x69b3ff : 0x0784ff; }
    // Filled buttons carry white text: keep >= 4.5:1 in both themes.
    UINT32 primary() const { return dark ? 0x1a6fd6 : 0x0784ff; }
    UINT32 disabled() const { return dark ? 0x5b6677 : 0xaab4c2; }
    UINT32 accent_soft() const { return dark ? 0x183e66 : 0xe2efff; }
    UINT32 ok() const { return dark ? 0x5fd08a : 0x1f9d55; }
    UINT32 danger() const { return dark ? 0xff7b84 : 0xd9434e; }
    COLORREF Ref(UINT32 c) const { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }
};

enum class Role { None, Endpoint, Key, Model, AppId, Region };

struct Draft {
    std::string endpoint, model, app_id, region, typed_key;
    bool saved_key{};
};

struct Window {
    HWND window{};
    bool dark{};
    float scale{1};
    translate::Config config;
    std::map<std::string, Draft> drafts;
    std::string selected;
    translate::Language default_target{translate::Language::Auto};
    std::array<Role, EditCount> roles{};
    std::array<HWND, EditCount> edits{};
    std::array<std::wstring, EditCount> cues{};
    ComPtr<ID2D1DCRenderTarget> cue_target;
    HFONT font{};
    HBRUSH edit_brush{};
    int hover{None}, down{None};
    enum class Status { Idle, Running, Ok, Error } status{};
    std::wstring status_title, status_body;
    bool engine_open{}, lang_open{}, advanced{}, reveal{};
    bool testing{}, fetching{};
    std::vector<std::string> models;
    bool models_open{};
    bool ollama_running{}, lmstudio_running{};
    std::map<std::string, std::string> detected_model;
    std::jthread test_thread, models_thread, probe_thread, hardware_thread;
    float H{BaseH};
    // Offline models: one download at a time; closing the window pauses it
    // (the .partial file resumes next time).
    offline::Hardware hardware;
    bool hardware_ready{}, details_open{}, pausing{};
    std::jthread install_thread;
    std::string installing, install_error_model, confirm_delete;
    std::wstring install_error;
    offline::Progress install_progress;
    double rate{};
    ULONGLONG rate_tick{};
    std::uint64_t rate_bytes{};
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> dw;
    std::map<std::pair<int, int>, ComPtr<IDWriteTextFormat>> formats;
    TextRenderer renderer;
    struct Hit { int id; D2D1_RECT_F rect; bool enabled; };
    std::vector<Hit> hits;

    Theme theme() const { return {dark}; }
    const translate::Preset* Preset() const { return translate::FindPreset(selected); }

    // ---------- data ----------
    void Load() {
        config = translate::LoadConfig(translate::DefaultConfigPath());
        default_target = config.target;
        for (const auto& preset : translate::Presets()) {
            Draft d;
            if (const auto* s = config.Settings(preset.id)) { d.endpoint = s->endpoint; d.model = s->model; d.app_id = s->app_id; d.region = s->region; d.saved_key = !s->protected_key.empty(); }
            drafts[std::string(preset.id)] = d;
        }
        selected = config.provider.empty() ? std::string("offline") : config.provider;
        if (!Preset()) selected = "offline";
        advanced = !drafts[selected].endpoint.empty();
    }
    void SetStatus(Status kind, std::wstring title, std::wstring body = {}) {
        status = kind; status_title = std::move(title); status_body = std::move(body);
        if (window) InvalidateRect(window, nullptr, FALSE);
    }
    std::string EditText(int index) const {
        const int length = GetWindowTextLengthW(edits[size_t(index)]);
        std::wstring text(size_t(length) + 1, L'\0');
        GetWindowTextW(edits[size_t(index)], text.data(), length + 1);
        text.resize(size_t(length));
        return translate::json::Utf8(text);
    }
    void SetEdit(int index, const std::string& text) { SetWindowTextW(edits[size_t(index)], translate::json::Utf16(text).c_str()); }
    bool OfflineMode() const { return selected == "offline"; }
    const offline::Model& Chosen() const {
        const auto it = drafts.find("offline");
        if (it != drafts.end()) if (const auto* m = offline::FindModel(it->second.model)) return *m;
        return *offline::FindModel(offline::DefaultModel);
    }
    void Choose(const offline::Model& model) { drafts["offline"].model = model.id == offline::DefaultModel ? std::string() : std::string(model.id); }
    bool AnyOfflineInstalled() const { for (const auto& m : offline::Models()) if (offline::ModelInstalled(m)) return true; return false; }
    std::vector<Role> RolesFor(const translate::Preset& preset) const {
        if (preset.id == "offline") return {};
        switch (preset.engine) {
        case translate::Engine::OpenAi:
            if (preset.local) return {Role::Endpoint, Role::Model};
            if (!HasAdvanced(preset)) return {Role::Endpoint, Role::Key, Role::Model};
            return advanced ? std::vector<Role>{Role::Key, Role::Model, Role::Endpoint} : std::vector<Role>{Role::Key, Role::Model};
        case translate::Engine::DeepL: return {Role::Key};
        case translate::Engine::Tencent: return {Role::AppId, Role::Key, Role::Region};
        default: return {Role::AppId, Role::Key};
        }
    }
    int RowOf(Role role) const { for (int i = 0; i < EditCount; ++i) if (roles[size_t(i)] == role) return i; return -1; }
    // Edits → draft of the selected preset.
    void Capture() {
        auto it = drafts.find(selected);
        if (it == drafts.end()) return;
        auto& d = it->second;
        for (int i = 0; i < EditCount; ++i) {
            const auto text = EditText(i);
            switch (roles[size_t(i)]) {
            // Values equal to the preset default are stored empty so they keep
            // following future preset updates (e.g. a retired model id).
            case Role::Endpoint: d.endpoint = text == Preset()->endpoint ? std::string() : text; break;
            case Role::Model: d.model = text == Preset()->model ? std::string() : text; break;
            case Role::AppId: d.app_id = text; break;
            case Role::Region: d.region = text; break;
            case Role::Key: d.typed_key = text; break;
            case Role::None: break;
            }
        }
    }
    std::wstring Cue(Role role, const translate::Preset& preset, const Draft& d) const {
        switch (role) {
        case Role::Endpoint: return preset.endpoint.empty() ? L"https://…/v1" : translate::json::Utf16(preset.endpoint);
        case Role::Model: return preset.model.empty() ? L"点击“获取”选择模型，或直接填写" : translate::json::Utf16(preset.model);
        case Role::Key: return d.saved_key ? L"已加密保存（不修改请留空）" : L"粘贴 " + std::wstring(preset.key_label);
        case Role::AppId: return L"粘贴 " + std::wstring(preset.id_label);
        case Role::Region: return L"ap-guangzhou";
        case Role::None: break;
        }
        return {};
    }
    std::wstring Label(Role role, const translate::Preset& preset) const {
        switch (role) {
        case Role::Endpoint: return L"接口地址";
        case Role::Model: return L"模型";
        case Role::Key: return std::wstring(preset.key_label);
        case Role::AppId: return std::wstring(preset.id_label);
        case Role::Region: return L"地域";
        case Role::None: break;
        }
        return {};
    }
    void Apply() {
        const auto* preset = Preset();
        if (!preset) return;
        const auto rows = RolesFor(*preset);
        const auto& d = drafts[selected];
        for (int i = 0; i < EditCount; ++i) {
            const Role role = i < int(rows.size()) ? rows[size_t(i)] : Role::None;
            roles[size_t(i)] = role;
            ShowWindow(edits[size_t(i)], role == Role::None ? SW_HIDE : SW_SHOWNOACTIVATE);
            SendMessageW(edits[size_t(i)], EM_SETPASSWORDCHAR, role == Role::Key && !reveal ? WPARAM(L'●') : 0, 0);
            std::string value;
            switch (role) {
            case Role::Endpoint: value = d.endpoint.empty() ? std::string(preset->endpoint) : d.endpoint; break;
            case Role::Model: value = !d.model.empty() ? d.model : !preset->model.empty() ? std::string(preset->model) : detected_model[selected]; break;
            case Role::AppId: value = d.app_id; break;
            case Role::Region: value = d.region; break;
            case Role::Key: value = d.typed_key; break;
            case Role::None: break;
            }
            SetEdit(i, value);
            cues[size_t(i)] = Cue(role, *preset, d);
            InvalidateRect(edits[size_t(i)], nullptr, TRUE);
        }
        models.clear();
        models_open = false;
        SetStatus(Status::Idle, {});
        Layout();
        const float height = OfflineMode() ? OfflineH : BaseH;
        if (height != H) { H = height; target.Reset(); brush.Reset(); Resize(); }
        SyncEdits();
        InvalidateRect(window, nullptr, FALSE);
    }
    // Native EDITs always paint above the D2D surface, so any field a popup
    // covers is hidden while the popup is open and drawn as static text instead.
    bool Covered() const { return engine_open || lang_open || details_open; }
    void SyncEdits() {
        const int model_row = RowOf(Role::Model);
        for (int i = 0; i < EditCount; ++i) {
            const HWND edit = edits[size_t(i)];
            const bool want = roles[size_t(i)] != Role::None && !Covered() && !(models_open && i > model_row);
            const bool visible = (GetWindowLongW(edit, GWL_STYLE) & WS_VISIBLE) != 0;
            if (want == visible) continue;
            if (!want && GetFocus() == edit) SetFocus(window);
            ShowWindow(edit, want ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
    }
    translate::Config Candidate(bool protect_typed) {
        Capture();
        translate::Config next = config;
        next.provider = selected;
        next.target = default_target;
        for (const auto& [id, d] : drafts) {
            auto& s = next.Edit(id);
            s.endpoint = d.endpoint; s.model = d.model; s.app_id = d.app_id; s.region = d.region;
            if (!d.typed_key.empty() && (protect_typed || id == selected)) s.protected_key = translate::crypto::Protect(d.typed_key);
        }
        return next;
    }

    // ---------- async ----------
    void RunTest() {
        if (testing) return;
        auto candidate = Candidate(false);
        std::wstring reason;
        auto credentials = translate::Resolve(candidate, &reason);
        if (!credentials) { SetStatus(Status::Error, L"还不能测试", reason); return; }
        testing = true;
        if (OfflineMode() && !offline::ServerRunning()) SetStatus(Status::Running, L"正在启动离线引擎…", L"首次加载模型约需 5–20 秒，之后会快很多");
        else SetStatus(Status::Running, L"正在测试…", L"发送一句英文，检查引擎能否返回译文");
        const auto language = translate::ResolveTarget(default_target, translate::Language::English);
        const HWND owner = window;
        test_thread = std::jthread([owner, credentials = std::move(*credentials), language](std::stop_token stop) mutable {
            const auto start = std::chrono::steady_clock::now();
            translate::Job job{{L"Hello! This screenshot was translated by LumaShot."}, translate::Language::English, language};
            translate::http::Limits limits;
            limits.receive = std::chrono::seconds(45);
            auto outcome = translate::Run(credentials, job, stop, limits);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (stop.stop_requested()) return;
            auto* result = new TestResult{outcome.ok, outcome.ok && !outcome.texts.empty() ? outcome.texts.front() : outcome.error, ms};
            if (!PostMessageW(owner, TestDone, 0, reinterpret_cast<LPARAM>(result))) delete result;
        });
        InvalidateRect(window, nullptr, FALSE);
    }
    void RunModels() {
        if (fetching) return;
        auto candidate = Candidate(false);
        auto& s = candidate.Edit(selected);
        if (s.model.empty()) s.model = "probe";
        std::wstring reason;
        auto credentials = translate::Resolve(candidate, &reason);
        if (!credentials) { SetStatus(Status::Error, L"无法获取模型", reason); return; }
        fetching = true;
        const HWND owner = window;
        models_thread = std::jthread([owner, credentials = std::move(*credentials)](std::stop_token stop) {
            auto list = translate::ListModels(credentials, stop, std::chrono::seconds(8));
            if (stop.stop_requested()) return;
            auto* result = new std::vector<std::string>(std::move(list));
            if (!PostMessageW(owner, ModelsDone, 0, reinterpret_cast<LPARAM>(result))) delete result;
        });
        InvalidateRect(window, nullptr, FALSE);
    }
    void Probe() {
        const HWND owner = window;
        probe_thread = std::jthread([owner](std::stop_token stop) {
            auto* found = new std::map<std::string, std::string>();
            for (const char* id : {"ollama", "lmstudio"}) {
                translate::Credentials c;
                c.preset = translate::FindPreset(id);
                c.endpoint = std::string(c.preset->endpoint);
                const auto list = translate::ListModels(c, stop, std::chrono::milliseconds(700));
                if (!list.empty()) (*found)[id] = list.front();
            }
            if (stop.stop_requested() || !PostMessageW(owner, ProbeDone, 0, reinterpret_cast<LPARAM>(found))) delete found;
        });
    }
    void ProbeHardware() {
        const HWND owner = window;
        hardware_thread = std::jthread([owner](std::stop_token stop) {
            auto* found = new offline::Hardware(offline::Probe());
            if (stop.stop_requested() || !PostMessageW(owner, HardwareDone, 0, reinterpret_cast<LPARAM>(found))) delete found;
        });
    }
    void StartInstall(const offline::Model& model) {
        if (!installing.empty()) return;
        const std::uint64_t need = model.size - std::min(model.size, offline::PartialBytes(model)) + (offline::RuntimeInstalled() ? 0 : 128ull << 20);
        if (const auto space = offline::FreeBytes(); space && space < need + (64ull << 20)) {
            install_error_model = std::string(model.id);
            install_error = L"磁盘空间不足：需要 " + Bytes(need) + L"，剩余 " + Bytes(space);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        installing = std::string(model.id);
        install_error.clear(); install_error_model.clear();
        install_progress = {offline::RuntimeInstalled() ? offline::Progress::Stage::Verify : offline::Progress::Stage::Runtime, 0, 0};
        rate = 0; rate_tick = 0; rate_bytes = 0; pausing = false;
        const HWND owner = window;
        const auto* target_model = &model;
        install_thread = std::jthread([owner, target_model](std::stop_token stop) {
            ULONGLONG last = 0;
            auto last_stage = offline::Progress::Stage::Runtime;
            auto result = offline::Install(*target_model, stop, [&](const offline::Progress& step) {
                const ULONGLONG now = GetTickCount64();
                if (step.stage == last_stage && now - last < 150 && step.done < step.total) return;
                last = now; last_stage = step.stage;
                auto* copy = new offline::Progress(step);
                if (!PostMessageW(owner, InstallStep, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
            });
            auto* done = new offline::Result(std::move(result));
            if (!PostMessageW(owner, InstallDone, 0, reinterpret_cast<LPARAM>(done))) delete done;
        });
        InvalidateRect(window, nullptr, FALSE);
    }
    void Step(const offline::Progress& step) {
        const ULONGLONG now = GetTickCount64();
        if (step.stage != install_progress.stage || step.done < rate_bytes) { rate = 0; rate_tick = now; rate_bytes = step.done; }
        else if (now - rate_tick >= 700) {
            const double sample = double(step.done - rate_bytes) * 1000.0 / double(now - rate_tick);
            rate = rate > 0 ? rate * .7 + sample * .3 : sample;
            rate_tick = now; rate_bytes = step.done;
        }
        install_progress = step;
        InvalidateRect(window, nullptr, FALSE);
    }
    void Finish(const offline::Result& result) {
        const std::string id = installing;
        installing.clear();
        pausing = false;
        if (install_thread.joinable()) install_thread.join();
        if (result.ok()) {
            if (const auto* model = offline::FindModel(id)) Choose(*model);
            SetStatus(Status::Idle, {});
        } else if (result.kind != offline::Result::Kind::Canceled) {
            install_error_model = id;
            install_error = result.message.empty() ? L"下载失败" : result.message;
        }
        InvalidateRect(window, nullptr, FALSE);
    }
    void OpenUrl(const std::wstring& url) { ShellExecuteW(window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
    void SaveAndClose() {
        auto next = Candidate(true);
        std::wstring reason;
        if (!translate::Resolve(next, &reason)) { SetStatus(Status::Error, L"还不能保存", reason); return; }
        if (!translate::SaveConfig(translate::DefaultConfigPath(), next)) { SetStatus(Status::Error, L"无法保存", L"无法写入设置文件"); return; }
        if (const HWND host = FindWindowW(L"LumaShot.Host", nullptr)) PostMessageW(host, kTranslationConfigChanged, 0, 0);
        DestroyWindow(window);
    }

    // ---------- layout & paint ----------
    int VisibleRows() const { int n = 0; for (auto role : roles) n += role != Role::None; return n; }
    D2D1_RECT_F EditBox(int row) const {
        const float y = FieldTop + row * FieldPitch;
        const bool model = roles[size_t(row)] == Role::Model && Preset() && Preset()->engine == translate::Engine::OpenAi;
        return {FieldLeft, y, model ? W - 24 - 80 : W - 24, y + 34};
    }
    D2D1_RECT_F FetchRect() const { const int row = RowOf(Role::Model); const float y = FieldTop + row * FieldPitch; return {W - 24 - 72, y, W - 24, y + 34}; }
    D2D1_RECT_F EngineRect() const { return {FieldLeft, 76, W - 24, 112}; }
    float AdvancedTop() const { return FieldTop + VisibleRows() * FieldPitch - 4; }
    float FooterY() const { return H - 62; }
    D2D1_RECT_F CardRect(size_t index) const { const float y = CardTop + float(index) * (CardHeight + CardGap); return {24, y, W - 24, y + CardHeight}; }
    float OfflineInfoTop() const { return CardRect(offline::Models().size() - 1).bottom + 8; }
    float LanguageTop() const {
        if (OfflineMode()) return OfflineInfoTop() + 46;
        return AdvancedTop() + 4 + (Preset() && HasAdvanced(*Preset()) ? 28.f : 0.f);
    }
    D2D1_RECT_F LanguageRect() const { const float y = LanguageTop(); return {FieldLeft, y, W - 24, y + 34}; }
    D2D1_RECT_F TestCard() const { const float y = LanguageTop() + 48; return {24, y, W - 24, y + 60}; }
    void Layout() {
        const int inset_x = int(std::lround(10 * scale)), inset_y = int(std::lround(8 * scale)), eye = int(std::lround(30 * scale));
        for (int i = 0; i < EditCount; ++i) {
            const auto r = EditBox(i);
            const int extra = roles[size_t(i)] == Role::Key ? eye : 0;
            SetWindowPos(edits[size_t(i)], nullptr, int(r.left * scale) + inset_x, int(r.top * scale) + inset_y, int((r.right - r.left) * scale) - 2 * inset_x - extra, int((r.bottom - r.top) * scale) - 2 * inset_y + 2, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    void Font() {
        if (font) DeleteObject(font);
        font = CreateFontW(-int(std::lround(14 * scale)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        for (auto edit : edits) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    IDWriteTextFormat* Format(float size, bool bold) {
        auto& slot = formats[{int(size * 10), bold ? 1 : 0}];
        if (!slot) {
            dw->CreateTextFormat(L"Microsoft YaHei UI", nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", slot.GetAddressOf());
            if (slot) slot->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return slot.Get();
    }
    float Measure(const std::wstring& text, float size, bool bold = false) {
        auto* format = Format(size, bold);
        if (!format || text.empty()) return 0;
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dw->CreateTextLayout(text.c_str(), UINT32(text.size()), format, 4096, 64, layout.GetAddressOf()))) return 0;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return metrics.widthIncludingTrailingWhitespace;
    }
    void Text(const std::wstring& text, D2D1_RECT_F r, float size, UINT32 color, bool bold = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false) {
        auto* format = Format(size, bold);
        if (!format || text.empty()) return;
        format->SetTextAlignment(align);
        format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        renderer.Draw(target.Get(), dw.Get(), text, format, r, D2D1::ColorF(color), true);
    }
    void Fill(D2D1_RECT_F r, UINT32 c, float radius = 8, float opacity = 1) { brush->SetColor(D2D1::ColorF(c, opacity)); target->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush.Get()); }
    void Stroke(D2D1_RECT_F r, UINT32 c, float radius = 8, float width = 1, float opacity = 1) { brush->SetColor(D2D1::ColorF(c, opacity)); target->DrawRoundedRectangle(D2D1::RoundedRect({r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f}, radius, radius), brush.Get(), width); }
    void Line(D2D1_POINT_2F a, D2D1_POINT_2F b, UINT32 c, float width = 1.5f) { brush->SetColor(D2D1::ColorF(c)); target->DrawLine(a, b, brush.Get(), width); }
    void Chevron(float x, float y, UINT32 c, bool up = false) { const float d = up ? -1.f : 1.f; Line({x - 4, y - 2 * d}, {x, y + 2 * d}, c); Line({x, y + 2 * d}, {x + 4, y - 2 * d}, c); }
    void Button(int id, D2D1_RECT_F r, const std::wstring& label, bool primary, bool enabled = true) {
        const auto t = theme();
        const bool active = enabled && hover == id;
        if (primary) Fill(r, enabled ? t.primary() : t.border(), 8, active ? .88f : 1.f);
        else { Fill(r, active ? t.hover() : t.card(), 8, enabled ? 1.f : .6f); Stroke(r, active ? t.accent() : t.border(), 8, 1, active ? .7f : 1.f); }
        Text(label, r, 13.5f, enabled ? (primary ? 0xffffff : t.ink()) : t.disabled(), primary, DWRITE_TEXT_ALIGNMENT_CENTER);
        hits.push_back({id, r, enabled});
    }
    void BadgeAt(const translate::Preset& preset, D2D1_RECT_F r) {
        const auto& badge = BadgeOf(preset.id);
        Fill(r, badge.color, 6);
        Text(badge.text, r, (r.bottom - r.top) * .48f, 0xffffff, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // Rounded tag sized to its text; returns the left edge so callers can right-align.
    float Tag(const std::wstring& text, float right, float cy, UINT32 ink, UINT32 fill) {
        const float width = Measure(text, 10.5f) + 14;
        const D2D1_RECT_F r{right - width, cy - 9, right, cy + 9};
        Fill(r, fill, 9);
        Text(text, r, 10.5f, ink, false, DWRITE_TEXT_ALIGNMENT_CENTER);
        return r.left;
    }
    bool Running(std::string_view id) const { return (id == "ollama" && ollama_running) || (id == "lmstudio" && lmstudio_running); }
    void Shadow(D2D1_RECT_F pop) {
        brush->SetColor(D2D1::ColorF(0, theme().dark ? .38f : .13f));
        target->FillRoundedRectangle(D2D1::RoundedRect({pop.left - 1, pop.top + 3, pop.right + 1, pop.bottom + 6}, 11, 11), brush.Get());
    }
    // Grouped two-column engine list: local + translation APIs | LLM APIs.
    void EnginePopup(std::vector<Hit>& popup) {
        const auto t = theme();
        const auto presets = translate::Presets();
        struct Entry { const wchar_t* header; int index; };
        std::vector<Entry> left, right;
        left.push_back({L"离线 / 本地", -1});
        for (int i = 0; i < int(presets.size()); ++i) if (presets[size_t(i)].local) left.push_back({nullptr, i});
        left.push_back({L"翻译 API", -1});
        for (int i = 0; i < int(presets.size()); ++i) if (presets[size_t(i)].engine != translate::Engine::OpenAi) left.push_back({nullptr, i});
        right.push_back({L"大模型 API", -1});
        for (int i = 0; i < int(presets.size()); ++i) if (presets[size_t(i)].engine == translate::Engine::OpenAi && !presets[size_t(i)].local) right.push_back({nullptr, i});
        const auto height = [](const std::vector<Entry>& column) { float h = 0; for (const auto& e : column) h += e.header ? 26.f : 32.f; return h; };
        const auto box = EngineRect();
        const D2D1_RECT_F pop{24, box.bottom + 4, W - 24, box.bottom + 4 + 12 + std::max(height(left), height(right))};
        Shadow(pop);
        Fill(pop, t.card(), 10);
        Stroke(pop, t.border(), 10);
        const float mid = (pop.left + pop.right) / 2;
        brush->SetColor(D2D1::ColorF(t.border()));
        target->DrawLine({mid, pop.top + 10}, {mid, pop.bottom - 10}, brush.Get(), 1);
        for (int column = 0; column < 2; ++column) {
            const auto& entries = column == 0 ? left : right;
            const float x0 = column == 0 ? pop.left + 6 : mid + 6, x1 = column == 0 ? mid - 6 : pop.right - 6;
            float y = pop.top + 6;
            for (const auto& e : entries) {
                if (e.header) { Text(e.header, {x0 + 8, y, x1, y + 26}, 11, t.muted(), true); y += 26; continue; }
                const auto& preset = presets[size_t(e.index)];
                const D2D1_RECT_F r{x0, y, x1, y + 32};
                const bool on = preset.id == selected, active = hover == PresetBase + e.index;
                if (on) Fill(r, t.accent_soft(), 7); else if (active) Fill(r, t.hover(), 7);
                BadgeAt(preset, {r.left + 8, r.top + 6, r.left + 28, r.top + 26});
                float label_right = r.right - 8;
                const bool ready = Running(preset.id) || (preset.id == "offline" && AnyOfflineInstalled());
                const std::wstring tag = Running(preset.id) ? L"运行中" : ready ? L"已安装" : BadgeOf(preset.id).tag;
                if (!tag.empty()) label_right = Tag(tag, r.right - 8, (r.top + r.bottom) / 2, ready || tag.find(L"免费") != std::wstring::npos ? t.ok() : t.muted(), t.dark ? 0x323b4a : 0xeef2f7) - 6;
                Text(std::wstring(preset.label), {r.left + 36, r.top, label_right, r.bottom}, 13, on ? t.accent() : t.ink(), on);
                popup.push_back({PresetBase + e.index, r, true});
                y += 32;
            }
        }
        popup.push_back({None - 1, pop, false});
    }
    void LanguagePopup(std::vector<Hit>& popup) {
        const auto t = theme();
        const auto box = LanguageRect();
        const float height = 12 + 5 * 32.f;
        const bool below = box.bottom + 4 + height <= H - 6;
        const D2D1_RECT_F pop{box.left, below ? box.bottom + 4 : box.top - 4 - height, box.right, below ? box.bottom + 4 + height : box.top - 4};
        Shadow(pop);
        Fill(pop, t.card(), 10);
        Stroke(pop, t.border(), 10);
        const float cw = (pop.right - pop.left - 12) / 2;
        for (int i = 0; i < translate::LanguageCount; ++i) {
            const float x = pop.left + 6 + (i / 5) * cw, y = pop.top + 6 + (i % 5) * 32.f;
            const D2D1_RECT_F r{x, y, x + cw, y + 32};
            const auto language = static_cast<translate::Language>(i);
            const bool on = language == default_target, active = hover == LanguageBase + i;
            if (on) Fill(r, t.accent_soft(), 7); else if (active) Fill(r, t.hover(), 7);
            Text(i == 0 ? std::wstring(L"自动") : std::wstring(translate::LanguageLabel(language)), {r.left + 12, r.top, r.right - 8, r.bottom}, 13, on ? t.accent() : t.ink(), on);
            popup.push_back({LanguageBase + i, r, true});
        }
        popup.push_back({None - 1, pop, false});
    }
    float TextHeight(const std::wstring& text, float size, float width) {
        auto* format = Format(size, false);
        if (!format || text.empty()) return 0;
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dw->CreateTextLayout(text.c_str(), UINT32(text.size()), format, width, 4096, layout.GetAddressOf()))) return size * 1.4f;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return std::ceil(metrics.height);
    }
    static std::wstring Speed(double bytes_per_second) {
        wchar_t text[32]{};
        if (bytes_per_second >= 1024.0 * 1024.0) swprintf_s(text, L"%.1f MB/s", bytes_per_second / (1024.0 * 1024.0));
        else swprintf_s(text, L"%.0f KB/s", bytes_per_second / 1024.0);
        return text;
    }
    void OfflineCard(size_t index, const offline::Model& m, const std::string& recommended) {
        const auto t = theme();
        const auto r = CardRect(index);
        const int i = int(index);
        const bool on = m.id == Chosen().id, installed = offline::ModelInstalled(m), busy = installing == m.id;
        const bool active = hover == CardBase + i;
        Fill(r, on ? (t.dark ? 0x26354a : 0xf5f9ff) : t.card(), 10);
        Stroke(r, on || active ? t.accent() : t.border(), 10, on ? 1.5f : 1.f, on ? 1.f : active ? .6f : 1.f);
        {   // radio
            const float cx = r.left + 18, cy = r.top + 19;
            brush->SetColor(D2D1::ColorF(on ? t.accent() : t.muted()));
            target->DrawEllipse(D2D1::Ellipse({cx, cy}, 7, 7), brush.Get(), 1.4f);
            if (on) target->FillEllipse(D2D1::Ellipse({cx, cy}, 3.6f, 3.6f), brush.Get());
        }
        const std::wstring name(m.name);
        const float name_width = Measure(name, 13.5f, true);
        Text(name, {r.left + 34, r.top + 8, r.left + 36 + name_width, r.top + 30}, 13.5f, t.ink(), true);
        float x = r.left + 34 + name_width + 8;
        const auto tag = [&](const std::wstring& text, UINT32 ink, UINT32 fill) { const float w = Measure(text, 10.5f) + 14; Tag(text, x + w, r.top + 19, ink, fill); x += w + 5; };
        if (m.id == recommended) tag(L"推荐", t.ok(), t.dark ? 0x1f3d2c : 0xe3f5ea);
        if (installed) tag(L"已安装", t.accent(), t.accent_soft());
        if (hardware_ready) switch (offline::Assess(m, hardware)) {
            case offline::Fit::Fast: tag(L"显卡加速", t.ok(), t.dark ? 0x1f3d2c : 0xe3f5ea); break;
            case offline::Fit::Ok: tag(L"CPU 运行", t.muted(), t.dark ? 0x323b4a : 0xeef2f7); break;
            case offline::Fit::Slow: tag(L"可能较慢", t.warn(), t.dark ? 0x3d3020 : 0xfff3dc); break;
            case offline::Fit::TooLarge: tag(L"配置不足", t.danger(), t.dark ? 0x44262a : 0xfde7e9); break;
        }
        const float tx = r.left + 34, tr = r.right - 116;
        const D2D1_RECT_F button{r.right - 96, r.top + 34, r.right - 12, r.top + 62};
        if (busy) {
            const auto& step = install_progress;
            const bool bytes = step.stage == offline::Progress::Stage::Runtime || step.stage == offline::Progress::Stage::Model;
            const double fraction = bytes && step.total ? std::min(1.0, double(step.done) / double(step.total)) : 0.0;
            std::wstring line = pausing ? L"正在暂停…"
                : step.stage == offline::Progress::Stage::Runtime ? L"下载推理组件"
                : step.stage == offline::Progress::Stage::Extract ? L"正在解压推理组件…"
                : step.stage == offline::Progress::Stage::Verify ? L"正在检查已下载的部分…" : L"下载模型";
            if (bytes && step.total && !pausing) {
                line += L"  " + std::to_wstring(int(fraction * 100)) + L"%";
                if (rate > 1 && step.total > step.done) {
                    const double seconds = double(step.total - step.done) / rate;
                    line += seconds < 60 ? std::wstring(L" · 不到 1 分钟") : L" · 剩余约 " + std::to_wstring(int(std::ceil(seconds / 60))) + L" 分钟";
                }
            }
            Text(line, {tx, r.top + 29, tr, r.top + 47}, 12, t.ink());
            const D2D1_RECT_F track{tx, r.top + 53, tr, r.top + 58};
            Fill(track, t.dark ? 0x3a4454 : 0xe6ebf2, 2.5f);
            if (bytes) { if (fraction > 0) Fill({track.left, track.top, track.left + std::max(5.f, float((track.right - track.left) * fraction)), track.bottom}, t.accent(), 2.5f); }
            else Fill(track, t.accent(), 2.5f, .35f);
            if (bytes && rate > 0) Text(Speed(rate), {r.right - 116, r.top + 8, r.right - 12, r.top + 28}, 12, t.muted(), false, DWRITE_TEXT_ALIGNMENT_TRAILING);
            Button(ActionBase + i, button, pausing ? L"暂停中…" : L"暂停", false, !pausing);
        } else {
            Text(Bytes(m.size), {r.right - 116, r.top + 8, r.right - 12, r.top + 28}, 12, t.muted(), false, DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(std::wstring(m.summary), {tx, r.top + 29, tr, r.top + 47}, 12, t.muted());
            const auto partial = installed ? 0 : offline::PartialBytes(m);
            const bool failed = install_error_model == m.id && !install_error.empty();
            if (failed) Text(install_error, {tx, r.top + 47, tr, r.top + 65}, 11.5f, t.danger());
            else if (partial > 0) Text(L"已暂停 · 已下载 " + Bytes(partial) + L" / " + Bytes(m.size), {tx, r.top + 47, tr, r.top + 65}, 11.5f, t.warn());
            else Text(L"内存 " + Gb(m.ram_gb) + L" 起 · 显存 " + Gb(m.vram_gb) + L" 可全速运行", {tx, r.top + 47, tr, r.top + 65}, 11.5f, t.muted());
            if (installed) {
                // Acquire() holds the server lock while a test starts the engine: no deleting meanwhile.
                if (confirm_delete == m.id && !testing) {
                    Fill(button, t.danger(), 8, hover == DeleteBase + i ? .88f : 1.f);
                    Text(L"确认删除", button, 13, 0xffffff, true, DWRITE_TEXT_ALIGNMENT_CENTER);
                    hits.push_back({DeleteBase + i, button, true});
                } else Button(DeleteBase + i, button, L"删除", false, !testing);
            } else Button(ActionBase + i, button, failed ? L"重试" : partial > 0 ? L"继续" : L"下载", on, installing.empty());
        }
        hits.push_back({CardBase + i, r, true});
    }
    void PaintOffline() {
        const auto t = theme();
        std::wstring machine;
        if (!hardware_ready) machine = L"正在检测本机配置…";
        else {
            machine = L"本机 " + Gb(std::round(float(double(hardware.ram) / double(1ull << 30)))) + L" 内存";
            if (hardware.vram) machine += L" · " + ShortGpu(hardware.gpu) + L"（" + Bytes(hardware.vram) + L" 显存）";
            else machine += hardware.gpu.empty() ? L" · 无独立显卡，用 CPU 运行" : L" · 集成显卡，主要用 CPU 运行";
        }
        const std::wstring more = L"要求与许可";
        const float more_width = Measure(more, 11.5f) + 12;
        const D2D1_RECT_F link{W - 24 - more_width, 114, W - 24, 140};
        Text(machine, {FieldLeft, 114, link.left - 8, 140}, 11.5f, t.muted());
        const UINT32 link_color = hover == Details ? t.ink() : t.accent();
        Text(more, {link.left, link.top, link.right - 10, link.bottom}, 11.5f, link_color);
        Line({link.right - 6, 124}, {link.right - 3, 127}, link_color, 1.3f);
        Line({link.right - 3, 127}, {link.right - 6, 130}, link_color, 1.3f);
        hits.push_back({Details, link, true});
        const std::string recommended(hardware_ready ? offline::Recommend(hardware).id : offline::DefaultModel);
        const auto catalog = offline::Models();
        for (size_t i = 0; i < catalog.size(); ++i) OfflineCard(i, catalog[i], recommended);
        const float y = OfflineInfoTop();
        const std::wstring info = L"首次下载会一并安装推理组件 llama.cpp（" + Bytes(offline::RuntimePackage().size) + L"）。有支持 Vulkan 的显卡时自动加速，否则使用 CPU；只在翻译时运行，闲置 5 分钟自动退出并释放内存。";
        Text(info, {24, y, W - 24, y + 38}, 11.5f, t.muted(), false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    }
    void DetailsPopup(std::vector<Hit>& popup) {
        const auto t = theme();
        brush->SetColor(D2D1::ColorF(0, t.dark ? .45f : .26f));
        target->FillRectangle({0, 0, W, H}, brush.Get());
        const D2D1_RECT_F pop{32, 52, W - 32, H - 30};
        Shadow(pop);
        Fill(pop, t.card(), 12);
        Stroke(pop, t.border(), 12);
        Text(L"离线翻译 · 要求与说明", {pop.left + 20, pop.top + 14, pop.right - 20, pop.top + 42}, 15.5f, t.ink(), true);
        const auto size_of = [](std::string_view id) { return Bytes(offline::FindModel(id)->size); };
        const std::wstring sizes = L"1.8B " + size_of("hy-mt1.5-1.8b") + L"、4B " + size_of("translategemma-4b") + L"、7B " + size_of("hy-mt1.5-7b");
        const std::wstring folder = offline::Root().wstring();
        const std::pair<const wchar_t*, std::wstring> items[] = {
            {L"系统", L"Windows 10 1803 或更高版本（64 位）"},
            {L"内存", L"混元 1.8B 需 4 GB 起，TranslateGemma 4B 需 8 GB，混元 7B 需 16 GB；显存充足时对内存的要求会降低"},
            {L"显卡", L"可选。支持 Vulkan 的 NVIDIA / AMD / Intel 显卡会自动加速：1.8B 约需 1.5 GB、4B 约 3.5 GB、7B 约 5.5 GB 显存即可整模加速，不足时部分加速或改用 CPU"},
            {L"磁盘", L"模型 " + sizes + L"，推理组件约 85 MB。保存在：" + folder},
            {L"速度", L"首次翻译需先加载模型（约 5–20 秒），之后一屏文字通常几秒内完成，显卡加速时更快"},
            {L"隐私", L"识别与翻译都在本机完成，文字不会上传；模型只在翻译时运行，闲置 5 分钟自动退出"},
            {L"下载", L"优先使用国内镜像（ModelScope / hf-mirror），自动断点续传并校验 SHA-256；关闭窗口会暂停，下次打开可继续"},
            {L"许可", L"混元翻译遵循《腾讯混元社区许可协议》（不适用于欧盟、英国和韩国），TranslateGemma 遵循《Gemma 使用条款》，llama.cpp 为 MIT 许可。下载即表示你同意相应许可"},
        };
        const float lx = pop.left + 20, tx = pop.left + 64, tr = pop.right - 20;
        float y = pop.top + 50;
        for (const auto& [label, text] : items) {
            const float h = std::max(18.f, TextHeight(text, 12, tr - tx));
            Text(label, {lx, y, tx - 6, y + 18}, 12, t.ink(), true);
            Text(text, {tx, y, tr, y + h}, 12, t.muted(), false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
            y += h + 7;
        }
        const std::pair<const wchar_t*, int> links[] = {{L"混元许可协议", 0}, {L"Gemma 使用条款", 1}, {L"llama.cpp 许可", 2}};
        float x = tx;
        for (const auto& [label, k] : links) {
            const float w = Measure(label, 12) + 14;
            const D2D1_RECT_F r{x, y, x + w, y + 20};
            const UINT32 c = hover == LinkBase + k ? t.ink() : t.accent();
            Text(label, {r.left, r.top, r.right - 12, r.bottom}, 12, c);
            Line({r.right - 9, r.top + 6}, {r.right - 3, r.top + 6}, c, 1.1f);
            Line({r.right - 3, r.top + 6}, {r.right - 3, r.top + 12}, c, 1.1f);
            Line({r.right - 3, r.top + 6}, {r.right - 9, r.top + 12}, c, 1.1f);
            popup.push_back({LinkBase + k, r, true});
            x += w + 14;
        }
        Button(OpenFolder, {pop.left + 20, pop.bottom - 50, pop.left + 150, pop.bottom - 16}, L"打开模型文件夹", false);
        popup.push_back(hits.back()); hits.pop_back();
        Button(DetailsClose, {pop.right - 20 - 96, pop.bottom - 50, pop.right - 20, pop.bottom - 16}, L"知道了", true);
        popup.push_back(hits.back()); hits.pop_back();
        popup.push_back({None - 1, pop, false});
        popup.push_back({None, {0, 0, W, H}, false});
    }
    void Paint() {
        EnsureFactories();
        RECT client{};
        GetClientRect(window, &client);
        if (!target) {
            factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96, 96), D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(UINT32(client.right), UINT32(client.bottom))), target.GetAddressOf());
            if (!target) return;
            target->CreateSolidColorBrush(D2D1::ColorF(0), brush.GetAddressOf());
        }
        const auto t = theme();
        const auto* preset = Preset();
        hits.clear();
        // DIP coordinates via the target DPI, not a scale transform: TextRenderer
        // hands the target DPI to LumaText, which then rasterizes glyphs at the
        // monitor's real pixel size (a 96-DPI target + transform would stretch them).
        target->SetDpi(96 * scale, 96 * scale);
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->Clear(D2D1::ColorF(t.bg()));
        Text(L"翻译引擎", {24, 12, W - 24, 42}, 19, t.ink(), true);
        {   // shield + check
            brush->SetColor(D2D1::ColorF(t.ok()));
            const float x = 25, y = 46;
            target->DrawLine({x + 6, y}, {x + 12, y + 2.5f}, brush.Get(), 1.4f); target->DrawLine({x + 6, y}, {x, y + 2.5f}, brush.Get(), 1.4f);
            target->DrawLine({x, y + 2.5f}, {x + .6f, y + 7.5f}, brush.Get(), 1.4f); target->DrawLine({x + 12, y + 2.5f}, {x + 11.4f, y + 7.5f}, brush.Get(), 1.4f);
            target->DrawLine({x + .6f, y + 7.5f}, {x + 6, y + 13}, brush.Get(), 1.4f); target->DrawLine({x + 11.4f, y + 7.5f}, {x + 6, y + 13}, brush.Get(), 1.4f);
            target->DrawLine({x + 3.5f, y + 6.5f}, {x + 5.5f, y + 8.5f}, brush.Get(), 1.4f); target->DrawLine({x + 5.5f, y + 8.5f}, {x + 9, y + 4.5f}, brush.Get(), 1.4f);
        }
        Text(OfflineMode() ? L"离线翻译：文字只在本机处理，不会上传到任何服务器" : L"只发送识别出的文字 · 密钥用 Windows DPAPI 加密保存在本机", {43, 42, W - 24, 64}, 12, t.muted());
        if (preset) {
            // Engine dropdown
            const auto box = EngineRect();
            Text(L"引擎", {24, box.top, FieldLeft - 8, box.bottom}, 13, t.ink());
            Fill(box, t.field(), 8);
            Stroke(box, engine_open ? t.accent() : hover == EngineBox ? t.accent() : t.border(), 8, engine_open ? 1.5f : 1.f, hover == EngineBox && !engine_open ? .6f : 1.f);
            const float cy = (box.top + box.bottom) / 2;
            BadgeAt(*preset, {box.left + 8, cy - 11, box.left + 30, cy + 11});
            const std::wstring label(preset->label);
            const float label_width = std::min(Measure(label, 13.5f), box.right - box.left - 150);
            Text(label, {box.left + 40, box.top, box.left + 40 + label_width + 2, box.bottom}, 13.5f, t.ink());
            const float kind_width = Measure(KindOf(*preset), 10.5f) + 14;
            Tag(KindOf(*preset), box.left + 48 + label_width + kind_width, cy, t.muted(), t.dark ? 0x323b4a : 0xeef2f7);
            if (Running(preset->id)) { brush->SetColor(D2D1::ColorF(t.ok())); target->FillEllipse(D2D1::Ellipse({box.left + 60 + label_width + kind_width, cy}, 3.5f, 3.5f), brush.Get()); }
            Chevron(box.right - 18, cy, t.muted(), engine_open);
            hits.push_back({EngineBox, box, true});
            if (OfflineMode()) PaintOffline();
            else {
            // Where to get a key / how to start the local service
            std::wstring prefix = preset->local ? (Running(preset->id) ? L"已检测到本机服务正在运行 · " : L"") : L"获取密钥：";
            const float prefix_width = Measure(prefix, 11.5f);
            Text(prefix, {FieldLeft, 114, FieldLeft + prefix_width + 1, 140}, 11.5f, t.muted());
            Text(std::wstring(preset->signup), {FieldLeft + prefix_width, 114, W - 24, 144}, 11.5f, preset->local ? t.muted() : t.accent(), false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
            // Fields
            for (int i = 0; i < EditCount; ++i) {
                if (roles[size_t(i)] == Role::None) continue;
                const auto field = EditBox(i);
                Text(Label(roles[size_t(i)], *preset), {24, field.top, FieldLeft - 8, field.bottom}, 13, t.ink());
                Fill(field, t.field(), 8);
                const bool focused = GetFocus() == edits[size_t(i)];
                Stroke(field, focused ? t.accent() : t.border(), 8, focused ? 1.5f : 1.f);
                if (!(GetWindowLongW(edits[size_t(i)], GWL_STYLE) & WS_VISIBLE)) {
                    // Hidden under a popup: keep the field looking unchanged.
                    std::wstring value = translate::json::Utf16(EditText(i));
                    const bool cue = value.empty();
                    if (cue) value = cues[size_t(i)];
                    else if (roles[size_t(i)] == Role::Key && !reveal) value.assign(value.size(), L'●');
                    Text(value, {field.left + 10, field.top, field.right - (roles[size_t(i)] == Role::Key ? 34.f : 10.f), field.bottom}, 14, cue ? (t.dark ? 0x6f7c90 : 0x9aa6b5) : t.ink());
                }
                if (roles[size_t(i)] == Role::Key) {
                    const D2D1_RECT_F eye{field.right - 32, field.top, field.right, field.bottom};
                    const float ex = (eye.left + eye.right) / 2, ey = (eye.top + eye.bottom) / 2;
                    const UINT32 c = hover == Reveal ? t.ink() : t.muted();
                    brush->SetColor(D2D1::ColorF(c));
                    target->DrawEllipse(D2D1::Ellipse({ex, ey}, 7, 4.2f), brush.Get(), 1.3f);
                    target->FillEllipse(D2D1::Ellipse({ex, ey}, 1.9f, 1.9f), brush.Get());
                    if (reveal) target->DrawLine({ex - 7, ey + 6}, {ex + 7, ey - 6}, brush.Get(), 1.4f);
                    hits.push_back({Reveal, eye, true});
                }
            }
            if (RowOf(Role::Model) >= 0) Button(FetchModels, FetchRect(), fetching ? L"获取中…" : L"获取", false, !fetching);
            if (HasAdvanced(*preset)) {
                const float y = AdvancedTop();
                const D2D1_RECT_F r{FieldLeft, y, FieldLeft + 150, y + 24};
                const UINT32 c = hover == Advanced ? t.ink() : t.muted();
                if (advanced) Chevron(r.left + 5, y + 12, c); else { Line({r.left + 3, y + 8}, {r.left + 7, y + 12}, c); Line({r.left + 7, y + 12}, {r.left + 3, y + 16}, c); }
                Text(L"高级：接口地址", {r.left + 16, y, r.right, y + 24}, 12.5f, c);
                hits.push_back({Advanced, r, true});
            }
            }
        }
        // Default target language
        {
            const auto box = LanguageRect();
            Text(L"默认译为", {24, box.top, FieldLeft - 4, box.bottom}, 13, t.ink());
            Fill(box, t.field(), 8);
            Stroke(box, lang_open || hover == LanguageBox ? t.accent() : t.border(), 8, lang_open ? 1.5f : 1.f, hover == LanguageBox && !lang_open ? .6f : 1.f);
            const std::wstring name = default_target == translate::Language::Auto ? std::wstring(L"自动") : std::wstring(translate::LanguageLabel(default_target));
            const float name_width = Measure(name, 13.5f);
            Text(name, {box.left + 12, box.top, box.left + 14 + name_width, box.bottom}, 13.5f, t.ink());
            if (default_target == translate::Language::Auto) Text(L"外文 → 简体中文，中文 → 英文", {box.left + 24 + name_width, box.top, box.right - 32, box.bottom}, 12, t.muted());
            Chevron(box.right - 18, (box.top + box.bottom) / 2, t.muted(), lang_open);
            hits.push_back({LanguageBox, box, true});
        }
        // Test card
        {
            const auto card = TestCard();
            Fill(card, t.card(), 10);
            Stroke(card, t.border(), 10);
            const UINT32 dot = status == Status::Ok ? t.ok() : status == Status::Error ? t.danger() : status == Status::Running ? t.accent() : t.border();
            brush->SetColor(D2D1::ColorF(dot));
            target->FillEllipse(D2D1::Ellipse({card.left + 18, card.top + 20}, 4, 4), brush.Get());
            const float text_right = card.right - 128;
            if (status == Status::Idle) {
                Text(L"测试翻译", {card.left + 32, card.top + 9, text_right, card.top + 31}, 12.5f, t.ink(), true);
                Text(L"发送一句英文，检查引擎能否返回译文", {card.left + 32, card.top + 30, text_right, card.top + 50}, 12, t.muted());
            } else {
                Text(status_title, {card.left + 32, card.top + 9, text_right, card.top + 31}, 12.5f, status == Status::Ok ? t.ok() : status == Status::Error ? t.danger() : t.ink(), true);
                Text(status_body, {card.left + 32, card.top + 29, text_right, card.bottom - 4}, 12, status == Status::Error ? t.ink() : t.muted(), false, DWRITE_TEXT_ALIGNMENT_LEADING, status == Status::Error);
            }
            Button(Test, {card.right - 116, card.top + 12, card.right - 12, card.bottom - 12}, testing ? L"测试中…" : L"测试翻译", false, !testing);
        }
        brush->SetColor(D2D1::ColorF(t.border()));
        const float footer = FooterY();
        target->DrawLine({0, footer + .5f}, {W, footer + .5f}, brush.Get(), 1);
        Button(Cancel, {W - 24 - 192, footer + 12, W - 24 - 104, footer + 48}, L"取消", false);
        Button(Save, {W - 24 - 96, footer + 12, W - 24, footer + 48}, L"保存", true);
        std::vector<Hit> popup;
        if (details_open) DetailsPopup(popup);
        else if (engine_open && preset) EnginePopup(popup);
        else if (lang_open) LanguagePopup(popup);
        else if (models_open && RowOf(Role::Model) >= 0) {
            const auto box = EditBox(RowOf(Role::Model));
            const int count = int(std::min<size_t>(models.size(), 8));
            const float height = 8 + 30.f * std::max(1, count);
            const bool below = box.bottom + 4 + height <= H - 6;
            const D2D1_RECT_F pop{box.left, below ? box.bottom + 4 : box.top - 4 - height, W - 24, below ? box.bottom + 4 + height : box.top - 4};
            Shadow(pop);
            Fill(pop, t.card(), 9);
            Stroke(pop, t.border(), 9);
            if (models.empty()) Text(L"没有获取到模型，请检查地址、密钥或服务是否运行", {pop.left + 12, pop.top + 2, pop.right - 8, pop.bottom - 2}, 12.5f, t.muted());
            for (int i = 0; i < count; ++i) {
                const D2D1_RECT_F r{pop.left + 4, pop.top + 4 + 30.f * i, pop.right - 4, pop.top + 34 + 30.f * i};
                if (hover == ModelBase + i) Fill(r, t.hover(), 6);
                Text(translate::json::Utf16(models[size_t(i)]), {r.left + 10, r.top, r.right - 8, r.bottom}, 13, t.ink());
                popup.push_back({ModelBase + i, r, true});
            }
            popup.push_back({None - 1, pop, false});
        }
        hits.insert(hits.begin(), popup.begin(), popup.end());
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { target.Reset(); brush.Reset(); InvalidateRect(window, nullptr, FALSE); }
    }
    // Placeholder for an empty, unfocused field. The system cue banner uses a
    // fixed gray that reads like real text on white, so draw it with LumaText.
    void EnsureFactories() {
        if (!factory) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
        if (!dw) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
    }
    bool HasCue(HWND edit) const { for (int i = 0; i < EditCount; ++i) if (edits[size_t(i)] == edit) return !cues[size_t(i)].empty(); return false; }
    bool PaintCue(HWND edit, HDC dc) {
        int index = -1;
        for (int i = 0; i < EditCount; ++i) if (edits[size_t(i)] == edit) index = i;
        if (index < 0 || cues[size_t(index)].empty()) return false;
        EnsureFactories();
        if (!factory || !dw) return false;
        if (!cue_target) {
            const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            if (FAILED(factory->CreateDCRenderTarget(&props, cue_target.GetAddressOf()))) return false;
        }
        RECT rc{};
        GetClientRect(edit, &rc);
        if (FAILED(cue_target->BindDC(dc, &rc))) return false;
        cue_target->SetDpi(96 * scale, 96 * scale);
        const auto t = theme();
        auto* format = Format(14, false);
        if (!format) return false;
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        cue_target->BeginDraw();
        cue_target->Clear(D2D1::ColorF(t.field()));
        renderer.Draw(cue_target.Get(), dw.Get(), cues[size_t(index)], format, {1, 0, rc.right / scale, rc.bottom / scale}, D2D1::ColorF(t.dark ? 0x6f7c90 : 0x9aa6b5), true);
        if (cue_target->EndDraw() == D2DERR_RECREATE_TARGET) cue_target.Reset();
        return true;
    }
    static LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto* p = reinterpret_cast<Window*>(data);
        if (message == WM_PAINT && GetWindowTextLengthW(edit) == 0 && GetFocus() != edit && p->HasCue(edit)) {
            PAINTSTRUCT ps{};
            const HDC dc = BeginPaint(edit, &ps);
            if (!p->PaintCue(edit, dc)) FillRect(dc, &ps.rcPaint, p->edit_brush);
            EndPaint(edit, &ps);
            return 0;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, EditProc, 1);
        return DefSubclassProc(edit, message, wp, lp);
    }
    int HitTest(LPARAM lp) const {
        const float x = GET_X_LPARAM(lp) / scale, y = GET_Y_LPARAM(lp) / scale;
        for (const auto& hit : hits) if (x >= hit.rect.left && x < hit.rect.right && y >= hit.rect.top && y < hit.rect.bottom) return hit.enabled || hit.id < None ? hit.id : None;
        return None;
    }
    void Activate(int id) {
        const bool was_models = models_open, was_engine = engine_open, was_lang = lang_open, was_details = details_open;
        models_open = engine_open = lang_open = details_open = false;
        const auto catalog = offline::Models();
        const auto model_at = [&](int base) -> const offline::Model* { return id >= base && id < base + int(catalog.size()) ? &catalog[size_t(id - base)] : nullptr; };
        if (const auto* card = model_at(CardBase)) { Choose(*card); SetStatus(Status::Idle, {}); }
        else if (const auto* action = model_at(ActionBase)) {
            if (installing == action->id) { pausing = true; install_thread.request_stop(); }
            else { Choose(*action); StartInstall(*action); }
        } else if (const auto* doomed = model_at(DeleteBase)) {
            if (confirm_delete == doomed->id) {
                confirm_delete.clear();
                KillTimer(window, ConfirmTimer);
                if (!offline::Remove(*doomed)) SetStatus(Status::Error, L"无法删除模型", L"文件可能正被占用，请稍后再试");
            } else { confirm_delete = std::string(doomed->id); SetTimer(window, ConfirmTimer, 3000, nullptr); }
        }
        else if (id == Details) details_open = true;
        else if (id == DetailsClose) {}
        else if (id == OpenFolder) {
            std::error_code error;
            std::filesystem::create_directories(offline::Root(), error);
            ShellExecuteW(window, L"open", offline::Root().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            details_open = was_details;
        } else if (id >= LinkBase && id < LinkBase + 3) {
            const auto& hy = *offline::FindModel(offline::DefaultModel);
            const auto& gemma = *offline::FindModel("translategemma-4b");
            OpenUrl(id == LinkBase ? translate::json::Utf16(hy.license_url) : id == LinkBase + 1 ? translate::json::Utf16(gemma.license_url) : std::wstring(L"https://github.com/ggml-org/llama.cpp/blob/master/LICENSE"));
            details_open = was_details;
        }
        else if (id >= ModelBase && id < ModelBase + 8 && size_t(id - ModelBase) < models.size()) {
            const int row = RowOf(Role::Model);
            if (row >= 0) SetEdit(row, models[size_t(id - ModelBase)]);
            Capture();
        } else if (id >= PresetBase && id < PresetBase + int(translate::Presets().size())) {
            Capture();
            selected = std::string(translate::Presets()[size_t(id - PresetBase)].id);
            advanced = !drafts[selected].endpoint.empty();
            Apply();
        } else if (id >= LanguageBase && id < LanguageBase + translate::LanguageCount) default_target = static_cast<translate::Language>(id - LanguageBase);
        else if (id == EngineBox) { Capture(); engine_open = !was_engine; }
        else if (id == LanguageBox) lang_open = !was_lang;
        else if (id == Advanced) { Capture(); advanced = !advanced; Apply(); }
        else if (id == Reveal) {
            reveal = !reveal;
            const int row = RowOf(Role::Key);
            if (row >= 0) { SendMessageW(edits[size_t(row)], EM_SETPASSWORDCHAR, reveal ? 0 : WPARAM(L'●'), 0); InvalidateRect(edits[size_t(row)], nullptr, TRUE); }
        }
        else if (id == FetchModels) { if (!was_models || models.empty()) RunModels(); }
        else if (id == Test) RunTest();
        else if (id == Save) SaveAndClose();
        else if (id == Cancel) DestroyWindow(window);
        if (IsWindow(window)) { SyncEdits(); InvalidateRect(window, nullptr, FALSE); }
    }
    bool ClosePopups() {
        if (!models_open && !engine_open && !lang_open && !details_open) return false;
        models_open = engine_open = lang_open = details_open = false;
        SyncEdits();
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }
    void Resize() {
        RECT r{0, 0, int(std::lround(W * scale)), int(std::lround(H * scale))};
        AdjustWindowRectExForDpi(&r, GetWindowLongW(window, GWL_STYLE), FALSE, GetWindowLongW(window, GWL_EXSTYLE), UINT(scale * 96));
        RECT now{};
        GetWindowRect(window, &now);
        const int width = r.right - r.left, height = r.bottom - r.top;
        int top = now.top;
        // Growing for the offline cards must not push the footer off the monitor.
        MONITORINFO monitor{sizeof(monitor)};
        if (now.left > -30000 && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) top = std::max<int>(monitor.rcWork.top, std::min<int>(top, monitor.rcWork.bottom - height));
        SetWindowPos(window, nullptr, now.left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    static LRESULT CALLBACK Proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
        auto* p = reinterpret_cast<Window*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            p = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            p->window = w;
            SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        }
        if (!p) return DefWindowProcW(w, message, wp, lp);
        switch (message) {
        case WM_CREATE: {
            p->scale = GetDpiForWindow(w) / 96.f;
            const BOOL dark = p->dark;
            DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
            // Windows 11: tint the caption to the page so the title bar and client read as one surface.
            const COLORREF caption = p->theme().Ref(p->theme().bg()), caption_text = p->theme().Ref(p->theme().ink());
            DwmSetWindowAttribute(w, 35, &caption, sizeof(caption));
            DwmSetWindowAttribute(w, 36, &caption_text, sizeof(caption_text));
            for (int i = 0; i < EditCount; ++i) {
                p->edits[size_t(i)] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, w, reinterpret_cast<HMENU>(INT_PTR(EditBase + i)), GetModuleHandleW(nullptr), nullptr);
                SetWindowSubclass(p->edits[size_t(i)], EditProc, 1, reinterpret_cast<DWORD_PTR>(p));
            }
            p->edit_brush = CreateSolidBrush(p->theme().Ref(p->theme().field()));
            p->Font();
            p->Load();
            p->Apply();
            p->Resize();
            p->Probe();
            p->ProbeHardware();
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps{}; BeginPaint(w, &ps); p->Paint(); EndPaint(w, &ps); return 0; }
        case WM_SIZE: if (p->target) p->target->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp))); InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_DPICHANGED: {
            p->scale = HIWORD(wp) / 96.f;
            p->formats.clear();
            const auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            p->Font();
            p->Layout();
            p->Resize();
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            const auto t = p->theme();
            SetTextColor(reinterpret_cast<HDC>(wp), t.Ref(t.ink()));
            SetBkColor(reinterpret_cast<HDC>(wp), t.Ref(t.field()));
            return reinterpret_cast<LRESULT>(p->edit_brush);
        }
        case WM_COMMAND:
            if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) { if (HIWORD(wp) == EN_KILLFOCUS) p->Capture(); InvalidateRect(reinterpret_cast<HWND>(lp), nullptr, TRUE); InvalidateRect(w, nullptr, FALSE); }
            if (LOWORD(wp) == IDCANCEL) DestroyWindow(w);
            return 0;
        case WM_MOUSEMOVE: {
            const int hit = p->HitTest(lp);
            if (hit != p->hover) { p->hover = hit; InvalidateRect(w, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, w, 0};
            TrackMouseEvent(&track);
            SetCursor(LoadCursorW(nullptr, hit >= 0 ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT && reinterpret_cast<HWND>(wp) == w) return TRUE; break;
        case WM_MOUSELEAVE: p->hover = None; InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: SetFocus(w); p->down = p->HitTest(lp); SetCapture(w); return 0;
        case WM_LBUTTONUP: {
            const int hit = p->HitTest(lp), down = p->down;
            p->down = None;
            if (GetCapture() == w) ReleaseCapture();
            if (hit >= 0 && hit == down) p->Activate(hit);
            else if (hit == None) p->ClosePopups();
            return 0;
        }
        case TestDone: {
            std::unique_ptr<TestResult> result(reinterpret_cast<TestResult*>(lp));
            p->testing = false;
            wchar_t seconds[16]{};
            swprintf_s(seconds, L"%.1f", double(result->ms) / 1000.0);
            if (result->ok) p->SetStatus(Status::Ok, L"测试通过 · " + std::wstring(seconds) + L" 秒", L"→ " + result->text);
            else p->SetStatus(Status::Error, L"测试失败", result->text);
            return 0;
        }
        case ModelsDone: {
            std::unique_ptr<std::vector<std::string>> list(reinterpret_cast<std::vector<std::string>*>(lp));
            p->fetching = false;
            p->models = std::move(*list);
            p->models_open = true;
            p->SyncEdits();
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        case InstallStep: { std::unique_ptr<offline::Progress> step(reinterpret_cast<offline::Progress*>(lp)); p->Step(*step); return 0; }
        case InstallDone: { std::unique_ptr<offline::Result> result(reinterpret_cast<offline::Result*>(lp)); p->Finish(*result); return 0; }
        case HardwareDone: {
            std::unique_ptr<offline::Hardware> found(reinterpret_cast<offline::Hardware*>(lp));
            p->hardware = *found;
            p->hardware_ready = true;
            // Nothing chosen or installed yet: preselect what suits this PC.
            if (p->drafts["offline"].model.empty() && !p->AnyOfflineInstalled()) p->Choose(offline::Recommend(p->hardware));
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        case WM_TIMER:
            if (wp == ConfirmTimer) { KillTimer(w, ConfirmTimer); p->confirm_delete.clear(); InvalidateRect(w, nullptr, FALSE); }
            return 0;
        case ProbeDone: {
            std::unique_ptr<std::map<std::string, std::string>> found(reinterpret_cast<std::map<std::string, std::string>*>(lp));
            p->ollama_running = found->count("ollama") != 0;
            p->lmstudio_running = found->count("lmstudio") != 0;
            p->detected_model = *found;
            // First run: preselect a detected local engine so it works with zero typing.
            if (p->config.provider.empty() && !found->empty() && !p->AnyOfflineInstalled()) { p->Capture(); p->selected = found->count("ollama") ? "ollama" : "lmstudio"; p->Apply(); }
            else if (p->Preset() && p->Preset()->local && p->RowOf(Role::Model) >= 0 && p->EditText(p->RowOf(Role::Model)).empty() && found->count(p->selected)) p->SetEdit(p->RowOf(Role::Model), (*found)[p->selected]);
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(w); return 0;
        case WM_DESTROY:
            for (auto* thread : {&p->test_thread, &p->models_thread, &p->probe_thread, &p->hardware_thread, &p->install_thread}) thread->request_stop();
            offline::Shutdown();
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            if (p->font) DeleteObject(p->font);
            if (p->edit_brush) DeleteObject(p->edit_brush);
            p->target.Reset();
            p->cue_target.Reset();
            SetWindowLongPtrW(w, GWLP_USERDATA, 0);
            break;
        }
        return DefWindowProcW(w, message, wp, lp);
    }
};
}

int TranslationSettingsMain(bool dark) {
    if (const HWND existing = FindWindowW(ClassName, nullptr)) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); return 0; }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = Window::Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = ClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Same artwork as the tray/app icon, loaded at the exact metric sizes so
    // the caption and taskbar do not show a scaled or generic icon.
    HICON large{}, small_icon{};
    if (FAILED(LoadIconMetric(wc.hInstance, MAKEINTRESOURCEW(IDI_LUMASHOT), LIM_LARGE, &large))) large = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_LUMASHOT));
    if (FAILED(LoadIconMetric(wc.hInstance, MAKEINTRESOURCEW(IDI_LUMASHOT), LIM_SMALL, &small_icon))) small_icon = large;
    wc.hIcon = large;
    wc.hIconSm = small_icon;
    RegisterClassExW(&wc);
    Window state;
    state.dark = dark;
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    const HWND window = CreateWindowExW(0, ClassName, L"截图翻译 · 引擎设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        monitor.rcWork.left + 80, monitor.rcWork.top + 60, 600, 760, nullptr, nullptr, wc.hInstance, &state);
    if (!window) return 1;
    RECT r{};
    GetWindowRect(window, &r);
    const int x = monitor.rcWork.left + ((monitor.rcWork.right - monitor.rcWork.left) - (r.right - r.left)) / 2;
    const int y = monitor.rcWork.top + std::max<int>(0, ((monitor.rcWork.bottom - monitor.rcWork.top) - (r.bottom - r.top)) / 2);
    // Visual checks (tests/translation_preview.cpp) park the window off-screen
    // without activation; PrintWindow still reads the composed surface.
    const bool preview = GetEnvironmentVariableW(L"LUMASHOT_TRANSLATION_PREVIEW", nullptr, 0) != 0;
    SetWindowPos(window, nullptr, preview ? -32000 : x, preview ? -32000 : y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(window, preview ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!preview) SetForegroundWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { if (!state.ClosePopups()) DestroyWindow(window); continue; }
        if (IsDialogMessageW(window, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

bool LaunchTranslationSettings(bool dark) {
    if (const HWND existing = FindWindowW(ClassName, nullptr)) {
        DWORD process = 0;
        GetWindowThreadProcessId(existing, &process);
        AllowSetForegroundWindow(process);
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return true;
    }
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    std::wstring command = L"\"" + std::wstring(path) + L"\" --translation-settings" + (dark ? L" --dark" : L"");
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info)) return false;
    AllowSetForegroundWindow(info.dwProcessId);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return true;
}
}
