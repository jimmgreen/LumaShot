#include "internal.hpp"
#include <lumatext/lumatext.hpp>
#include <wincodec.h>
#include <cstdio>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); return __LINE__; } } while (false)

int main(int argc, char**) {
  CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
  lt::ComPtr<IWICImagingFactory> wic;
  lt::ComPtr<ID2D1Factory> d2d;
  lt::ComPtr<IWICBitmap> surface;
  lt::ComPtr<ID2D1RenderTarget> target;
  CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))));
  CHECK(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())));
  CHECK(SUCCEEDED(wic->CreateBitmap(960, 640, GUID_WICPixelFormat32bppPBGRA,
      WICBitmapCacheOnLoad, &surface)));
  CHECK(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(surface.Get(),
      D2D1::RenderTargetProperties(), &target)));
  auto cd = LumaText::Descriptor<lt_context_desc>();
  LumaText::Context context;
  CHECK(lt_context_create(&cd, context.put()) == LT_OK);
  auto fs = LumaText::Descriptor<lt_font_source_desc>();
  fs.source_type = LT_FONT_SOURCE_FILE;
  fs.file_path = L"C:\\Windows\\Fonts\\msyh.ttc";
  LumaText::FontFace face;
  CHECK(lt_font_face_create(context.get(), &fs, face.put()) == LT_OK);
  const lt_font_cascade_entry entry{face.get(), 400, 0};
  auto cs = LumaText::Descriptor<lt_font_cascade_desc>();
  cs.entries = &entry;
  cs.entry_count = 1;
  LumaText::FontCascade cascade;
  CHECK(lt_font_cascade_create(context.get(), &cs, cascade.put()) == LT_OK);
  auto ld = LumaText::Descriptor<lt_text_layout_desc>();
  ld.text = L"LumaText 项目计划与渲染性能 2026-09-20 fi 1234567890";
  ld.text_length = static_cast<uint32_t>(wcslen(ld.text));
  ld.base_style = LumaText::Descriptor<lt_text_style>();
  ld.base_style.cascade = cascade.get();
  ld.base_style.font_size = 16;
  ld.base_style.weight = 400;
  ld.max_width = 600;
  ld.locale = "zh-CN";
  LumaText::TextLayout layout;
  CHECK(lt_text_layout_create(context.get(), &ld, layout.put()) == LT_OK);
  auto rd = LumaText::Descriptor<lt_d2d_desc>();
  rd.render_target = target.Get();
  rd.manage_begin_end_draw = true;
  LumaText::Renderer renderer;
  CHECK(lt_d2d_renderer_create(context.get(), &rd, renderer.put()) == LT_OK);
  auto fd = LumaText::Descriptor<lt_frame_desc>();
  fd.dpi_x = fd.dpi_y = 144;
  target->SetDpi(144, 144);
  auto draw = LumaText::Descriptor<lt_draw_text_desc>();
  draw.foreground = {0.1f, 0.2f, 0.3f, 0.8f};
  draw.background = {0.95f, 0.96f, 0.97f, 1};
  draw.render_config = LumaText::Descriptor<lt_render_config>();
  draw.render_config.coverage_gamma = 0.85f;
  draw.render_config.coverage_contrast = 1;
  std::vector<uint8_t> reference(960 * 640 * 4), pixels(reference.size());
  for (bool solid : {false, true}) {
    draw.background_type = solid ? LT_BACKGROUND_SOLID : LT_BACKGROUND_TRANSPARENT;
    const int frames = argc > 1 ? 120 : 2;
    const auto start = std::chrono::steady_clock::now();
    for (int frame_index = 0; frame_index < frames; ++frame_index) {
      LumaText::Frame frame;
      CHECK(lt_frame_begin(renderer.get(), &fd, frame.put()) == LT_OK);
      target->Clear(D2D1::ColorF(0.7f, 0.7f, 0.7f, 1));
      for (int row = 0; row < 12; ++row) {
        draw.origin_x = 8.25f;
        draw.origin_y = 8.125f + row * 28.0f;
        CHECK(lt_frame_draw_text_layout(frame.get(), layout.get(), &draw) == LT_OK);
      }
      CHECK(lt_frame_end(frame.get()) == LT_OK);
      if (frame_index == 0 || frame_index == frames - 1) {
        CHECK(SUCCEEDED(surface->CopyPixels(nullptr, 960 * 4,
            static_cast<UINT>(pixels.size()), pixels.data())));
        if (frame_index == 0) reference = pixels;
        else CHECK(reference == pixels);
      }
    }
    std::printf("background=%s frames=%d rows=12 ms=%.3f\n", solid ? "solid" : "transparent",
        frames, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
  }
  if (argc > 1) return 0;

  auto render = [&](lt_renderer* current, const lt_draw_text_desc& settings,
                    std::vector<uint8_t>& output, uint32_t& cache_hits) {
    LumaText::Frame frame;
    if (lt_frame_begin(current, &fd, frame.put()) != LT_OK) return false;
    target->Clear(D2D1::ColorF(0.7f, 0.7f, 0.7f, 1));
    if (lt_frame_draw_text_layout(frame.get(), layout.get(), &settings) != LT_OK) return false;
    auto stats = LumaText::Descriptor<lt_frame_stats>();
    if (lt_frame_get_stats(frame.get(), &stats) != LT_OK) return false;
    cache_hits = stats.line_cache_hits;
    auto legacy_stats = LumaText::Descriptor<lt_frame_stats>();
    legacy_stats.struct_size = offsetof(lt_frame_stats, line_cache_hits);
    legacy_stats.line_cache_hits = 0xdeadbeefu;
    if (lt_frame_get_stats(frame.get(), &legacy_stats) != LT_OK ||
        legacy_stats.line_cache_hits != 0xdeadbeefu) return false;
    if (lt_frame_end(frame.get()) != LT_OK) return false;
    return SUCCEEDED(surface->CopyPixels(nullptr, 960 * 4,
        static_cast<UINT>(output.size()), output.data()));
  };
  renderer.get()->clear_line_cache();
  draw.origin_y = 8.125f;
  uint32_t hits = 0;
  // Every visible input change must match rendering with a fresh renderer.
  for (int variant = 0; variant < 8; ++variant) {
    if (variant == 1) draw.foreground = {0.8f, 0.1f, 0.2f, 0.5f};
    if (variant == 2) draw.background = {0.03f, 0.04f, 0.05f, 0.7f};
    if (variant == 3) draw.background_type = LT_BACKGROUND_TRANSPARENT;
    if (variant == 4) draw.origin_y += 0.375f;
    if (variant == 5) draw.render_config.coverage_gamma = 1.2f;
    if (variant == 6) draw.render_config.raster_filter = LT_RASTER_FILTER_DIRECT;
    if (variant == 7) draw.render_config.stem_strength = 0.2f;
    CHECK(render(renderer.get(), draw, pixels, hits));
    CHECK(hits == 0);
    CHECK(render(renderer.get(), draw, pixels, hits));
    CHECK(hits == 1);
    LumaText::Renderer fresh;
    CHECK(lt_d2d_renderer_create(context.get(), &rd, fresh.put()) == LT_OK);
    CHECK(render(fresh.get(), draw, reference, hits));
    CHECK(reference == pixels);
  }
  // Clip is applied at draw time; it must not be baked into cached content.
  draw.clip_enabled = true;
  draw.clip = {15, 10, 130, 26};
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 1);
  LumaText::Renderer fresh;
  CHECK(lt_d2d_renderer_create(context.get(), &rd, fresh.put()) == LT_OK);
  CHECK(render(fresh.get(), draw, reference, hits));
  CHECK(reference == pixels);
  draw.clip_enabled = false;

  // Profile values, not the profile address, determine the cached appearance.
  auto pd = LumaText::Descriptor<lt_render_profile_desc>();
  pd.light = pd.dark = draw.render_config;
  pd.light.coverage_gamma = pd.dark.coverage_gamma = 0.7f;
  LumaText::RenderProfile profile;
  // Old profile prefixes omit the optical tail, even if memory beyond it has values.
  auto old_pd = pd;
  old_pd.struct_size = offsetof(lt_render_profile_desc, regular_optical_weight);
  old_pd.regular_optical_weight = old_pd.bold_optical_weight = 1.0f;
  CHECK(lt_render_profile_create(&old_pd, profile.put()) == LT_OK);
  CHECK(profile.get()->regular_optical_weight == 0 && profile.get()->bold_optical_weight == 0);
  CHECK(lt_render_profile_create(&pd, profile.put()) == LT_OK);
  draw.profile = profile.get();
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 0);
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 1);
  CHECK(lt_d2d_renderer_create(context.get(), &rd, fresh.put()) == LT_OK);
  CHECK(render(fresh.get(), draw, reference, hits));
  CHECK(reference == pixels);
  draw.profile = nullptr;

  // Optical outline weight must affect pixels and participate in both cache keys.
  pd.regular_optical_weight = 0.2f;
  CHECK(lt_render_profile_create(&pd, profile.put()) == LT_OK);
  draw.profile = profile.get();
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 0 && pixels != reference);
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 1);
  CHECK(lt_d2d_renderer_create(context.get(), &rd, fresh.put()) == LT_OK);
  CHECK(render(fresh.get(), draw, reference, hits));
  CHECK(reference == pixels);
  draw.profile = nullptr;

  // Explicit known opaque background must match the solid linear-composition path.
  draw.background = {0.7f, 0.7f, 0.7f, 1};
  draw.background_type = LT_BACKGROUND_SOLID;
  CHECK(render(renderer.get(), draw, reference, hits));
  draw.background_type = LT_BACKGROUND_TRANSPARENT;
  draw.render_config.flags |= LT_RENDER_CONFIG_KNOWN_BACKGROUND;
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(pixels == reference);
  draw.background.a = 0.5f;
  {
    LumaText::Frame frame;
    CHECK(lt_frame_begin(renderer.get(), &fd, frame.put()) == LT_OK);
    CHECK(lt_frame_draw_text_layout(frame.get(), layout.get(), &draw) == LT_E_INVALID_ARGUMENT);
    CHECK(lt_frame_end(frame.get()) == LT_OK);
  }
  draw.background.a = 1;

  // DPI and target changes must discard device-dependent images.
  target->SetDpi(120, 120);
  fd.dpi_x = fd.dpi_y = 120;
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 0);
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 1);
  CHECK(lt_d2d_renderer_create(context.get(), &rd, fresh.put()) == LT_OK);
  CHECK(render(fresh.get(), draw, reference, hits));
  CHECK(reference == pixels);
  lt::ComPtr<ID2D1RenderTarget> replacement;
  CHECK(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(surface.Get(),
      D2D1::RenderTargetProperties(), &replacement)));
  replacement->SetDpi(120, 120);
  target = replacement;
  rd.render_target = target.Get();
  CHECK(lt_d2d_renderer_set_target(renderer.get(), target.Get()) == LT_OK);
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 0 && reference == pixels);

  // Retired layouts must not keep their font cascade/context alive through the cache.
  const uint32_t refs = context.get()->references.load();
  layout.reset();
  CHECK(context.get()->references.load() < refs);
  ld.text = L"different layout";
  ld.text_length = static_cast<uint32_t>(wcslen(ld.text));
  CHECK(lt_text_layout_create(context.get(), &ld, layout.put()) == LT_OK);
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 0 && pixels != reference);

  // A scrolling workload must stay bounded and re-render evicted entries correctly.
  CHECK(render(renderer.get(), draw, reference, hits));
  const float first_y = draw.origin_y;
  for (int i = 1; i <= 80; ++i) {
    draw.origin_y = first_y + i * 0.125f;
    CHECK(render(renderer.get(), draw, pixels, hits));
    CHECK(renderer.get()->line_bitmap_cache.size() <= lt_renderer::line_cache_entries);
    CHECK(renderer.get()->line_bitmap_bytes <= lt_renderer::line_cache_limit);
  }
  draw.origin_y = first_y;
  CHECK(render(renderer.get(), draw, pixels, hits));
  CHECK(hits == 0 && pixels == reference);
  return 0;
}
