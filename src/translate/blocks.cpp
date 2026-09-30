#include "translate/blocks.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace lumashot::translate {
namespace {
float Height(const Box& b) { return std::max(1.f, b.bottom - b.top); }
bool Wide(wchar_t c) { return (c >= 0x2e80 && c <= 0x9fff) || (c >= 0xac00 && c <= 0xd7af) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff00 && c <= 0xffef) || (c >= 0x3000 && c <= 0x303f); }
bool Terminal(wchar_t c) { return c == L'.' || c == L'!' || c == L'?' || c == L'。' || c == L'！' || c == L'？' || c == L':' || c == L'：'; }

bool Bullet(std::wstring_view text) {
    if (text.empty()) return false;
    const wchar_t c = text[0];
    if (c == L'•' || c == L'·' || c == L'●' || c == L'○' || c == L'■' || c == L'□' || c == L'▪' || c == L'◆' || c == L'★' || c == L'✓' || c == L'√' || c == L'-' || c == L'*' || c == L'–' || c == L'—') return true;
    size_t digits = 0;
    while (digits < text.size() && digits < 3 && std::iswdigit(text[digits])) ++digits;
    return digits && digits < text.size() && (text[digits] == L'.' || text[digits] == L')' || text[digits] == L'、' || text[digits] == L'．');
}

bool Joinable(const ocr::Line& a, const std::wstring& a_text, const ocr::Line& b, const std::wstring& b_text) {
    const float ha = Height(a.box), hb = Height(b.box), h = std::max(ha, hb);
    if (std::min(ha, hb) / h < 0.72f) return false;
    const float gap = b.box.top - a.box.bottom;
    if (gap < -0.35f * h || gap > 0.8f * h) return false;
    const float left = std::abs(a.box.left - b.box.left);
    const float overlap = std::min(a.box.right, b.box.right) - std::max(a.box.left, b.box.left);
    const float narrow = std::max(1.f, std::min(a.box.right - a.box.left, b.box.right - b.box.left));
    if (left > 1.4f * h && overlap < 0.6f * narrow) return false;
    if (Bullet(b_text)) return false;
    // A short line ending a sentence is the last line of its paragraph.
    const float wa = a.box.right - a.box.left, wb = b.box.right - b.box.left;
    if (!a_text.empty() && Terminal(a_text.back()) && wa < 0.7f * std::max(wa, wb)) return false;
    return true;
}

void Append(std::wstring& text, std::wstring_view next) {
    if (text.empty()) { text = next; return; }
    if (next.empty()) return;
    const wchar_t last = text.back(), first = next.front();
    if (last == L'-' && text.size() > 1 && std::iswalpha(text[text.size() - 2]) && std::iswlower(first)) { text.pop_back(); text += next; return; }
    if (Wide(last) || Wide(first)) text += next;
    else { text.push_back(L' '); text += next; }
}
}

std::wstring LineText(const ocr::Line& line) {
    std::wstring out;
    for (const auto& glyph : line.glyphs) out += glyph.text;
    size_t a = 0, b = out.size();
    while (a < b && std::iswspace(out[a])) ++a;
    while (b > a && std::iswspace(out[b - 1])) --b;
    return out.substr(a, b - a);
}

std::vector<Block> BuildBlocks(const ocr::Text& text) {
    std::vector<Block> blocks;
    std::vector<float> heights;
    const bool table = text.table.has_value();
    std::wstring previous_text;
    for (size_t i = 0; i < text.lines.size(); ++i) {
        const auto& line = text.lines[i];
        auto content = LineText(line);
        if (content.empty()) continue;
        const bool join = !table && !blocks.empty() && Joinable(text.lines[blocks.back().lines.back()], previous_text, line, content);
        if (!join) {
            if (!blocks.empty()) {
                std::sort(heights.begin(), heights.end());
                blocks.back().line_height = heights[heights.size() / 2];
            }
            heights.clear();
            blocks.push_back({{i}, line.box, 0, content});
        } else {
            auto& block = blocks.back();
            block.lines.push_back(i);
            block.box.left = std::min(block.box.left, line.box.left);
            block.box.top = std::min(block.box.top, line.box.top);
            block.box.right = std::max(block.box.right, line.box.right);
            block.box.bottom = std::max(block.box.bottom, line.box.bottom);
            Append(block.text, content);
        }
        heights.push_back(Height(line.box));
        previous_text = std::move(content);
    }
    if (!blocks.empty() && !heights.empty()) {
        std::sort(heights.begin(), heights.end());
        blocks.back().line_height = heights[heights.size() / 2];
    }
    return blocks;
}
}
