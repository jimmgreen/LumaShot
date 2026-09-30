#include "app/translation_settings.h"
#include "translate/crypto.h"
#include "translate/engine.h"
#include "translate/json.h"
#include "ui/text_renderer.h"
#include <commctrl.h>
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
constexpr float W = 560.f, H = 668.f;
constexpr UINT TestDone = WM_APP + 1, ModelsDone = WM_APP + 2, ProbeDone = WM_APP + 3;
enum Id { None = -1, PresetBase = 100, LanguageBase = 200, FetchModels = 300, Test, Save, Cancel, ModelBase = 400 };
constexpr int EditCount = 3;
constexpr int EditBase = 1001;
constexpr float FieldTop = 360.f, FieldPitch = 44.f, LabelWidth = 116.f;

struct Theme {
    bool dark{};
    UINT32 bg() const { return dark ? 0x1f2530 : 0xf6f8fb; }
    UINT32 card() const { return dark ? 0x2a313d : 0xffffff; }
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
    HFONT font{};
    HBRUSH edit_brush{};
    int hover{None}, down{None};
    std::wstring test_text;
    UINT32 test_color{};
    bool testing{}, fetching{};
    std::vector<std::string> models;
    bool models_open{};
    bool ollama_running{}, lmstudio_running{};
    std::map<std::string, std::string> detected_model;
    std::jthread test_thread, models_thread, probe_thread;
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
        selected = config.provider.empty() ? std::string("deepseek") : config.provider;
    }
    std::string EditText(int index) const {
        const int length = GetWindowTextLengthW(edits[size_t(index)]);
        std::wstring text(size_t(length) + 1, L'\0');
        GetWindowTextW(edits[size_t(index)], text.data(), length + 1);
        text.resize(size_t(length));
        return translate::json::Utf8(text);
    }
    void SetEdit(int index, const std::string& text) { SetWindowTextW(edits[size_t(index)], translate::json::Utf16(text).c_str()); }
    std::vector<Role> RolesFor(const translate::Preset& preset) const {
        switch (preset.engine) {
        case translate::Engine::OpenAi: return preset.local ? std::vector<Role>{Role::Endpoint, Role::Model} : std::vector<Role>{Role::Endpoint, Role::Key, Role::Model};
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
            SendMessageW(edits[size_t(i)], EM_SETPASSWORDCHAR, role == Role::Key ? WPARAM(L'●') : 0, 0);
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
            const auto cue = Cue(role, *preset, d);
            SendMessageW(edits[size_t(i)], EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue.c_str()));
        }
        models.clear();
        models_open = false;
        test_text.clear();
        Layout();
        InvalidateRect(window, nullptr, FALSE);
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
        if (!credentials) { test_text = reason; test_color = theme().danger(); InvalidateRect(window, nullptr, FALSE); return; }
        testing = true;
        test_text = L"正在测试…";
        test_color = theme().muted();
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
            auto* text = new std::wstring(outcome.ok ? L"✓ " + outcome.texts.front() + L"（" + std::to_wstring(ms / 100 / 10.0).substr(0, std::to_wstring(ms / 100 / 10.0).find('.') + 2) + L" 秒）" : L"✗ " + outcome.error);
            if (!PostMessageW(owner, TestDone, outcome.ok ? 1 : 0, reinterpret_cast<LPARAM>(text))) delete text;
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
        if (!credentials) { test_text = reason; test_color = theme().danger(); InvalidateRect(window, nullptr, FALSE); return; }
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
    void SaveAndClose() {
        auto next = Candidate(true);
        std::wstring reason;
        if (!translate::Resolve(next, &reason)) { test_text = reason; test_color = theme().danger(); InvalidateRect(window, nullptr, FALSE); return; }
        if (!translate::SaveConfig(translate::DefaultConfigPath(), next)) { test_text = L"无法写入设置文件"; test_color = theme().danger(); InvalidateRect(window, nullptr, FALSE); return; }
        if (const HWND host = FindWindowW(L"LumaShot.Host", nullptr)) PostMessageW(host, kTranslationConfigChanged, 0, 0);
        DestroyWindow(window);
    }

    // ---------- layout & paint ----------
    D2D1_RECT_F PresetRect(int index) const {
        const float cw = (W - 48 - 16) / 3.f;
        const int col = index % 3, row = index / 3;
        const float x = 24 + col * (cw + 8), y = 102 + row * 42.f;
        return {x, y, x + cw, y + 34};
    }
    D2D1_RECT_F EditBox(int row) const {
        const float y = FieldTop + row * FieldPitch;
        const bool model = roles[size_t(row)] == Role::Model && Preset() && Preset()->engine == translate::Engine::OpenAi;
        return {24 + LabelWidth, y, model ? W - 24 - 96 : W - 24, y + 34};
    }
    D2D1_RECT_F FetchRect() const { const int row = RowOf(Role::Model); const float y = FieldTop + row * FieldPitch; return {W - 24 - 88, y, W - 24, y + 34}; }
    D2D1_RECT_F LanguageRect(int i) const { const float cw = (W - 48 - LabelWidth - 16) / 5.f; const int col = i % 5, row = i / 5; const float x = 24 + LabelWidth + col * (cw + 4), y = 500 + row * 34.f; return {x, y, x + cw, y + 30}; }
    void Layout() {
        const int inset_x = int(std::lround(10 * scale)), inset_y = int(std::lround(8 * scale));
        for (int i = 0; i < EditCount; ++i) {
            const auto r = EditBox(i);
            SetWindowPos(edits[size_t(i)], nullptr, int(r.left * scale) + inset_x, int(r.top * scale) + inset_y, int((r.right - r.left) * scale) - 2 * inset_x, int((r.bottom - r.top) * scale) - 2 * inset_y + 2, SWP_NOZORDER | SWP_NOACTIVATE);
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
    void Text(const std::wstring& text, D2D1_RECT_F r, float size, UINT32 color, bool bold = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false) {
        auto* format = Format(size, bold);
        if (!format || text.empty()) return;
        format->SetTextAlignment(align);
        format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        renderer.Draw(target.Get(), dw.Get(), text, format, r, D2D1::ColorF(color), true);
    }
    void Fill(D2D1_RECT_F r, UINT32 c, float radius = 8, float opacity = 1) { brush->SetColor(D2D1::ColorF(c, opacity)); target->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush.Get()); }
    void Stroke(D2D1_RECT_F r, UINT32 c, float radius = 8, float width = 1) { brush->SetColor(D2D1::ColorF(c)); target->DrawRoundedRectangle(D2D1::RoundedRect({r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f}, radius, radius), brush.Get(), width); }
    void Button(int id, D2D1_RECT_F r, const std::wstring& label, bool primary, bool enabled = true) {
        const auto t = theme();
        const bool active = enabled && hover == id;
        if (primary) Fill(r, enabled ? t.primary() : t.border(), 8, active ? .88f : 1.f);
        else { Fill(r, active ? t.hover() : t.card(), 8, enabled ? 1.f : .6f); Stroke(r, t.border(), 8); }
        Text(label, r, 13.5f, enabled ? (primary ? 0xffffff : t.ink()) : t.disabled(), false, DWRITE_TEXT_ALIGNMENT_CENTER);
        hits.push_back({id, r, enabled});
    }
    void Paint() {
        if (!factory) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
        if (!dw) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
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
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
        target->Clear(D2D1::ColorF(t.bg()));
        Text(L"截图翻译", {24, 14, W - 24, 42}, 19, t.ink(), true);
        Text(L"只发送识别出的文字，不上传截图；密钥用 Windows DPAPI 加密保存在本机。", {24, 44, W - 24, 66}, 12.5f, t.muted());
        Text(L"翻译引擎", {24, 74, 300, 98}, 13.5f, t.ink(), true);
        const auto presets = translate::Presets();
        for (int i = 0; i < int(presets.size()); ++i) {
            const auto& item = presets[size_t(i)];
            const auto r = PresetRect(i);
            const bool on = item.id == selected, active = hover == PresetBase + i;
            Fill(r, on ? t.accent_soft() : active ? t.hover() : t.card(), 8);
            Stroke(r, on ? t.accent() : t.border(), 8, on ? 1.5f : 1.f);
            const bool running = (item.id == "ollama" && ollama_running) || (item.id == "lmstudio" && lmstudio_running);
            Text(std::wstring(item.label), {r.left + 12, r.top, r.right - (running ? 26 : 8), r.bottom}, 13, on ? t.accent() : t.ink(), on);
            if (running) { brush->SetColor(D2D1::ColorF(t.ok())); target->FillEllipse(D2D1::Ellipse({r.right - 14, (r.top + r.bottom) / 2}, 4, 4), brush.Get()); }
            hits.push_back({PresetBase + i, r, true});
        }
        if (preset) {
            std::wstring hint(preset->signup);
            if ((preset->id == "ollama" && ollama_running) || (preset->id == "lmstudio" && lmstudio_running)) hint = L"已检测到本机服务正在运行 · " + hint;
            Text(hint, {24, 316, W - 24, 352}, 12, t.muted(), false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
            for (int i = 0; i < EditCount; ++i) {
                if (roles[size_t(i)] == Role::None) continue;
                const auto box = EditBox(i);
                Text(Label(roles[size_t(i)], *preset), {24, box.top, 24 + LabelWidth - 8, box.bottom}, 13, t.ink());
                Fill(box, t.card(), 8);
                Stroke(box, GetFocus() == edits[size_t(i)] ? t.accent() : t.border(), 8);
            }
            if (RowOf(Role::Model) >= 0) Button(FetchModels, FetchRect(), fetching ? L"获取中…" : L"获取", false, !fetching);
        }
        Text(L"默认译为", {24, 500, 24 + LabelWidth, 530}, 13, t.ink());
        for (int i = 0; i < translate::LanguageCount; ++i) {
            const auto r = LanguageRect(i);
            const auto language = static_cast<translate::Language>(i);
            const bool on = language == default_target, active = hover == LanguageBase + i;
            Fill(r, on ? t.accent_soft() : active ? t.hover() : t.card(), 7);
            Stroke(r, on ? t.accent() : t.border(), 7);
            Text(i == 0 ? std::wstring(L"自动") : std::wstring(translate::LanguageLabel(language)), r, 12.5f, on ? t.accent() : t.ink(), on, DWRITE_TEXT_ALIGNMENT_CENTER);
            hits.push_back({LanguageBase + i, r, true});
        }
        Text(L"自动：外文译成简体中文，中文译成英文；贴图面板里可临时切换。", {24 + LabelWidth, 568, W - 24, 588}, 11.5f, t.muted());
        Button(Test, {24, 600, 24 + 112, 636}, testing ? L"测试中…" : L"测试翻译", false, !testing);
        Text(test_text, {148, 596, W - 24, 640}, 12.5f, test_color ? test_color : t.muted(), false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        brush->SetColor(D2D1::ColorF(t.border()));
        target->DrawLine({0, H - 0.5f}, {W, H - 0.5f}, brush.Get(), 1);
        // Footer buttons live below the fixed layout height.
        const float fy = H + 4;
        Button(Cancel, {W - 24 - 212, fy, W - 24 - 112, fy + 36}, L"取消", false);
        Button(Save, {W - 24 - 104, fy, W - 24, fy + 36}, L"保存", true);
        if (models_open && RowOf(Role::Model) >= 0) {
            const auto box = EditBox(RowOf(Role::Model));
            const int count = int(std::min<size_t>(models.size(), 8));
            const D2D1_RECT_F pop{box.left, box.bottom + 4, W - 24, box.bottom + 8 + 30.f * std::max(1, count)};
            brush->SetColor(D2D1::ColorF(0, t.dark ? .35f : .12f));
            target->FillRoundedRectangle(D2D1::RoundedRect({pop.left - 1, pop.top + 2, pop.right + 1, pop.bottom + 4}, 10, 10), brush.Get());
            Fill(pop, t.card(), 9);
            Stroke(pop, t.border(), 9);
            if (models.empty()) Text(L"没有获取到模型，请检查地址、密钥或服务是否运行", {pop.left + 12, pop.top + 2, pop.right - 8, pop.bottom - 2}, 12.5f, t.muted());
            std::vector<Hit> popup;
            for (int i = 0; i < count; ++i) {
                const D2D1_RECT_F r{pop.left + 4, pop.top + 4 + 30.f * i, pop.right - 4, pop.top + 34 + 30.f * i};
                if (hover == ModelBase + i) Fill(r, t.hover(), 6);
                Text(translate::json::Utf16(models[size_t(i)]), {r.left + 10, r.top, r.right - 8, r.bottom}, 13, t.ink());
                popup.push_back({ModelBase + i, r, true});
            }
            popup.push_back({None - 1, pop, false});
            hits.insert(hits.begin(), popup.begin(), popup.end());
        }
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { target.Reset(); brush.Reset(); InvalidateRect(window, nullptr, FALSE); }
    }
    int HitTest(LPARAM lp) const {
        const float x = GET_X_LPARAM(lp) / scale, y = GET_Y_LPARAM(lp) / scale;
        for (const auto& hit : hits) if (x >= hit.rect.left && x < hit.rect.right && y >= hit.rect.top && y < hit.rect.bottom) return hit.enabled ? hit.id : None;
        return None;
    }
    void Activate(int id) {
        const bool was_open = models_open;
        models_open = false;
        if (id >= ModelBase && id < ModelBase + 8 && size_t(id - ModelBase) < models.size()) {
            const int row = RowOf(Role::Model);
            if (row >= 0) SetEdit(row, models[size_t(id - ModelBase)]);
            Capture();
        } else if (id >= PresetBase && id < PresetBase + int(translate::Presets().size())) {
            Capture();
            selected = std::string(translate::Presets()[size_t(id - PresetBase)].id);
            Apply();
        } else if (id >= LanguageBase && id < LanguageBase + translate::LanguageCount) default_target = static_cast<translate::Language>(id - LanguageBase);
        else if (id == FetchModels) { if (!was_open || models.empty()) RunModels(); }
        else if (id == Test) RunTest();
        else if (id == Save) SaveAndClose();
        else if (id == Cancel) DestroyWindow(window);
        if (IsWindow(window)) InvalidateRect(window, nullptr, FALSE);
    }
    void Resize() {
        RECT r{0, 0, int(std::lround(W * scale)), int(std::lround((H + 56) * scale))};
        AdjustWindowRectExForDpi(&r, GetWindowLongW(window, GWL_STYLE), FALSE, GetWindowLongW(window, GWL_EXSTYLE), UINT(scale * 96));
        SetWindowPos(window, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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
            for (int i = 0; i < EditCount; ++i) p->edits[size_t(i)] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, w, reinterpret_cast<HMENU>(INT_PTR(EditBase + i)), GetModuleHandleW(nullptr), nullptr);
            p->edit_brush = CreateSolidBrush(p->theme().Ref(p->theme().card()));
            p->Font();
            p->Load();
            p->Apply();
            p->Resize();
            p->Probe();
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
            SetBkColor(reinterpret_cast<HDC>(wp), t.Ref(t.card()));
            return reinterpret_cast<LRESULT>(p->edit_brush);
        }
        case WM_COMMAND:
            if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) { if (HIWORD(wp) == EN_KILLFOCUS) p->Capture(); InvalidateRect(w, nullptr, FALSE); }
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
            else if (p->models_open && hit == None) { p->models_open = false; InvalidateRect(w, nullptr, FALSE); }
            return 0;
        }
        case TestDone: {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            p->testing = false;
            p->test_text = *text;
            p->test_color = wp ? p->theme().ok() : p->theme().danger();
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        case ModelsDone: {
            std::unique_ptr<std::vector<std::string>> list(reinterpret_cast<std::vector<std::string>*>(lp));
            p->fetching = false;
            p->models = std::move(*list);
            p->models_open = true;
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        case ProbeDone: {
            std::unique_ptr<std::map<std::string, std::string>> found(reinterpret_cast<std::map<std::string, std::string>*>(lp));
            p->ollama_running = found->count("ollama") != 0;
            p->lmstudio_running = found->count("lmstudio") != 0;
            p->detected_model = *found;
            // First run: preselect a detected local engine so it works with zero typing.
            if (p->config.provider.empty() && !found->empty()) { p->Capture(); p->selected = found->count("ollama") ? "ollama" : "lmstudio"; p->Apply(); }
            else if (p->Preset() && p->Preset()->local && p->RowOf(Role::Model) >= 0 && p->EditText(p->RowOf(Role::Model)).empty() && found->count(p->selected)) p->SetEdit(p->RowOf(Role::Model), (*found)[p->selected]);
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(w); return 0;
        case WM_DESTROY:
            for (auto* thread : {&p->test_thread, &p->models_thread, &p->probe_thread}) thread->request_stop();
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            if (p->font) DeleteObject(p->font);
            if (p->edit_brush) DeleteObject(p->edit_brush);
            p->target.Reset();
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
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    Window state;
    state.dark = dark;
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    const HWND window = CreateWindowExW(0, ClassName, L"LumaShot 截图翻译设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
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
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { DestroyWindow(window); continue; }
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
