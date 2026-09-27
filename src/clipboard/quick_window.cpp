#include "clipboard/quick_window.h"
#include "clipboard/composition.h"
#include "ui/text_renderer.h"
#include "ui/acrylic.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lumashot::clipboard {
namespace {
constexpr size_t Missing = (std::numeric_limits<size_t>::max)();
constexpr float Header = 38.f, RowHeight = 34.f, Footer = 40.f;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Quick clipboard rendering failed"); }
std::wstring SingleLine(std::wstring value) {
    for (auto& c : value) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    return value;
}
template<class F, class... A> void Invoke(const F& source, A... args) noexcept {
    try { auto callback = source; if (callback) callback(args...); } catch (...) { /* Never unwind through USER32. */ }
}
}
struct QuickWindow::Impl {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    QuickCallbacks callbacks;
    HWND window{};
    float scale{1.f};
    bool dark{}, continuous{}, busy{}, acrylic{};
    int tab{}, width{360}, height{270}, pressedTab{-1};
    size_t selected{}, first{}, pressed{Missing};
    uint64_t pressedId{};
    int wheelRemainder{}; LPARAM hoverPoint{}; bool hoverValid{};
    std::vector<QuickRow> rows;
    std::wstring status;
    Ptr<ID2D1Factory> factory;
    Ptr<IDWriteFactory> writer;
    Ptr<ID2D1DCRenderTarget> target;
    Ptr<ID2D1SolidColorBrush> brush;
    Ptr<IDWriteTextFormat> font, smallFont;
    std::unique_ptr<DibSurface> surface;
    std::unique_ptr<ClipboardComposition> composition;
    TextRenderer text;

    explicit Impl(QuickCallbacks value) : callbacks(std::move(value)) {}
    ~Impl() { composition.reset(); if (window) DestroyWindow(window); }
    void Invalidate() { if (window && IsWindowVisible(window)) InvalidateRect(window, nullptr, FALSE); }
    size_t HitRow(LPARAM lp) const {
        const float x = GET_X_LPARAM(lp) / scale, y = GET_Y_LPARAM(lp) / scale;
        if (x < 0 || x >= width / scale || y < Header || y >= Header + RowHeight * 6) return Missing;
        const size_t index = first + static_cast<size_t>((y - Header) / RowHeight);
        return index < rows.size() ? index : Missing;
    }
    int HitTab(LPARAM lp) const {
        const float x = GET_X_LPARAM(lp) / scale, y = GET_Y_LPARAM(lp) / scale;
        return y >= 0 && y < Header && x >= 0 && x < width / scale
            ? std::min(4, static_cast<int>(x / (width / scale / 5.f))) : -1;
    }
    void Label(std::wstring_view value, D2D1_RECT_F box, UINT32 color, bool tiny = false) {
        if (value.empty()) return;
        Ptr<IDWriteTextLayout> layout;
        const float available = std::max(1.f, box.right - box.left);
        auto measure = [&](std::wstring_view content) {
            layout.Reset();
            Check(writer->CreateTextLayout(content.data(), static_cast<UINT32>(content.size()),
                tiny ? smallFont.Get() : font.Get(), available,
                std::max(1.f, box.bottom - box.top), &layout));
            DWRITE_TEXT_METRICS metrics{}; Check(layout->GetMetrics(&metrics));
            return metrics.widthIncludingTrailingWhitespace;
        };
        // LumaText draws glyph runs, so use an actual ellipsis glyph rather
        // than DirectWrite's inline trimming object.
        if (measure(value) > available) {
            size_t low = 0, high = value.size(), best = 0;
            while (low <= high) {
                const size_t mid = low + (high - low) / 2;
                size_t count = mid;
                if (count > 0 && count < value.size() && value[count - 1] >= 0xd800 && value[count - 1] <= 0xdbff
                    && value[count] >= 0xdc00 && value[count] <= 0xdfff) --count;
                std::wstring candidate(value.substr(0, count)); candidate += L'…';
                if (measure(candidate) <= available) { best = count; low = mid + 1; }
                else { if (mid == 0) break; high = mid - 1; }
            }
            std::wstring shortened(value.substr(0, best)); shortened += L'…'; measure(shortened);
        }
        text.DrawLayout(target.Get(), writer.Get(), layout.Get(), D2D1::Point2F(box.left, box.top), box, D2D1::ColorF(color));
    }
    void Fill(D2D1_RECT_F box, UINT32 color) {
        brush->SetColor(D2D1::ColorF(color)); target->FillRectangle(box, brush.Get());
    }
    void Rounded(D2D1_RECT_F box, float radius, UINT32 color, float opacity = 1.f) {
        brush->SetColor(D2D1::ColorF(color, opacity));
        target->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), brush.Get());
    }
    void Icon(Kind kind, float top, UINT32 color) {
        brush->SetColor(D2D1::ColorF(color));
        const float x = 15.f, y = top + 10.f;
        auto line = [&](float x1, float y1, float x2, float y2) {
            target->DrawLine(D2D1::Point2F(x + x1, y + y1), D2D1::Point2F(x + x2, y + y2), brush.Get(), 1.2f);
        };
        if (kind == Kind::Text) {
            line(2, 0, 10, 0); line(10, 0, 14, 4); line(14, 4, 14, 15);
            line(14, 15, 2, 15); line(2, 15, 2, 0);
            line(10, 0, 10, 4); line(10, 4, 14, 4);
            line(5, 7, 11, 7); line(5, 10, 11, 10);
        } else if (kind == Kind::Image) {
            target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y + 1, x + 16, y + 14), 2, 2), brush.Get(), 1.2f);
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 5, y + 5), 1.4f, 1.4f), brush.Get());
            line(2, 12, 7, 8); line(7, 8, 10, 10); line(10, 10, 13, 6); line(13, 6, 16, 10);
        } else {
            line(0, 4, 0, 1); line(0, 1, 6, 1); line(6, 1, 8, 4); line(8, 4, 15, 4);
            line(15, 4, 15, 6); line(0, 4, 0, 14); line(0, 14, 14, 14);
            line(14, 14, 17, 6); line(17, 6, 3, 6); line(3, 6, 0, 14);
        }
    }
    void Render() {
        if (!surface) surface = std::make_unique<DibSurface>(width, height);
        if (!factory) Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        if (!writer) Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(writer.GetAddressOf())));
        if (!target) {
            const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            Check(factory->CreateDCRenderTarget(&props, &target));
            Check(target->CreateSolidColorBrush(D2D1::ColorF(0), &brush));
        }
        auto makeFont = [&](Ptr<IDWriteTextFormat>& result, float size) {
            if (result) return;
            Check(writer->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &result));
            Check(result->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            Check(result->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
        };
        makeFont(font, 12.f); makeFont(smallFont, 11.f);
        const RECT client{0, 0, width, height};
        Check(target->BindDC(surface->Dc(), &client));
        target->SetDpi(96.f * scale, 96.f * scale);
        target->BeginDraw();
        try {
            const float w = width / scale, h = height / scale;
            const UINT32 ink = dark ? 0xe6edf8 : 0x243042, muted = dark ? 0x98abc4 : 0x70829b;
            target->Clear(D2D1::ColorF(0, 0.f));
            if (!acrylic) Rounded(D2D1::RectF(.5f, .5f, w - .5f, h - .5f), 10.f, PanelBackground(dark));
            const wchar_t* tabs[]{L"全部", L"收藏", L"文本", L"图片", L"文件"};
            for (int i = 0; i < 5; ++i) {
                const float left = i * w / 5.f;
                if (tab == i) Rounded(D2D1::RectF(left + 7, 5, left + w / 5.f - 7, Header - 5), 6.f, dark ? 0x334d70 : 0xd5e7ff);
                Label(tabs[i], D2D1::RectF(left + (w / 5.f - 24) / 2, 0, left + w / 5.f - 6, Header), tab == i ? (dark ? 0xaed1ff : 0x245b9d) : muted);
            }
            for (size_t i = first; i < rows.size() && i < first + 6; ++i) {
                const float top = Header + static_cast<float>(i - first) * RowHeight;
                if (i == selected) Rounded(D2D1::RectF(5, top + 2, w - 5, top + RowHeight - 2), 6.f, dark ? 0x304563 : 0xe0edff);
                const auto& row = rows[i];
                Icon(row.kind, top, muted);
                Label(row.text, D2D1::RectF(38, top, w - 32, top + RowHeight), ink);
                if (row.favorite) Label(L"★", D2D1::RectF(w - 25, top, w - 7, top + RowHeight), 0xd9a325);
            }
            if (rows.empty()) {
                font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                Label(busy ? L"正在加载…" : status.empty() ? L"暂无记录" : status,
                    D2D1::RectF(16, Header, w - 16, h - Footer), muted);
                font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            Fill(D2D1::RectF(0, h - Footer, w, h - Footer + 1), dark ? 0x344257 : 0xdce5f1);
            if (!status.empty() || busy) {
                Label(!status.empty() ? status : L"正在加载…", D2D1::RectF(12, h - Footer, w - 12, h), muted);
            } else {
                const auto shortcut = [&](float x, float y, float keyWidth, const wchar_t* key,
                    const wchar_t* description, bool active = false) {
                    const auto cap = D2D1::RectF(x, y, x + keyWidth, y + 23);
                    Rounded(cap, 4, active ? (dark ? 0x35577e : 0xd5e7ff) : (dark ? 0x2a384b : 0xe9eef5));
                    brush->SetColor(D2D1::ColorF(active ? (dark ? 0x729dcc : 0x8cb3e5) : (dark ? 0x4a5d76 : 0xcbd6e4)));
                    target->DrawRoundedRectangle(D2D1::RoundedRect(cap, 4, 4), brush.Get(), 1.f);
                    smallFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    Label(key, cap, active ? (dark ? 0xc7e0ff : 0x245b9d) : ink, true);
                    smallFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    Label(description, D2D1::RectF(x + keyWidth + 8, y, x + keyWidth + 70, y + 23), muted, true);
                };
                const float top = h - Footer + 8;
                shortcut(12, top, 38, L"← →", L"分类");
                shortcut(120, top, 38, L"↑ ↓", L"选择");
                shortcut(228, top, 28, L"F2", continuous ? L"连续取用中" : L"连续取用", continuous);
            }
            if (rows.size() > 6) {
                const float track = RowHeight * 6 - 8;
                const float thumb = std::max(18.f, track * 6 / static_cast<float>(rows.size()));
                const float y = Header + 4 + (track - thumb) * static_cast<float>(first) / static_cast<float>(rows.size() - 6);
                Rounded(D2D1::RectF(w - 4, y, w - 2, y + thumb), 1, muted, .65f);
            }
            brush->SetColor(D2D1::ColorF(dark ? 0x40516a : 0xc9d9ed));
            target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(.5f, .5f, w - .5f, h - .5f), 10, 10), brush.Get(), 1.f);
        } catch (...) { target->EndDraw(); throw; }
        const HRESULT hr = target->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { brush.Reset(); target.Reset(); }
        Check(hr);
    }
    void ApplyBackdrop() {
        const BOOL night = dark;
        DwmSetWindowAttribute(window, 20, &night, sizeof(night));
        const DWORD backdrop = 3, corner = 2;
        DwmSetWindowAttribute(window, 33, &corner, sizeof(corner));
        const MARGINS margins{-1}; DwmExtendFrameIntoClientArea(window, &margins);
        acrylic = SUCCEEDED(DwmSetWindowAttribute(window, 38, &backdrop, sizeof(backdrop)));
    }
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wp, lp);
        switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_SETTINGCHANGE: self->ApplyBackdrop(); self->Invalidate(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
            try {
                self->Render();
                if (!self->composition) self->composition = std::make_unique<ClipboardComposition>();
                self->composition->Present(hwnd, static_cast<UINT>(self->width), static_cast<UINT>(self->height), self->surface->Pixels());
            } catch (...) {}
            EndPaint(hwnd, &ps); return 0;
        }
        case WM_MOUSEMOVE: {
            if (self->hoverValid && self->hoverPoint == lp) return 0;
            self->hoverPoint = lp; self->hoverValid = true;
            const size_t index = self->HitRow(lp);
            if (index != Missing && index != self->selected) Invoke(self->callbacks.select, index);
            return 0;
        }
        case WM_LBUTTONDOWN:
            self->pressed = self->HitRow(lp); self->pressedTab = self->HitTab(lp);
            self->pressedId = self->pressed == Missing ? 0 : self->rows[self->pressed].id;
            SetCapture(hwnd); return 0;
        case WM_LBUTTONUP: {
            const size_t index = self->HitRow(lp), pressed = self->pressed;
            const int category = self->HitTab(lp), pressedTab = self->pressedTab;
            const uint64_t id = self->pressedId;
            self->pressed = Missing; self->pressedTab = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            if (category >= 0 && category == pressedTab) Invoke(self->callbacks.category, category);
            else if (index != Missing && index == pressed && self->rows[index].id == id && !self->busy) Invoke(self->callbacks.paste, index);
            return 0;
        }
        case WM_CAPTURECHANGED: self->pressed = Missing; self->pressedTab = -1; return 0;
        case WM_MOUSEWHEEL: {
            if (self->busy) return 0;
            self->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wp);
            const int steps = self->wheelRemainder / WHEEL_DELTA;
            self->wheelRemainder %= WHEEL_DELTA;
            if (!steps || self->rows.size() <= 6) return 0;
            UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            const int amount = lines == WHEEL_PAGESCROLL ? 6 : static_cast<int>(std::min(lines, 6u));
            const int oldFirst = static_cast<int>(self->first);
            const int next = std::clamp(oldFirst - steps * amount, 0, static_cast<int>(self->rows.size()) - 6);
            const int delta = next - oldFirst;
            if (!delta) return 0;
            self->first = static_cast<size_t>(next);
            self->pressed = Missing; self->pressedTab = -1;
            POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd, &point);
            self->hoverPoint = MAKELPARAM(point.x, point.y); self->hoverValid = true;
            self->Invalidate();
            Invoke(self->callbacks.move, delta);
            return 0;
        }
        case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); Invoke(self->callbacks.close); return 0;
        case WM_NCDESTROY: self->window = nullptr; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); break;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
};
QuickWindow::QuickWindow(QuickCallbacks callbacks) : impl_(std::make_unique<Impl>(std::move(callbacks))) {}
QuickWindow::~QuickWindow() = default;
void QuickWindow::Show(HWND owner, POINT anchor, float scale, bool dark) {
    auto& p = *impl_;
    p.wheelRemainder = 0; p.hoverValid = false;
    p.scale = std::isfinite(scale) && scale > 0.f ? std::clamp(scale, .25f, 8.f) : 1.f; p.dark = dark;
    if (!p.window) {
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = Impl::Proc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"LumaShot.Clipboard.Quick";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Quick clipboard class failed");
        if (!CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
            wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, wc.hInstance, &p)) throw std::runtime_error("Quick clipboard window failed");
    }
    SetWindowLongPtrW(p.window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    p.ApplyBackdrop();
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor)) throw std::runtime_error("Quick clipboard monitor failed");
    const RECT work = monitor.rcWork;
    const int width = std::max(1, std::min(static_cast<int>(std::ceil(360.f * p.scale)), static_cast<int>(work.right - work.left)));
    const int height = std::max(1, std::min(static_cast<int>(std::ceil((Header + 6 * RowHeight + Footer) * p.scale)), static_cast<int>(work.bottom - work.top)));
    if (width != p.width || height != p.height) { p.surface.reset(); p.width = width; p.height = height; }
    const int x = std::clamp(static_cast<int>(anchor.x), static_cast<int>(work.left), static_cast<int>(work.right) - width);
    const int y = std::clamp(static_cast<int>(anchor.y) - height - static_cast<int>(6 * p.scale), static_cast<int>(work.top), static_cast<int>(work.bottom) - height);
    SetWindowPos(p.window, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    p.Invalidate();
}
void QuickWindow::Update(std::vector<QuickRow> rows, size_t selected, int tab, bool continuous, bool busy, std::wstring status) {
    auto& p = *impl_;
    if (p.tab != tab) { p.first = 0; p.wheelRemainder = 0; }
    for (auto& row : rows) row.text = SingleLine(std::move(row.text));
    p.rows = std::move(rows); p.selected = p.rows.empty() ? 0 : std::min(selected, p.rows.size() - 1);
    p.tab = std::clamp(tab, 0, 4); p.continuous = continuous; p.busy = busy; p.status = SingleLine(std::move(status));
    if (p.selected < p.first) p.first = p.selected;
    if (p.selected >= p.first + 6) p.first = p.selected - 5;
    p.first = std::min(p.first, p.rows.size() > 6 ? p.rows.size() - 6 : 0);
    p.Invalidate();
}
void QuickWindow::Hide() {
    auto& p = *impl_; p.pressed = Missing; p.pressedTab = -1;
    if (p.window) { if (GetCapture() == p.window) ReleaseCapture(); ShowWindow(p.window, SW_HIDE); }
    p.composition.reset(); p.surface.reset(); p.brush.Reset(); p.target.Reset();
}
bool QuickWindow::Visible() const { return impl_->window && IsWindowVisible(impl_->window); }
HWND QuickWindow::Window() const { return impl_->window; }
RECT QuickWindow::Bounds() const { RECT bounds{}; if (impl_->window) GetWindowRect(impl_->window, &bounds); return bounds; }
Frame QuickWindow::Snapshot() {
    auto& p = *impl_; p.Render();
    Frame frame = MakeFrame({0, 0, p.width, p.height});
    std::copy_n(p.surface->Pixels(), frame.pixels.size(), frame.pixels.begin()); return frame;
}
}
