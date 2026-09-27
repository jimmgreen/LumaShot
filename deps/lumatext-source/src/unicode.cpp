#include "unicode.hpp"

#include "unicode_grapheme_data.hpp"

#include <algorithm>

namespace lt::unicode {
namespace {

enum class Grapheme : uint8_t {
  other = 0,
  cr = 1,
  lf = 2,
  control = 3,
  extend = 4,
  zwj = 5,
  regional_indicator = 6,
  prepend = 7,
  spacing_mark = 8,
  l = 9,
  v = 10,
  t = 11,
  lv = 12,
  lvt = 13,
};

enum class Indic : uint8_t {
  none = 0,
  consonant = 1,
  linker = 2,
  extend = 3,
};

struct Properties {
  Grapheme grapheme = Grapheme::other;
  Indic indic = Indic::none;
  bool extended_pictographic = false;
};

Properties properties(char32_t codepoint) noexcept {
  const auto* begin = std::begin(data::kPropertyRanges);
  const auto* end = std::end(data::kPropertyRanges);
  const auto* found = std::lower_bound(begin, end, codepoint,
      [](const data::PropertyRange& range, char32_t value) {
        return range.last < value;
      });
  if (found == end || codepoint < found->first) return {};
  return {
      static_cast<Grapheme>(found->value & 0x0f),
      static_cast<Indic>((found->value >> 4) & 0x03),
      (found->value & 0x40) != 0};
}

bool is_control(Grapheme value) noexcept {
  return value == Grapheme::control || value == Grapheme::cr || value == Grapheme::lf;
}

bool should_break(const std::vector<Properties>& values, size_t index) noexcept {
  const Properties& before = values[index - 1];
  const Properties& after = values[index];
  if (before.grapheme == Grapheme::cr && after.grapheme == Grapheme::lf) return false;
  if (is_control(before.grapheme) || is_control(after.grapheme)) return true;
  if (before.grapheme == Grapheme::l &&
      (after.grapheme == Grapheme::l || after.grapheme == Grapheme::v ||
       after.grapheme == Grapheme::lv || after.grapheme == Grapheme::lvt)) return false;
  if ((before.grapheme == Grapheme::lv || before.grapheme == Grapheme::v) &&
      (after.grapheme == Grapheme::v || after.grapheme == Grapheme::t)) return false;
  if ((before.grapheme == Grapheme::lvt || before.grapheme == Grapheme::t) &&
      after.grapheme == Grapheme::t) return false;
  if (after.grapheme == Grapheme::extend || after.grapheme == Grapheme::zwj ||
      after.grapheme == Grapheme::spacing_mark) return false;
  if (before.grapheme == Grapheme::prepend) return false;

  if (after.indic == Indic::consonant) {
    bool linker = false;
    size_t cursor = index;
    while (cursor > 0) {
      --cursor;
      if (values[cursor].indic == Indic::linker) {
        linker = true;
      } else if (values[cursor].indic != Indic::extend) {
        break;
      }
    }
    if (linker && values[cursor].indic == Indic::consonant) return false;
  }

  if (after.extended_pictographic && before.grapheme == Grapheme::zwj) {
    size_t cursor = index - 1;
    while (cursor > 0 && values[cursor - 1].grapheme == Grapheme::extend) --cursor;
    if (cursor > 0 && values[cursor - 1].extended_pictographic) return false;
  }

  if (before.grapheme == Grapheme::regional_indicator &&
      after.grapheme == Grapheme::regional_indicator) {
    size_t count = 0;
    size_t cursor = index;
    while (cursor > 0 &&
           values[cursor - 1].grapheme == Grapheme::regional_indicator) {
      --cursor;
      ++count;
    }
    if ((count & 1u) != 0) return false;
  }
  return true;
}

}  // namespace

bool next_utf16(const char16_t* text, uint32_t length, uint32_t& offset,
                Codepoint& output) noexcept {
  if (!text || offset >= length) return false;
  output.start = offset;
  const uint32_t first = text[offset++];
  if (first >= 0xd800 && first <= 0xdbff) {
    if (offset >= length) return false;
    const uint32_t second = text[offset];
    if (second < 0xdc00 || second > 0xdfff) return false;
    ++offset;
    output.value = static_cast<char32_t>(
        0x10000 + ((first - 0xd800) << 10) + (second - 0xdc00));
  } else {
    if (first >= 0xdc00 && first <= 0xdfff) return false;
    output.value = static_cast<char32_t>(first);
  }
  output.end = offset;
  return true;
}

bool validate_utf16(const char16_t* text, uint32_t length) noexcept {
  if (!text && length != 0) return false;
  uint32_t offset = 0;
  while (offset < length) {
    Codepoint value;
    if (!next_utf16(text, length, offset, value)) return false;
  }
  return true;
}

std::vector<uint32_t> grapheme_boundaries(const char16_t* text, uint32_t length) {
  std::vector<uint32_t> boundaries{0};
  if (length == 0) return boundaries;
  std::vector<Codepoint> codepoints;
  std::vector<Properties> values;
  uint32_t offset = 0;
  while (offset < length) {
    Codepoint value;
    if (!next_utf16(text, length, offset, value)) return {};
    codepoints.push_back(value);
    values.push_back(properties(value.value));
  }
  for (size_t index = 1; index < values.size(); ++index) {
    if (should_break(values, index)) boundaries.push_back(codepoints[index].start);
  }
  boundaries.push_back(length);
  return boundaries;
}

std::vector<BidiRun> visual_bidi_runs(const std::vector<uint8_t>& levels) {
  std::vector<BidiRun> runs;
  for (uint32_t start = 0; start < levels.size();) {
    uint32_t end = start + 1;
    while (end < levels.size() && levels[end] == levels[start]) ++end;
    runs.push_back({start, end, levels[start]});
    start = end;
  }
  if (runs.empty()) return runs;

  uint8_t maximum = 0;
  uint8_t lowest_odd = 0xff;
  for (const BidiRun& run : runs) {
    maximum = std::max(maximum, run.level);
    if ((run.level & 1u) != 0) lowest_odd = std::min(lowest_odd, run.level);
  }
  if (lowest_odd == 0xff) return runs;
  for (int level = maximum; level >= lowest_odd; --level) {
    size_t start = 0;
    while (start < runs.size()) {
      while (start < runs.size() && runs[start].level < level) ++start;
      size_t end = start;
      while (end < runs.size() && runs[end].level >= level) ++end;
      std::reverse(runs.begin() + start, runs.begin() + end);
      start = end;
    }
  }
  return runs;
}

}  // namespace lt::unicode
