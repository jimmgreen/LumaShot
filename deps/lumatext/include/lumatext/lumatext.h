#ifndef LUMATEXT_LUMATEXT_H
#define LUMATEXT_LUMATEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(LUMATEXT_STATIC)
#define LT_API
#elif defined(LUMATEXT_BUILD_DLL)
#define LT_API __declspec(dllexport)
#else
#define LT_API __declspec(dllimport)
#endif

#define LT_ABI_VERSION 1u
#define LT_STRUCT_HEADER uint32_t struct_size; uint32_t abi_version

typedef int32_t lt_result;

enum {
  LT_OK = 0,
  LT_E_INVALID_ARGUMENT = -1,
  LT_E_OUT_OF_MEMORY = -2,
  LT_E_UNSUPPORTED = -3,
  LT_E_WRONG_THREAD = -4,
  LT_E_INVALID_STATE = -5,
  LT_E_DEVICE_LOST = -6,
  LT_E_FONT_UNAVAILABLE = -7,
  LT_E_INTERNAL = -8
};

typedef struct lt_context lt_context;
typedef struct lt_renderer lt_renderer;
typedef struct lt_frame lt_frame;
typedef struct lt_input lt_input;
typedef struct lt_font_face lt_font_face;
typedef struct lt_font_cascade lt_font_cascade;
typedef struct lt_text_layout lt_text_layout;
typedef struct lt_render_profile lt_render_profile;
typedef struct lt_glyph_image lt_glyph_image;

typedef struct IDWriteFactory IDWriteFactory;
typedef struct IDWriteTextLayout IDWriteTextLayout;
typedef struct ID2D1RenderTarget ID2D1RenderTarget;
typedef struct ID3D11Device ID3D11Device;
typedef struct ID3D11DeviceContext ID3D11DeviceContext;

typedef void(__cdecl* lt_log_callback)(void* user_data, int32_t level, const char* message);
typedef void(__cdecl* lt_glyph_ready_callback)(void* user_data);

typedef struct lt_context_desc {
  LT_STRUCT_HEADER;
  IDWriteFactory* dwrite_factory;
  lt_log_callback log_callback;
  void* log_user_data;
  lt_glyph_ready_callback glyph_ready_callback;
  void* glyph_ready_user_data;
  uint64_t cpu_cache_limit_bytes;
  uint32_t raster_threads;
  uint32_t reserved;
} lt_context_desc;

typedef struct lt_render_config {
  LT_STRUCT_HEADER;
  float coverage_gamma;
  float coverage_contrast;
  float stem_strength;
  uint32_t flags;
  uint8_t raster_filter;
  uint8_t reserved[3];
} lt_render_config;

enum {
  LT_RASTER_FILTER_DEFAULT = 0,
  LT_RASTER_FILTER_DIRECT = 1,
  LT_RASTER_FILTER_BOX = 2,
  LT_RASTER_FILTER_MITCHELL = 3
};

enum {
  LT_RENDER_CONFIG_LINEAR_BLEND = 1u << 0,
  LT_RENDER_CONFIG_DISABLE_STEM_COMPENSATION = 1u << 1,
  LT_RENDER_CONFIG_HINTED_OUTLINES = 1u << 2,
  // Transparent text over a caller-declared uniform, opaque background color.
  // Precomposes in linear light; invalid when background.a != 1.
  LT_RENDER_CONFIG_KNOWN_BACKGROUND = 1u << 3
};

typedef enum lt_font_source_type {
  LT_FONT_SOURCE_FILE = 0,
  LT_FONT_SOURCE_MEMORY = 1
} lt_font_source_type;

typedef struct lt_font_axis {
  uint32_t tag;
  float value;
} lt_font_axis;

typedef struct lt_font_source_desc {
  LT_STRUCT_HEADER;
  lt_font_source_type source_type;
  uint32_t face_index;
  const wchar_t* file_path;
  const void* memory_data;
  uint64_t memory_size;
  const lt_font_axis* axes;
  uint32_t axis_count;
  uint32_t flags;
} lt_font_source_desc;

typedef struct lt_font_cascade_entry {
  lt_font_face* face;
  uint16_t weight;
  uint16_t reserved;
} lt_font_cascade_entry;

typedef struct lt_font_cascade_desc {
  LT_STRUCT_HEADER;
  const lt_font_cascade_entry* entries;
  uint32_t entry_count;
  bool allow_system_fallback;
  uint8_t reserved[3];
  const wchar_t* system_fallback_family;
} lt_font_cascade_desc;

typedef struct lt_open_type_feature {
  uint32_t tag;
  uint32_t value;
} lt_open_type_feature;

enum {
  LT_TEXT_STYLE_ALLOW_SYNTHETIC_BOLD = 1u << 0
};

typedef struct lt_text_style {
  LT_STRUCT_HEADER;
  lt_font_cascade* cascade;
  float font_size;
  uint16_t weight;
  uint16_t reserved;
  float letter_spacing;
  const lt_open_type_feature* features;
  uint32_t feature_count;
  uint32_t flags;
} lt_text_style;

typedef struct lt_text_style_span {
  uint32_t text_position;
  uint32_t text_length;
  lt_text_style style;
} lt_text_style_span;

typedef enum lt_text_direction {
  LT_TEXT_DIRECTION_AUTO = 0,
  LT_TEXT_DIRECTION_LTR = 1,
  LT_TEXT_DIRECTION_RTL = 2
} lt_text_direction;

typedef enum lt_text_alignment {
  LT_TEXT_ALIGNMENT_START = 0,
  LT_TEXT_ALIGNMENT_CENTER = 1,
  LT_TEXT_ALIGNMENT_END = 2
} lt_text_alignment;

typedef enum lt_text_ellipsis {
  LT_TEXT_ELLIPSIS_NONE = 0,
  LT_TEXT_ELLIPSIS_START = 1,
  LT_TEXT_ELLIPSIS_MIDDLE = 2,
  LT_TEXT_ELLIPSIS_END = 3
} lt_text_ellipsis;

typedef struct lt_text_layout_desc {
  LT_STRUCT_HEADER;
  const wchar_t* text;
  uint32_t text_length;
  lt_text_style base_style;
  const lt_text_style_span* style_spans;
  uint32_t style_span_count;
  const char* locale;
  lt_text_direction direction;
  lt_text_alignment alignment;
  lt_text_ellipsis ellipsis;
  float max_width;
} lt_text_layout_desc;

typedef struct lt_text_metrics {
  LT_STRUCT_HEADER;
  float width;
  float height;
  float ascent;
  float descent;
  float leading;
  uint32_t line_count;
  uint32_t glyph_count;
  uint32_t run_count;
  uint32_t text_length;
} lt_text_metrics;

typedef struct lt_hit_test_metrics {
  LT_STRUCT_HEADER;
  uint32_t text_position;
  uint32_t text_length;
  float left;
  float top;
  float width;
  float height;
  bool is_trailing;
  bool is_inside;
  bool is_right_to_left;
  uint8_t reserved;
} lt_hit_test_metrics;

typedef struct lt_render_profile_desc {
  LT_STRUCT_HEADER;
  lt_render_config light;
  lt_render_config dark;
  // Additional horizontal outline weight in physical pixels [0, 1].
  // Applied independently of the coverage curve; does not change glyph advances.
  float regular_optical_weight;
  float bold_optical_weight;
  uint32_t flags;
  uint32_t reserved;
} lt_render_profile_desc;

#ifdef __cplusplus
struct IDWriteFontFace;
#else
typedef struct IDWriteFontFace IDWriteFontFace;
#endif

typedef struct lt_glyph_request {
  LT_STRUCT_HEADER;
  const void* font_bytes;
  uint64_t font_size;
  uint32_t face_index;
  IDWriteFontFace* dwrite_face;
  uint16_t glyph_id;
  uint16_t synth_weight;
  float px_em;
  float dpi_x;
  float dpi_y;
  uint8_t x_phase;
  // Eighth-pixel baseline phase, increasing downwards in render-target coordinates.
  uint8_t y_phase;
  uint8_t reserved[2];
  lt_render_config cfg;
  /* Optional preloaded face from the same context; takes precedence over other sources. */
  lt_font_face* font_face;
} lt_glyph_request;

typedef struct lt_glyph_bitmap {
  LT_STRUCT_HEADER;
  const uint8_t* a8;
  int32_t width;
  int32_t height;
  int32_t left;
  int32_t top;
  float advance;
} lt_glyph_bitmap;

typedef struct lt_d2d_desc {
  LT_STRUCT_HEADER;
  ID2D1RenderTarget* render_target;
  bool manage_begin_end_draw;
  uint8_t reserved[7];
} lt_d2d_desc;

typedef struct lt_d3d11_desc {
  LT_STRUCT_HEADER;
  ID3D11Device* device;
  ID3D11DeviceContext* device_context;
  uint64_t atlas_limit_bytes;
} lt_d3d11_desc;

typedef struct lt_frame_desc {
  LT_STRUCT_HEADER;
  float dpi_x;
  float dpi_y;
  uint32_t flags;
  uint32_t reserved;
} lt_frame_desc;

typedef struct lt_rect {
  float left;
  float top;
  float right;
  float bottom;
} lt_rect;

typedef struct lt_color {
  float r;
  float g;
  float b;
  float a;
} lt_color;

typedef enum lt_background_type {
  LT_BACKGROUND_TRANSPARENT = 0,
  LT_BACKGROUND_SOLID = 1
} lt_background_type;

typedef struct lt_draw_text_desc {
  LT_STRUCT_HEADER;
  float origin_x;
  float origin_y;
  lt_rect clip;
  lt_color foreground;
  lt_color background;
  lt_background_type background_type;
  bool clip_enabled;
  uint8_t reserved[3];
  lt_render_config render_config;
  lt_render_profile* profile;
} lt_draw_text_desc;

typedef struct lt_frame_stats {
  LT_STRUCT_HEADER;
  uint32_t harfbuzz_runs;
  uint32_t freetype_glyphs;
  uint32_t directwrite_color_runs;
  uint32_t compatibility_fallback_runs;
  uint32_t glyph_cache_hits;
  uint32_t glyph_cache_misses;
  uint64_t shaping_time_us;
  uint64_t raster_time_us;
  uint64_t composition_time_us;
  uint64_t upload_time_us;
  uint32_t line_cache_hits;
} lt_frame_stats;

typedef struct lt_input_desc {
  LT_STRUCT_HEADER;
  HWND hwnd;
  IDWriteFactory* dwrite_factory;
  uint32_t flags;
  uint32_t reserved;
} lt_input_desc;

typedef struct lt_input_visual_state {
  LT_STRUCT_HEADER;
  const wchar_t* text;
  uint32_t text_length;
  uint32_t selection_start;
  uint32_t selection_length;
  uint32_t composition_start;
  uint32_t composition_length;
} lt_input_visual_state;

LT_API uint32_t __cdecl lt_get_abi_version(void);
LT_API const char* __cdecl lt_get_version_string(void);
LT_API const char* __cdecl lt_result_string(lt_result result);

LT_API lt_result __cdecl lt_context_create(const lt_context_desc* desc, lt_context** out_context);
LT_API lt_result __cdecl lt_font_face_create(lt_context* context, const lt_font_source_desc* desc, lt_font_face** out_face);
LT_API lt_result __cdecl lt_font_cascade_create(lt_context* context, const lt_font_cascade_desc* desc, lt_font_cascade** out_cascade);
LT_API lt_result __cdecl lt_render_profile_create(const lt_render_profile_desc* desc, lt_render_profile** out_profile);
LT_API lt_result __cdecl lt_glyph_provider_get(lt_context* context, const lt_glyph_request* request, lt_glyph_image** out_image);
LT_API lt_result __cdecl lt_glyph_image_describe(const lt_glyph_image* image, lt_glyph_bitmap* out_bitmap);
LT_API lt_result __cdecl lt_text_layout_create(lt_context* context, const lt_text_layout_desc* desc, lt_text_layout** out_layout);
LT_API lt_result __cdecl lt_text_layout_get_metrics(const lt_text_layout* layout, lt_text_metrics* out_metrics);
LT_API lt_result __cdecl lt_text_layout_hit_test_point(const lt_text_layout* layout, float x, float y, lt_hit_test_metrics* out_metrics);
LT_API lt_result __cdecl lt_text_layout_hit_test_position(const lt_text_layout* layout, uint32_t text_position, bool trailing, float* out_x, float* out_y, lt_hit_test_metrics* out_metrics);
LT_API lt_result __cdecl lt_d2d_renderer_create(lt_context* context, const lt_d2d_desc* desc, lt_renderer** out_renderer);
// Call on the renderer's owner thread, outside a frame; invalidates device caches.
LT_API lt_result __cdecl lt_d2d_renderer_set_target(lt_renderer* renderer, ID2D1RenderTarget* target);
LT_API lt_result __cdecl lt_d3d11_renderer_create(lt_context* context, const lt_d3d11_desc* desc, lt_renderer** out_renderer);
LT_API lt_result __cdecl lt_frame_begin(lt_renderer* renderer, const lt_frame_desc* desc, lt_frame** out_frame);
LT_API lt_result __cdecl lt_frame_draw_layout(lt_frame* frame, IDWriteTextLayout* layout, const lt_draw_text_desc* desc);
LT_API lt_result __cdecl lt_frame_draw_text_layout(lt_frame* frame, const lt_text_layout* layout, const lt_draw_text_desc* desc);
LT_API lt_result __cdecl lt_frame_get_stats(const lt_frame* frame, lt_frame_stats* out_stats);
LT_API lt_result __cdecl lt_frame_flush(lt_frame* frame);
LT_API lt_result __cdecl lt_frame_end(lt_frame* frame);
LT_API lt_result __cdecl lt_input_create(const lt_input_desc* desc, lt_input** out_input);
LT_API bool __cdecl lt_input_handle_message(lt_input* input, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
LT_API lt_result __cdecl lt_input_get_visual_state(lt_input* input, lt_input_visual_state* state);
LT_API void __cdecl lt_release(void* object);

#ifdef __cplusplus
}
#endif

#endif
