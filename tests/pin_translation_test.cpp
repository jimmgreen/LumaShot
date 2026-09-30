#include "pin/translation.h"
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <iostream>

using namespace lumashot;

namespace {
int failures = 0;
void Expect(bool value, const char* message) { std::cout << (value ? "PASS " : "FAIL ") << message << '\n'; failures += !value; }
Frame Solid(int w, int h, uint32_t color) { Frame f; f.bounds = {0, 0, w, h}; f.pixels.assign(size_t(w) * h, color); return f; }
void Fill(Frame& f, int l, int t, int r, int b, uint32_t color) { for (int y = t; y < b; ++y) for (int x = l; x < r; ++x) f.pixels[size_t(y) * f.Width() + x] = color; }
int Channel(uint32_t c, int shift) { return int((c >> shift) & 255); }
bool Near(uint32_t a, uint32_t b, int tolerance) { for (int s : {0, 8, 16}) if (std::abs(Channel(a, s) - Channel(b, s)) > tolerance) return false; return true; }
translate::Block MakeBlock(float l, float t, float r, float b, float line) { translate::Block block; block.box = {l, t, r, b}; block.line_height = line; block.text = L"source"; return block; }
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // Contrast follows the WCAG relative-luminance ratio.
    Expect(std::abs(pin_translation::Contrast(0xffffffff, 0xff000000) - 21.0) < 0.05, "black on white contrast is 21:1");
    Expect(std::abs(pin_translation::Contrast(0xff3080c0, 0xff3080c0) - 1.0) < 1e-9, "identical colours have contrast 1");
    Expect(std::abs(pin_translation::Contrast(0xff102030, 0xffe0f0ff) - pin_translation::Contrast(0xffe0f0ff, 0xff102030)) < 1e-9, "contrast is symmetric");

    // Palette: white text on a blue banner keeps the banner colour and picks the text colour.
    {
        auto image = Solid(200, 80, 0xff2050a0);
        for (int x = 40; x < 160; x += 6) Fill(image, x, 30, x + 3, 50, 0xfff4f4f4);
        const auto palette = pin_translation::SamplePalette(image, {36, 26, 164, 54});
        Expect(Near(palette.background, 0xff2050a0, 2), "background is the ring median around the block");
        Expect(Near(palette.ink, 0xfff4f4f4, 24), "ink is the text colour inside the block");
        Expect((palette.background >> 24) == 0xff && (palette.ink >> 24) == 0xff, "palette colours are opaque");
    }
    // Low contrast interior (anti-aliased smear) falls back to black or white.
    {
        auto image = Solid(120, 60, 0xffd8d8d8);
        Fill(image, 30, 20, 90, 40, 0xffcfcfcf);
        const auto palette = pin_translation::SamplePalette(image, {28, 18, 92, 42});
        Expect(palette.ink == 0xff111111 || palette.ink == 0xffffffff, "low-contrast ink falls back to near-black/white");
        Expect(pin_translation::Contrast(palette.ink, palette.background) >= 3.0, "fallback ink stays readable");
    }
    // Dark UI: light-grey text on near-black keeps a light ink.
    {
        auto image = Solid(160, 60, 0xff15171a);
        for (int x = 20; x < 140; x += 5) Fill(image, x, 22, x + 2, 38, 0xffc8ccd2);
        const auto palette = pin_translation::SamplePalette(image, {18, 20, 142, 40});
        Expect(pin_translation::Contrast(palette.ink, palette.background) >= 3.0 && Channel(palette.ink, 0) > 128, "dark theme text keeps a light ink");
    }

    // Render: covers only the block (plus padding), keeps size, forces opacity.
    {
        auto image = Solid(320, 160, 0xfff0f0f0);
        for (int x = 40; x < 200; x += 7) Fill(image, x, 40, x + 3, 60, 0xff202020);
        image.pixels[0] = 0x00f0f0f0; // transparent corner (e.g. captured layered window)
        const std::vector<translate::Block> blocks{MakeBlock(38, 38, 202, 62, 22)};
        const auto out = pin_translation::Render(image, blocks, {L"你好，世界"}, translate::Language::ChineseSimplified);
        Expect(out.Width() == image.Width() && out.Height() == image.Height(), "rendered frame keeps the original size");
        bool opaque = true;
        for (auto p : out.pixels) opaque &= (p >> 24) == 0xff;
        Expect(opaque, "rendered frame is fully opaque");
        bool outside_same = true;
        for (int y = 0; y < 160; ++y) for (int x = 0; x < 320; ++x) {
            if (x >= 30 && x < 210 && y >= 30 && y < 100) continue;
            if (x == 0 && y == 0) continue;
            outside_same &= out.pixels[size_t(y) * 320 + x] == image.pixels[size_t(y) * 320 + x];
        }
        Expect(outside_same, "pixels away from the block are untouched");
        int dark = 0, stripes = 0;
        for (int y = 38; y < 62; ++y) for (int x = 38; x < 202; ++x) {
            const auto p = out.pixels[size_t(y) * 320 + x];
            if (Channel(p, 0) < 110) ++dark;
        }
        for (int x = 40; x < 200; x += 7) stripes += Channel(out.pixels[size_t(50) * 320 + x + 1], 0) < 60 && Channel(out.pixels[size_t(50) * 320 + x + 5], 0) > 200;
        Expect(dark > 40, "translation glyphs are drawn in the sampled ink");
        Expect(stripes < 10, "original text is covered by the sampled background");
    }
    // Long translation in a tiny block extends downward instead of spilling out sideways.
    {
        auto image = Solid(240, 200, 0xffffffff);
        Fill(image, 20, 20, 120, 34, 0xff000000);
        const std::vector<translate::Block> blocks{MakeBlock(20, 20, 120, 34, 14)};
        const auto out = pin_translation::Render(image, blocks, {L"This is a considerably longer translated sentence that needs several lines"}, translate::Language::English);
        bool right_clean = true;
        for (int y = 0; y < 200; ++y) for (int x = 130; x < 240; ++x) right_clean &= out.pixels[size_t(y) * 240 + x] == 0xffffffff;
        int below = 0;
        for (int y = 40; y < 120; ++y) for (int x = 16; x < 124; ++x) below += Channel(out.pixels[size_t(y) * 240 + x], 0) < 128;
        Expect(right_clean, "overflowing text stays within the block width");
        Expect(below > 0, "overflowing text extends the patch downward");
    }
    // Empty / mismatched inputs are harmless.
    {
        const auto image = Solid(64, 32, 0xff808080);
        const auto out = pin_translation::Render(image, {MakeBlock(4, 4, 40, 20, 12)}, {L""}, translate::Language::English);
        Expect(out.pixels == image.pixels, "empty translation leaves the block untouched");
        const auto none = pin_translation::Render(image, {MakeBlock(4, 4, 40, 20, 12)}, {}, translate::Language::English);
        Expect(none.pixels == image.pixels, "missing translations are skipped");
    }
    // Clipboard text join.
    {
        const std::vector<std::wstring> sources{L"Hello", L"World"}, results{L"你好", L"世界"};
        Expect(pin_translation::JoinTranslation(sources, results, false) == L"你好\r\n世界", "translation-only copy is one block per line");
        Expect(pin_translation::JoinTranslation(sources, results, true) == L"Hello\r\n你好\r\n\r\nWorld\r\n世界", "bilingual copy pairs source and translation");
        Expect(pin_translation::JoinTranslation(sources, {L"你好"}, false) == L"你好", "join stops at the shorter list");
    }
    CoUninitialize();
    std::cout << (failures ? "FAILED " : "OK ") << failures << '\n';
    return failures ? 1 : 0;
}
