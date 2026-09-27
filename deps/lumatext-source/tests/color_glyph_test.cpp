#include <lumatext/lumatext.hpp>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>

using Microsoft::WRL::ComPtr;

#define CHECK(expression)                                                        \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
      return __LINE__;                                                           \
    }                                                                            \
  } while (false)

int main() {
  CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
  ComPtr<IWICImagingFactory> wic;
  ComPtr<IWICBitmap> surface;
  ComPtr<ID2D1Factory> d2d;
  ComPtr<ID2D1RenderTarget> target;
  CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))));
  CHECK(SUCCEEDED(wic->CreateBitmap(240, 120, GUID_WICPixelFormat32bppPBGRA,
      WICBitmapCacheOnLoad, &surface)));
  CHECK(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
      d2d.GetAddressOf())));
  CHECK(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(
      surface.Get(), D2D1::RenderTargetProperties(), &target)));
  target->SetDpi(144.0f, 144.0f);

  auto context_desc = LumaText::Descriptor<lt_context_desc>();
  LumaText::Context context;
  CHECK(lt_context_create(&context_desc, context.put()) == LT_OK);
  auto source = LumaText::Descriptor<lt_font_source_desc>();
  source.source_type = LT_FONT_SOURCE_FILE;
  source.file_path = L"C:\\Windows\\Fonts\\seguiemj.ttf";
  LumaText::FontFace emoji;
  CHECK(lt_font_face_create(context.get(), &source, emoji.put()) == LT_OK);
  const lt_font_cascade_entry entry{emoji.get(), 400, 0};
  auto cascade_desc = LumaText::Descriptor<lt_font_cascade_desc>();
  cascade_desc.entries = &entry;
  cascade_desc.entry_count = 1;
  LumaText::FontCascade cascade;
  CHECK(lt_font_cascade_create(context.get(), &cascade_desc, cascade.put()) == LT_OK);

  auto style = LumaText::Descriptor<lt_text_style>();
  style.cascade = cascade.get();
  style.font_size = 32.0f;
  style.weight = 400;
  constexpr wchar_t text[] = L"\U0001F600";
  auto layout_desc = LumaText::Descriptor<lt_text_layout_desc>();
  layout_desc.text = text;
  layout_desc.text_length = ARRAYSIZE(text) - 1;
  layout_desc.base_style = style;
  layout_desc.locale = "en-US";
  layout_desc.direction = LT_TEXT_DIRECTION_LTR;
  layout_desc.max_width = 160.0f;
  LumaText::TextLayout layout;
  CHECK(lt_text_layout_create(context.get(), &layout_desc, layout.put()) == LT_OK);

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
  auto stats = LumaText::Descriptor<lt_frame_stats>();
  CHECK(lt_frame_get_stats(frame.get(), &stats) == LT_OK);
  CHECK(stats.harfbuzz_runs > 0);
  CHECK(stats.compatibility_fallback_runs == 0);
  CHECK(lt_frame_end(frame.get()) == LT_OK);

  WICRect area{0, 0, 240, 120};
  ComPtr<IWICBitmapLock> lock;
  CHECK(SUCCEEDED(surface->Lock(&area, WICBitmapLockRead, &lock)));
  UINT stride = 0;
  UINT byte_count = 0;
  BYTE* pixels = nullptr;
  CHECK(SUCCEEDED(lock->GetStride(&stride)));
  CHECK(SUCCEEDED(lock->GetDataPointer(&byte_count, &pixels)));
  bool chromatic_pixel = false;
  for (int row = 0; row < 120; ++row) {
    for (int column = 0; column < 240; ++column) {
      const BYTE* pixel = pixels + static_cast<size_t>(row) * stride + column * 4;
      const BYTE minimum = std::min({pixel[0], pixel[1], pixel[2]});
      const BYTE maximum = std::max({pixel[0], pixel[1], pixel[2]});
      chromatic_pixel = chromatic_pixel ||
          (pixel[3] > 200 && static_cast<int>(maximum) - minimum >= 24);
    }
  }
  CHECK(chromatic_pixel);
  lock.Reset();

  // Palette layers must respect the same foreground opacity as monochrome text.
  draw.background_type = LT_BACKGROUND_TRANSPARENT;
  uint64_t opaque_alpha_sum = 0;
  for (float opacity : {1.0f, 0.5f, 0.0f}) {
    CHECK(lt_frame_begin(renderer.get(), &frame_desc, frame.put()) == LT_OK);
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    draw.foreground.a = opacity;
    CHECK(lt_frame_draw_text_layout(frame.get(), layout.get(), &draw) == LT_OK);
    CHECK(lt_frame_end(frame.get()) == LT_OK);
    CHECK(SUCCEEDED(surface->Lock(&area, WICBitmapLockRead, &lock)));
    CHECK(SUCCEEDED(lock->GetStride(&stride)));
    CHECK(SUCCEEDED(lock->GetDataPointer(&byte_count, &pixels)));
    uint64_t alpha_sum = 0;
    bool half_opaque_color = false;
    for (int row = 0; row < 120; ++row) {
      for (int column = 0; column < 240; ++column) {
        const BYTE* pixel = pixels + static_cast<size_t>(row) * stride + column * 4;
        alpha_sum += pixel[3];
        const BYTE minimum = std::min({pixel[0], pixel[1], pixel[2]});
        const BYTE maximum = std::max({pixel[0], pixel[1], pixel[2]});
        half_opaque_color = half_opaque_color ||
            (pixel[3] >= 126 && pixel[3] <= 129 && maximum - minimum >= 12);
        if (opacity == 0.0f) {
          CHECK(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 0);
        }
      }
    }
    if (opacity == 1.0f) {
      opaque_alpha_sum = alpha_sum;
      CHECK(opaque_alpha_sum > 0);
    } else if (opacity == 0.5f) {
      // Overlapping palette layers may exceed half opacity after compositing.
      CHECK(alpha_sum > 0 && alpha_sum < opaque_alpha_sum);
      CHECK(half_opaque_color);
    } else {
      CHECK(alpha_sum == 0);
    }
    lock.Reset();
  }
  return 0;
}
