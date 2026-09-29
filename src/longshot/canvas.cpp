#include "longshot/canvas.h"
#include "ui/glass_surface.h"
#include "ui/memory_target.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace lumashot::longshot {
namespace {
void Check(HRESULT result, const char* text) {
    if (FAILED(result)) throw std::runtime_error(text);
}
}

Painter::Painter() {
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()), "Long capture drawing");
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(write_.GetAddressOf())), "Long capture fonts");
}

void Painter::Begin(ID2D1RenderTarget* target, float scale, bool dark) {
    target_ = target;
    scale_ = scale;
    dark_ = dark;
    brush_.Reset();
    Check(target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_), "Long capture brush");
}

ID2D1SolidColorBrush* Painter::Brush(uint32_t argb) {
    brush_->SetColor(Color(argb));
    return brush_.Get();
}

IDWriteTextFormat* Painter::Format(float px, bool bold, DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT vertical, bool wrap) {
    const auto key = std::make_tuple(static_cast<int>(std::lround(px * 4)), bold, static_cast<int>(align), static_cast<int>(vertical), wrap);
    auto& format = formats_[key];
    if (!format) {
        Check(write_->CreateTextFormat(ui::ToolFont, nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, L"zh-CN", &format), "Long capture text");
        format->SetTextAlignment(align);
        format->SetParagraphAlignment(vertical);
        format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!wrap) {
            ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(write_->CreateEllipsisTrimmingSign(format.Get(), &ellipsis))) {
                DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                format->SetTrimming(&trimming, ellipsis.Get());
            }
        }
    }
    return format.Get();
}

void Painter::Text(std::wstring_view text, Box bounds, float size, uint32_t color, bool bold,
    DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT vertical, bool wrap) {
    if (text.empty() || bounds.right <= bounds.left) return;
    text_.Draw(target_, write_.Get(), text, Format(size * scale_, bold, align, vertical, wrap), Rect(bounds), Color(color));
}

Point Painter::Measure(std::wstring_view text, float size, bool bold) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(write_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
            Format(size * scale_, bold, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false), 1e6f, 1e6f, &layout)))
        return {};
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

void Painter::Fill(Box b, uint32_t color, float radius) {
    if (radius > 0) target_->FillRoundedRectangle(D2D1::RoundedRect(Rect(b), radius, radius), Brush(color));
    else target_->FillRectangle(Rect(b), Brush(color));
}

void Painter::Stroke(Box b, uint32_t color, float radius, float width) {
    if (radius > 0) target_->DrawRoundedRectangle(D2D1::RoundedRect(Rect(b), radius, radius), Brush(color), width);
    else target_->DrawRectangle(Rect(b), Brush(color), width);
}

void Painter::Line(Point a, Point b, uint32_t color, float width) {
    target_->DrawLine({a.x, a.y}, {b.x, b.y}, Brush(color), width);
}

void Painter::Panel(Box b, bool acrylic, float radius_dip) {
    ui::DrawGlassSurface(target_, brush_.Get(), Rect(b), dark_, acrylic, radius_dip, scale_);
}

void Painter::Backdrop(Box b, bool acrylic) {
    target_->FillRectangle(Rect(b), Brush(acrylic ? 0x01000000u : (0xff000000u | PanelBackground(dark_))));
}

ui::PaintContext Painter::Context(bool acrylic) {
    ui::PaintContext paint;
    paint.target = target_;
    paint.brush = [this](uint32_t color) { return Brush(color); };
    // DrawControls passes pixel sizes (already scaled); convert back to DIP.
    paint.text = [this](const std::wstring& text, Box b, float size, uint32_t color) { Text(text, b, size / scale_, color); };
    paint.measure_text = [this](const std::wstring& text, float size) { return Measure(text, size / scale_); };
    paint.icon = [this](int id, Box b, uint32_t color) { Icon(static_cast<longshot::Glyph>(id), b, color); };
    paint.surface = [this, acrylic](Box b, float radius) { ui::DrawGlassSurface(target_, brush_.Get(), Rect(b), dark_, acrylic, radius / scale_, scale_); };
    return paint;
}

void Painter::Icon(longshot::Glyph glyph, Box rect, uint32_t color) {
    // 32-unit grid like the toolbar icons, centred in the box.
    const float size = std::min(rect.right - rect.left, rect.bottom - rect.top);
    const float unit = size / 32;
    const float cx = (rect.left + rect.right) / 2, cy = (rect.top + rect.bottom) / 2;
    const float stroke = 1.65f * unit;
    auto* brush = Brush(color);
    const auto line = [&](float x1, float y1, float x2, float y2) {
        target_->DrawLine({cx + x1 * unit, cy + y1 * unit}, {cx + x2 * unit, cy + y2 * unit}, brush, stroke);
    };
    const auto box = [&](float l, float t, float r, float b, float radius) {
        target_->DrawRoundedRectangle(D2D1::RoundedRect({cx + l * unit, cy + t * unit, cx + r * unit, cy + b * unit}, radius * unit, radius * unit), brush, stroke);
    };
    switch (glyph) {
    case Glyph::Pause:
        target_->FillRoundedRectangle(D2D1::RoundedRect({cx - 6 * unit, cy - 7 * unit, cx - 2 * unit, cy + 7 * unit}, unit, unit), brush);
        target_->FillRoundedRectangle(D2D1::RoundedRect({cx + 2 * unit, cy - 7 * unit, cx + 6 * unit, cy + 7 * unit}, unit, unit), brush);
        break;
    case Glyph::Play: {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) break;
        sink->BeginFigure({cx - 5 * unit, cy - 8 * unit}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine({cx + 8 * unit, cy});
        sink->AddLine({cx - 5 * unit, cy + 8 * unit});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        target_->FillGeometry(path.Get(), brush);
        break;
    }
    case Glyph::Undo: {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) break;
        sink->BeginFigure({cx - 7 * unit, cy - 3 * unit}, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine({cx + 1 * unit, cy - 3 * unit});
        sink->AddBezier(D2D1::BezierSegment({cx + 10 * unit, cy - 3 * unit}, {cx + 10 * unit, cy + 8 * unit}, {cx + 1 * unit, cy + 8 * unit}));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        target_->DrawGeometry(path.Get(), brush, stroke);
        line(-7, -3, -2, -8);
        line(-7, -3, -2, 2);
        break;
    }
    case Glyph::Close: line(-6, -6, 6, 6); line(6, -6, -6, 6); break;
    case Glyph::Check: line(-7, 0, -1, 6); line(-1, 6, 8, -6); break;
    case Glyph::Auto:
        // Page with a downward arrow: the long-capture mark.
        line(-7, -9, 7, -9); line(-7, -9, -7, 1); line(7, -9, 7, 1);
        line(-7, 4, -7, 6); line(7, 4, 7, 6);
        line(0, -5, 0, 9); line(-4, 5, 0, 9); line(4, 5, 0, 9);
        break;
    case Glyph::Manual:
        box(-5, -9, 5, 7, 5);
        line(0, -9, 0, -3);
        break;
    case Glyph::Speed:
        target_->DrawEllipse(D2D1::Ellipse({cx, cy + 2 * unit}, 8 * unit, 8 * unit), brush, stroke);
        line(0, 2, 5, -3);
        break;
    case Glyph::Recapture: {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) break;
        sink->BeginFigure({cx + 7 * unit, cy - 2 * unit}, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment({cx - 7 * unit, cy + 1 * unit}, D2D1::SizeF(7.5f * unit, 7.5f * unit), 0,
            D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        target_->DrawGeometry(path.Get(), brush, stroke);
        line(7, -2, 8, -8); line(7, -2, 1, -3);
        break;
    }
    case Glyph::Ocr:
        line(-9, -5, -9, -9); line(-9, -9, -5, -9); line(5, -9, 9, -9); line(9, -9, 9, -5);
        line(-9, 5, -9, 9); line(-9, 9, -5, 9); line(5, 9, 9, 9); line(9, 9, 9, 5);
        line(-4, -3, 4, -3); line(0, -3, 0, 5);
        break;
    case Glyph::Pin:
        line(-5, -7, 5, -7); line(-3, -7, -3, 0); line(3, -7, 3, 0); line(-6, 2, 6, 2);
        line(-3, 0, -6, 2); line(3, 0, 6, 2); line(0, 2, 0, 9);
        break;
    case Glyph::Copy:
        box(-7, -4, 3, 8, 1);
        line(-3, -8, 7, -8); line(7, -8, 7, 4);
        break;
    case Glyph::Save:
        target_->DrawRectangle({cx - 7 * unit, cy - 8 * unit, cx + 7 * unit, cy + 8 * unit}, brush, stroke);
        line(-3, -8, -3, -2); line(-3, -2, 4, -2); line(4, -2, 4, -8);
        line(-3, 8, -3, 3); line(-3, 3, 4, 3); line(4, 3, 4, 8);
        break;
    case Glyph::Minimize: line(-6, 0, 6, 0); break;
    case Glyph::Maximize: box(-6, -6, 6, 6, 1); break;
    case Glyph::Restore: box(-6, -3, 3, 6, 1); line(-3, -6, 6, -6); line(6, -6, 6, 3); break;
    case Glyph::Warning: {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) break;
        sink->BeginFigure({cx, cy - 9 * unit}, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine({cx + 9.5f * unit, cy + 8 * unit});
        sink->AddLine({cx - 9.5f * unit, cy + 8 * unit});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        target_->DrawGeometry(path.Get(), brush, stroke);
        line(0, -3, 0, 2);
        target_->FillEllipse(D2D1::Ellipse({cx, cy + 5 * unit}, 1.2f * unit, 1.2f * unit), brush);
        break;
    }
    case Glyph::Info:
        target_->DrawEllipse(D2D1::Ellipse({cx, cy}, 9 * unit, 9 * unit), brush, stroke);
        line(0, -1, 0, 5);
        target_->FillEllipse(D2D1::Ellipse({cx, cy - 4.5f * unit}, 1.2f * unit, 1.2f * unit), brush);
        break;
    case Glyph::Seams:
        box(-8, -9, 8, 9, 2);
        for (float x = -8; x < 8; x += 4) line(x, 0, x + 2, 0);
        break;
    case Glyph::Scrollbar:
        box(-8, -9, 8, 9, 2);
        line(4, -9, 4, 9);
        target_->FillRectangle({cx + 5 * unit, cy - 6 * unit, cx + 7 * unit, cy - 1 * unit}, brush);
        break;
    case Glyph::Trim:
        line(-9, -8, -9, 8); line(9, -8, 9, 8);
        line(-5, 0, 5, 0); line(-5, 0, -2, -3); line(-5, 0, -2, 3); line(5, 0, 2, -3); line(5, 0, 2, 3);
        break;
    case Glyph::Fit:
        line(-9, -4, -9, -9); line(-9, -9, -4, -9); line(4, -9, 9, -9); line(9, -9, 9, -4);
        line(-9, 4, -9, 9); line(-9, 9, -4, 9); line(4, 9, 9, 9); line(9, 9, 9, 4);
        break;
    case Glyph::Actual:
        box(-9, -7, 9, 7, 2);
        line(-4, -3, -4, 3); line(4, -3, 4, 3);
        break;
    case Glyph::Annotate:
        // Pencil over a short stroke.
        line(-7, 7, -8, 9); line(-8, 9, -6, 8);
        line(-7, 7, 5, -5); line(-5, 9, 7, -3); line(5, -5, 7, -3);
        line(5, -5, 7, -7); line(7, -7, 9, -5); line(9, -5, 7, -3);
        line(1, 9, 9, 9);
        break;
    case Glyph::Redo: {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) break;
        sink->BeginFigure({cx + 7 * unit, cy - 3 * unit}, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine({cx - 1 * unit, cy - 3 * unit});
        sink->AddBezier(D2D1::BezierSegment({cx - 10 * unit, cy - 3 * unit}, {cx - 10 * unit, cy + 8 * unit}, {cx - 1 * unit, cy + 8 * unit}));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        target_->DrawGeometry(path.Get(), brush, stroke);
        line(7, -3, 2, -8);
        line(7, -3, 2, 2);
        break;
    }
    case Glyph::Trash:
        line(-8, -6, 8, -6);
        line(-3, -6, -2, -9); line(-2, -9, 2, -9); line(2, -9, 3, -6);
        line(-6, -6, -5, 9); line(-5, 9, 5, 9); line(5, 9, 6, -6);
        line(-2, -2, -2, 5); line(2, -2, 2, 5);
        break;
    }
}

void LayeredCanvas::Paint(HWND window, Painter& painter, RECT screen, float scale, bool dark, const std::function<void()>& draw) {
    const SIZE size{screen.right - screen.left, screen.bottom - screen.top};
    if (size.cx <= 0 || size.cy <= 0) return;
    // D2D draws in place into the layered DIB. Painter::Begin makes its brush
    // per frame, so a resized DIB simply gets a new target.
    if (!surface_ || size.cx != size_.cx || size.cy != size_.cy) {
        target_.Reset();
        surface_ = std::make_unique<DibSurface>(size.cx, size.cy);
        size_ = size;
    }
    if (!target_) target_ = CreateMemoryRenderTarget(painter.Factory(), surface_->Pixels(), size.cx, size.cy, size.cx);
    target_->SetDpi(96, 96);
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0));
    painter.Begin(target_.Get(), scale, dark);
    draw();
    painter.End();
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) target_.Reset();
    POINT origin{}, position{screen.left, screen.top};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, nullptr, &position, const_cast<SIZE*>(&size), surface_->Dc(), &origin, 0, &blend, ULW_ALPHA);
}

Frame Downscale(const Frame& source, int max_width, int max_height) {
    const int w = source.Width(), h = source.Height();
    if (w <= 0 || h <= 0) return {};
    const double f = std::min({1.0, double(max_width) / w, double(max_height) / h});
    const int dw = std::max(1, static_cast<int>(std::lround(w * f)));
    const int dh = std::max(1, static_cast<int>(std::lround(h * f)));
    Frame result = MakeFrame({0, 0, dw, dh});
    std::vector<int> column(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) column[x] = std::min(dw - 1, static_cast<int>(int64_t(x) * dw / w));
    std::vector<uint32_t> sum(static_cast<size_t>(dw) * 4), count(static_cast<size_t>(dw));
    int source_row = 0;
    for (int y = 0; y < dh; ++y) {
        const int end = std::max(source_row + 1, static_cast<int>(int64_t(y + 1) * h / dh));
        std::fill(sum.begin(), sum.end(), 0u);
        std::fill(count.begin(), count.end(), 0u);
        for (; source_row < end && source_row < h; ++source_row) {
            const uint32_t* row = source.pixels.data() + static_cast<size_t>(source_row) * w;
            for (int x = 0; x < w; ++x) {
                const uint32_t p = row[x];
                uint32_t* s = &sum[static_cast<size_t>(column[x]) * 4];
                s[0] += p & 255; s[1] += (p >> 8) & 255; s[2] += (p >> 16) & 255;
                ++count[column[x]];
            }
        }
        uint32_t* out = result.pixels.data() + static_cast<size_t>(y) * dw;
        for (int x = 0; x < dw; ++x) {
            const uint32_t n = std::max(1u, count[x]);
            const uint32_t* s = &sum[static_cast<size_t>(x) * 4];
            out[x] = 0xff000000u | ((s[2] / n) << 16) | ((s[1] / n) << 8) | (s[0] / n);
        }
    }
    return result;
}

std::wstring Thousands(long long value) {
    std::wstring digits = std::to_wstring(value < 0 ? -value : value), result;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) result += L',';
        result += digits[i];
    }
    return value < 0 ? L"-" + result : result;
}
}
