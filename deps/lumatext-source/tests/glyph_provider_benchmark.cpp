#include <lumatext/lumatext.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

int main() {
  std::ifstream file("C:\\Windows\\Fonts\\msyh.ttc", std::ios::binary);
  if (!file) return 1;
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
  auto desc = LumaText::Descriptor<lt_context_desc>();
  LumaText::Context context;
  if (lt_context_create(&desc, context.put()) != LT_OK) return 2;
  auto source = LumaText::Descriptor<lt_font_source_desc>();
  source.source_type = LT_FONT_SOURCE_MEMORY;
  source.memory_data = bytes.data();
  source.memory_size = bytes.size();
  LumaText::FontFace face;
  if (lt_font_face_create(context.get(), &source, face.put()) != LT_OK) return 3;

  auto byte_request = LumaText::Descriptor<lt_glyph_request>();
  byte_request.font_bytes = bytes.data();
  byte_request.font_size = bytes.size();
  byte_request.px_em = 16.0f;
  byte_request.dpi_x = byte_request.dpi_y = 96.0f;
  auto face_request = byte_request;
  face_request.font_bytes = nullptr;
  face_request.font_size = 0;
  face_request.font_face = face.get();
  constexpr int iterations = 100;
  auto measure = [&](lt_glyph_request& request, double& elapsed) {
    // Identical four-glyph working sets; exclude face creation and cold rasterization.
    for (int i = 0; i < 4; ++i) {
      request.glyph_id = static_cast<uint16_t>(i);
      LumaText::GlyphImage image;
      if (lt_glyph_provider_get(context.get(), &request, image.put()) != LT_OK) return false;
    }
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
      request.glyph_id = static_cast<uint16_t>(i % 4);
      LumaText::GlyphImage image;
      if (lt_glyph_provider_get(context.get(), &request, image.put()) != LT_OK) return false;
    }
    elapsed = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start).count() / iterations;
    return true;
  };
  double byte_us = 0.0;
  double face_us = 0.0;
  if (!measure(byte_request, byte_us) || !measure(face_request, face_us)) return 4;
  std::printf("Warm glyph lookup (%d calls, %zu font bytes): bytes %.3f us/call, "
              "preloaded face %.3f us/call, %.2fx speedup\n",
              iterations, bytes.size(), byte_us, face_us, byte_us / face_us);
  return 0;
}
