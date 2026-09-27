#include <lumatext/lumatext.hpp>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>

using Microsoft::WRL::ComPtr;

#define CHECK(expression)                                                        \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
      return __LINE__;                                                           \
    }                                                                            \
  } while (false)

std::vector<uint8_t> read_bytes(const wchar_t* path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

lt_result make_face(lt_context* context, const wchar_t* path,
                    LumaText::FontFace& output) {
  auto desc = LumaText::Descriptor<lt_font_source_desc>();
  desc.source_type = LT_FONT_SOURCE_FILE;
  desc.file_path = path;
  return lt_font_face_create(context, &desc, output.put());
}

int run() {
  ComPtr<IWICImagingFactory> wic;
  CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))));
  ComPtr<IWICBitmap> bitmap;
  CHECK(SUCCEEDED(wic->CreateBitmap(720, 240, GUID_WICPixelFormat32bppPBGRA,
      WICBitmapCacheOnLoad, &bitmap)));
  ComPtr<ID2D1Factory> d2d;
  CHECK(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
      d2d.GetAddressOf())));
  ComPtr<ID2D1RenderTarget> target;
  CHECK(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(
      bitmap.Get(), D2D1::RenderTargetProperties(), &target)));

  auto context_desc = LumaText::Descriptor<lt_context_desc>();
  LumaText::Context context;
  CHECK(lt_context_create(&context_desc, context.put()) == LT_OK);

  constexpr wchar_t regular_path[] = L"C:\\Windows\\Fonts\\segoeui.ttf";
  constexpr wchar_t bold_path[] = L"C:\\Windows\\Fonts\\segoeuib.ttf";
  std::vector<uint8_t> regular_bytes = read_bytes(regular_path);
  CHECK(!regular_bytes.empty());
  auto memory_desc = LumaText::Descriptor<lt_font_source_desc>();
  memory_desc.source_type = LT_FONT_SOURCE_MEMORY;
  memory_desc.memory_data = regular_bytes.data();
  memory_desc.memory_size = regular_bytes.size();
  LumaText::FontFace regular;
  CHECK(lt_font_face_create(context.get(), &memory_desc, regular.put()) == LT_OK);
  regular_bytes.clear();
  regular_bytes.shrink_to_fit();

  LumaText::FontFace bold;
  CHECK(make_face(context.get(), bold_path, bold) == LT_OK);
  LumaText::FontFace arial;
  CHECK(make_face(context.get(), L"C:\\Windows\\Fonts\\arial.ttf", arial) == LT_OK);
  const lt_font_cascade_entry entries[] = {{regular.get(), 400, 0},
                                            {bold.get(), 700, 0},
                                            {arial.get(), 400, 0}};
  auto cascade_desc = LumaText::Descriptor<lt_font_cascade_desc>();
  cascade_desc.entries = entries;
  cascade_desc.entry_count = ARRAYSIZE(entries);
  LumaText::FontCascade cascade;
  CHECK(lt_font_cascade_create(context.get(), &cascade_desc, cascade.put()) == LT_OK);

  constexpr wchar_t quick[] = L"Quick Look";
  auto style = LumaText::Descriptor<lt_text_style>();
  style.cascade = cascade.get();
  style.font_size = 16.0f;
  style.weight = 700;
  auto layout_desc = LumaText::Descriptor<lt_text_layout_desc>();
  layout_desc.text = quick;
  layout_desc.text_length = ARRAYSIZE(quick) - 1;
  layout_desc.base_style = style;
  layout_desc.locale = "en-US";
  layout_desc.direction = LT_TEXT_DIRECTION_AUTO;
  layout_desc.alignment = LT_TEXT_ALIGNMENT_START;
  layout_desc.ellipsis = LT_TEXT_ELLIPSIS_END;
  layout_desc.max_width = 300.0f;
  LumaText::TextLayout quick_layout;
  CHECK(lt_text_layout_create(context.get(), &layout_desc, quick_layout.put()) == LT_OK);

  auto metrics = LumaText::Descriptor<lt_text_metrics>();
  CHECK(lt_text_layout_get_metrics(quick_layout.get(), &metrics) == LT_OK);
  CHECK(metrics.width > 20.0f && metrics.height > 5.0f);
  CHECK(metrics.glyph_count > 0 && metrics.run_count == 1);
  auto hit = LumaText::Descriptor<lt_hit_test_metrics>();
  float hit_x = 0.0f;
  float hit_y = 0.0f;
  CHECK(lt_text_layout_hit_test_position(quick_layout.get(), 2, false,
      &hit_x, &hit_y, &hit) == LT_OK);
  CHECK(hit_x >= 0.0f && hit.text_position <= 2);
  hit = LumaText::Descriptor<lt_hit_test_metrics>();
  CHECK(lt_text_layout_hit_test_point(quick_layout.get(), hit_x, 2.0f, &hit) == LT_OK);
  CHECK(hit.is_inside);

  auto renderer_desc = LumaText::Descriptor<lt_d2d_desc>();
  renderer_desc.render_target = target.Get();
  renderer_desc.manage_begin_end_draw = true;
  LumaText::Renderer renderer;
  CHECK(lt_d2d_renderer_create(context.get(), &renderer_desc, renderer.put()) == LT_OK);
  auto frame_desc = LumaText::Descriptor<lt_frame_desc>();
  frame_desc.dpi_x = 144.0f;
  frame_desc.dpi_y = 144.0f;
  LumaText::Frame frame;
  CHECK(lt_frame_begin(renderer.get(), &frame_desc, frame.put()) == LT_OK);
  auto draw = LumaText::Descriptor<lt_draw_text_desc>();
  draw.origin_x = 12.0f;
  draw.origin_y = 12.0f;
  draw.foreground = {0.07f, 0.08f, 0.09f, 1.0f};
  draw.background = {0.96f, 0.97f, 0.98f, 1.0f};
  draw.background_type = LT_BACKGROUND_SOLID;
  draw.render_config = LumaText::Descriptor<lt_render_config>();
  draw.render_config.coverage_gamma = 1.0f;
  draw.render_config.coverage_contrast = 1.0f;
  draw.render_config.stem_strength = 0.06f;
  CHECK(lt_frame_draw_text_layout(frame.get(), quick_layout.get(), &draw) == LT_OK);
  auto stats = LumaText::Descriptor<lt_frame_stats>();
  CHECK(lt_frame_get_stats(frame.get(), &stats) == LT_OK);
  CHECK(stats.harfbuzz_runs > 0);
  CHECK(stats.freetype_glyphs > 0);
  CHECK(stats.compatibility_fallback_runs == 0);
  CHECK(lt_frame_end(frame.get()) == LT_OK);

  constexpr wchar_t bidi_text[] = L"file العربية 123 עברית.txt";
  style.weight = 400;
  layout_desc.text = bidi_text;
  layout_desc.text_length = ARRAYSIZE(bidi_text) - 1;
  layout_desc.base_style = style;
  layout_desc.locale = "ar";
  layout_desc.max_width = 600.0f;
  LumaText::TextLayout bidi_layout;
  CHECK(lt_text_layout_create(context.get(), &layout_desc, bidi_layout.put()) == LT_OK);
  metrics = LumaText::Descriptor<lt_text_metrics>();
  CHECK(lt_text_layout_get_metrics(bidi_layout.get(), &metrics) == LT_OK);
  CHECK(metrics.run_count >= 3);
  hit = LumaText::Descriptor<lt_hit_test_metrics>();
  CHECK(lt_text_layout_hit_test_point(bidi_layout.get(), metrics.width * 0.5f,
      metrics.height * 0.5f, &hit) == LT_OK);

  // Logical end positions must work in both visual directions.
  for (bool rtl : {false, true}) {
    layout_desc.text = rtl ? L"\x05d0\x05d1" : L"ab";
    layout_desc.text_length = 2;
    layout_desc.direction = rtl ? LT_TEXT_DIRECTION_RTL : LT_TEXT_DIRECTION_LTR;
    LumaText::TextLayout edge_layout;
    CHECK(lt_text_layout_create(context.get(), &layout_desc, edge_layout.put()) == LT_OK);
    CHECK(lt_text_layout_get_metrics(edge_layout.get(), &metrics) == LT_OK);
    CHECK(lt_text_layout_hit_test_position(edge_layout.get(), 2, false,
        &hit_x, &hit_y, &hit) == LT_OK);
    CHECK(std::abs(hit_x - (rtl ? 0.0f : metrics.width)) < 0.01f);
  }
  const lt_open_type_feature liga{0x6c696761u, 1};
  layout_desc.text = L"fix";
  layout_desc.text_length = 3;
  layout_desc.direction = LT_TEXT_DIRECTION_LTR;
  layout_desc.base_style.features = &liga;
  layout_desc.base_style.feature_count = 1;
  LumaText::TextLayout ligature_layout;
  CHECK(lt_text_layout_create(context.get(), &layout_desc, ligature_layout.put()) == LT_OK);
  CHECK(lt_text_layout_hit_test_position(ligature_layout.get(), 0, false,
      &hit_x, &hit_y, &hit) == LT_OK);
  CHECK(hit.text_position == 0 && hit.text_length == 2);
  const float ligature_x = hit_x;
  CHECK(lt_text_layout_hit_test_position(ligature_layout.get(), 1, false,
      &hit_x, &hit_y, &hit) == LT_OK);
  CHECK(hit.text_position == 0 && hit.text_length == 2 && hit_x == ligature_x);

  if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msyh.ttc") != INVALID_FILE_ATTRIBUTES) {
    LumaText::FontFace ttc;
    CHECK(make_face(context.get(), L"C:\\Windows\\Fonts\\msyh.ttc", ttc) == LT_OK);
  }
  if (GetFileAttributesW(L"C:\\Windows\\Fonts\\NotoSansSC-VF.ttf") !=
      INVALID_FILE_ATTRIBUTES) {
    const lt_font_axis axis{
        (static_cast<uint32_t>('w') << 24) | (static_cast<uint32_t>('g') << 16) |
        (static_cast<uint32_t>('h') << 8) | static_cast<uint32_t>('t'), 650.0f};
    auto variable_desc = LumaText::Descriptor<lt_font_source_desc>();
    variable_desc.source_type = LT_FONT_SOURCE_FILE;
    variable_desc.file_path = L"C:\\Windows\\Fonts\\NotoSansSC-VF.ttf";
    variable_desc.axes = &axis;
    variable_desc.axis_count = 1;
    LumaText::FontFace variable;
    CHECK(lt_font_face_create(context.get(), &variable_desc, variable.put()) == LT_OK);
  }
  return 0;
}

int main() {
  CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
  const int result = run();
  CoUninitialize();
  return result;
}
