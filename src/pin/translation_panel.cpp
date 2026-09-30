#include "pin/translation_panel.h"
#include "pin/translation.h"
#include "export/clipboard.h"
#include "ui/text_renderer.h"
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace lumashot {
using Microsoft::WRL::ComPtr;
namespace {
constexpr float Width = 348.f;
constexpr float MinHeight = 420.f;
constexpr float BodyTop = 110.f, FooterHeight = 60.f;
constexpr UINT_PTR DotsTimer = 1, CopiedTimer = 2;
enum Id { None = -1, Close = 1, ShowTranslated, ShowOriginal, Language, CopyTranslation, CopyBilingual, Settings, Retry, OpenSettings, LanguageOption = 100, Row = 1000 };

struct Theme {
    bool dark{};
    UINT32 bg() const { return dark ? 0x232a35 : 0xf8fafd; }
    UINT32 card() const { return dark ? 0x2c3441 : 0xffffff; }
    UINT32 hover() const { return dark ? 0x354257 : 0xe9eef6; }
    UINT32 ink() const { return dark ? 0xedf4ff : 0x243142; }
    UINT32 muted() const { return dark ? 0x8f9bb0 : 0x7d8a9c; }
    UINT32 border() const { return dark ? 0x3d4858 : 0xdfe5ee; }
    UINT32 accent() const { return dark ? 0x69b3ff : 0x0784ff; }
    // Filled buttons carry white text: keep >= 4.5:1 in both themes.
    UINT32 primary() const { return dark ? 0x1a6fd6 : 0x0784ff; }
    UINT32 disabled() const { return dark ? 0x5b6677 : 0xaab4c2; }
    UINT32 accent_soft() const { return dark ? 0x183e66 : 0xe2efff; }
    UINT32 danger() const { return dark ? 0xff7b84 : 0xd9434e; }
};

bool Contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
}

struct TranslationPanel::Impl {
    HWND owner{}, window{};
    Callbacks callbacks;
    TranslationView view;
    float scale{1}, scroll{0}, content_height{0}, layout_width{0};
    int hover{None}, down{None}, dots{0};
    bool picker{}, layouts_dirty{true};
    ULONGLONG copied_until{};
    std::wstring copied_label;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1SolidColorBrush> brush;
    std::map<std::pair<int, int>, ComPtr<IDWriteTextFormat>> formats;
    TextRenderer renderer;
    struct Hit { int id; D2D1_RECT_F rect; bool enabled; };
    std::vector<Hit> hits;
    struct RowLayout { float top{}, height{}; ComPtr<IDWriteTextLayout> source, result; float source_height{}, result_height{}; };
    std::vector<RowLayout> rows;

    Theme theme() const { return {view.dark}; }
    float Height() const { RECT r{}; GetClientRect(window, &r); return float(r.bottom) / scale; }
    D2D1_RECT_F Body() const { return {16, BodyTop, Width - 16, Height() - FooterHeight - 4}; }

    IDWriteTextFormat* Format(float size, bool bold = false) {
        auto& slot = formats[{int(size * 10), bold ? 1 : 0}];
        if (!slot) {
            dw->CreateTextFormat(L"Microsoft YaHei UI", nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", slot.GetAddressOf());
            if (slot) { slot->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); slot->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
        }
        return slot.Get();
    }
    void Text(const std::wstring& text, D2D1_RECT_F r, float size, UINT32 color, bool bold = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false) {
        auto* format = Format(size, bold);
        if (!format || text.empty()) return;
        format->SetTextAlignment(align);
        format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!wrap) {
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis;
            dw->CreateEllipsisTrimmingSign(format, ellipsis.GetAddressOf());
            format->SetTrimming(&trimming, ellipsis.Get());
        }
        renderer.Draw(target.Get(), dw.Get(), text, format, r, D2D1::ColorF(color), true);
        DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
        format->SetTrimming(&none, nullptr);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    void Fill(D2D1_RECT_F r, UINT32 color, float radius = 8, float opacity = 1) {
        brush->SetColor(D2D1::ColorF(color, opacity));
        target->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush.Get());
    }
    void Stroke(D2D1_RECT_F r, UINT32 color, float radius = 8) {
        brush->SetColor(D2D1::ColorF(color));
        target->DrawRoundedRectangle(D2D1::RoundedRect({r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f}, radius, radius), brush.Get(), 1);
    }
    void AddHit(int id, D2D1_RECT_F r, bool enabled = true) { hits.push_back({id, r, enabled}); }
    void Button(int id, D2D1_RECT_F r, const std::wstring& label, bool primary, bool enabled = true) {
        const auto t = theme();
        const bool active = enabled && hover == id;
        if (primary) Fill(r, enabled ? t.primary() : t.border(), 8, active ? .88f : 1.f);
        else { Fill(r, active ? t.hover() : t.card(), 8, enabled ? 1.f : .6f); Stroke(r, t.border(), 8); }
        Text(label, r, 13, enabled ? (primary ? 0xffffff : t.ink()) : t.disabled(), false, DWRITE_TEXT_ALIGNMENT_CENTER);
        AddHit(id, r, enabled);
    }

    void BuildLayouts() {
        const float width = Width - 32 - 24;
        if (!layouts_dirty && std::abs(layout_width - width) < .5f) return;
        layouts_dirty = false;
        layout_width = width;
        rows.clear();
        float y = 0;
        for (size_t i = 0; i < view.results.size() && i < view.sources.size(); ++i) {
            RowLayout row;
            row.top = y;
            auto* small_format = Format(12);
            auto* large_format = Format(14.5f);
            small_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            large_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            small_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            large_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            DWRITE_TEXT_METRICS m{};
            if (SUCCEEDED(dw->CreateTextLayout(view.sources[i].data(), UINT32(view.sources[i].size()), small_format, width, 100000, row.source.GetAddressOf()))) { row.source->GetMetrics(&m); row.source_height = m.height; }
            if (SUCCEEDED(dw->CreateTextLayout(view.results[i].data(), UINT32(view.results[i].size()), large_format, width, 100000, row.result.GetAddressOf()))) { row.result->GetMetrics(&m); row.result_height = m.height; }
            small_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            large_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            row.height = 10 + row.source_height + 6 + row.result_height + 10;
            y += row.height + 8;
            rows.push_back(std::move(row));
        }
        content_height = rows.empty() ? 0 : y - 8;
    }
    void ClampScroll() {
        const auto body = Body();
        scroll = std::clamp(scroll, 0.f, std::max(0.f, content_height - (body.bottom - body.top)));
    }

    std::wstring Subtitle() const {
        std::wstring text = view.engine.empty() ? L"截图翻译" : view.engine;
        if (view.state == TranslationView::State::Done || view.state == TranslationView::State::Running) {
            text += L" · ";
            text += view.source == translate::Language::Auto ? L"自动检测" : std::wstring(translate::LanguageLabel(view.source));
            text += L" → ";
            text += translate::LanguageLabel(view.target);
        }
        return text;
    }

    void Paint() {
        if (!factory) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
        if (!dw) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
        RECT client{};
        GetClientRect(window, &client);
        if (!target) {
            factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96, 96),
                D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(UINT32(client.right), UINT32(client.bottom))), target.GetAddressOf());
            if (!target) return;
            target->CreateSolidColorBrush(D2D1::ColorF(0), brush.GetAddressOf());
        }
        const auto t = theme();
        const float h = Height();
        hits.clear();
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
        target->Clear(D2D1::ColorF(t.bg()));
        // Header
        Text(L"翻译", {16, 10, 200, 34}, 16, t.ink(), true);
        Text(Subtitle(), {16, 34, Width - 56, 54}, 12, t.muted());
        const D2D1_RECT_F close{Width - 44, 12, Width - 14, 42};
        if (hover == Close) Fill(close, t.hover(), 8);
        brush->SetColor(D2D1::ColorF(t.muted()));
        target->DrawLine({close.left + 10, close.top + 10}, {close.right - 10, close.bottom - 10}, brush.Get(), 1.5f);
        target->DrawLine({close.right - 10, close.top + 10}, {close.left + 10, close.bottom - 10}, brush.Get(), 1.5f);
        AddHit(Close, close);
        // Controls: original/translation toggle + target language chip
        const bool done = view.state == TranslationView::State::Done;
        const D2D1_RECT_F segment{16, 64, 16 + 132, 96};
        Fill(segment, t.dark ? 0x1b212b : 0xeef2f7, 9);
        const D2D1_RECT_F left{segment.left + 3, segment.top + 3, segment.left + 66, segment.bottom - 3}, right{segment.left + 66, segment.top + 3, segment.right - 3, segment.bottom - 3};
        if (done) Fill(view.show_translation ? left : right, t.card(), 7);
        Text(L"译文", left, 13, !done ? t.muted() : view.show_translation ? t.accent() : t.ink(), view.show_translation && done, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(L"原图", right, 13, !done ? t.muted() : !view.show_translation ? t.accent() : t.ink(), !view.show_translation && done, DWRITE_TEXT_ALIGNMENT_CENTER);
        AddHit(ShowTranslated, left, done);
        AddHit(ShowOriginal, right, done);
        const D2D1_RECT_F chip{Width - 16 - 164, 64, Width - 16, 96};
        Fill(chip, picker || hover == Language ? t.hover() : t.card(), 9);
        Stroke(chip, picker ? t.accent() : t.border(), 9);
        std::wstring chip_label = L"译为 ";
        chip_label += view.requested == translate::Language::Auto ? L"自动 · " + std::wstring(translate::LanguageLabel(view.target)) : std::wstring(translate::LanguageLabel(view.requested));
        Text(chip_label, {chip.left + 12, chip.top, chip.right - 26, chip.bottom}, 13, t.ink());
        brush->SetColor(D2D1::ColorF(t.muted()));
        const float cx = chip.right - 16, cy = (chip.top + chip.bottom) / 2;
        target->DrawLine({cx - 4, cy - 2}, {cx, cy + 2}, brush.Get(), 1.4f);
        target->DrawLine({cx, cy + 2}, {cx + 4, cy - 2}, brush.Get(), 1.4f);
        AddHit(Language, chip);
        brush->SetColor(D2D1::ColorF(t.border()));
        target->DrawLine({16, BodyTop - 4}, {Width - 16, BodyTop - 4}, brush.Get(), 1);
        // Body
        const auto body = Body();
        target->PushAxisAlignedClip(D2D1::RectF(0, body.top, Width, body.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
        using State = TranslationView::State;
        const auto center_message = [&](const std::wstring& title, const std::wstring& detail, UINT32 title_color) {
            const float mid = body.top + (body.bottom - body.top) * 0.36f;
            Text(title, {body.left, mid - 24, body.right, mid + 4}, 15, title_color, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            if (!detail.empty()) Text(detail, {body.left + 8, mid + 8, body.right - 8, mid + 92}, 12.5f, t.muted(), false, DWRITE_TEXT_ALIGNMENT_CENTER, true);
            return mid + 104;
        };
        const std::wstring dotted = std::wstring(size_t(dots % 4), L'.');
        if (view.state == State::WaitingOcr) center_message(L"正在识别文字" + dotted, L"识别完成后会自动翻译", t.ink());
        else if (view.state == State::Running) center_message(L"正在翻译" + dotted, L"共 " + std::to_wstring(view.sources.size()) + L" 段文字 · 只发送文字，不上传图片", t.ink());
        else if (view.state == State::NeedsSetup) {
            const float y = center_message(L"还没有配置翻译引擎", L"可使用本机模型（Ollama、LM Studio），或 DeepSeek、通义千问、DeepL、百度、有道、腾讯等服务。", t.ink());
            Button(OpenSettings, {Width / 2 - 110, y, Width / 2 + 2, y + 38}, L"打开翻译设置", true);
            Button(Retry, {Width / 2 + 10, y, Width / 2 + 110, y + 38}, L"重试", false);
        } else if (view.state == State::Failed) {
            const float y = center_message(L"翻译失败", view.error, t.danger());
            Button(Retry, {Width / 2 - 110, y, Width / 2 + 2, y + 38}, L"重试", true);
            Button(OpenSettings, {Width / 2 + 10, y, Width / 2 + 110, y + 38}, L"翻译设置", false);
        } else if (done) {
            BuildLayouts();
            ClampScroll();
            if (rows.empty()) center_message(L"没有可翻译的文字", L"", t.ink());
            for (size_t i = 0; i < rows.size(); ++i) {
                const auto& row = rows[i];
                const float top = body.top + row.top - scroll;
                if (top > body.bottom || top + row.height < body.top) continue;
                const D2D1_RECT_F card{body.left, top, body.right, top + row.height};
                const bool active = hover == Row + int(i);
                Fill(card, active ? t.hover() : t.card(), 10);
                if (!active) Stroke(card, t.border(), 10);
                if (row.source) renderer.DrawLayout(target.Get(), dw.Get(), row.source.Get(), {card.left + 12, card.top + 10}, D2D1::RectF(card.left, std::max(card.top, body.top), card.right, std::min(card.bottom, body.bottom)), D2D1::ColorF(t.muted()), true);
                if (row.result) renderer.DrawLayout(target.Get(), dw.Get(), row.result.Get(), {card.left + 12, card.top + 16 + row.source_height}, D2D1::RectF(card.left, std::max(card.top, body.top), card.right, std::min(card.bottom, body.bottom)), D2D1::ColorF(t.ink()), true);
                AddHit(Row + int(i), {card.left, std::max(card.top, body.top), card.right, std::min(card.bottom, body.bottom)});
            }
            // Scroll indicator
            const float visible = body.bottom - body.top;
            if (content_height > visible) {
                const float track = visible - 8, thumb = std::max(28.f, track * visible / content_height);
                const float y = body.top + 4 + (track - thumb) * (scroll / (content_height - visible));
                Fill({Width - 12, y, Width - 8, y + thumb}, t.muted(), 2, .45f);
            }
        }
        target->PopAxisAlignedClip();
        // Footer
        brush->SetColor(D2D1::ColorF(t.border()));
        target->DrawLine({16, h - FooterHeight}, {Width - 16, h - FooterHeight}, brush.Get(), 1);
        const float fy = h - FooterHeight + 12;
        Button(CopyTranslation, {16, fy, 16 + 104, fy + 36}, L"复制译文", true, done && !rows.empty());
        Button(CopyBilingual, {128, fy, 128 + 104, fy + 36}, L"复制双语", false, done && !rows.empty());
        if (GetTickCount64() < copied_until) Text(copied_label, {240, fy, Width - 16, fy + 36}, 12.5f, t.accent(), false, DWRITE_TEXT_ALIGNMENT_TRAILING);
        else {
            const D2D1_RECT_F settings{Width - 16 - 72, fy, Width - 16, fy + 36};
            if (hover == Settings) Fill(settings, t.hover(), 8);
            Text(L"设置", settings, 13, t.muted(), false, DWRITE_TEXT_ALIGNMENT_CENTER);
            AddHit(Settings, settings);
        }
        // Language popover (drawn last, above the body)
        if (picker) {
            const float top = chip.bottom + 6, row_h = 30;
            const D2D1_RECT_F pop{chip.left - 20, top, chip.right, top + 8 + row_h * translate::LanguageCount};
            brush->SetColor(D2D1::ColorF(0, t.dark ? .35f : .12f));
            target->FillRoundedRectangle(D2D1::RoundedRect({pop.left - 1, pop.top + 2, pop.right + 1, pop.bottom + 4}, 11, 11), brush.Get());
            Fill(pop, t.card(), 10);
            Stroke(pop, t.border(), 10);
            for (int i = 0; i < translate::LanguageCount; ++i) {
                const D2D1_RECT_F r{pop.left + 4, pop.top + 4 + row_h * i, pop.right - 4, pop.top + 4 + row_h * (i + 1)};
                const auto language = static_cast<translate::Language>(i);
                const bool selected = language == view.requested;
                if (hover == LanguageOption + i || selected) Fill(r, hover == LanguageOption + i ? t.hover() : t.accent_soft(), 6);
                const std::wstring label = i == 0 ? std::wstring(L"自动（外文→中文，中文→英文）") : std::wstring(translate::LanguageLabel(language));
                Text(label, {r.left + 12, r.top, r.right - 8, r.bottom}, 13, selected ? t.accent() : t.ink());
                AddHit(LanguageOption + i, r);
            }
            // The popover captures clicks over its area before body rows.
            std::stable_partition(hits.begin(), hits.end(), [](const Hit& hit) { return hit.id >= LanguageOption && hit.id < Row; });
        }
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { target.Reset(); brush.Reset(); InvalidateRect(window, nullptr, FALSE); }
    }

    int HitTest(LPARAM lp) const {
        const float x = GET_X_LPARAM(lp) / scale, y = GET_Y_LPARAM(lp) / scale;
        for (const auto& hit : hits) if (Contains(hit.rect, x, y)) return hit.enabled ? hit.id : None;
        return None;
    }

    void Copied(std::wstring label) {
        copied_label = std::move(label);
        copied_until = GetTickCount64() + 1400;
        SetTimer(window, CopiedTimer, 1450, nullptr);
    }

    void Activate(int id) {
        if (id >= LanguageOption && id < Row) {
            picker = false;
            const auto language = static_cast<translate::Language>(id - LanguageOption);
            if (callbacks.retarget) callbacks.retarget(language);
            return;
        }
        if (id != Language) picker = false;
        switch (id) {
        case Close: if (callbacks.closed) callbacks.closed(); break;
        case ShowTranslated: if (callbacks.show_translation) callbacks.show_translation(true); break;
        case ShowOriginal: if (callbacks.show_translation) callbacks.show_translation(false); break;
        case Language: picker = !picker; break;
        case CopyTranslation: lumashot::CopyText(window, Join(false)); Copied(L"已复制译文"); break;
        case CopyBilingual: lumashot::CopyText(window, Join(true)); Copied(L"已复制原文和译文"); break;
        case Settings: case OpenSettings: if (callbacks.settings) callbacks.settings(); break;
        case Retry: if (callbacks.retry) callbacks.retry(); break;
        default:
            if (id >= Row && size_t(id - Row) < view.results.size()) { lumashot::CopyText(window, view.results[size_t(id - Row)]); Copied(L"已复制这一段"); }
        }
    }

    std::wstring Join(bool bilingual) const { return pin_translation::JoinTranslation(view.sources, view.results, bilingual); }

    void SyncTimer() {
        using State = TranslationView::State;
        if (view.state == State::Running || view.state == State::WaitingOcr) SetTimer(window, DotsTimer, 420, nullptr);
        else KillTimer(window, DotsTimer);
    }

    static LRESULT CALLBACK Proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
        auto* p = reinterpret_cast<Impl*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            p = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            p->window = w;
            SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        }
        if (!p) return DefWindowProcW(w, message, wp, lp);
        switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps{}; BeginPaint(w, &ps); p->Paint(); EndPaint(w, &ps); return 0; }
        case WM_SIZE: if (p->target) p->target->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp))); InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_DPICHANGED: p->scale = HIWORD(wp) / 96.f; p->formats.clear(); p->layouts_dirty = true; InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE: {
            const int hit = p->HitTest(lp);
            if (hit != p->hover) { p->hover = hit; InvalidateRect(w, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, w, 0};
            TrackMouseEvent(&track);
            SetCursor(LoadCursorW(nullptr, hit != None ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) return TRUE; break;
        case WM_MOUSELEAVE: p->hover = None; InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: p->down = p->HitTest(lp); SetCapture(w); return 0;
        case WM_LBUTTONUP: {
            const int hit = p->HitTest(lp), down = p->down;
            p->down = None;
            if (GetCapture() == w) ReleaseCapture();
            if (hit != None && hit == down) p->Activate(hit);
            else if (hit == None && p->picker) p->picker = false;
            if (IsWindow(w)) InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (p->picker) return 0;
            p->scroll -= float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 64.f;
            p->ClampScroll();
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        case WM_TIMER:
            if (wp == DotsTimer) { ++p->dots; InvalidateRect(w, nullptr, FALSE); return 0; }
            if (wp == CopiedTimer) { KillTimer(w, CopiedTimer); InvalidateRect(w, nullptr, FALSE); return 0; }
            break;
        case WM_NCDESTROY:
            KillTimer(w, DotsTimer);
            KillTimer(w, CopiedTimer);
            p->target.Reset();
            p->brush.Reset();
            p->window = nullptr;
            SetWindowLongPtrW(w, GWLP_USERDATA, 0);
            break;
        }
        return DefWindowProcW(w, message, wp, lp);
    }
};

TranslationPanel::TranslationPanel(HWND owner, Callbacks callbacks) : impl_(std::make_unique<Impl>()) {
    impl_->owner = owner;
    impl_->callbacks = std::move(callbacks);
    WNDCLASSW wc{};
    wc.lpfnWndProc = Impl::Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LumaShot.TranslationPanel";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);
    impl_->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, wc.lpszClassName, L"LumaShot 翻译", WS_POPUP,
        0, 0, 1, 1, owner, nullptr, wc.hInstance, impl_.get());
    if (impl_->window) {
        impl_->scale = GetDpiForWindow(impl_->window) / 96.f;
        const DWORD round = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(impl_->window, 33, &round, sizeof(round));
    }
}

TranslationPanel::~TranslationPanel() { if (impl_->window) DestroyWindow(impl_->window); }

void TranslationPanel::Update(const TranslationView& view) {
    const bool content = view.results != impl_->view.results || view.sources != impl_->view.sources;
    if (view.dark != impl_->view.dark && impl_->window) {
        const BOOL dark = view.dark;
        DwmSetWindowAttribute(impl_->window, 20, &dark, sizeof(dark));
    }
    impl_->view = view;
    if (content) { impl_->layouts_dirty = true; impl_->scroll = 0; }
    if (view.state != TranslationView::State::Done) impl_->picker = impl_->picker && view.state != TranslationView::State::NeedsSetup;
    if (impl_->window) { impl_->SyncTimer(); InvalidateRect(impl_->window, nullptr, FALSE); }
}

void TranslationPanel::Place(RECT anchor) {
    if (!impl_->window) return;
    const float scale = GetDpiForWindow(impl_->window) / 96.f;
    if (std::abs(scale - impl_->scale) > .001f) { impl_->scale = scale; impl_->formats.clear(); impl_->layouts_dirty = true; }
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT work = monitor.rcWork;
    const int width = int(std::lround(Width * scale));
    const int gap = int(std::lround(10 * scale)), margin = int(std::lround(8 * scale));
    const int height = std::clamp<int>(anchor.bottom - anchor.top, int(MinHeight * scale), std::max<int>(int(MinHeight * scale), work.bottom - work.top - 2 * margin));
    int x = anchor.right + gap;
    if (x + width > work.right - margin) x = anchor.left - gap - width;
    if (x < work.left + margin) x = work.right - margin - width;
    const int y = std::clamp<int>(anchor.top, work.top + margin, std::max<int>(work.top + margin, work.bottom - margin - height));
    SetWindowPos(impl_->window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void TranslationPanel::Show() { if (impl_->window) ShowWindow(impl_->window, SW_SHOWNOACTIVATE); }
void TranslationPanel::Hide() { if (impl_->window) { impl_->picker = false; ShowWindow(impl_->window, SW_HIDE); } }
bool TranslationPanel::Visible() const { return impl_->window && IsWindowVisible(impl_->window); }
HWND TranslationPanel::Window() const { return impl_->window; }
std::wstring TranslationPanel::CopyText(bool bilingual) const { return impl_->Join(bilingual); }
}
