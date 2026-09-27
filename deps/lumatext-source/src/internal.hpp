#pragma once

#include <lumatext/lumatext.h>

#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <windows.h>
#include <wrl/client.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H

#include <hb.h>
#include <hb-ot.h>

#include "unicode.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lt {

using Microsoft::WRL::ComPtr;

constexpr uint64_t kObjectMagic = 0x4c554d4154455854ull;
constexpr float kDefaultCoverageGamma = 0.85f;
constexpr float kDefaultCoverageContrast = 1.00f;

struct Object {
  uint64_t magic = kObjectMagic;
  std::atomic<uint32_t> references{1};
  virtual ~Object() { magic = 0; }
  void retain() noexcept { references.fetch_add(1, std::memory_order_relaxed); }
  void release() noexcept {
    if (references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
  }
};

template <typename T>
bool descriptor_valid(const T* desc, size_t required_size) noexcept {
  return desc && desc->abi_version == LT_ABI_VERSION && desc->struct_size >= required_size;
}

inline float finite_or(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

struct MappedFile {
  HANDLE file = INVALID_HANDLE_VALUE;
  HANDLE mapping = nullptr;
  const uint8_t* view = nullptr;
  size_t size = 0;

  MappedFile() = default;
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  ~MappedFile() {
    if (view) UnmapViewOfFile(view);
    if (mapping) CloseHandle(mapping);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  }
};

struct DWriteFragment {
  ComPtr<IDWriteFontFileStream> stream;
  void* fragment_context = nullptr;
  const uint8_t* data = nullptr;
  size_t size = 0;

  DWriteFragment() = default;
  DWriteFragment(const DWriteFragment&) = delete;
  DWriteFragment& operator=(const DWriteFragment&) = delete;
  ~DWriteFragment() {
    if (stream) stream->ReleaseFileFragment(fragment_context);
  }
};

struct FontBlob {
  struct AxisValue {
    uint32_t tag = 0;
    float value = 0.0f;
  };
  uint64_t identity = 0;
  uint32_t face_index = 0;
  uint16_t weight = 400;
  bool has_color = false;
  std::shared_ptr<MappedFile> mapping;
  std::shared_ptr<DWriteFragment> dwrite;
  std::vector<uint8_t> owned;
  std::vector<AxisValue> axes;

  const uint8_t* data() const noexcept {
    if (mapping && mapping->view) return mapping->view;
    if (dwrite && dwrite->data) return dwrite->data;
    return owned.empty() ? nullptr : owned.data();
  }
  size_t size() const noexcept {
    if (mapping) return mapping->size;
    if (dwrite) return dwrite->size;
    return owned.size();
  }
  bool empty() const noexcept { return data() == nullptr || size() == 0; }
};

uint64_t next_font_identity() noexcept;

struct GlyphKey {
  uint64_t font_identity = 0;
  uint32_t glyph_index = 0;
  uint32_t em_size_26_6 = 0;
  uint16_t dpi_x = 96;
  uint16_t dpi_y = 96;
  uint8_t x_phase = 0;
  uint8_t y_phase = 0;
  uint8_t raster_filter = LT_RASTER_FILTER_MITCHELL;
  uint8_t gamma_64 = 64;
  uint8_t contrast_64 = 64;
  uint8_t stem_64 = 0;
  uint8_t synthetic_64 = 0;
  uint8_t hinted = 0;
  uint8_t optical_64 = 0;

  bool operator==(const GlyphKey&) const noexcept = default;
};

struct GlyphKeyHash {
  size_t operator()(const GlyphKey& key) const noexcept;
};

struct GlyphBitmap {
  int32_t left = 0;
  int32_t top = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  float advance = 0.0f;
  std::vector<uint8_t> pixels;
};

struct CachedGlyph {
  std::shared_ptr<const GlyphBitmap> bitmap;
  std::list<GlyphKey>::iterator lru_position;
};

class FontBridge {
 public:
  lt_result get_blob(IDWriteFontFace* face, std::shared_ptr<const FontBlob>& out);

 private:
  struct FaceEntry {
    ComPtr<IDWriteFontFace> face;
    std::shared_ptr<const FontBlob> blob;
  };
  std::mutex mutex_;
  std::unordered_map<IDWriteFontFace*, FaceEntry> cache_;
};

class Rasterizer {
 public:
  static lt_result render(std::shared_ptr<const FontBlob> font, const GlyphKey& key,
                          std::shared_ptr<const GlyphBitmap>& out);
};

struct ShapedGlyph {
  uint32_t glyph_index = 0;
  uint32_t cluster = 0;
  float x = 0.0f;
  float y = 0.0f;
  float advance = 0.0f;
};

struct ShapedRun {
  lt_font_face* face = nullptr;
  std::vector<ShapedGlyph> glyphs;
  float font_size = 0.0f;
  uint16_t weight = 400;
  uint32_t style_flags = 0;
  uint32_t text_start = 0;
  uint32_t text_length = 0;
  bool right_to_left = false;
  bool color = false;
  bool synthetic_bold = false;
};

struct ClusterBox {
  uint32_t text_position = 0;
  uint32_t text_length = 0;
  float left = 0.0f;
  float width = 0.0f;
  bool right_to_left = false;
};

struct OwnedStyle {
  lt_font_cascade* cascade = nullptr;
  float font_size = 14.0f;
  float letter_spacing = 0.0f;
  uint16_t weight = 400;
  uint32_t flags = 0;
  std::vector<lt_open_type_feature> features;
};

}  // namespace lt

struct lt_context final : lt::Object {
  lt::ComPtr<IDWriteFactory> dwrite_factory;
  lt::ComPtr<IDWriteTextAnalyzer1> text_analyzer;
  lt_log_callback log_callback = nullptr;
  void* log_user_data = nullptr;
  lt_glyph_ready_callback glyph_ready_callback = nullptr;
  void* glyph_ready_user_data = nullptr;
  uint64_t cpu_cache_limit = 32ull * 1024ull * 1024ull;
  lt::FontBridge font_bridge;

  std::mutex glyph_mutex;
  std::unordered_map<lt::GlyphKey, lt::CachedGlyph, lt::GlyphKeyHash> glyph_cache;
  std::list<lt::GlyphKey> glyph_lru;
  uint64_t glyph_cache_bytes = 0;

  std::mutex memory_font_mutex;
  std::unordered_map<uint64_t, std::shared_ptr<const lt::FontBlob>> memory_fonts;
  std::mutex mapped_font_mutex;
  std::unordered_map<std::wstring, std::shared_ptr<lt::MappedFile>> mapped_fonts;

  void log(int32_t level, const char* message) const noexcept;
  lt_result get_mapped_file(const wchar_t* path, std::shared_ptr<lt::MappedFile>& out);
  lt_result get_glyph(IDWriteFontFace* face, const lt::GlyphKey& key,
                      std::shared_ptr<const lt::GlyphBitmap>& out, bool* cache_hit = nullptr);
  lt_result get_glyph(std::shared_ptr<const lt::FontBlob> font, const lt::GlyphKey& key,
                      std::shared_ptr<const lt::GlyphBitmap>& out, bool* cache_hit = nullptr);
};

struct lt_font_face final : lt::Object {
  lt_context* context = nullptr;
  std::shared_ptr<const lt::FontBlob> blob;
  std::wstring source_path;

  ~lt_font_face() override;
};

struct lt_font_cascade final : lt::Object {
  struct Entry {
    lt_font_face* face = nullptr;
    uint16_t weight = 400;
  };
  lt_context* context = nullptr;
  std::vector<Entry> entries;
  bool allow_system_fallback = false;
  std::wstring system_fallback_family;

  ~lt_font_cascade() override;
};

struct lt_render_profile final : lt::Object {
  lt_render_config light{};
  lt_render_config dark{};
  float regular_optical_weight = 0.0f;
  float bold_optical_weight = 0.0f;
  uint32_t flags = 0;
};

struct lt_glyph_image final : lt::Object {
  std::shared_ptr<const lt::GlyphBitmap> bitmap;
  float advance = 0.0f;
};

struct lt_text_layout final : lt::Object {
  // Share the process-wide identity allocator; cached images never retain a layout.
  uint64_t cache_identity = lt::next_font_identity();
  lt_context* context = nullptr;
  std::u16string text;
  std::vector<lt::ShapedRun> runs;
  std::vector<lt::ClusterBox> clusters;
  std::vector<lt_font_face*> retained_faces;
  lt_text_metrics metrics{};
  float alignment_offset = 0.0f;
  uint64_t shaping_time_us = 0;

  ~lt_text_layout() override;
};

enum class lt_renderer_kind { d2d };

struct lt_line_bitmap_key {
  uint64_t layout_identity = 0;
  float origin_x = 0, origin_y = 0, dpi_x = 0, dpi_y = 0;
  lt_color foreground{}, background{};
  lt_background_type background_type{};
  lt_render_config config{};
  float regular_optical_weight = 0, bold_optical_weight = 0;

  bool operator==(const lt_line_bitmap_key& other) const noexcept {
    const auto color_equal = [](const lt_color& a, const lt_color& b) {
      return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    return layout_identity == other.layout_identity && origin_x == other.origin_x &&
        origin_y == other.origin_y && dpi_x == other.dpi_x && dpi_y == other.dpi_y &&
        color_equal(foreground, other.foreground) && color_equal(background, other.background) &&
        background_type == other.background_type &&
        config.coverage_gamma == other.config.coverage_gamma &&
        config.coverage_contrast == other.config.coverage_contrast &&
        config.stem_strength == other.config.stem_strength &&
        config.flags == other.config.flags && config.raster_filter == other.config.raster_filter &&
        regular_optical_weight == other.regular_optical_weight &&
        bold_optical_weight == other.bold_optical_weight;
  }
};

struct lt_line_bitmap_entry {
  lt_line_bitmap_key key;
  lt::ComPtr<ID2D1Bitmap> bitmap;
  D2D1_RECT_F destination{};
  uint64_t bytes = 0;
};

struct lt_renderer final : lt::Object {
  lt_context* context = nullptr;
  lt_renderer_kind kind = lt_renderer_kind::d2d;
  lt::ComPtr<ID2D1RenderTarget> d2d_target;
  DWORD owner_thread = 0;
  bool manage_begin_end_draw = false;
  bool frame_active = false;
  std::unordered_map<const lt::GlyphBitmap*, lt::ComPtr<ID2D1Bitmap>> a8_bitmap_cache;
  std::unordered_map<const lt::GlyphBitmap*, std::shared_ptr<const lt::GlyphBitmap>>
      a8_bitmap_owners;
  float a8_cache_dpi_x = 0.0f;
  float a8_cache_dpi_y = 0.0f;
  static constexpr uint64_t line_cache_limit = 8ull * 1024 * 1024;
  static constexpr size_t line_cache_entries = 64;
  std::list<lt_line_bitmap_entry> line_bitmap_cache;
  uint64_t line_bitmap_bytes = 0;

  void clear_line_cache() noexcept {
    line_bitmap_cache.clear();
    line_bitmap_bytes = 0;
  }

  ~lt_renderer() override;
};

struct lt_frame final : lt::Object {
  lt_renderer* renderer = nullptr;
  float dpi_x = 96.0f;
  float dpi_y = 96.0f;
  bool ended = false;
  bool owns_draw = false;
  lt_frame_stats stats{};

  ~lt_frame() override;
};
