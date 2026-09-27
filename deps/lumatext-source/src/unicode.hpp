#pragma once

#include <cstdint>
#include <vector>

namespace lt::unicode {

struct Codepoint {
  char32_t value = 0;
  uint32_t start = 0;
  uint32_t end = 0;
};

struct BidiRun {
  uint32_t start = 0;
  uint32_t end = 0;
  uint8_t level = 0;
};

bool next_utf16(const char16_t* text, uint32_t length, uint32_t& offset,
                Codepoint& output) noexcept;
bool validate_utf16(const char16_t* text, uint32_t length) noexcept;
std::vector<uint32_t> grapheme_boundaries(const char16_t* text, uint32_t length);
std::vector<BidiRun> visual_bidi_runs(const std::vector<uint8_t>& levels);

}  // namespace lt::unicode
