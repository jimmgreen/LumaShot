#include "pin/translation.h"
#include "ui/memory_target.h"
#include "ui/text_renderer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace lumashot::pin_translation {
using Microsoft::WRL::ComPtr;
namespace {
int Channel(uint32_t c, int shift) { return int((c >> shift) & 255); }
double Linear(int v) { const double s = v / 255.0; return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); }
double Luminance(uint32_t c) { return 0.2126 * Linear(Channel(c, 16)) + 0.7152 * Linear(Channel(c, 8)) + 0.0722 * Linear(Channel(c, 0)); }
int Distance(uint32_t a, uint32_t b) { return std::abs(Channel(a, 16) - Channel(b, 16)) + std::abs(Channel(a, 8) - Channel(b, 8)) + std::abs(Channel(a, 0) - Channel(b, 0)); }
D2D1_COLOR_F Color(uint32_t c) { return D2D1::ColorF(Channel(c, 16) / 255.f, Channel(c, 8) / 255.f, Channel(c, 0) / 255.f, 1.f); }

struct FontChoice { const wchar_t* family; const wchar_t* locale; };
FontChoice FontFor(translate::Language language) {
    using translate::Language;
    switch (language) {
    case Language::ChineseSimplified: return {L"Microsoft YaHei UI", L"zh-CN"};
    case Language::ChineseTraditional: return {L"Microsoft JhengHei UI", L"zh-TW"};
    case Language::Japanese: return {L"Yu Gothic UI", L"ja-JP"};
    case Language::Korean: return {L"Malgun Gothic", L"ko-KR"};
    default: return {L"Segoe UI", L"en-US"};
    }
}
}

double Contrast(uint32_t a, uint32_t b) {
    const double la = Luminance(a), lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Palette SamplePalette(const Frame& image, Box box) {
    Palette palette;
    const int w = image.Width(), h = image.Height();
    if (w <= 0 || h <= 0 || image.pixels.empty()) return palette;
    const int left = std::clamp(int(std::floor(box.left)) - 2, 0, w - 1), right = std::clamp(int(std::ceil(box.right)) + 1, 0, w - 1);
    const int top = std::clamp(int(std::floor(box.top)) - 2, 0, h - 1), bottom = std::clamp(int(std::ceil(box.bottom)) + 1, 0, h - 1);
    std::array<std::vector<int>, 3> ring;
    const auto add = [&](int x, int y) {
        const auto c = image.pixels[size_t(y) * w + x];
        ring[0].push_back(Channel(c, 16)); ring[1].push_back(Channel(c, 8)); ring[2].push_back(Channel(c, 0));
    };
    const int step_x = std::max(1, (right - left) / 96), step_y = std::max(1, (bottom - top) / 48);
    for (int x = left; x <= right; x += step_x) { add(x, top); add(x, bottom); }
    for (int y = top; y <= bottom; y += step_y) { add(left, y); add(right, y); }
    const auto median = [](std::vector<int>& v) { std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
    palette.background = 0xff000000u | (uint32_t(median(ring[0])) << 16) | (uint32_t(median(ring[1])) << 8) | uint32_t(median(ring[2]));

    // Ink: average of interior pixels that differ most from the background.
    int best = 0;
    const int inner_left = std::clamp(int(box.left), 0, w - 1), inner_right = std::clamp(int(box.right), 0, w - 1);
    const int inner_top = std::clamp(int(box.top), 0, h - 1), inner_bottom = std::clamp(int(box.bottom), 0, h - 1);
    const int sx = std::max(1, (inner_right - inner_left) / 160), sy = std::max(1, (inner_bottom - inner_top) / 40);
    for (int y = inner_top; y <= inner_bottom; y += sy)
        for (int x = inner_left; x <= inner_right; x += sx) best = std::max(best, Distance(image.pixels[size_t(y) * w + x], palette.background));
    const bool dark_background = Luminance(palette.background) < 0.4;
    uint32_t fallback = dark_background ? 0xffffffffu : 0xff111111u;
    if (best >= 90) {
        long long r = 0, g = 0, b = 0, n = 0;
        for (int y = inner_top; y <= inner_bottom; y += sy)
            for (int x = inner_left; x <= inner_right; x += sx) {
                const auto c = image.pixels[size_t(y) * w + x];
                if (Distance(c, palette.background) * 10 >= best * 7) { r += Channel(c, 16); g += Channel(c, 8); b += Channel(c, 0); ++n; }
            }
        if (n) palette.ink = 0xff000000u | (uint32_t(r / n) << 16) | (uint32_t(g / n) << 8) | uint32_t(b / n);
        else palette.ink = fallback;
    } else palette.ink = fallback;
    if (Contrast(palette.ink, palette.background) < 3.0) palette.ink = Contrast(0xffffffffu, palette.background) >= Contrast(0xff111111u, palette.background) ? 0xffffffffu : 0xff111111u;
    return palette;
}

std::wstring JoinTranslation(const std::vector<std::wstring>& sources, const std::vector<std::wstring>& results, bool bilingual) {
    std::wstring out;
    for (size_t i = 0; i < results.size() && i < sources.size(); ++i) {
        if (!out.empty()) out += bilingual ? L"\r\n\r\n" : L"\r\n";
        if (bilingual) { out += sources[i]; out += L"\r\n"; }
        out += results[i];
    }
    return out;
}
Frame Render(const Frame& image, const std::vector<translate::Block>& blocks, const std::vector<std::wstring>& texts, translate::Language target) {
    Frame out = image;
    const int w = out.Width(), h = out.Height();
    if (w <= 0 || h <= 0 || out.pixels.size() != size_t(w) * h) return out;
    for (auto& p : out.pixels) p |= 0xff000000u;
    ComPtr<ID2D1Factory> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) throw std::runtime_error("Create translation renderer");
    auto rt = CreateMemoryRenderTarget(factory.Get(), out.pixels.data(), w, h, w);
    if (!rt) throw std::runtime_error("Create translation target");
    ComPtr<IDWriteFactory> dw;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf())))) throw std::runtime_error("Create translation text");
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0), brush.GetAddressOf());
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    const auto font = FontFor(target);
    TextRenderer renderer;
    // Sample every palette from the untouched image before any block is covered.
    std::vector<Palette> palettes;
    palettes.reserve(blocks.size());
    for (const auto& block : blocks) palettes.push_back(SamplePalette(image, block.box));
    rt->BeginDraw();
    for (size_t i = 0; i < blocks.size() && i < texts.size(); ++i) {
        const auto& block = blocks[i];
        if (texts[i].empty()) continue;
        const float lh = std::max(6.f, block.line_height);
        const float pad = std::max(2.f, lh * 0.12f);
        D2D1_RECT_F rect{std::max(0.f, block.box.left - pad), std::max(0.f, block.box.top - pad), std::min(float(w), block.box.right + pad), std::min(float(h), block.box.bottom + pad)};
        const float width = std::max(8.f, rect.right - rect.left);
        float size = std::max(8.f, lh * 0.8f);
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        for (;;) {
            ComPtr<IDWriteTextFormat> format;
            if (FAILED(dw->CreateTextFormat(font.family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, font.locale, format.GetAddressOf()))) break;
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            layout.Reset();
            if (FAILED(dw->CreateTextLayout(texts[i].data(), UINT32(texts[i].size()), format.Get(), width, 100000.f, layout.GetAddressOf()))) break;
            layout->GetMetrics(&metrics);
            if (metrics.height <= rect.bottom - rect.top + 0.5f || size <= 8.f) break;
            size = std::max(8.f, size * 0.9f);
        }
        if (!layout) continue;
        // Text that cannot fit even at the minimum size extends the patch downward.
        if (metrics.height > rect.bottom - rect.top) rect.bottom = std::min(float(h), rect.top + metrics.height + pad);
        const auto& palette = palettes[i];
        brush->SetColor(Color(palette.background));
        const float radius = std::min(4.f, lh * 0.2f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
        const float slack = std::max(0.f, (rect.bottom - rect.top) - metrics.height);
        const D2D1_POINT_2F origin{rect.left, rect.top + slack / 2};
        renderer.DrawLayout(rt.Get(), dw.Get(), layout.Get(), origin, rect, Color(palette.ink), true);
    }
    if (FAILED(rt->EndDraw())) throw std::runtime_error("Render translation");
    rt.Reset();
    for (auto& p : out.pixels) p |= 0xff000000u;
    return out;
}
}
