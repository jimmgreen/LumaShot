#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <lumatext/lumatext.hpp>
#include <string_view>
#include <memory>
#include "model/document.h"

namespace lumashot {
// Uses the same DirectWrite font, fallback and wrapping as annotation rendering.
void FitTextBounds(Mark& mark);
// DirectWrite keeps layout and font fallback; the DLL rasterizes the glyphs.
// Owners on one thread share a bounded CPU glyph context; no render target
// survives a Draw call.
class TextRenderer {
public:
    lt_frame_stats Draw(ID2D1RenderTarget* target, IDWriteFactory* factory,
        std::wstring_view text, IDWriteTextFormat* format, D2D1_RECT_F bounds,
        D2D1_COLOR_F color, bool clip = true);
    lt_frame_stats DrawLayout(ID2D1RenderTarget* target, IDWriteFactory* factory,
        IDWriteTextLayout* layout, D2D1_POINT_2F origin, D2D1_RECT_F clip,
        D2D1_COLOR_F color, bool clipped = true);
private:
    struct Context;
    std::shared_ptr<Context> context_;
};
}
