#include "longshot/viewer.h"
#include "app/resource.h"
#include "export/png.h"
#include <stdexcept>
#include <utility>
#include <commdlg.h>
#include <dwmapi.h>
#include <objbase.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")

namespace lumashot::longshot {
namespace {
constexpr UINT kOverviewReady = WM_APP + 311, kJobDone = WM_APP + 312;
constexpr wchar_t kClass[] = L"LumaShot.LongCaptureViewer";
constexpr int kTile = 2048;
constexpr float kTitle = 44, kActions = 60, kSide = 252, kMargin = 20;
enum Id { Minimize = 1, MaximizeId = 2, CloseId = 3, FitId = 30, ActualId = 31, RecaptureId = 40, OcrId = 41, PinId = 42,
    CopyId = 43, SaveId = 44, ResetCrop = 50, SeamsId = 60, ScrollbarId = 61, TrimId = 62 };

Frame Extract(const Frame& image, RECT crop) {
    const int w = crop.right - crop.left, h = crop.bottom - crop.top;
    Frame result = MakeFrame({0, 0, w, h});
    for (int y = 0; y < h; ++y)
        std::copy_n(image.pixels.data() + static_cast<size_t>(crop.top + y) * image.Width() + crop.left, w,
            result.pixels.data() + static_cast<size_t>(y) * w);
    return result;
}

// Blank margins: leading/trailing columns that keep one colour on every row.
std::pair<int, int> BlankColumns(const Frame& image, int usable) {
    const int w = std::min(usable, image.Width()), h = image.Height();
    if (w < 64) return {0, 0};
    std::vector<uint32_t> first(image.pixels.begin(), image.pixels.begin() + w);
    std::vector<uint8_t> uniform(static_cast<size_t>(w), 1);
    for (int y = 1; y < h; ++y) {
        const uint32_t* row = image.pixels.data() + static_cast<size_t>(y) * image.Width();
        for (int x = 0; x < w; ++x) uniform[x] &= static_cast<uint8_t>(((row[x] ^ first[x]) & 0xffffff) == 0);
    }
    const auto same = [&](int x, int y) { return ((first[x] ^ first[y]) & 0xffffff) == 0; };
    int left = 0, right = 0;
    while (left < w * 45 / 100 && uniform[left] && same(left, 0)) ++left;
    while (right < w * 45 / 100 && uniform[w - 1 - right] && same(w - 1 - right, w - 1)) ++right;
    // Keep a little breathing room around the content.
    return {std::max(0, left - 8), std::max(0, right - 8)};
}

std::wstring DefaultName() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t name[64]{};
    swprintf_s(name, L"LumaShot-长截图-%04u%02u%02u-%02u%02u%02u.png", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return name;
}
}

Viewer::Viewer(std::unique_ptr<CaptureResult> result, Host host, std::function<void(Viewer*)> closed)
    : host_(std::move(host)), closed_(std::move(closed)) {
    seams_ = std::move(result->seams);
    header_ = result->header;
    footer_ = result->footer;
    scrollbar_ = std::min(result->scrollbar, std::max(0, result->image.Width() - 32));
    region_ = result->region;
    image_ = std::make_shared<const Frame>(std::move(result->image));
    crop_bottom_ = image_->Height();
    remove_scrollbar_ = scrollbar_ > 0;
    dark_ = host_.dark && host_.dark();

    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_LUMASHOT));
    RegisterClassW(&wc);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&region_, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT work = monitor.rcWork;
    window_ = CreateWindowExW(0, kClass, L"LumaShot 长截图", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        work.left, work.top, 800, 600, nullptr, nullptr, wc.hInstance, this);
    if (!window_) throw std::runtime_error("Unable to open the long capture viewer");
    scale_ = Scale(window_);
    const int work_w = work.right - work.left, work_h = work.bottom - work.top;
    const int wanted = static_cast<int>(static_cast<float>(image_->Width()) + (kSide + 2 * kMargin + 24) * scale_);
    const int w = std::clamp(wanted, std::min(work_w, static_cast<int>(900 * scale_)), static_cast<int>(work_w * .86));
    const int h = static_cast<int>(work_h * .88);
    const DWORD round = 2;
    DwmSetWindowAttribute(window_, 33, &round, sizeof(round));
    const BOOL dark_frame = dark_;
    DwmSetWindowAttribute(window_, 20, &dark_frame, sizeof(dark_frame));
    const MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(window_, &margins);
    SetWindowPos(window_, nullptr, work.left + (work_w - w) / 2, work.top + (work_h - h) / 2, w, h, SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);

    // Overview for the minimap and blank-margin detection happen off the UI thread.
    const int strip_width = static_cast<int>((kSide - 56) * scale_);
    overview_worker_ = std::jthread([this, image = image_, strip_width, usable = image_->Width() - scrollbar_, window = window_] {
        try {
            auto overview = std::make_shared<Frame>(Downscale(*image, std::max(32, strip_width), 4096));
            const auto [left, right] = BlankColumns(*image, usable);
            {
                std::lock_guard lock(mutex_);
                overview_ = std::move(overview);
                trim_left_ = left;
                trim_right_ = right;
            }
            PostMessageW(window, kOverviewReady, 0, 0);
        } catch (...) {}
    });
}

Viewer::~Viewer() {
    if (overview_worker_.joinable()) overview_worker_.join();
    if (job_worker_.joinable()) job_worker_.join();
    if (window_ && IsWindow(window_)) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
    }
}

// --------------------------------------------------------------------------
// Geometry
// --------------------------------------------------------------------------
Viewer::Layout Viewer::Compute() const {
    RECT client{};
    GetClientRect(window_, &client);
    const float s = scale_, w = static_cast<float>(client.right), h = static_cast<float>(client.bottom);
    Layout l;
    l.title = {0, 0, w, kTitle * s};
    l.actions = {0, h - kActions * s, w, h};
    l.side = {w - kSide * s, l.title.bottom, w, l.actions.top};
    l.main = {0, l.title.bottom, l.side.left, l.actions.top};
    const float options = (36 + 3 * 34 + 58) * s;
    l.minimap = {l.side.left + 16 * s, l.side.top + 52 * s, l.side.right - 16 * s, std::max(l.side.top + 140 * s, l.side.bottom - options - 12 * s)};
    const float iw = static_cast<float>(image_->Width()), ih = static_cast<float>(image_->Height());
    float sh = l.minimap.bottom - l.minimap.top - 20 * s;
    float sw = sh * iw / ih;
    const float max_w = l.minimap.right - l.minimap.left - 40 * s;
    if (sw > max_w) { sw = max_w; sh = sw * ih / iw; }
    const float cx = (l.minimap.left + l.minimap.right) / 2;
    l.strip = {cx - sw / 2, l.minimap.top + 10 * s, cx + sw / 2, l.minimap.top + 10 * s + sh};
    return l;
}

float Viewer::StripScale(const Layout& l) const {
    return (l.strip.bottom - l.strip.top) / static_cast<float>(image_->Height());
}

float Viewer::Zoom() const {
    if (!fit_) return zoom_;
    const Layout l = Compute();
    const float available = l.main.right - l.main.left - 2 * kMargin * scale_;
    return std::clamp(available / static_cast<float>(image_->Width()), .02f, 1.f);
}

float Viewer::ContentLeft(const Layout& l) const {
    const float width = static_cast<float>(image_->Width()) * Zoom();
    const float available = l.main.right - l.main.left - 2 * kMargin * scale_;
    if (width <= available) return std::round(l.main.left + (l.main.right - l.main.left - width) / 2);
    return std::round(l.main.left + kMargin * scale_ - scroll_x_ * Zoom());
}

float Viewer::ContentTop(const Layout& l) const {
    return std::round(l.main.top + kMargin * scale_ - scroll_y_ * Zoom());
}

void Viewer::Clamp() {
    const Layout l = Compute();
    const float z = Zoom();
    const float visible_rows = (l.main.bottom - l.main.top - 2 * kMargin * scale_) / z;
    const float visible_cols = (l.main.right - l.main.left - 2 * kMargin * scale_) / z;
    scroll_y_ = std::clamp(scroll_y_, 0.f, std::max(0.f, static_cast<float>(image_->Height()) - visible_rows));
    scroll_x_ = std::clamp(scroll_x_, 0.f, std::max(0.f, static_cast<float>(image_->Width()) - visible_cols));
}

RECT Viewer::Crop() const {
    const int w = image_->Width();
    int left = trim_ ? trim_left_.load() : 0;
    int right = w - (remove_scrollbar_ ? scrollbar_ + (trim_ ? trim_right_.load() : 0) : 0);
    if (right - left < 16) { left = 0; right = w; }
    return {left, crop_top_, right, crop_bottom_};
}

std::vector<Viewer::Item> Viewer::Items() const {
    const Layout l = Compute();
    const float s = scale_;
    std::vector<Item> items;
    const float bw = 46 * s;
    items.push_back({Minimize, {l.title.right - 3 * bw, 0, l.title.right - 2 * bw, l.title.bottom}});
    items.push_back({MaximizeId, {l.title.right - 2 * bw, 0, l.title.right - bw, l.title.bottom}});
    items.push_back({CloseId, {l.title.right - bw, 0, l.title.right, l.title.bottom}});
    const float top = l.actions.top + 14 * s, bottom = l.actions.bottom - 14 * s;
    items.push_back({FitId, {16 * s, top, 118 * s, bottom}});
    items.push_back({ActualId, {124 * s, top, 206 * s, bottom}});
    float x = l.actions.right - 16 * s;
    const auto right = [&](int id, float width) { items.push_back({id, {x - width * s, top, x, bottom}}); x -= (width + 8) * s; };
    right(SaveId, 112);
    right(CopyId, 84);
    right(PinId, 108);
    if (host_.ocr_available && host_.ocr_available()) right(OcrId, 108);
    right(RecaptureId, 108);
    if (crop_top_ > 0 || crop_bottom_ < image_->Height())
        items.push_back({ResetCrop, {l.side.right - 16 * s - 52 * s, l.side.top + 14 * s, l.side.right - 16 * s, l.side.top + 38 * s}});
    float y = l.minimap.bottom + 12 * s + 30 * s;
    for (int id : {SeamsId, ScrollbarId, TrimId}) {
        items.push_back({id, {l.side.left + 12 * s, y, l.side.right - 12 * s, y + 32 * s}});
        y += 34 * s;
    }
    return items;
}

int Viewer::Hit(Point p) const {
    for (const auto& item : Items()) if (Inside(item.box, p)) return item.id;
    return -1;
}

// --------------------------------------------------------------------------
// Painting
// --------------------------------------------------------------------------
void Viewer::ResetTarget() {
    tiles_.clear();
    overview_bitmap_.Reset();
    target_.Reset();
}

void Viewer::Paint() {
    PAINTSTRUCT ps{};
    BeginPaint(window_, &ps);
    try {
        RECT client{};
        GetClientRect(window_, &client);
        if (!target_) {
            const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
            if (FAILED(painter_.Factory()->CreateHwndRenderTarget(properties,
                    D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom))), &target_)))
                throw std::runtime_error("Viewer render target");
        }
        target_->BeginDraw();
        painter_.Begin(target_.Get(), scale_, dark_);
        target_->Clear(Color(dark_ ? 0xff161b24 : 0xfff3f5f8));
        const Layout l = Compute();
        DrawImage(l);
        DrawSide(l);
        DrawActions(l);
        DrawTitle(l);
        painter_.End();
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET) ResetTarget();
    } catch (...) {
        painter_.End();
        ResetTarget();
    }
    EndPaint(window_, &ps);
}

void Viewer::DrawImage(const Layout& l) {
    const float s = scale_;
    auto* target = painter_.Target();
    painter_.Fill(l.main, dark_ ? 0xff0e131b : 0xffe4e8ef);
    target->PushAxisAlignedClip(Rect(l.main), D2D1_ANTIALIAS_MODE_ALIASED);
    const Frame& image = *image_;
    const float z = Zoom();
    const float x0 = ContentLeft(l), y0 = ContentTop(l);
    const float iw = static_cast<float>(image.Width()) * z;
    const int h = image.Height();
    const int r0 = std::clamp(static_cast<int>(std::floor((l.main.top - y0) / z)), 0, h);
    const int r1 = std::clamp(static_cast<int>(std::ceil((l.main.bottom - y0) / z)), 0, h);
    painter_.Fill({x0 - 1, y0 - 1, x0 + iw + 1, y0 + static_cast<float>(h) * z + 1}, dark_ ? 0x60000000 : 0x30203040);
    const int count = (h + kTile - 1) / kTile;
    if (static_cast<int>(tiles_.size()) != count) tiles_.assign(static_cast<size_t>(count), nullptr);
    const int first = r0 / kTile, last = r1 > r0 ? (r1 - 1) / kTile : first;
    for (int t = 0; t < count; ++t) {
        if (t < first - 2 || t > last + 2) { tiles_[t].Reset(); continue; } // bounded GPU memory
        if (t < first || t > last || r1 <= r0) continue;
        const int rows = std::min(kTile, h - t * kTile);
        if (!tiles_[t]) {
            const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            if (FAILED(target->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(image.Width()), static_cast<UINT32>(rows)),
                    image.pixels.data() + static_cast<size_t>(t) * kTile * image.Width(), static_cast<UINT32>(image.Width() * 4),
                    properties, &tiles_[t])))
                continue;
        }
        const float top = y0 + static_cast<float>(t * kTile) * z;
        target->DrawBitmap(tiles_[t].Get(), D2D1::RectF(x0, top, x0 + iw, top + static_cast<float>(rows) * z), 1,
            z >= .999f ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    // What is left out of the export is dimmed, with the crop edges marked.
    const RECT crop = Crop();
    const uint32_t veil = dark_ ? 0xb00a0e14 : 0xa0dde3eb;
    const float ct = y0 + static_cast<float>(crop.top) * z, cb = y0 + static_cast<float>(crop.bottom) * z;
    const float cl = x0 + static_cast<float>(crop.left) * z, cr = x0 + static_cast<float>(crop.right) * z;
    const float bottom = y0 + static_cast<float>(h) * z;
    if (crop.top > 0) painter_.Fill({x0, y0, x0 + iw, ct}, veil);
    if (crop.bottom < h) painter_.Fill({x0, cb, x0 + iw, bottom}, veil);
    if (crop.left > 0) painter_.Fill({x0, ct, cl, cb}, veil);
    if (crop.right < image.Width()) painter_.Fill({cr, ct, x0 + iw, cb}, veil);
    const auto edge = [&](float y, const wchar_t* label) {
        painter_.Line({x0, y}, {x0 + iw, y}, 0xff2f8cff, 2 * s);
        const float tw = painter_.Measure(label, 11).x + 16 * s;
        const Box chip{x0 + 8 * s, y - 11 * s, x0 + 8 * s + tw, y + 11 * s};
        painter_.Fill(chip, 0xff2f8cff, 11 * s);
        painter_.Text(label, chip, 11, 0xffffffff, true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };
    if (crop.top > 0) edge(ct, L"起点");
    if (crop.bottom < h) edge(cb, L"终点");
    if (show_seams_) {
        int index = 2;
        for (int seam : seams_) {
            const float y = y0 + static_cast<float>(seam) * z;
            if (y >= l.main.top && y <= l.main.bottom) {
                for (float x = x0; x < x0 + iw; x += 10 * s)
                    painter_.Line({x, y}, {std::min(x0 + iw, x + 6 * s), y}, 0xe0f59e0b, 1.5f * s);
                const std::wstring label = L"第 " + std::to_wstring(index) + L" 段";
                const float tw = painter_.Measure(label, 10.5f).x + 12 * s;
                const Box chip{x0 + iw - tw - 6 * s, y + 3 * s, x0 + iw - 6 * s, y + 21 * s};
                painter_.Fill(chip, 0xe0f59e0b, 9 * s);
                painter_.Text(label, chip, 10.5f, 0xff1f1300, true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            ++index;
        }
    }
    // Slim position indicator.
    const float view_h = l.main.bottom - l.main.top;
    const float content_h = static_cast<float>(h) * z + 2 * kMargin * s;
    if (content_h > view_h) {
        const float track = view_h - 16 * s;
        const float thumb = std::max(28 * s, track * view_h / content_h);
        const float pos = (scroll_y_ * z) / (content_h - view_h);
        const float ty = l.main.top + 8 * s + (track - thumb) * std::clamp(pos, 0.f, 1.f);
        painter_.Fill({l.main.right - 8 * s, ty, l.main.right - 4 * s, ty + thumb}, dark_ ? 0x80ffffff : 0x60202838, 2 * s);
    }
    if (!toast_.empty()) {
        const float tw = painter_.Measure(toast_, 12.5f).x + 36 * s;
        const float cx = (l.main.left + l.main.right) / 2;
        const Box pill{cx - tw / 2, l.main.bottom - 58 * s, cx + tw / 2, l.main.bottom - 22 * s};
        painter_.Fill(pill, dark_ ? 0xf0232b3a : 0xf5ffffff, 18 * s);
        painter_.Stroke(pill, painter_.Theme().Border(), 18 * s, s);
        painter_.Text(toast_, pill, 12.5f, painter_.Theme().Ink(), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    target->PopAxisAlignedClip();
}

void Viewer::DrawSide(const Layout& l) {
    const float s = scale_;
    const auto theme = painter_.Theme();
    painter_.Fill(l.side, dark_ ? 0xff1b212c : 0xfffafbfc);
    painter_.Line({l.side.left + .5f, l.side.top}, {l.side.left + .5f, l.side.bottom}, theme.Border(), s);
    painter_.Text(L"导航", {l.side.left + 16 * s, l.side.top + 12 * s, l.side.right - 80 * s, l.side.top + 32 * s}, 13, theme.Ink(), true);
    painter_.Text(L"拖动上下手柄裁剪", {l.side.left + 16 * s, l.side.top + 31 * s, l.side.right - 16 * s, l.side.top + 48 * s}, 11, theme.Muted());
    painter_.Fill(l.minimap, dark_ ? 0xff121822 : 0xffeef1f5, 10 * s);
    const Box strip = l.strip;
    {
        std::shared_ptr<Frame> overview;
        {
            std::lock_guard lock(mutex_);
            overview = overview_;
        }
        if (overview && !overview_bitmap_) {
            const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            painter_.Target()->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(overview->Width()), static_cast<UINT32>(overview->Height())),
                overview->pixels.data(), static_cast<UINT32>(overview->Width() * 4), properties, &overview_bitmap_);
        }
    }
    if (overview_bitmap_) painter_.Target()->DrawBitmap(overview_bitmap_.Get(), Rect(strip), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else painter_.Fill(strip, dark_ ? 0xff26303f : 0xffdfe4eb);
    painter_.Stroke(strip, theme.Border(), 0, s);
    const float k = StripScale(l);
    const float yt = strip.top + static_cast<float>(crop_top_) * k, yb = strip.top + static_cast<float>(crop_bottom_) * k;
    const uint32_t veil = dark_ ? 0xc0121822 : 0xc0eef1f5;
    if (crop_top_ > 0) painter_.Fill({strip.left, strip.top, strip.right, yt}, veil);
    if (crop_bottom_ < image_->Height()) painter_.Fill({strip.left, yb, strip.right, strip.bottom}, veil);
    // Viewport.
    const float z = Zoom();
    const float rows = (l.main.bottom - l.main.top - 2 * kMargin * s) / z;
    const float vy0 = strip.top + scroll_y_ * k, vy1 = std::min(strip.bottom, strip.top + (scroll_y_ + rows) * k);
    const Box view{strip.left - 5 * s, vy0, strip.right + 5 * s, std::max(vy0 + 4 * s, vy1)};
    painter_.Fill(view, 0x263b82f6, 3 * s);
    painter_.Stroke(view, theme.Accent(), 3 * s, 1.5f * s);
    // Crop handles.
    for (const float y : {yt, yb}) {
        painter_.Line({l.minimap.left + 10 * s, y}, {l.minimap.right - 10 * s, y}, 0xff2f8cff, 2 * s);
        const float cx = (l.minimap.left + l.minimap.right) / 2;
        const Box grip{cx - 16 * s, y - 5 * s, cx + 16 * s, y + 5 * s};
        painter_.Fill(grip, 0xff2f8cff, 5 * s);
        for (int i = -1; i <= 1; ++i)
            painter_.Line({cx + static_cast<float>(i) * 5 * s, y - 2.5f * s}, {cx + static_cast<float>(i) * 5 * s, y + 2.5f * s}, 0xffffffff, 1.2f * s);
    }
    // Options.
    const float oy = l.minimap.bottom + 12 * s;
    painter_.Text(L"选项", {l.side.left + 16 * s, oy, l.side.right - 16 * s, oy + 24 * s}, 13, theme.Ink(), true);
    for (const auto& item : Items()) {
        if (item.id == ResetCrop) {
            const bool hot = hover_ == item.id;
            painter_.Fill(item.box, hot ? 0x303b82f6 : 0x1a3b82f6, 7 * s);
            painter_.Text(L"重置", item.box, 11.5f, theme.Accent(), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            continue;
        }
        if (item.id < SeamsId || item.id > TrimId) continue;
        const bool enabled = item.id != ScrollbarId || scrollbar_ > 0;
        const bool on = item.id == SeamsId ? show_seams_ : item.id == ScrollbarId ? remove_scrollbar_ && scrollbar_ > 0 : trim_;
        if (hover_ == item.id && enabled) painter_.Fill(item.box, dark_ ? 0x14ffffff : 0x0c203040, 7 * s);
        const Glyph glyph = item.id == SeamsId ? Glyph::Seams : item.id == ScrollbarId ? Glyph::Scrollbar : Glyph::Trim;
        painter_.Icon(glyph, {item.box.left + 4 * s, item.box.top + 4 * s, item.box.left + 28 * s, item.box.top + 28 * s}, enabled ? theme.Ink() : theme.Muted());
        std::wstring label = item.id == SeamsId ? L"显示拼接接缝" : item.id == TrimId ? L"修剪左右空白"
            : scrollbar_ > 0 ? L"去掉滚动条（" + std::to_wstring(scrollbar_) + L" px）" : L"未检测到滚动条";
        painter_.Text(label, {item.box.left + 34 * s, item.box.top, item.box.right - 48 * s, item.box.bottom}, 12, enabled ? theme.Ink() : theme.Muted(),
            false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        const Box track{item.box.right - 42 * s, item.box.top + 7 * s, item.box.right - 6 * s, item.box.top + 25 * s};
        painter_.Fill(track, on ? theme.Accent() : (dark_ ? 0xff3a4558 : 0xffc9d1dc), 9 * s);
        const float knob = on ? track.right - 9 * s : track.left + 9 * s;
        painter_.Target()->FillEllipse(D2D1::Ellipse({knob, (track.top + track.bottom) / 2}, 6.5f * s, 6.5f * s), painter_.Brush(enabled ? 0xffffffff : 0xffe5e7eb));
    }
    const RECT crop = Crop();
    const float iy = l.side.bottom - 50 * s;
    painter_.Line({l.side.left + 16 * s, iy - 6 * s}, {l.side.right - 16 * s, iy - 6 * s}, theme.Border(), s);
    painter_.Text(L"导出尺寸", {l.side.left + 16 * s, iy, l.side.right - 16 * s, iy + 18 * s}, 11, theme.Muted());
    painter_.Text(Thousands(crop.right - crop.left) + L" × " + Thousands(crop.bottom - crop.top) + L" px",
        {l.side.left + 16 * s, iy + 17 * s, l.side.right - 16 * s, iy + 40 * s}, 14, theme.Ink(), true);
}

void Viewer::DrawActions(const Layout& l) {
    const float s = scale_;
    const auto theme = painter_.Theme();
    painter_.Fill(l.actions, dark_ ? 0xff1b212c : 0xfffafbfc);
    painter_.Line({0, l.actions.top + .5f}, {l.actions.right, l.actions.top + .5f}, theme.Border(), s);
    std::vector<ui::Control> controls;
    const bool busy = busy_.load();
    for (const auto& item : Items()) {
        if (item.id < FitId || item.id > SaveId) continue;
        ui::Control c;
        c.id = item.id;
        c.kind = ui::Kind::Button;
        c.bounds = item.box;
        switch (item.id) {
        case FitId: c.icon = static_cast<int>(Glyph::Fit); c.text = L"适应宽度"; c.selected = fit_; break;
        case ActualId: c.icon = static_cast<int>(Glyph::Actual); c.text = L"100%"; c.selected = !fit_ && std::abs(zoom_ - 1) < .001f; break;
        case RecaptureId: c.icon = static_cast<int>(Glyph::Recapture); c.text = L"重新截取"; break;
        case OcrId: c.icon = static_cast<int>(Glyph::Ocr); c.text = L"识别文字"; c.enabled = !busy; break;
        case PinId: c.icon = static_cast<int>(Glyph::Pin); c.text = L"贴到桌面"; c.enabled = !busy; break;
        case CopyId: c.icon = static_cast<int>(Glyph::Copy); c.text = L"复制"; c.enabled = !busy; break;
        case SaveId: c.icon = static_cast<int>(Glyph::Save); c.text = L"保存 PNG"; c.primary = true; c.enabled = !busy; break;
        default: break;
        }
        controls.push_back(std::move(c));
    }
    auto paint = painter_.Context(false);
    ui::DrawControls(paint, theme, controls, hover_, pressed_);
    const std::wstring zoom = L"缩放 " + std::to_wstring(static_cast<int>(std::lround(Zoom() * 100))) + L"%";
    painter_.Text(zoom, {216 * s, l.actions.top, 330 * s, l.actions.bottom}, 12, theme.Muted(), false,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    painter_.Text(L"Ctrl+滚轮缩放 · 拖动平移", {330 * s, l.actions.top, 520 * s, l.actions.bottom}, 11, theme.Muted(), false,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void Viewer::DrawTitle(const Layout& l) {
    const float s = scale_;
    const auto theme = painter_.Theme();
    painter_.Fill(l.title, dark_ ? 0xff1b212c : 0xfffafbfc);
    painter_.Line({0, l.title.bottom - .5f}, {l.title.right, l.title.bottom - .5f}, theme.Border(), s);
    const Box badge{14 * s, 11 * s, 36 * s, 33 * s};
    painter_.Fill(badge, theme.Accent(), 6 * s);
    painter_.Icon(Glyph::Auto, {badge.left + 1 * s, badge.top + 1 * s, badge.right - 1 * s, badge.bottom - 1 * s}, 0xffffffff);
    painter_.Text(L"长截图", {46 * s, 0, 120 * s, l.title.bottom}, 13, theme.Ink(), true, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    const std::wstring info = Thousands(image_->Width()) + L" × " + Thousands(image_->Height()) + L" px · " +
        Thousands(static_cast<long long>(seams_.size()) + 1) + L" 段" + (header_ > 0 ? L" · 吸顶栏已去重" : L"");
    painter_.Text(info, {104 * s, 0, l.title.right - 160 * s, l.title.bottom}, 11.5f, theme.Muted(), false,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    for (const auto& item : Items()) {
        if (item.id > CloseId) continue;
        const bool hot = hover_ == item.id, down = pressed_ == item.id;
        uint32_t ink = theme.Ink();
        if (item.id == CloseId && (hot || down)) { painter_.Fill(item.box, down ? 0xfff1707a : 0xffe81123); ink = 0xffffffff; }
        else if (hot || down) painter_.Fill(item.box, dark_ ? (down ? 0x30ffffff : 0x18ffffff) : (down ? 0x24000000 : 0x12000000));
        const Glyph glyph = item.id == Minimize ? Glyph::Minimize : item.id == CloseId ? Glyph::Close : IsZoomed(window_) ? Glyph::Restore : Glyph::Maximize;
        const float cx = (item.box.left + item.box.right) / 2, cy = (item.box.top + item.box.bottom) / 2;
        painter_.Icon(glyph, {cx - 14 * s, cy - 14 * s, cx + 14 * s, cy + 14 * s}, ink);
    }
}

// --------------------------------------------------------------------------
// Interaction
// --------------------------------------------------------------------------
void Viewer::ZoomAt(float zoom, Point anchor) {
    const Layout l = Compute();
    const float before = Zoom();
    zoom = std::clamp(zoom, .05f, 4.f);
    const float x0 = ContentLeft(l), y0 = ContentTop(l);
    const float ix = (anchor.x - x0) / before, iy = (anchor.y - y0) / before;
    fit_ = false;
    zoom_ = zoom;
    scroll_y_ = iy - (anchor.y - l.main.top - kMargin * scale_) / zoom;
    scroll_x_ = ix - (anchor.x - l.main.left - kMargin * scale_) / zoom;
    Clamp();
    Invalidate();
}

void Viewer::Wheel(short delta, POINT screen, bool control, bool shift) {
    POINT client = screen;
    ScreenToClient(window_, &client);
    const Point p{static_cast<float>(client.x), static_cast<float>(client.y)};
    const Layout l = Compute();
    const float notches = static_cast<float>(delta) / WHEEL_DELTA;
    if (control) {
        const Point anchor = Inside(l.main, p) ? p : Point{(l.main.left + l.main.right) / 2, (l.main.top + l.main.bottom) / 2};
        ZoomAt(Zoom() * std::pow(1.15f, notches), anchor);
        return;
    }
    const float step = 120 * scale_ / Zoom();
    if (shift) scroll_x_ -= notches * step;
    else scroll_y_ -= notches * step;
    Clamp();
    Invalidate();
}

void Viewer::Toast(std::wstring text) {
    toast_ = std::move(text);
    SetTimer(window_, 1, 2400, nullptr);
    Invalidate();
}

void Viewer::Action(int id) {
    switch (id) {
    case Minimize: ShowWindow(window_, SW_MINIMIZE); break;
    case MaximizeId: ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE); break;
    case CloseId: PostMessageW(window_, WM_CLOSE, 0, 0); break;
    case FitId: fit_ = true; scroll_x_ = 0; Clamp(); break;
    case ActualId: {
        const Layout l = Compute();
        ZoomAt(1, {(l.main.left + l.main.right) / 2, l.main.top + kMargin * scale_});
        break;
    }
    case RecaptureId: {
        auto recapture = host_.recapture;
        DestroyWindow(window_);
        if (recapture) recapture();
        return;
    }
    case OcrId: case PinId: case CopyId: Start(id); break;
    case SaveId: {
        if (busy_) break;
        std::wstring name = DefaultName();
        std::vector<wchar_t> buffer(32768);
        std::copy(name.begin(), name.end(), buffer.begin());
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"PNG 图片 (*.png)\0*.png\0\0";
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.lpstrDefExt = L"png";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        const std::wstring folder = host_.save_directory ? host_.save_directory().wstring() : std::wstring{};
        if (!folder.empty()) dialog.lpstrInitialDir = folder.c_str();
        if (!GetSaveFileNameW(&dialog)) break;
        const std::filesystem::path path = buffer.data();
        if (host_.remember_directory) host_.remember_directory(path.parent_path());
        Start(SaveId, path);
        break;
    }
    case ResetCrop: crop_top_ = 0; crop_bottom_ = image_->Height(); break;
    case SeamsId: show_seams_ = !show_seams_; break;
    case ScrollbarId: if (scrollbar_ > 0) remove_scrollbar_ = !remove_scrollbar_; break;
    case TrimId: trim_ = !trim_; break;
    default: break;
    }
    Invalidate();
}

void Viewer::Start(int kind, std::filesystem::path path) {
    if (busy_.exchange(true)) return;
    Toast(kind == CopyId ? L"正在复制…" : kind == SaveId ? L"正在保存…" : L"正在准备…");
    KillTimer(window_, 1);
    const RECT crop = Crop();
    const int format = host_.clipboard_format ? host_.clipboard_format() : -1;
    if (job_worker_.joinable()) job_worker_.join();
    job_worker_ = std::jthread([this, image = image_, crop, kind, path = std::move(path), format, window = window_] {
        auto result = std::make_unique<JobResult>();
        result->kind = kind;
        result->path = path;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        try {
            Frame frame = Extract(*image, crop);
            if (kind == CopyId) {
                auto file = format >= 0 ? PrepareClipboardFile(frame, format) : nullptr;
                result->clipboard = PrepareClipboardImage(frame, file);
            } else if (kind == SaveId) {
                SavePng(frame, path);
            } else {
                result->frame = std::move(frame);
            }
        } catch (const std::exception& e) {
            const std::string text = e.what();
            result->error.assign(text.begin(), text.end());
            if (result->error.empty()) result->error = L"未知错误";
        }
        if (SUCCEEDED(com)) CoUninitialize();
        {
            std::lock_guard lock(mutex_);
            job_result_ = std::move(result);
        }
        PostMessageW(window, kJobDone, 0, 0);
    });
}

void Viewer::Finished() {
    std::unique_ptr<JobResult> result;
    {
        std::lock_guard lock(mutex_);
        result = std::move(job_result_);
    }
    busy_ = false;
    if (!result) return;
    if (!result->error.empty()) {
        Toast((result->kind == SaveId ? L"保存失败：" : result->kind == CopyId ? L"复制失败：" : L"操作失败：") + result->error);
        return;
    }
    switch (result->kind) {
    case CopyId:
        try {
            PublishClipboardImage(window_, *result->clipboard);
            Toast(L"已复制到剪贴板");
        } catch (const std::exception&) {
            Toast(L"复制失败：剪贴板被其他程序占用，请重试");
        }
        break;
    case SaveId: Toast(L"已保存：" + result->path.filename().wstring()); break;
    case PinId: case OcrId:
        try {
            if (host_.pin) host_.pin(std::move(result->frame), POINT{region_.left, region_.top}, result->kind == OcrId);
            Toast(result->kind == OcrId ? L"已贴到桌面，正在识别文字" : L"已贴到桌面");
        } catch (const std::exception&) {
            Toast(L"无法贴到桌面：图片过大或内存不足");
        }
        break;
    default: break;
    }
}

void Viewer::Key(WPARAM key) {
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const Layout l = Compute();
    const float page = (l.main.bottom - l.main.top) / Zoom() * .9f;
    switch (key) {
    case VK_ESCAPE: PostMessageW(window_, WM_CLOSE, 0, 0); return;
    case VK_HOME: scroll_y_ = 0; break;
    case VK_END: scroll_y_ = static_cast<float>(image_->Height()); break;
    case VK_PRIOR: scroll_y_ -= page; break;
    case VK_NEXT: case VK_SPACE: scroll_y_ += page; break;
    case VK_UP: scroll_y_ -= 60 * scale_ / Zoom(); break;
    case VK_DOWN: scroll_y_ += 60 * scale_ / Zoom(); break;
    case 'C': if (control) Action(CopyId); return;
    case 'S': if (control) Action(SaveId); return;
    case '0': case VK_NUMPAD0: if (control) Action(FitId); return;
    case '1': case VK_NUMPAD1: if (control) Action(ActualId); return;
    case VK_OEM_PLUS: case VK_ADD: case VK_OEM_MINUS: case VK_SUBTRACT:
        if (control) {
            const bool in = key == VK_OEM_PLUS || key == VK_ADD;
            ZoomAt(Zoom() * (in ? 1.2f : 1 / 1.2f), {(l.main.left + l.main.right) / 2, (l.main.top + l.main.bottom) / 2});
        }
        return;
    default: return;
    }
    Clamp();
    Invalidate();
}

LRESULT CALLBACK Viewer::Proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Viewer*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Viewer*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wp, lp);
    try {
        return self->Message(message, wp, lp);
    } catch (const std::exception&) {
        return DefWindowProcW(window, message, wp, lp);
    }
}

LRESULT Viewer::Message(UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (wp) {
            if (IsZoomed(window_)) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                const UINT dpi = GetDpiForWindow(window_);
                const int frame = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                InflateRect(&params->rgrc[0], -frame, -frame);
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(window_, &p);
        RECT client{};
        GetClientRect(window_, &client);
        if (!IsZoomed(window_)) {
            const int border = static_cast<int>(6 * scale_);
            const bool left = p.x < border, right = p.x >= client.right - border;
            const bool top = p.y < border, bottom = p.y >= client.bottom - border;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        const Point point{static_cast<float>(p.x), static_cast<float>(p.y)};
        if (point.y < kTitle * scale_ && Hit(point) < 0) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize = {static_cast<LONG>(760 * scale_), static_cast<LONG>(520 * scale_)};
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(); return 0;
    case WM_SIZE:
        if (target_) {
            RECT client{};
            GetClientRect(window_, &client);
            if (FAILED(target_->Resize(D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom))))) ResetTarget();
        }
        Clamp();
        Invalidate();
        return 0;
    case WM_DPICHANGED: {
        scale_ = static_cast<float>(HIWORD(wp)) / 96.f;
        const auto* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(window_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Clamp();
        Invalidate();
        return 0;
    }
    case WM_SETTINGCHANGE: case WM_THEMECHANGED:
        dark_ = host_.dark && host_.dark();
        Invalidate();
        break;
    case WM_TIMER:
        if (wp == 1) { KillTimer(window_, 1); toast_.clear(); Invalidate(); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(window_, &p);
            const Layout l = Compute();
            const float k = StripScale(l);
            const float y = static_cast<float>(p.y);
            const bool handle = Inside(l.minimap, {static_cast<float>(p.x), y}) &&
                (std::abs(y - (l.strip.top + static_cast<float>(crop_top_) * k)) <= 7 * scale_ ||
                 std::abs(y - (l.strip.top + static_cast<float>(crop_bottom_) * k)) <= 7 * scale_);
            const bool pan = Inside(l.main, {static_cast<float>(p.x), y});
            SetCursor(LoadCursorW(nullptr, handle || drag_ >= 3 ? IDC_SIZENS : pan || drag_ == 1 ? IDC_SIZEALL : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        const Point p{static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))};
        SetFocus(window_);
        const int id = Hit(p);
        if (id >= 0) {
            pressed_ = id;
            SetCapture(window_);
            Invalidate();
            return 0;
        }
        const Layout l = Compute();
        if (Inside(l.minimap, p)) {
            const float k = StripScale(l);
            const float yt = l.strip.top + static_cast<float>(crop_top_) * k, yb = l.strip.top + static_cast<float>(crop_bottom_) * k;
            if (std::abs(p.y - yt) <= 7 * scale_) drag_ = 3;
            else if (std::abs(p.y - yb) <= 7 * scale_) drag_ = 4;
            else drag_ = 2;
        } else if (Inside(l.main, p)) {
            drag_ = 1;
        }
        if (drag_) {
            drag_start_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            drag_x_ = scroll_x_;
            drag_y_ = scroll_y_;
            SetCapture(window_);
            SendMessageW(window_, WM_MOUSEMOVE, wp, lp);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const Point p{static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))};
        if (drag_) {
            const Layout l = Compute();
            const float k = StripScale(l);
            const int row = std::clamp(static_cast<int>(std::lround((p.y - l.strip.top) / k)), 0, image_->Height());
            const int minimum = std::min(image_->Height(), 16);
            if (drag_ == 1) {
                scroll_x_ = drag_x_ - static_cast<float>(GET_X_LPARAM(lp) - drag_start_.x) / Zoom();
                scroll_y_ = drag_y_ - static_cast<float>(GET_Y_LPARAM(lp) - drag_start_.y) / Zoom();
            } else if (drag_ == 2) {
                const float rows = (l.main.bottom - l.main.top - 2 * kMargin * scale_) / Zoom();
                scroll_y_ = static_cast<float>(row) - rows / 2;
            } else if (drag_ == 3) {
                crop_top_ = std::min(row, crop_bottom_ - minimum);
            } else {
                crop_bottom_ = std::max(row, crop_top_ + minimum);
            }
            Clamp();
            Invalidate();
            return 0;
        }
        const int hit = Hit(p);
        if (hit != hover_) {
            hover_ = hit;
            Invalidate();
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
            TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        const Point p{static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))};
        const int pressed = std::exchange(pressed_, -1);
        drag_ = 0;
        if (GetCapture() == window_) ReleaseCapture();
        if (pressed >= 0 && Hit(p) == pressed) Action(pressed);
        Invalidate();
        return 0;
    }
    case WM_CAPTURECHANGED: drag_ = 0; pressed_ = -1; Invalidate(); return 0;
    case WM_MOUSELEAVE: hover_ = -1; Invalidate(); return 0;
    case WM_MOUSEWHEEL:
        Wheel(GET_WHEEL_DELTA_WPARAM(wp), {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0,
            (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0);
        return 0;
    case WM_MOUSEHWHEEL:
        Wheel(static_cast<short>(-GET_WHEEL_DELTA_WPARAM(wp)), {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, false, true);
        return 0;
    case WM_KEYDOWN: Key(wp); return 0;
    case kOverviewReady: overview_bitmap_.Reset(); Invalidate(); return 0;
    case kJobDone: Finished(); Invalidate(); return 0;
    case WM_CLOSE: DestroyWindow(window_); return 0;
    case WM_DESTROY:
        KillTimer(window_, 1);
        ResetTarget();
        if (closed_) closed_(this);
        return 0;
    default: break;
    }
    return DefWindowProcW(window_, message, wp, lp);
}
}
