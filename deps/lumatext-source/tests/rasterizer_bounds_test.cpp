#include "internal.hpp"
#include <lumatext/lumatext.hpp>

#include <d2d1.h>
#include <wincodec.h>

#include <cstdio>

#define CHECK(expression)                                                        \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
      return __LINE__;                                                           \
    }                                                                            \
  } while (false)

int main() {
  CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
  auto context_desc = LumaText::Descriptor<lt_context_desc>();
  LumaText::Context context;
  CHECK(lt_context_create(&context_desc, context.put()) == LT_OK);

  auto source = LumaText::Descriptor<lt_font_source_desc>();
  source.source_type = LT_FONT_SOURCE_FILE;
  source.file_path = L"C:\\Windows\\Fonts\\msyh.ttc";
  LumaText::FontFace face;
  CHECK(lt_font_face_create(context.get(), &source, face.put()) == LT_OK);

  FT_Library library = nullptr;
  FT_Face ft_face = nullptr;
  CHECK(FT_Init_FreeType(&library) == 0);
  CHECK(FT_New_Memory_Face(library, face.get()->blob->data(),
      static_cast<FT_Long>(face.get()->blob->size()), 0, &ft_face) == 0);
  const FT_UInt glyph_index = FT_Get_Char_Index(ft_face, 'y');
  CHECK(glyph_index != 0);
  FT_Done_Face(ft_face);
  FT_Done_FreeType(library);

  lt::GlyphKey key{};
  key.font_identity = face.get()->blob->identity;
  key.glyph_index = glyph_index;
  key.em_size_26_6 = 13 * 64;
  key.dpi_x = 144;
  key.dpi_y = 144;
  key.gamma_64 = 64;
  key.contrast_64 = 64;

  std::shared_ptr<const lt::GlyphBitmap> glyph;
  CHECK(lt::Rasterizer::render(face.get()->blob, key, glyph) == LT_OK);
  CHECK(glyph && glyph->height > static_cast<uint32_t>(glyph->top + 1));
  bool glyph_ink_below_baseline = false;
  for (uint32_t row = static_cast<uint32_t>(std::max(0, glyph->top + 1));
       row < glyph->height; ++row) {
    for (uint32_t column = 0; column < glyph->width; ++column) {
      glyph_ink_below_baseline = glyph_ink_below_baseline ||
          glyph->pixels[static_cast<size_t>(row) * glyph->width + column] > 8;
    }
  }
  CHECK(glyph_ink_below_baseline);

  // Direct and supersampled outlines must use the same physical emboldening.
  auto ink_width = [](const lt::GlyphBitmap& bitmap) {
    double coverage = 0;
    for (uint8_t pixel : bitmap.pixels) coverage += pixel / 255.0;
    return coverage;
  };
  double added_ink[2]{};
  for (int mode = 0; mode < 2; ++mode) {
    key.raster_filter = mode == 0 ? LT_RASTER_FILTER_DIRECT : LT_RASTER_FILTER_BOX;
    key.stem_64 = 0;
    CHECK(lt::Rasterizer::render(face.get()->blob, key, glyph) == LT_OK);
    const double normal = ink_width(*glyph);
    key.stem_64 = 32;
    CHECK(lt::Rasterizer::render(face.get()->blob, key, glyph) == LT_OK);
    added_ink[mode] = ink_width(*glyph) - normal;
  }
  CHECK(added_ink[0] > 0 && added_ink[1] > 0);
  CHECK(added_ink[0] / added_ink[1] > 0.75 && added_ink[0] / added_ink[1] < 1.25);

  // Baseline Y increases downwards in the public renderer coordinates.
  key.stem_64 = 0;
  key.em_size_26_6 = 32 * 64;
  key.dpi_x = key.dpi_y = 96;
  auto centroid_y = [](const lt::GlyphBitmap& bitmap) {
    double total = 0, moment = 0;
    for (uint32_t row = 0; row < bitmap.height; ++row) {
      for (uint32_t column = 0; column < bitmap.width; ++column) {
        const double alpha = bitmap.pixels[static_cast<size_t>(row) * bitmap.width + column];
        total += alpha;
        moment += alpha * (static_cast<double>(row) + 0.5 - bitmap.top);
      }
    }
    return moment / total;
  };
  for (uint8_t filter : {LT_RASTER_FILTER_DIRECT, LT_RASTER_FILTER_BOX, LT_RASTER_FILTER_MITCHELL}) {
    key.raster_filter = filter;
    double start_y = 0;
    for (uint8_t phase = 0; phase < 8; ++phase) {
      key.y_phase = phase;
      CHECK(lt::Rasterizer::render(face.get()->blob, key, glyph) == LT_OK);
      const double position = centroid_y(*glyph);
      if (phase == 0) start_y = position;
      CHECK(std::abs(position - start_y - phase / 8.0) < 0.10);
    }
  }

  // Profile compensation is explicit and must also work for real bold outlines.
  source.file_path = L"C:\\Windows\\Fonts\\msyhbd.ttc";
  LumaText::FontFace bold_face;
  CHECK(lt_font_face_create(context.get(), &source, bold_face.put()) == LT_OK);
  key.y_phase = 0;
  key.optical_64 = 0;
  CHECK(lt::Rasterizer::render(bold_face.get()->blob, key, glyph) == LT_OK);
  const double plain_bold_ink = ink_width(*glyph);
  const float plain_bold_advance = glyph->advance;
  key.optical_64 = 16;
  CHECK(lt::Rasterizer::render(bold_face.get()->blob, key, glyph) == LT_OK);
  CHECK(ink_width(*glyph) > plain_bold_ink);
  CHECK(glyph->advance == plain_bold_advance);

  lt::ComPtr<IWICImagingFactory> wic;
  lt::ComPtr<IWICBitmap> surface;
  lt::ComPtr<ID2D1Factory> d2d;
  lt::ComPtr<ID2D1RenderTarget> target;
  CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))));
  CHECK(SUCCEEDED(wic->CreateBitmap(240, 120, GUID_WICPixelFormat32bppPBGRA,
      WICBitmapCacheOnLoad, &surface)));
  CHECK(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
      d2d.GetAddressOf())));
  CHECK(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(
      surface.Get(), D2D1::RenderTargetProperties(), &target)));
  target->SetDpi(144.0f, 144.0f);

  const lt_font_cascade_entry entry{face.get(), 400, 0};
  auto cascade_desc = LumaText::Descriptor<lt_font_cascade_desc>();
  cascade_desc.entries = &entry;
  cascade_desc.entry_count = 1;
  LumaText::FontCascade cascade;
  CHECK(lt_font_cascade_create(context.get(), &cascade_desc, cascade.put()) == LT_OK);
  auto style = LumaText::Descriptor<lt_text_style>();
  style.cascade = cascade.get();
  style.font_size = 13.0f;
  style.weight = 400;
  constexpr wchar_t text[] = L"y项目计划";
  auto layout_desc = LumaText::Descriptor<lt_text_layout_desc>();
  layout_desc.text = text;
  layout_desc.text_length = ARRAYSIZE(text) - 1;
  layout_desc.base_style = style;
  layout_desc.locale = "zh-CN";
  layout_desc.direction = LT_TEXT_DIRECTION_LTR;
  layout_desc.max_width = 200.0f;
  LumaText::TextLayout layout;
  CHECK(lt_text_layout_create(context.get(), &layout_desc, layout.put()) == LT_OK);
  auto metrics = LumaText::Descriptor<lt_text_metrics>();
  CHECK(lt_text_layout_get_metrics(layout.get(), &metrics) == LT_OK);

  auto renderer_desc = LumaText::Descriptor<lt_d2d_desc>();
  renderer_desc.render_target = target.Get();
  renderer_desc.manage_begin_end_draw = true;
  LumaText::Renderer renderer;
  CHECK(lt_d2d_renderer_create(context.get(), &renderer_desc, renderer.put()) == LT_OK);
  auto frame_desc = LumaText::Descriptor<lt_frame_desc>();
  frame_desc.dpi_x = frame_desc.dpi_y = 144.0f;
  LumaText::Frame frame;
  CHECK(lt_frame_begin(renderer.get(), &frame_desc, frame.put()) == LT_OK);
  auto draw = LumaText::Descriptor<lt_draw_text_desc>();
  draw.origin_x = draw.origin_y = 8.0f;
  draw.foreground = {0.0f, 0.0f, 0.0f, 1.0f};
  draw.background = {1.0f, 1.0f, 1.0f, 1.0f};
  draw.background_type = LT_BACKGROUND_SOLID;
  draw.render_config = LumaText::Descriptor<lt_render_config>();
  draw.render_config.coverage_gamma = 1.0f;
  draw.render_config.coverage_contrast = 1.0f;
  CHECK(lt_frame_draw_text_layout(frame.get(), layout.get(), &draw) == LT_OK);
  CHECK(lt_frame_end(frame.get()) == LT_OK);

  WICRect area{0, 0, 240, 120};
  lt::ComPtr<IWICBitmapLock> lock;
  CHECK(SUCCEEDED(surface->Lock(&area, WICBitmapLockRead, &lock)));
  UINT stride = 0;
  UINT byte_count = 0;
  BYTE* pixels = nullptr;
  CHECK(SUCCEEDED(lock->GetStride(&stride)));
  CHECK(SUCCEEDED(lock->GetDataPointer(&byte_count, &pixels)));
  int last_ink_row = -1;
  for (int row = 0; row < 120; ++row) {
    for (int column = 0; column < 240; ++column) {
      const BYTE* pixel = pixels + static_cast<size_t>(row) * stride + column * 4;
      if (pixel[3] > 200 && pixel[0] < 220 && pixel[1] < 220 && pixel[2] < 220) {
        last_ink_row = row;
      }
    }
  }
  const int baseline = static_cast<int>(std::lround((8.0f + metrics.ascent) * 1.5f));
  CHECK(last_ink_row >= baseline + 2);
  return 0;
}
