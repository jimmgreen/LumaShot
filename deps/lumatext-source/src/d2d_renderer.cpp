#include "internal.hpp"

#include <d2d1helper.h>
#include <dwrite_2.h>

namespace {

uint8_t phase_for(double physical) noexcept {
  const int64_t eighth = static_cast<int64_t>(std::llround(physical * 8.0));
  return static_cast<uint8_t>((eighth % 8 + 8) % 8);
}

lt::GlyphKey make_key(UINT16 glyph_index, FLOAT em_size, float dpi_x, float dpi_y,
                      float baseline_x, float baseline_y,
                      const lt_render_config& config) noexcept {
  const double physical_x = baseline_x * dpi_x / 96.0;
  const int64_t eighth_pixel_position = static_cast<int64_t>(std::llround(physical_x * 8.0));
  const int64_t normalized_phase = (eighth_pixel_position % 8 + 8) % 8;
  lt::GlyphKey key{};
  key.glyph_index = glyph_index;
  const int64_t quantized_em_size = static_cast<int64_t>(std::llround(em_size * 64.0f));
  key.em_size_26_6 = static_cast<uint32_t>(std::clamp<int64_t>(
      quantized_em_size, 1, static_cast<int64_t>(UINT32_MAX)));
  key.dpi_x = static_cast<uint16_t>(std::clamp(std::lround(dpi_x), 1l, 65535l));
  key.dpi_y = static_cast<uint16_t>(std::clamp(std::lround(dpi_y), 1l, 65535l));
  key.x_phase = static_cast<uint8_t>(normalized_phase);
  key.y_phase = phase_for(static_cast<double>(baseline_y) * dpi_y / 96.0);
  key.raster_filter = config.raster_filter == LT_RASTER_FILTER_DIRECT ||
      config.raster_filter == LT_RASTER_FILTER_BOX ||
      config.raster_filter == LT_RASTER_FILTER_MITCHELL
      ? config.raster_filter : LT_RASTER_FILTER_MITCHELL;
  key.gamma_64 = static_cast<uint8_t>(std::clamp(
      std::lround(lt::finite_or(config.coverage_gamma,
                               lt::kDefaultCoverageGamma) * 64.0f), 16l, 255l));
  key.contrast_64 = static_cast<uint8_t>(std::clamp(
      std::lround(lt::finite_or(config.coverage_contrast,
                               lt::kDefaultCoverageContrast) * 64.0f), 16l, 255l));
  const float stem = (config.flags & LT_RENDER_CONFIG_DISABLE_STEM_COMPENSATION)
      ? 0.0f : std::max(0.0f, lt::finite_or(config.stem_strength, 0.06f));
  key.stem_64 = static_cast<uint8_t>(std::clamp(std::lround(stem * 64.0f), 0l, 64l));
  key.hinted = (config.flags & LT_RENDER_CONFIG_HINTED_OUTLINES) ? 1 : 0;
  return key;
}

bool is_color_font(IDWriteFontFace* face) noexcept {
  lt::ComPtr<IDWriteFontFace2> face2;
  return SUCCEEDED(face->QueryInterface(IID_PPV_ARGS(&face2))) && face2->IsColorFont();
}

class LayoutRenderer final : public IDWriteTextRenderer {
 public:
  LayoutRenderer(lt_frame* frame, const lt_draw_text_desc& desc)
      : frame_(frame), desc_(desc) {
    frame_->retain();
    auto* target = frame_->renderer->d2d_target.Get();
    target->CreateSolidColorBrush(
        D2D1::ColorF(desc.foreground.r, desc.foreground.g, desc.foreground.b,
                     desc.foreground.a), &brush_);
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) ||
        iid == __uuidof(IDWriteTextRenderer)) {
      *object = static_cast<IDWriteTextRenderer*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() override {
    ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) delete this;
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
    if (!disabled) return E_POINTER;
    *disabled = FALSE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
    if (!transform) return E_POINTER;
    D2D1_MATRIX_3X2_F value{};
    frame_->renderer->d2d_target->GetTransform(&value);
    transform->m11 = value._11;
    transform->m12 = value._12;
    transform->m21 = value._21;
    transform->m22 = value._22;
    transform->dx = value._31;
    transform->dy = value._32;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixels_per_dip) override {
    if (!pixels_per_dip) return E_POINTER;
    *pixels_per_dip = frame_->dpi_y / 96.0f;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawGlyphRun(
      void*, FLOAT baseline_origin_x, FLOAT baseline_origin_y,
      DWRITE_MEASURING_MODE measuring_mode, const DWRITE_GLYPH_RUN* glyph_run,
      const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
    if (!glyph_run || !glyph_run->fontFace || !brush_) return E_INVALIDARG;
    const float baseline_x = desc_.origin_x + baseline_origin_x;
    const float baseline_y = desc_.origin_y + baseline_origin_y;
    if (glyph_run->isSideways || (glyph_run->bidiLevel & 1u) ||
        !glyph_run->glyphAdvances ||
        glyph_run->fontFace->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE ||
        is_color_font(glyph_run->fontFace)) {
      fallback(baseline_x, baseline_y, measuring_mode, glyph_run);
      return S_OK;
    }

    float pen = 0.0f;
    for (UINT32 index = 0; index < glyph_run->glyphCount; ++index) {
      const DWRITE_GLYPH_OFFSET offset = glyph_run->glyphOffsets
          ? glyph_run->glyphOffsets[index] : DWRITE_GLYPH_OFFSET{};
      const float glyph_baseline_x = baseline_x + pen + offset.advanceOffset;
      const float glyph_baseline_y = baseline_y - offset.ascenderOffset;
      lt::GlyphKey key = make_key(glyph_run->glyphIndices[index], glyph_run->fontEmSize,
                                  frame_->dpi_x, frame_->dpi_y, glyph_baseline_x,
                                  glyph_baseline_y,
                                  desc_.render_config);
      std::shared_ptr<const lt::GlyphBitmap> bitmap;
      bool cache_hit = false;
      const auto raster_start = std::chrono::steady_clock::now();
      lt_result result = frame_->renderer->context->get_glyph(
          glyph_run->fontFace, key, bitmap, &cache_hit);
      frame_->stats.raster_time_us += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - raster_start).count());
      if (result != LT_OK) {
        frame_->renderer->context->log(1, "FreeType glyph unavailable; using DirectWrite for the run");
        fallback(baseline_x, baseline_y, measuring_mode, glyph_run);
        return S_OK;
      }
      frame_->stats.freetype_glyphs++;
      if (cache_hit) frame_->stats.glyph_cache_hits++;
      else frame_->stats.glyph_cache_misses++;
      draw_bitmap(glyph_baseline_x, glyph_baseline_y, std::move(bitmap));
      if (glyph_run->glyphAdvances) pen += glyph_run->glyphAdvances[index];
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawUnderline(
      void*, FLOAT baseline_origin_x, FLOAT baseline_origin_y,
      const DWRITE_UNDERLINE* underline, IUnknown*) override {
    if (!underline || !brush_) return E_INVALIDARG;
    const D2D1_RECT_F rectangle = D2D1::RectF(
        desc_.origin_x + baseline_origin_x,
        desc_.origin_y + baseline_origin_y + underline->offset,
        desc_.origin_x + baseline_origin_x + underline->width,
        desc_.origin_y + baseline_origin_y + underline->offset + underline->thickness);
    frame_->renderer->d2d_target->FillRectangle(rectangle, brush_.Get());
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawStrikethrough(
      void*, FLOAT baseline_origin_x, FLOAT baseline_origin_y,
      const DWRITE_STRIKETHROUGH* strike, IUnknown*) override {
    if (!strike || !brush_) return E_INVALIDARG;
    const D2D1_RECT_F rectangle = D2D1::RectF(
        desc_.origin_x + baseline_origin_x,
        desc_.origin_y + baseline_origin_y + strike->offset,
        desc_.origin_x + baseline_origin_x + strike->width,
        desc_.origin_y + baseline_origin_y + strike->offset + strike->thickness);
    frame_->renderer->d2d_target->FillRectangle(rectangle, brush_.Get());
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawInlineObject(
      void* client_context, FLOAT origin_x, FLOAT origin_y,
      IDWriteInlineObject* inline_object, BOOL is_sideways, BOOL is_right_to_left,
      IUnknown* effect) override {
    if (!inline_object) return E_INVALIDARG;
    return inline_object->Draw(client_context, this,
                               desc_.origin_x + origin_x, desc_.origin_y + origin_y,
                               is_sideways, is_right_to_left, effect);
  }

 private:
  ~LayoutRenderer() { frame_->release(); }

  void fallback(float x, float y, DWRITE_MEASURING_MODE mode,
                const DWRITE_GLYPH_RUN* run) noexcept {
    frame_->stats.compatibility_fallback_runs++;
    frame_->renderer->d2d_target->DrawGlyphRun(
        D2D1::Point2F(x, y), run, brush_.Get(), mode);
  }

  void draw_bitmap(float baseline_x, float baseline_y,
                   std::shared_ptr<const lt::GlyphBitmap> bitmap_ref) noexcept {
    if (!bitmap_ref) return;
    const lt::GlyphBitmap& bitmap = *bitmap_ref;
    if (bitmap.width == 0 || bitmap.height == 0) return;
    const float width_dip = bitmap.width * 96.0f / frame_->dpi_x;
    const float height_dip = bitmap.height * 96.0f / frame_->dpi_y;
    const double quantized_x = std::round(
        static_cast<double>(baseline_x) * frame_->dpi_x / 96.0 * 8.0) / 8.0;
    const double mask_origin_x = std::floor(quantized_x);
    const double quantized_y = std::round(
        static_cast<double>(baseline_y) * frame_->dpi_y / 96.0 * 8.0) / 8.0;
    const double mask_origin_y = std::floor(quantized_y);
    const float left = static_cast<float>(
        (mask_origin_x + bitmap.left) * 96.0 / frame_->dpi_x);
    const float top = static_cast<float>(
        (mask_origin_y - bitmap.top) * 96.0 / frame_->dpi_y);
    lt::ComPtr<ID2D1Bitmap> mask;
    auto cached = frame_->renderer->a8_bitmap_cache.find(&bitmap);
    if (cached != frame_->renderer->a8_bitmap_cache.end()) {
      mask = cached->second;
    } else {
      const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
          D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_STRAIGHT),
          frame_->dpi_x, frame_->dpi_y);
      HRESULT hr = frame_->renderer->d2d_target->CreateBitmap(
          D2D1::SizeU(bitmap.width, bitmap.height), bitmap.pixels.data(), bitmap.width,
          properties, &mask);
      if (FAILED(hr)) return;
      if (frame_->renderer->a8_bitmap_cache.size() >= 512) {
        const auto victim = frame_->renderer->a8_bitmap_cache.begin();
        frame_->renderer->a8_bitmap_owners.erase(victim->first);
        frame_->renderer->a8_bitmap_cache.erase(victim);
      }
      frame_->renderer->a8_bitmap_cache.emplace(&bitmap, mask);
      frame_->renderer->a8_bitmap_owners.emplace(&bitmap, std::move(bitmap_ref));
    }
    const D2D1_RECT_F destination = D2D1::RectF(left, top, left + width_dip, top + height_dip);
    const D2D1_RECT_F source = D2D1::RectF(0.0f, 0.0f, width_dip, height_dip);
    frame_->renderer->d2d_target->FillOpacityMask(
        mask.Get(), brush_.Get(), D2D1_OPACITY_MASK_CONTENT_TEXT_NATURAL,
        &destination, &source);
  }

  std::atomic<ULONG> references_{1};
  lt_frame* frame_ = nullptr;
  lt_draw_text_desc desc_{};
  lt::ComPtr<ID2D1SolidColorBrush> brush_;
};

}  // namespace

lt_result lt_draw_layout_d2d(lt_frame* frame, IDWriteTextLayout* layout,
                             const lt_draw_text_desc& desc) noexcept {
  auto* target = frame->renderer->d2d_target.Get();
  D2D1_ANTIALIAS_MODE old_antialias = target->GetAntialiasMode();
  D2D1_TEXT_ANTIALIAS_MODE old_text_antialias = target->GetTextAntialiasMode();
  target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  if (desc.clip_enabled) {
    target->PushAxisAlignedClip(
        D2D1::RectF(desc.clip.left, desc.clip.top, desc.clip.right, desc.clip.bottom),
        D2D1_ANTIALIAS_MODE_ALIASED);
  }

  auto* renderer = new (std::nothrow) LayoutRenderer(frame, desc);
  HRESULT hr = renderer ? layout->Draw(nullptr, renderer, 0.0f, 0.0f) : E_OUTOFMEMORY;
  if (renderer) renderer->Release();

  if (desc.clip_enabled) target->PopAxisAlignedClip();
  target->SetTextAntialiasMode(old_text_antialias);
  target->SetAntialiasMode(old_antialias);
  return SUCCEEDED(hr) ? LT_OK : LT_E_INTERNAL;
}
