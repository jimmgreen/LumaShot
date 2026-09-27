#include "internal.hpp"

#include <d2d1helper.h>
#include <dwrite_2.h>

namespace {

lt::GlyphKey make_key(const lt::ShapedRun& run, const lt::ShapedGlyph& glyph,
                      float absolute_x, float absolute_y, float dpi_x, float dpi_y,
                      const lt_render_config& config) noexcept {
  const double physical_x = absolute_x * dpi_x / 96.0;
  const int64_t eighth_position = static_cast<int64_t>(std::llround(physical_x * 8.0));
  const int64_t phase = (eighth_position % 8 + 8) % 8;
  lt::GlyphKey key{};
  key.font_identity = run.face->blob->identity;
  key.glyph_index = glyph.glyph_index;
  key.em_size_26_6 = static_cast<uint32_t>(std::clamp<int64_t>(
      std::llround(run.font_size * 64.0), 1, UINT32_MAX));
  key.dpi_x = static_cast<uint16_t>(std::clamp(std::lround(dpi_x), 1l, 65535l));
  key.dpi_y = static_cast<uint16_t>(std::clamp(std::lround(dpi_y), 1l, 65535l));
  key.x_phase = static_cast<uint8_t>(phase);
  const double physical_y = absolute_y * dpi_y / 96.0;
  const int64_t eighth_y = static_cast<int64_t>(std::llround(physical_y * 8.0));
  key.y_phase = static_cast<uint8_t>((eighth_y % 8 + 8) % 8);
  key.raster_filter = config.raster_filter == LT_RASTER_FILTER_DIRECT ||
      config.raster_filter == LT_RASTER_FILTER_BOX ||
      config.raster_filter == LT_RASTER_FILTER_MITCHELL
      ? config.raster_filter : LT_RASTER_FILTER_MITCHELL;
  const float physical_em = run.font_size * dpi_y / 96.0f;
  const float optical_gamma = run.weight >= 600
      ? std::max(0.0f, 20.0f - physical_em) * 0.0094f
      : 0.015f + std::max(0.0f, 18.0f - physical_em) * 0.018f;
  key.gamma_64 = static_cast<uint8_t>(std::clamp(
      std::lround((lt::finite_or(config.coverage_gamma, lt::kDefaultCoverageGamma) +
                   optical_gamma) * 64.0f),
      16l, 192l));
  key.contrast_64 = static_cast<uint8_t>(std::clamp(
      std::lround(lt::finite_or(config.coverage_contrast,
                               lt::kDefaultCoverageContrast) * 64.0f), 16l, 192l));
  const float stem = (config.flags & LT_RENDER_CONFIG_DISABLE_STEM_COMPENSATION) ||
      run.weight >= 600 ? 0.0f : std::max(0.0f, lt::finite_or(config.stem_strength, 0.0f));
  key.stem_64 = static_cast<uint8_t>(std::clamp(std::lround(stem * 64.0f), 0l, 64l));
  key.synthetic_64 = run.synthetic_bold ? 22 : 0;
  key.hinted = (config.flags & LT_RENDER_CONFIG_HINTED_OUTLINES) ? 1 : 0;
  return key;
}

double srgb_to_linear(double value) noexcept {
  value = std::clamp(value, 0.0, 1.0);
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) noexcept {
  value = std::clamp(value, 0.0, 1.0);
  return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

uint8_t channel_byte(double value) noexcept {
  return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

lt_render_config effective_config(const lt_draw_text_desc& desc) noexcept {
  if (!desc.profile || desc.profile->magic != lt::kObjectMagic) return desc.render_config;
  const double luminance = 0.2126 * desc.background.r +
      0.7152 * desc.background.g + 0.0722 * desc.background.b;
  return luminance < 0.5 ? desc.profile->dark : desc.profile->light;
}

lt_line_bitmap_key line_key(const lt_frame* frame, const lt_text_layout* layout,
                            const lt_draw_text_desc& desc, const lt_render_config& config) {
  return {layout->cache_identity, desc.origin_x, desc.origin_y, frame->dpi_x, frame->dpi_y,
          desc.foreground, desc.background, desc.background_type, config,
          desc.profile ? desc.profile->regular_optical_weight : 0.0f,
          desc.profile ? desc.profile->bold_optical_weight : 0.0f};
}

void cache_line(lt_renderer* renderer, const lt_line_bitmap_key& key,
                ID2D1Bitmap* bitmap, const D2D1_RECT_F& destination,
                uint64_t bytes) noexcept {
  if (bytes > lt_renderer::line_cache_limit) return;
  try {
    // Insert first: allocation failure must not turn a successful draw into an error.
    renderer->line_bitmap_cache.push_front({key, bitmap, destination, bytes});
    renderer->line_bitmap_bytes += bytes;
    while (renderer->line_bitmap_bytes > lt_renderer::line_cache_limit ||
           renderer->line_bitmap_cache.size() > lt_renderer::line_cache_entries) {
      renderer->line_bitmap_bytes -= renderer->line_bitmap_cache.back().bytes;
      renderer->line_bitmap_cache.pop_back();
    }
  } catch (...) {
    // Caching is optional; the current bitmap remains usable by the caller.
  }
}

bool draw_cached_line(lt_frame* frame, const lt_text_layout* layout,
                      const lt_draw_text_desc& desc, const lt_line_bitmap_key& key) {
  auto& cache = frame->renderer->line_bitmap_cache;
  for (auto it = cache.begin(); it != cache.end(); ++it) {
    if (!(it->key == key)) continue;
    cache.splice(cache.begin(), cache, it);
    const auto start = std::chrono::steady_clock::now();
    auto* target = frame->renderer->d2d_target.Get();
    if (desc.clip_enabled) target->PushAxisAlignedClip(
        D2D1::RectF(desc.clip.left, desc.clip.top, desc.clip.right, desc.clip.bottom),
        D2D1_ANTIALIAS_MODE_ALIASED);
    target->DrawBitmap(cache.front().bitmap.Get(), cache.front().destination, 1.0f,
                       D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    if (desc.clip_enabled) target->PopAxisAlignedClip();
    frame->stats.line_cache_hits++;
    frame->stats.harfbuzz_runs += static_cast<uint32_t>(layout->runs.size());
    frame->stats.freetype_glyphs += layout->metrics.glyph_count;
    frame->stats.shaping_time_us += layout->shaping_time_us;
    frame->stats.upload_time_us += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
    return true;
  }
  return false;
}

struct PlacedGlyph {
  std::shared_ptr<const lt::GlyphBitmap> bitmap;
  int x = 0;
  int y = 0;
};

struct ColorRun {
  const lt::ShapedRun* shaped = nullptr;
  lt::ComPtr<IDWriteFontFace> face;
};

bool make_dwrite_face(lt_context* context, const lt_font_face* source,
                      lt::ComPtr<IDWriteFontFace>& output) noexcept {
  if (!context || !context->dwrite_factory || !source || source->source_path.empty() ||
      !source->blob || !source->blob->axes.empty()) {
    return false;
  }
  lt::ComPtr<IDWriteFontFile> file;
  if (FAILED(context->dwrite_factory->CreateFontFileReference(
          source->source_path.c_str(), nullptr, &file))) {
    return false;
  }
  BOOL supported = FALSE;
  DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
  DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
  UINT32 face_count = 0;
  if (FAILED(file->Analyze(&supported, &file_type, &face_type, &face_count)) ||
      !supported || source->blob->face_index >= face_count) {
    return false;
  }
  IDWriteFontFile* files[] = {file.Get()};
  return SUCCEEDED(context->dwrite_factory->CreateFontFace(
      face_type, 1, files, source->blob->face_index,
      DWRITE_FONT_SIMULATIONS_NONE, &output));
}

bool draw_color_run(lt_frame* frame, const lt_text_layout* layout,
                    const lt_draw_text_desc& desc, const ColorRun& color_run,
                    ID2D1SolidColorBrush* foreground,
                    ID2D1SolidColorBrush* palette) noexcept {
  lt::ComPtr<IDWriteFactory2> factory;
  if (!color_run.shaped || !color_run.face || !foreground || !palette ||
      FAILED(frame->renderer->context->dwrite_factory.As(&factory))) {
    return false;
  }

  bool complete = true;
  for (const auto& glyph : color_run.shaped->glyphs) {
    if (glyph.glyph_index > UINT16_MAX) {
      complete = false;
      continue;
    }
    const UINT16 glyph_index = static_cast<UINT16>(glyph.glyph_index);
    const FLOAT advance = glyph.advance;
    const DWRITE_GLYPH_OFFSET offset{};
    const DWRITE_GLYPH_RUN run{
        color_run.face.Get(), color_run.shaped->font_size, 1,
        &glyph_index, &advance, &offset, FALSE,
        color_run.shaped->right_to_left ? 1u : 0u};
    const FLOAT baseline_x = desc.origin_x + glyph.x;
    const FLOAT baseline_y = desc.origin_y + layout->metrics.ascent + glyph.y;
    lt::ComPtr<IDWriteColorGlyphRunEnumerator> layers;
    HRESULT hr = factory->TranslateColorGlyphRun(
        baseline_x, baseline_y, &run, nullptr, DWRITE_MEASURING_MODE_NATURAL,
        nullptr, 0, &layers);
    bool drew_layer = false;
    if (SUCCEEDED(hr) && layers) {
      BOOL has_layer = FALSE;
      while (SUCCEEDED(hr = layers->MoveNext(&has_layer)) && has_layer) {
        const DWRITE_COLOR_GLYPH_RUN* layer = nullptr;
        if (FAILED(layers->GetCurrentRun(&layer)) || !layer) {
          hr = E_FAIL;
          break;
        }
        ID2D1Brush* brush = foreground;
        if (layer->paletteIndex != DWRITE_NO_PALETTE_INDEX) {
          palette->SetColor(D2D1::ColorF(
              layer->runColor.r, layer->runColor.g, layer->runColor.b,
              layer->runColor.a * std::clamp(desc.foreground.a, 0.0f, 1.0f)));
          brush = palette;
        }
        frame->renderer->d2d_target->DrawGlyphRun(
            D2D1::Point2F(layer->baselineOriginX, layer->baselineOriginY),
            &layer->glyphRun, brush, DWRITE_MEASURING_MODE_NATURAL);
        drew_layer = true;
      }
    }
    if (FAILED(hr) || !drew_layer) {
      frame->renderer->d2d_target->DrawGlyphRun(
          D2D1::Point2F(baseline_x, baseline_y), &run, foreground,
          DWRITE_MEASURING_MODE_NATURAL);
      complete = false;
    }
  }
  return complete;
}

}  // namespace

lt_result lt_draw_text_layout_d2d(lt_frame* frame, const lt_text_layout* layout,
                                  const lt_draw_text_desc& desc) noexcept {
  try {
    const float scale_x = frame->dpi_x / 96.0f;
    const float scale_y = frame->dpi_y / 96.0f;
    const lt_render_config config = effective_config(desc);
    const bool known_background = (config.flags & LT_RENDER_CONFIG_KNOWN_BACKGROUND) != 0;
    if (known_background && desc.background.a != 1.0f) return LT_E_INVALID_ARGUMENT;
    const auto bitmap_key = line_key(frame, layout, desc, config);
    // Palette runs retain their existing DirectWrite path and are never cached here.
    const bool cacheable = std::none_of(layout->runs.begin(), layout->runs.end(),
        [](const lt::ShapedRun& run) { return run.color; });
    if (cacheable && draw_cached_line(frame, layout, desc, bitmap_key)) return LT_OK;
    std::vector<PlacedGlyph> glyphs;
    glyphs.reserve(layout->metrics.glyph_count);
    std::vector<ColorRun> color_runs;

    int left = static_cast<int>(std::floor(desc.origin_x * scale_x));
    int top = static_cast<int>(std::floor(desc.origin_y * scale_y));
    int right = static_cast<int>(std::ceil(
        (desc.origin_x + layout->metrics.width + layout->alignment_offset) * scale_x));
    int bottom = static_cast<int>(std::ceil(
        (desc.origin_y + layout->metrics.height) * scale_y));
    const auto raster_start = std::chrono::steady_clock::now();
    for (const auto& run : layout->runs) {
      frame->stats.harfbuzz_runs++;
      if (run.color) {
        lt::ComPtr<IDWriteFontFace> dwrite_face;
        if (make_dwrite_face(frame->renderer->context, run.face, dwrite_face)) {
          color_runs.push_back(ColorRun{&run, std::move(dwrite_face)});
          continue;
        }
        frame->stats.compatibility_fallback_runs++;
      }
      for (const auto& glyph : run.glyphs) {
        const float absolute_x = desc.origin_x + glyph.x;
        const float absolute_y = desc.origin_y + layout->metrics.ascent + glyph.y;
        lt::GlyphKey key = make_key(run, glyph, absolute_x, absolute_y,
                                    frame->dpi_x, frame->dpi_y, config);
        if (desc.profile && !(config.flags & LT_RENDER_CONFIG_DISABLE_STEM_COMPENSATION)) {
          const float optical = run.weight >= 600 ? desc.profile->bold_optical_weight
                                                 : desc.profile->regular_optical_weight;
          key.optical_64 = static_cast<uint8_t>(std::lround(optical * 64.0f));
        }
        bool cache_hit = false;
        std::shared_ptr<const lt::GlyphBitmap> bitmap;
        const lt_result result = frame->renderer->context->get_glyph(
            run.face->blob, key, bitmap, &cache_hit);
        if (result != LT_OK) {
          frame->stats.compatibility_fallback_runs++;
          return result;
        }
        frame->stats.freetype_glyphs++;
        if (cache_hit) frame->stats.glyph_cache_hits++;
        else frame->stats.glyph_cache_misses++;
        const double quantized_x = std::round(absolute_x * scale_x * 8.0) / 8.0;
        const int baseline_x = static_cast<int>(std::floor(quantized_x));
        const double quantized_y = std::round(
            static_cast<double>(absolute_y) * scale_y * 8.0) / 8.0;
        const int baseline_y = static_cast<int>(std::floor(quantized_y));
        PlacedGlyph placed{bitmap, baseline_x + bitmap->left,
                           baseline_y - bitmap->top};
        left = std::min(left, placed.x);
        top = std::min(top, placed.y);
        right = std::max(right, placed.x + static_cast<int>(bitmap->width));
        bottom = std::max(bottom, placed.y + static_cast<int>(bitmap->height));
        glyphs.push_back(std::move(placed));
      }
    }
    frame->stats.raster_time_us += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - raster_start).count());
    frame->stats.shaping_time_us += layout->shaping_time_us;
    // Mitchell / optical ink extends past the advance. Cropping the atlas to
    // metrics.width shaves the last CJK stem even when the caller clip is wide.
    const int ink_pad = std::max(4, static_cast<int>(std::ceil(4.0f * std::max(scale_x, scale_y))));
    left -= ink_pad;
    top -= ink_pad;
    right += ink_pad;
    bottom += ink_pad;
    if (right <= left || bottom <= top) return LT_OK;
    const uint32_t width = static_cast<uint32_t>(right - left);
    const uint32_t height = static_cast<uint32_t>(bottom - top);
    if (width > 32768 || height > 32768 ||
        static_cast<uint64_t>(width) * height > 128ull * 1024ull * 1024ull) {
      return LT_E_UNSUPPORTED;
    }

    const bool solid = desc.background_type == LT_BACKGROUND_SOLID || known_background;
    const double background_alpha = solid ? std::clamp<double>(desc.background.a, 0.0, 1.0) : 0.0;
    const double foreground_alpha = std::clamp<double>(desc.foreground.a, 0.0, 1.0);
    const auto composition_start = std::chrono::steady_clock::now();
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    if (!solid || background_alpha <= 0.0) {
      // Transparent text is the hot path in Pulse. Compose directly into
      // premultiplied BGRA; this avoids a float coverage buffer and a second
      // full-frame conversion pass.
      std::fill(pixels.begin(), pixels.end(), 0);
      const uint32_t foreground_alpha_255 = static_cast<uint32_t>(
          std::lround(foreground_alpha * 255.0));
      const uint32_t red = static_cast<uint32_t>(std::lround(desc.foreground.r * 255.0f));
      const uint32_t green = static_cast<uint32_t>(std::lround(desc.foreground.g * 255.0f));
      const uint32_t blue = static_cast<uint32_t>(std::lround(desc.foreground.b * 255.0f));
      for (const auto& placed : glyphs) {
        for (uint32_t row = 0; row < placed.bitmap->height; ++row) {
          const int destination_y = placed.y - top + static_cast<int>(row);
          if (destination_y < 0 || destination_y >= static_cast<int>(height)) continue;
          for (uint32_t column = 0; column < placed.bitmap->width; ++column) {
            const int destination_x = placed.x - left + static_cast<int>(column);
            if (destination_x < 0 || destination_x >= static_cast<int>(width)) continue;
            const uint32_t index = static_cast<uint32_t>(destination_y) * width +
                                   static_cast<uint32_t>(destination_x);
            const uint32_t source = placed.bitmap->pixels[
                static_cast<size_t>(row) * placed.bitmap->width + column];
            const uint32_t old_alpha = pixels[index * 4 + 3];
            const uint32_t old_coverage = foreground_alpha_255 == 0
                ? 0u : std::min(255u, (old_alpha * 255u + foreground_alpha_255 / 2u) /
                                      foreground_alpha_255);
            const uint32_t inverse = 255u - old_coverage;
            const uint32_t coverage = 255u -
                (inverse * (255u - source) + 127u) / 255u;
            const uint32_t alpha = (coverage * foreground_alpha_255 + 127u) / 255u;
            pixels[index * 4 + 0] = static_cast<uint8_t>((blue * alpha + 127u) / 255u);
            pixels[index * 4 + 1] = static_cast<uint8_t>((green * alpha + 127u) / 255u);
            pixels[index * 4 + 2] = static_cast<uint8_t>((red * alpha + 127u) / 255u);
            pixels[index * 4 + 3] = static_cast<uint8_t>(alpha);
          }
        }
      }
    } else {
      std::vector<float> coverage(static_cast<size_t>(width) * height, 0.0f);
      for (const auto& placed : glyphs) {
        for (uint32_t row = 0; row < placed.bitmap->height; ++row) {
          const int destination_y = placed.y - top + static_cast<int>(row);
          if (destination_y < 0 || destination_y >= static_cast<int>(height)) continue;
          for (uint32_t column = 0; column < placed.bitmap->width; ++column) {
            const int destination_x = placed.x - left + static_cast<int>(column);
            if (destination_x < 0 || destination_x >= static_cast<int>(width)) continue;
            const float source = placed.bitmap->pixels[
                static_cast<size_t>(row) * placed.bitmap->width + column] / 255.0f;
            float& destination = coverage[
                static_cast<size_t>(destination_y) * width + destination_x];
            destination = 1.0f - (1.0f - destination) * (1.0f - source);
          }
        }
      }
      const double foreground[3] = {srgb_to_linear(desc.foreground.r),
                                    srgb_to_linear(desc.foreground.g),
                                    srgb_to_linear(desc.foreground.b)};
      const double background[3] = {srgb_to_linear(desc.background.r),
                                    srgb_to_linear(desc.background.g),
                                    srgb_to_linear(desc.background.b)};
      for (size_t index = 0; index < coverage.size(); ++index) {
        const double glyph_alpha = foreground_alpha * coverage[index];
        const double output_alpha = glyph_alpha + background_alpha * (1.0 - glyph_alpha);
        double output[3]{};
        if (output_alpha > 0.0) {
          for (size_t channel = 0; channel < 3; ++channel) {
            const double linear = (foreground[channel] * glyph_alpha +
                background[channel] * background_alpha * (1.0 - glyph_alpha)) / output_alpha;
            output[channel] = linear_to_srgb(linear) * output_alpha;
          }
        }
        pixels[index * 4 + 0] = channel_byte(output[2]);
        pixels[index * 4 + 1] = channel_byte(output[1]);
        pixels[index * 4 + 2] = channel_byte(output[0]);
        pixels[index * 4 + 3] = channel_byte(output_alpha);
      }
    }
    frame->stats.composition_time_us += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - composition_start).count());

    const auto upload_start = std::chrono::steady_clock::now();
    const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                         D2D1_ALPHA_MODE_PREMULTIPLIED),
        frame->dpi_x, frame->dpi_y);
    lt::ComPtr<ID2D1Bitmap> bitmap;
    HRESULT hr = frame->renderer->d2d_target->CreateBitmap(
        D2D1::SizeU(width, height), pixels.data(), width * 4, properties, &bitmap);
    if (FAILED(hr)) return hr == D2DERR_RECREATE_TARGET ? LT_E_DEVICE_LOST : LT_E_INTERNAL;
    auto* target = frame->renderer->d2d_target.Get();
    if (desc.clip_enabled) {
      target->PushAxisAlignedClip(
          D2D1::RectF(desc.clip.left, desc.clip.top, desc.clip.right, desc.clip.bottom),
          D2D1_ANTIALIAS_MODE_ALIASED);
    }
    const D2D1_RECT_F destination = D2D1::RectF(
        left / scale_x, top / scale_y, right / scale_x, bottom / scale_y);
    if (cacheable) cache_line(frame->renderer, bitmap_key, bitmap.Get(), destination,
                             static_cast<uint64_t>(width) * height * 4);
    target->DrawBitmap(bitmap.Get(), destination, 1.0f,
                       D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    if (!color_runs.empty()) {
      lt::ComPtr<ID2D1SolidColorBrush> foreground_brush;
      lt::ComPtr<ID2D1SolidColorBrush> palette_brush;
      const D2D1_COLOR_F foreground_color = D2D1::ColorF(
          desc.foreground.r, desc.foreground.g, desc.foreground.b,
          desc.foreground.a);
      if (SUCCEEDED(target->CreateSolidColorBrush(
              foreground_color, &foreground_brush)) &&
          SUCCEEDED(target->CreateSolidColorBrush(
              foreground_color, &palette_brush))) {
        for (const auto& run : color_runs) {
          if (!draw_color_run(frame, layout, desc, run,
                              foreground_brush.Get(), palette_brush.Get())) {
            frame->stats.compatibility_fallback_runs++;
          }
        }
      } else {
        frame->stats.compatibility_fallback_runs +=
            static_cast<uint32_t>(color_runs.size());
      }
    }
    if (desc.clip_enabled) target->PopAxisAlignedClip();
    frame->stats.upload_time_us += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - upload_start).count());
    return LT_OK;
  } catch (const std::bad_alloc&) {
    return LT_E_OUT_OF_MEMORY;
  } catch (...) {
    return LT_E_INTERNAL;
  }
}
