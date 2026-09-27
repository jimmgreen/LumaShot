#include "internal.hpp"

namespace {

constexpr size_t request_required =
    offsetof(lt_glyph_request, px_em) + sizeof(float);

uint64_t hash_bytes(const uint8_t* data, size_t size, uint32_t face_index) noexcept {
  uint64_t hash = 14695981039346656037ull;
  auto mix = [&](uint8_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
  };
  mix(static_cast<uint8_t>(face_index));
  mix(static_cast<uint8_t>(face_index >> 8));
  mix(static_cast<uint8_t>(face_index >> 16));
  mix(static_cast<uint8_t>(face_index >> 24));
  mix(static_cast<uint8_t>(size));
  mix(static_cast<uint8_t>(size >> 8));
  mix(static_cast<uint8_t>(size >> 16));
  mix(static_cast<uint8_t>(size >> 24));
  for (size_t index = 0; index < size; ++index) mix(data[index]);
  return hash == 0 ? 1 : hash;
}

lt_result inspect_face(lt::FontBlob& blob) {
  if (blob.empty()) return LT_E_FONT_UNAVAILABLE;
  FT_Library library = nullptr;
  FT_Face face = nullptr;
  if (FT_Init_FreeType(&library) != 0 || !library ||
      FT_New_Memory_Face(library, blob.data(),
          static_cast<FT_Long>(blob.size()), blob.face_index, &face) != 0 ||
      !face) {
    if (face) FT_Done_Face(face);
    if (library) FT_Done_FreeType(library);
    return LT_E_FONT_UNAVAILABLE;
  }
  if (const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, ft_sfnt_os2))) {
    blob.weight = static_cast<uint16_t>(std::clamp<unsigned>(
        os2->usWeightClass ? os2->usWeightClass : 400, 1, 1000));
  } else if (face->style_flags & FT_STYLE_FLAG_BOLD) {
    blob.weight = 700;
  }
  blob.has_color = FT_HAS_COLOR(face) != 0;
  FT_Done_Face(face);
  FT_Done_FreeType(library);
  return LT_OK;
}

lt_result blob_from_bytes(lt_context* context, const void* bytes, uint64_t size,
                          uint32_t face_index, std::shared_ptr<const lt::FontBlob>& out) {
  if (!bytes || size == 0 || size > static_cast<uint64_t>(LONG_MAX)) {
    return LT_E_INVALID_ARGUMENT;
  }
  const auto* data = static_cast<const uint8_t*>(bytes);
  const uint64_t key = hash_bytes(data, static_cast<size_t>(size), face_index);
  {
    std::lock_guard lock(context->memory_font_mutex);
    auto found = context->memory_fonts.find(key);
    if (found != context->memory_fonts.end()) {
      out = found->second;
      return LT_OK;
    }
  }

  auto blob = std::make_shared<lt::FontBlob>();
  blob->identity = lt::next_font_identity();
  blob->face_index = face_index;
  try {
    blob->owned.assign(data, data + static_cast<size_t>(size));
  } catch (...) {
    return LT_E_OUT_OF_MEMORY;
  }
  const lt_result inspect = inspect_face(*blob);
  if (inspect != LT_OK) return inspect;

  std::lock_guard lock(context->memory_font_mutex);
  auto [position, inserted] = context->memory_fonts.emplace(key, blob);
  out = inserted ? std::move(blob) : position->second;
  return LT_OK;
}

lt::GlyphKey make_key(const lt_glyph_request& request, uint16_t weight) noexcept {
  const float dpi_x_raw = lt::finite_or(request.dpi_x, 96.0f);
  const float dpi_y_raw = lt::finite_or(request.dpi_y, 96.0f);
  const float dpi_x = dpi_x_raw > 0.0f ? std::clamp(dpi_x_raw, 1.0f, 65535.0f) : 96.0f;
  const float dpi_y = dpi_y_raw > 0.0f ? std::clamp(dpi_y_raw, 1.0f, 65535.0f) : 96.0f;
  const float px_em = std::max(1.0f, lt::finite_or(request.px_em, 0.0f));
  const double dip_em = std::min(static_cast<double>(px_em) * 96.0 / dpi_y,
                                static_cast<double>(UINT32_MAX) / 64.0);
  lt::GlyphKey key{};
  key.glyph_index = request.glyph_id;
  key.em_size_26_6 = static_cast<uint32_t>(std::clamp<int64_t>(
      std::llround(dip_em * 64.0), 1, UINT32_MAX));
  key.dpi_x = static_cast<uint16_t>(std::clamp(std::lround(dpi_x), 1l, 65535l));
  key.dpi_y = static_cast<uint16_t>(std::clamp(std::lround(dpi_y), 1l, 65535l));
  key.x_phase = static_cast<uint8_t>(request.x_phase & 7u);
  if (request.struct_size >= offsetof(lt_glyph_request, y_phase) + sizeof(uint8_t)) {
    key.y_phase = static_cast<uint8_t>(request.y_phase & 7u);
  }
  const uint8_t requested_filter = request.cfg.struct_size >=
      offsetof(lt_render_config, raster_filter) + sizeof(uint8_t)
      ? request.cfg.raster_filter : LT_RASTER_FILTER_DEFAULT;
  key.raster_filter = requested_filter == LT_RASTER_FILTER_DIRECT ||
      requested_filter == LT_RASTER_FILTER_BOX ||
      requested_filter == LT_RASTER_FILTER_MITCHELL
      ? requested_filter : LT_RASTER_FILTER_MITCHELL;
  const float optical_gamma = weight >= 600
      ? std::max(0.0f, 20.0f - px_em) * 0.0094f
      : 0.015f + std::max(0.0f, 18.0f - px_em) * 0.018f;
  const float gamma = lt::finite_or(
      request.cfg.coverage_gamma, lt::kDefaultCoverageGamma);
  const float contrast = lt::finite_or(
      request.cfg.coverage_contrast, lt::kDefaultCoverageContrast);
  key.gamma_64 = static_cast<uint8_t>(std::clamp(
      std::lround(std::clamp(gamma + optical_gamma, 0.25f, 3.0f) * 64.0f), 16l, 192l));
  key.contrast_64 = static_cast<uint8_t>(std::clamp(
      std::lround(std::clamp(contrast, 0.25f, 3.0f) * 64.0f), 16l, 192l));
  const float stem = (request.cfg.flags & LT_RENDER_CONFIG_DISABLE_STEM_COMPENSATION) ||
      weight >= 600 ? 0.0f : std::max(0.0f, lt::finite_or(request.cfg.stem_strength, 0.0f));
  key.stem_64 = static_cast<uint8_t>(std::lround(std::clamp(stem, 0.0f, 1.0f) * 64.0f));
  key.synthetic_64 = request.synth_weight != 0 && weight < 600 ? 22 : 0;
  key.hinted = (request.cfg.flags & LT_RENDER_CONFIG_HINTED_OUTLINES) ? 1 : 0;
  return key;
}

}  // namespace

extern "C" {

lt_result __cdecl lt_glyph_provider_get(lt_context* context,
                                        const lt_glyph_request* request,
                                        lt_glyph_image** out_image) {
  if (!out_image) return LT_E_INVALID_ARGUMENT;
  *out_image = nullptr;
  if (!context || context->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(request, request_required)) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (request->px_em <= 0.0f || !std::isfinite(request->px_em)) {
    return LT_E_INVALID_ARGUMENT;
  }

  // Tail fields are optional. Never read beyond the caller's descriptor.
  lt_glyph_request local{};
  local.dpi_x = local.dpi_y = 96.0f;
  memcpy(&local, request, std::min<size_t>(request->struct_size,
                                         offsetof(lt_glyph_request, cfg)));
  local.cfg.struct_size = sizeof(local.cfg);
  local.cfg.abi_version = LT_ABI_VERSION;
  local.cfg.coverage_gamma = lt::kDefaultCoverageGamma;
  local.cfg.coverage_contrast = lt::kDefaultCoverageContrast;
  local.cfg.raster_filter = LT_RASTER_FILTER_MITCHELL;
  const size_t cfg_offset = offsetof(lt_glyph_request, cfg);
  if (request->struct_size >= cfg_offset + offsetof(lt_render_config, coverage_gamma)) {
    const auto& cfg = request->cfg;
    if (cfg.abi_version == LT_ABI_VERSION &&
        cfg.struct_size >= offsetof(lt_render_config, coverage_gamma) + sizeof(float)) {
      const size_t available = request->struct_size - cfg_offset;
      memcpy(&local.cfg, &cfg, std::min({available, static_cast<size_t>(cfg.struct_size),
                                        sizeof(local.cfg)}));
    }
  }
  if (request->struct_size >= offsetof(lt_glyph_request, font_face) +
                                  sizeof(local.font_face)) {
    local.font_face = request->font_face;
  }
  local.struct_size = sizeof(local);
  request = &local;

  try {
    std::shared_ptr<const lt::FontBlob> font;
    lt_result result = LT_OK;
    if (request->font_face) {
      const auto* face = request->font_face;
      if (face->magic != lt::kObjectMagic || face->context != context) {
        return LT_E_INVALID_ARGUMENT;
      }
      font = face->blob;
    } else if (request->dwrite_face) {
      result = context->font_bridge.get_blob(request->dwrite_face, font);
    } else if (request->font_bytes && request->font_size != 0) {
      result = blob_from_bytes(context, request->font_bytes, request->font_size,
                               request->face_index, font);
    } else {
      return LT_E_INVALID_ARGUMENT;
    }
    if (result != LT_OK) return result;
    if (!font) return LT_E_FONT_UNAVAILABLE;
    if (font->has_color) return LT_E_UNSUPPORTED;

    const uint16_t weight = request->synth_weight != 0
        ? static_cast<uint16_t>(std::min<int>(request->synth_weight, 1000))
        : font->weight;
    const lt::GlyphKey key = make_key(*request, weight);
    std::shared_ptr<const lt::GlyphBitmap> bitmap;
    result = context->get_glyph(font, key, bitmap);
    if (result != LT_OK) return result;

    auto image = std::make_unique<lt_glyph_image>();
    image->bitmap = std::move(bitmap);
    image->advance = image->bitmap ? image->bitmap->advance : 0.0f;
    *out_image = image.release();
    return LT_OK;
  } catch (const std::bad_alloc&) {
    return LT_E_OUT_OF_MEMORY;
  } catch (...) {
    return LT_E_INTERNAL;
  }
}

lt_result __cdecl lt_glyph_image_describe(const lt_glyph_image* image,
                                          lt_glyph_bitmap* out_bitmap) {
  if (!image || image->magic != lt::kObjectMagic || !out_bitmap ||
      out_bitmap->struct_size < offsetof(lt_glyph_bitmap, advance) + sizeof(float) ||
      out_bitmap->abi_version != LT_ABI_VERSION) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (!image->bitmap) return LT_E_INVALID_STATE;
  out_bitmap->a8 = image->bitmap->pixels.empty() ? nullptr : image->bitmap->pixels.data();
  out_bitmap->width = static_cast<int32_t>(image->bitmap->width);
  out_bitmap->height = static_cast<int32_t>(image->bitmap->height);
  out_bitmap->left = image->bitmap->left;
  out_bitmap->top = image->bitmap->top;
  out_bitmap->advance = image->advance;
  return LT_OK;
}

}  // extern "C"
