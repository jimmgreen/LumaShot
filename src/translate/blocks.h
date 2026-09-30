#pragma once
#include "ocr/text.h"
#include <string>
#include <vector>

// Groups OCR lines into paragraph blocks so wrapped sentences are translated
// as a whole. Lines are consecutive in reading order; a block never spans
// columns, font-size changes, list bullets or large vertical gaps.
namespace lumashot::translate {
struct Block {
    std::vector<size_t> lines;   // indices into ocr::Text::lines
    Box box{};                   // union of line boxes, image pixels
    float line_height{};         // median line height
    std::wstring text;           // joined source text
};
std::wstring LineText(const ocr::Line& line);
std::vector<Block> BuildBlocks(const ocr::Text& text);
}
