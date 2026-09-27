#include <lumatext/lumatext.hpp>

#include <cstdio>
#include <fstream>
#include <vector>
#include <windows.h>
#include <cmath>

#define CHECK(expression)                                                        \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
      return __LINE__;                                                           \
    }                                                                            \
  } while (false)

int main() {
  std::ifstream file("C:\\Windows\\Fonts\\msyh.ttc", std::ios::binary);
  CHECK(file.good());
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  CHECK(bytes.size() > 1024);

  auto context_desc = LumaText::Descriptor<lt_context_desc>();
  LumaText::Context context;
  CHECK(lt_context_create(&context_desc, context.put()) == LT_OK);

  auto request = LumaText::Descriptor<lt_glyph_request>();
  request.font_bytes = bytes.data();
  request.font_size = bytes.size();
  request.face_index = 0;
  request.glyph_id = 0;
  request.px_em = 16.0f;
  request.dpi_x = 96.0f;
  request.dpi_y = 96.0f;
  request.cfg = LumaText::Descriptor<lt_render_config>();
  request.cfg.coverage_gamma = 0.43f;
  request.cfg.coverage_contrast = 1.92f;

  LumaText::GlyphImage image;
  CHECK(lt_glyph_provider_get(context.get(), &request, image.put()) == LT_OK);

  auto metrics = LumaText::Descriptor<lt_glyph_bitmap>();
  CHECK(lt_glyph_image_describe(image.get(), &metrics) == LT_OK);
  CHECK(metrics.width > 0);
  CHECK(metrics.height > 0);
  CHECK(metrics.a8 != nullptr);
  CHECK(metrics.advance > 0.0f);

  bool has_coverage = false;
  const size_t count = static_cast<size_t>(metrics.width) * static_cast<size_t>(metrics.height);
  for (size_t index = 0; index < count; ++index) {
    has_coverage = has_coverage || metrics.a8[index] > 8;
  }
  CHECK(has_coverage);

  LumaText::GlyphImage cached;
  CHECK(lt_glyph_provider_get(context.get(), &request, cached.put()) == LT_OK);

  // The preloaded face owns its bytes and produces the same bitmap.
  LumaText::FontFace face;
  {
    std::vector<char> temporary(bytes);
    auto source = LumaText::Descriptor<lt_font_source_desc>();
    source.source_type = LT_FONT_SOURCE_MEMORY;
    source.memory_data = temporary.data();
    source.memory_size = temporary.size();
    CHECK(lt_font_face_create(context.get(), &source, face.put()) == LT_OK);
  }
  auto handle_request = request;
  handle_request.font_face = face.get();
  handle_request.font_bytes = nullptr;
  handle_request.font_size = 0;
  LumaText::GlyphImage handle_image;
  CHECK(lt_glyph_provider_get(context.get(), &handle_request, handle_image.put()) == LT_OK);
  auto handle_metrics = LumaText::Descriptor<lt_glyph_bitmap>();
  CHECK(lt_glyph_image_describe(handle_image.get(), &handle_metrics) == LT_OK);
  CHECK(handle_metrics.width == metrics.width && handle_metrics.height == metrics.height);
  CHECK(handle_metrics.left == metrics.left && handle_metrics.top == metrics.top);
  CHECK(handle_metrics.advance == metrics.advance);
  CHECK(memcmp(handle_metrics.a8, metrics.a8, count) == 0);

  LumaText::Context other_context;
  CHECK(lt_context_create(&context_desc, other_context.put()) == LT_OK);
  LumaText::GlyphImage wrong_context;
  CHECK(lt_glyph_provider_get(other_context.get(), &handle_request,
                              wrong_context.put()) == LT_E_INVALID_ARGUMENT);
  CHECK(wrong_context.get() == nullptr);

  // An explicit face takes precedence, even when the legacy source is invalid.
  handle_request.font_bytes = bytes.data();
  handle_request.font_size = static_cast<uint64_t>(LONG_MAX) + 1;
  CHECK(lt_glyph_provider_get(context.get(), &handle_request, handle_image.put()) == LT_OK);

  auto bad = LumaText::Descriptor<lt_glyph_request>();
  LumaText::GlyphImage rejected;
  CHECK(lt_glyph_provider_get(context.get(), &bad, rejected.put()) == LT_E_INVALID_ARGUMENT);
  CHECK(rejected.get() == nullptr);

  // A valid prefix ends at an inaccessible page: optional tails must not be read.
  SYSTEM_INFO system{};
  GetSystemInfo(&system);
  auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr,
      system.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  CHECK(pages != nullptr);
  DWORD previous = 0;
  CHECK(VirtualProtect(pages + system.dwPageSize, system.dwPageSize,
                       PAGE_NOACCESS, &previous));
  constexpr size_t prefix_size = offsetof(lt_glyph_request, px_em) + sizeof(float);
  request.struct_size = static_cast<uint32_t>(prefix_size);
  auto* prefix = reinterpret_cast<lt_glyph_request*>(pages + system.dwPageSize - prefix_size);
  memcpy(prefix, &request, prefix_size);
  CHECK(lt_glyph_provider_get(context.get(), prefix, rejected.put()) == LT_OK);
  // Old full descriptors and a truncated pointer tail cannot read the new field.
  for (size_t size : {offsetof(lt_glyph_request, font_face),
                     offsetof(lt_glyph_request, font_face) + sizeof(lt_font_face*) - 1}) {
    request.struct_size = static_cast<uint32_t>(size);
    request.font_face = face.get();
    auto* legacy = reinterpret_cast<lt_glyph_request*>(pages + system.dwPageSize - size);
    memcpy(legacy, &request, size);
    CHECK(lt_glyph_provider_get(other_context.get(), legacy, rejected.put()) == LT_OK);
  }
  request.font_face = nullptr;
  CHECK(VirtualFree(pages, 0, MEM_RELEASE));
  request.struct_size = sizeof(request);

  request.cfg.raster_filter = LT_RASTER_FILTER_DIRECT;
  CHECK(lt_glyph_provider_get(context.get(), &request, image.put()) == LT_OK);
  CHECK(lt_glyph_image_describe(image.get(), &metrics) == LT_OK);
  const float direct_advance = metrics.advance;
  request.cfg.raster_filter = LT_RASTER_FILTER_MITCHELL;
  CHECK(lt_glyph_provider_get(context.get(), &request, image.put()) == LT_OK);
  CHECK(lt_glyph_image_describe(image.get(), &metrics) == LT_OK);
  CHECK(std::abs(direct_advance - metrics.advance) < 0.05f);

  // The high face-index bits must not alias face 0 in the font cache.
  request.face_index = 0x10000;
  CHECK(lt_glyph_provider_get(context.get(), &request, rejected.put()) == LT_E_FONT_UNAVAILABLE);
  request.face_index = 0;
  request.font_size = static_cast<uint64_t>(LONG_MAX) + 1;
  CHECK(lt_glyph_provider_get(context.get(), &request, rejected.put()) == LT_E_INVALID_ARGUMENT);
  return 0;
}
