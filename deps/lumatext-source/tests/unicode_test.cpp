#include "unicode.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool equal_boundaries(const std::u16string& text,
                      const std::vector<uint32_t>& expected) {
  return lt::unicode::grapheme_boundaries(text.data(),
      static_cast<uint32_t>(text.size())) == expected;
}

void append_codepoint(std::u16string& text, uint32_t codepoint) {
  if (codepoint <= 0xffff) {
    text.push_back(static_cast<char16_t>(codepoint));
  } else {
    codepoint -= 0x10000;
    text.push_back(static_cast<char16_t>(0xd800 + (codepoint >> 10)));
    text.push_back(static_cast<char16_t>(0xdc00 + (codepoint & 0x3ff)));
  }
}

int run_official_tests(const char* path) {
  std::ifstream input(path);
  if (!input) {
    std::fprintf(stderr, "unable to open %s\n", path);
    return 1;
  }
  int failures = 0;
  int cases = 0;
  std::string line;
  while (std::getline(input, line)) {
    const size_t comment = line.find('#');
    if (comment != std::string::npos) line.resize(comment);
    std::istringstream fields(line);
    std::string token;
    std::u16string text;
    std::vector<uint32_t> expected;
    while (fields >> token) {
      if (token == "\xc3\xb7") {
        expected.push_back(static_cast<uint32_t>(text.size()));
      } else if (token != "\xc3\x97") {
        append_codepoint(text, static_cast<uint32_t>(std::stoul(token, nullptr, 16)));
      }
    }
    if (text.empty()) continue;
    ++cases;
    if (!equal_boundaries(text, expected)) {
      if (++failures <= 10) std::fprintf(stderr, "grapheme case failed: %s\n", line.c_str());
    }
  }
  std::printf("Unicode grapheme cases: %d, failures: %d\n", cases, failures);
  return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (!lt::unicode::validate_utf16(u"valid \xd83d\xde00", 8) ||
      lt::unicode::validate_utf16(u"\xd800", 1) ||
      lt::unicode::validate_utf16(u"\xdc00", 1)) return 1;
  if (!equal_boundaries(u"abc", {0, 1, 2, 3})) return 2;
  if (!equal_boundaries(u"\r\n", {0, 2})) return 3;
  if (!equal_boundaries(u"e\u0301", {0, 2})) return 4;
  if (!equal_boundaries(u"\u1100\u1161\u11a8", {0, 3})) return 5;
  if (!equal_boundaries(u"\U0001f469\u200d\U0001f469\u200d\U0001f467", {0, 8})) return 6;
  if (!equal_boundaries(u"\U0001f1e8\U0001f1f3\U0001f1fa", {0, 4, 6})) return 7;
  if (!equal_boundaries(u"\u0915\u094d\u0915", {0, 3})) return 8;

  const std::vector<uint8_t> levels{0, 0, 1, 1, 2, 2, 1, 0};
  const auto runs = lt::unicode::visual_bidi_runs(levels);
  if (runs.size() != 5 || runs[1].start != 6 || runs[2].start != 4 ||
      runs[3].start != 2) return 9;
  return argc > 1 ? run_official_tests(argv[1]) : 0;
}
