#pragma once
#include "capture/frame.h"
#include "model/document.h"
#include "translate/blocks.h"
#include "translate/engine.h"
#include <string>
#include <vector>

// Bakes translated text into a copy of a pinned image: each OCR block is
// covered with its sampled background colour and the translation is fitted
// inside it in the sampled ink colour. The result is an ordinary Frame, so the
// pin can display, copy and save it exactly like the original.
namespace lumashot::pin_translation {
struct Palette { uint32_t background{0xffffffff}, ink{0xff000000}; };
// Background = per-channel median of a ring just outside `box`; ink = the most
// distant interior colours, forced to black/white when contrast is too low.
Palette SamplePalette(const Frame& image, Box box);
double Contrast(uint32_t a, uint32_t b);
// Clipboard text for the panel: translations one per line, or bilingual
// paragraphs (source line, translation, blank line) in block order.
std::wstring JoinTranslation(const std::vector<std::wstring>& sources, const std::vector<std::wstring>& results, bool bilingual);
Frame Render(const Frame& image, const std::vector<translate::Block>& blocks,
    const std::vector<std::wstring>& texts, translate::Language target);
}
