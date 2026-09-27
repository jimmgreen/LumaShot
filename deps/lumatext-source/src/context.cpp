#include "internal.hpp"

namespace {

std::atomic<uint64_t> font_identity{1};

uint64_t glyph_bytes(const lt::GlyphBitmap& bitmap) noexcept {
  return static_cast<uint64_t>(bitmap.pixels.size()) + sizeof(bitmap);
}

}  // namespace

uint64_t lt::next_font_identity() noexcept {
  return font_identity.fetch_add(1, std::memory_order_relaxed);
}

size_t lt::GlyphKeyHash::operator()(const GlyphKey& key) const noexcept {
  auto mix = [](size_t seed, uint64_t value) {
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33;
    return seed ^ static_cast<size_t>(value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
  };
  size_t result = mix(0, key.font_identity);
  result = mix(result, key.glyph_index);
  result = mix(result, key.em_size_26_6);
  result = mix(result, static_cast<uint64_t>(key.dpi_x) << 16 | key.dpi_y);
  result = mix(result, static_cast<uint64_t>(key.x_phase) << 32 |
                           static_cast<uint64_t>(key.y_phase) << 24 |
                           static_cast<uint64_t>(key.raster_filter) << 20 |
                           static_cast<uint64_t>(key.gamma_64) << 12 |
                           static_cast<uint64_t>(key.contrast_64) << 4 | (key.stem_64 & 0xf));
  result = mix(result, static_cast<uint64_t>(key.stem_64) >> 4);
  result = mix(result, static_cast<uint64_t>(key.synthetic_64) << 8 | key.hinted);
  result = mix(result, key.optical_64);
  return result;
}

void lt_context::log(int32_t level, const char* message) const noexcept {
  if (log_callback) log_callback(log_user_data, level, message);
}

lt_result lt_context::get_glyph(IDWriteFontFace* face, const lt::GlyphKey& partial_key,
                                std::shared_ptr<const lt::GlyphBitmap>& out,
                                bool* cache_hit) {
  std::shared_ptr<const lt::FontBlob> font;
  lt_result result = font_bridge.get_blob(face, font);
  if (result != LT_OK) return result;

  return get_glyph(std::move(font), partial_key, out, cache_hit);
}

lt_result lt_context::get_glyph(std::shared_ptr<const lt::FontBlob> font,
                                const lt::GlyphKey& partial_key,
                                std::shared_ptr<const lt::GlyphBitmap>& out,
                                bool* cache_hit) {
  if (cache_hit) *cache_hit = false;
  if (!font) return LT_E_FONT_UNAVAILABLE;

  lt::GlyphKey key = partial_key;
  key.font_identity = font->identity;
  {
    std::lock_guard lock(glyph_mutex);
    auto found = glyph_cache.find(key);
    if (found != glyph_cache.end()) {
      glyph_lru.splice(glyph_lru.begin(), glyph_lru, found->second.lru_position);
      out = found->second.bitmap;
      if (cache_hit) *cache_hit = true;
      return LT_OK;
    }
  }

  std::shared_ptr<const lt::GlyphBitmap> rendered;
  const lt_result result = lt::Rasterizer::render(font, key, rendered);
  if (result != LT_OK) return result;

  std::lock_guard lock(glyph_mutex);
  auto found = glyph_cache.find(key);
  if (found != glyph_cache.end()) {
    glyph_lru.splice(glyph_lru.begin(), glyph_lru, found->second.lru_position);
    out = found->second.bitmap;
    if (cache_hit) *cache_hit = true;
    return LT_OK;
  }

  glyph_lru.push_front(key);
  glyph_cache.emplace(key, lt::CachedGlyph{rendered, glyph_lru.begin()});
  glyph_cache_bytes += glyph_bytes(*rendered);
  while (glyph_cache_bytes > cpu_cache_limit && glyph_cache.size() > 1) {
    const lt::GlyphKey& victim_key = glyph_lru.back();
    auto victim = glyph_cache.find(victim_key);
    if (victim != glyph_cache.end()) {
      glyph_cache_bytes -= glyph_bytes(*victim->second.bitmap);
      glyph_cache.erase(victim);
    }
    glyph_lru.pop_back();
  }
  out = std::move(rendered);
  return LT_OK;
}
