#pragma once
#include "model/document.h"
#include <span>
#include <string>
#include <vector>
#include <array>
#include <exception>

namespace lumashot::ocr {
struct Glyph { std::wstring text; Box box{}; float confidence{}; };
struct Line { std::vector<Glyph> glyphs; Box box{}; float confidence{}; std::array<Point,4> quad{}; };
struct Table { Box box{}; uint32_t rows{}, columns{}; std::vector<std::wstring> cells; };
struct Text { std::vector<Line> lines; std::optional<Table> table; };
std::wstring TableTsv(const Table& table);
// Positions are grapheme boundaries, not UTF-16 code units.
struct Position { size_t line{}, glyph{}; auto operator<=>(const Position&) const = default; };
struct Selection {
    Position anchor{}, caret{};
    bool Empty() const { return anchor==caret; }
};
Line DecodeCtc(std::span<const float> scores, size_t steps, size_t classes,
    const std::vector<std::wstring>& dictionary, Box box, float valid_ratio=1);
void ReadingOrder(Text& text);
Position Hit(const Text& text, Point point, bool nearest=false);
bool OnText(const Text& text, Point point);
Selection All(const Text& text);
Selection Word(const Text& text, Point point, bool entire_line);
std::wstring Selected(const Text& text, Selection selection);
std::vector<Box> Highlights(const Text& text, Selection selection);
std::wstring Utf16(const std::string& text);
std::wstring ErrorMessage(const std::exception& error);
}

