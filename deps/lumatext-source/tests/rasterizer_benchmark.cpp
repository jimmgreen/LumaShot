#include "internal.hpp"
#include <lumatext/lumatext.hpp>
#include <cstdio>

// Deliberately bypass the glyph cache: measure rasterization, not cache lookups.
int main() {
  auto desc = LumaText::Descriptor<lt_context_desc>();
  LumaText::Context context;
  if (lt_context_create(&desc, context.put()) != LT_OK) return 1;
  auto source = LumaText::Descriptor<lt_font_source_desc>();
  source.source_type = LT_FONT_SOURCE_FILE;
  source.file_path = L"C:\\Windows\\Fonts\\msyh.ttc";
  LumaText::FontFace face;
  if (lt_font_face_create(context.get(), &source, face.put()) != LT_OK) return 2;
  for (uint8_t filter : {LT_RASTER_FILTER_DIRECT, LT_RASTER_FILTER_BOX,
                         LT_RASTER_FILTER_MITCHELL}) {
    uint64_t checksum = 14695981039346656037ull;
    const auto start = std::chrono::steady_clock::now();
    unsigned count = 0;
    for (unsigned pass = 0; pass < 3; ++pass) {
      for (unsigned index = 1; index <= 256; ++index) {
        lt::GlyphKey key{};
        key.glyph_index = static_cast<uint16_t>(index);
        key.em_size_26_6 = (12 + (index % 13)) * 64;
        key.dpi_x = key.dpi_y = (index % 2) ? 144 : 96;
        key.x_phase = index % 8;
        key.y_phase = (index / 8) % 8;
        key.raster_filter = filter;
        key.gamma_64 = 55;
        key.contrast_64 = 64;
        std::shared_ptr<const lt::GlyphBitmap> bitmap;
        if (lt::Rasterizer::render(face.get()->blob, key, bitmap) != LT_OK) return 3;
        for (uint8_t pixel : bitmap->pixels) {
          checksum ^= pixel;
          checksum *= 1099511628211ull;
        }
        ++count;
      }
    }
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("filter=%u glyphs=%u ms=%.3f checksum=%llu\n",
        filter, count, ms, static_cast<unsigned long long>(checksum));
  }
}
