#include "internal.hpp"

#include <dwrite.h>

lt_result lt_draw_layout_d2d(lt_frame*, IDWriteTextLayout*, const lt_draw_text_desc&) noexcept;
lt_result lt_draw_text_layout_d2d(lt_frame*, const lt_text_layout*, const lt_draw_text_desc&) noexcept;

namespace {

constexpr size_t context_required = offsetof(lt_context_desc, dwrite_factory) + sizeof(IDWriteFactory*);
constexpr size_t d2d_required = offsetof(lt_d2d_desc, render_target) + sizeof(ID2D1RenderTarget*);
constexpr size_t frame_required = offsetof(lt_frame_desc, dpi_y) + sizeof(float);
constexpr size_t draw_required = offsetof(lt_draw_text_desc, foreground) + sizeof(lt_color);

bool owner_thread(const lt_renderer* renderer) noexcept {
  return renderer && renderer->owner_thread == GetCurrentThreadId();
}

lt_render_config default_render_config() noexcept {
  lt_render_config result{};
  result.struct_size = sizeof(result);
  result.abi_version = LT_ABI_VERSION;
  result.coverage_gamma = lt::kDefaultCoverageGamma;
  result.coverage_contrast = lt::kDefaultCoverageContrast;
  result.stem_strength = 0.00f;
  result.raster_filter = LT_RASTER_FILTER_MITCHELL;
  return result;
}

lt_result normalize_draw_desc(const lt_draw_text_desc* desc,
                              lt_draw_text_desc& local) noexcept {
  if (!lt::descriptor_valid(desc, draw_required)) return LT_E_INVALID_ARGUMENT;
  const size_t copy_size = std::min<size_t>(desc->struct_size, sizeof(local));
  memcpy(&local, desc, copy_size);
  local.struct_size = sizeof(local);
  if (!lt::descriptor_valid(&local.render_config,
                            offsetof(lt_render_config, coverage_gamma) + sizeof(float))) {
    local.render_config = default_render_config();
  }
  auto normalize_color = [](lt_color& color, float default_alpha) {
    color.r = std::clamp(lt::finite_or(color.r, 0.0f), 0.0f, 1.0f);
    color.g = std::clamp(lt::finite_or(color.g, 0.0f), 0.0f, 1.0f);
    color.b = std::clamp(lt::finite_or(color.b, 0.0f), 0.0f, 1.0f);
    color.a = std::clamp(lt::finite_or(color.a, default_alpha), 0.0f, 1.0f);
  };
  normalize_color(local.foreground, 1.0f);
  normalize_color(local.background, 1.0f);
  if (local.background_type != LT_BACKGROUND_TRANSPARENT &&
      local.background_type != LT_BACKGROUND_SOLID) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (local.profile && local.profile->magic != lt::kObjectMagic) {
    return LT_E_INVALID_ARGUMENT;
  }
  return LT_OK;
}

}  // namespace

lt_renderer::~lt_renderer() {
  if (context) context->release();
}

lt_frame::~lt_frame() {
  if (renderer) {
    if (!ended && owns_draw && owner_thread(renderer) &&
        renderer->d2d_target->EndDraw() == D2DERR_RECREATE_TARGET) {
      renderer->clear_line_cache();
      renderer->a8_bitmap_cache.clear();
      renderer->a8_bitmap_owners.clear();
      renderer->a8_cache_dpi_x = renderer->a8_cache_dpi_y = 0.0f;
    }
    renderer->frame_active = false;
    renderer->release();
  }
}

extern "C" {

uint32_t __cdecl lt_get_abi_version(void) { return LT_ABI_VERSION; }

const char* __cdecl lt_get_version_string(void) { return "0.1.0"; }

const char* __cdecl lt_result_string(lt_result result) {
  switch (result) {
    case LT_OK: return "success";
    case LT_E_INVALID_ARGUMENT: return "invalid argument or ABI descriptor";
    case LT_E_OUT_OF_MEMORY: return "out of memory";
    case LT_E_UNSUPPORTED: return "feature is not available in this release";
    case LT_E_WRONG_THREAD: return "renderer called from a thread other than its device thread";
    case LT_E_INVALID_STATE: return "object is not in the required state";
    case LT_E_DEVICE_LOST: return "render target must be recreated";
    case LT_E_FONT_UNAVAILABLE: return "font data or glyph is unavailable";
    default: return "internal error";
  }
}

lt_result __cdecl lt_context_create(const lt_context_desc* desc, lt_context** out_context) {
  if (!out_context) return LT_E_INVALID_ARGUMENT;
  *out_context = nullptr;
  if (!lt::descriptor_valid(desc, context_required)) return LT_E_INVALID_ARGUMENT;
  try {
    auto context = std::make_unique<lt_context>();
    if (desc->dwrite_factory) {
      context->dwrite_factory = desc->dwrite_factory;
    } else {
      HRESULT hr = DWriteCreateFactory(
          DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
          reinterpret_cast<IUnknown**>(context->dwrite_factory.GetAddressOf()));
      if (FAILED(hr)) return LT_E_INTERNAL;
    }
    lt::ComPtr<IDWriteTextAnalyzer> text_analyzer;
    if (FAILED(context->dwrite_factory->CreateTextAnalyzer(&text_analyzer)) ||
        FAILED(text_analyzer.As(&context->text_analyzer))) {
      return LT_E_INTERNAL;
    }
    if (desc->struct_size >= offsetof(lt_context_desc, log_user_data) + sizeof(void*)) {
      context->log_callback = desc->log_callback;
      context->log_user_data = desc->log_user_data;
    }
    if (desc->struct_size >= offsetof(lt_context_desc, glyph_ready_user_data) + sizeof(void*)) {
      context->glyph_ready_callback = desc->glyph_ready_callback;
      context->glyph_ready_user_data = desc->glyph_ready_user_data;
    }
    if (desc->struct_size >= offsetof(lt_context_desc, cpu_cache_limit_bytes) + sizeof(uint64_t) &&
        desc->cpu_cache_limit_bytes != 0) {
      context->cpu_cache_limit = std::max<uint64_t>(desc->cpu_cache_limit_bytes, 1024 * 1024);
    }
    *out_context = context.release();
    return LT_OK;
  } catch (const std::bad_alloc&) {
    return LT_E_OUT_OF_MEMORY;
  } catch (...) {
    return LT_E_INTERNAL;
  }
}

lt_result __cdecl lt_d2d_renderer_create(lt_context* context, const lt_d2d_desc* desc,
                                         lt_renderer** out_renderer) {
  if (!out_renderer) return LT_E_INVALID_ARGUMENT;
  *out_renderer = nullptr;
  if (!context || context->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(desc, d2d_required) || !desc->render_target) {
    return LT_E_INVALID_ARGUMENT;
  }
  auto renderer = std::unique_ptr<lt_renderer>(new (std::nothrow) lt_renderer());
  if (!renderer) return LT_E_OUT_OF_MEMORY;
  renderer->context = context;
  context->retain();
  renderer->d2d_target = desc->render_target;
  renderer->a8_bitmap_cache.clear();
  renderer->a8_bitmap_owners.clear();
  renderer->a8_cache_dpi_x = renderer->a8_cache_dpi_y = 0.0f;
  renderer->owner_thread = GetCurrentThreadId();
  if (desc->struct_size >= offsetof(lt_d2d_desc, manage_begin_end_draw) + sizeof(bool)) {
    renderer->manage_begin_end_draw = desc->manage_begin_end_draw;
  }
  *out_renderer = renderer.release();
  return LT_OK;
}

lt_result __cdecl lt_d2d_renderer_set_target(lt_renderer* renderer,
                                            ID2D1RenderTarget* target) {
  if (!renderer || renderer->magic != lt::kObjectMagic || !target) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (!owner_thread(renderer)) return LT_E_WRONG_THREAD;
  if (renderer->frame_active) return LT_E_INVALID_STATE;
  renderer->d2d_target = target;
  renderer->clear_line_cache();
  renderer->a8_bitmap_cache.clear();
  renderer->a8_bitmap_owners.clear();
  renderer->a8_cache_dpi_x = renderer->a8_cache_dpi_y = 0.0f;
  return LT_OK;
}

lt_result __cdecl lt_d3d11_renderer_create(lt_context*, const lt_d3d11_desc*, lt_renderer** out_renderer) {
  if (out_renderer) *out_renderer = nullptr;
  return LT_E_UNSUPPORTED;
}

lt_result __cdecl lt_frame_begin(lt_renderer* renderer, const lt_frame_desc* desc,
                                 lt_frame** out_frame) {
  if (!out_frame) return LT_E_INVALID_ARGUMENT;
  *out_frame = nullptr;
  if (!renderer || renderer->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(desc, frame_required)) return LT_E_INVALID_ARGUMENT;
  if (!owner_thread(renderer)) return LT_E_WRONG_THREAD;
  if (renderer->frame_active) return LT_E_INVALID_STATE;

  auto frame = std::unique_ptr<lt_frame>(new (std::nothrow) lt_frame());
  if (!frame) return LT_E_OUT_OF_MEMORY;
  frame->renderer = renderer;
  renderer->retain();
  frame->dpi_x = std::max(1.0f, lt::finite_or(desc->dpi_x, 96.0f));
  frame->dpi_y = std::max(1.0f, lt::finite_or(desc->dpi_y, 96.0f));
  if (renderer->a8_cache_dpi_x != frame->dpi_x ||
      renderer->a8_cache_dpi_y != frame->dpi_y) {
    renderer->clear_line_cache();
    renderer->a8_bitmap_cache.clear();
    renderer->a8_bitmap_owners.clear();
    renderer->a8_cache_dpi_x = frame->dpi_x;
    renderer->a8_cache_dpi_y = frame->dpi_y;
  }
  frame->stats.struct_size = sizeof(frame->stats);
  frame->stats.abi_version = LT_ABI_VERSION;
  renderer->frame_active = true;
  if (renderer->manage_begin_end_draw) {
    renderer->d2d_target->BeginDraw();
    frame->owns_draw = true;
  }
  *out_frame = frame.release();
  return LT_OK;
}

lt_result __cdecl lt_frame_draw_layout(lt_frame* frame, IDWriteTextLayout* layout,
                                       const lt_draw_text_desc* desc) {
  if (!frame || frame->magic != lt::kObjectMagic || !layout ||
      !lt::descriptor_valid(desc, draw_required)) return LT_E_INVALID_ARGUMENT;
  if (!owner_thread(frame->renderer)) return LT_E_WRONG_THREAD;
  if (frame->ended) return LT_E_INVALID_STATE;
  try {
    lt_draw_text_desc local{};
    const lt_result normalized = normalize_draw_desc(desc, local);
    if (normalized != LT_OK) return normalized;
    return lt_draw_layout_d2d(frame, layout, local);
  } catch (const std::bad_alloc&) {
    return LT_E_OUT_OF_MEMORY;
  } catch (...) {
    return LT_E_INTERNAL;
  }
}

lt_result __cdecl lt_frame_draw_text_layout(lt_frame* frame,
                                            const lt_text_layout* layout,
                                            const lt_draw_text_desc* desc) {
  if (!frame || frame->magic != lt::kObjectMagic || !layout ||
      layout->magic != lt::kObjectMagic || layout->context != frame->renderer->context) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (!owner_thread(frame->renderer)) return LT_E_WRONG_THREAD;
  if (frame->ended) return LT_E_INVALID_STATE;
  lt_draw_text_desc local{};
  const lt_result normalized = normalize_draw_desc(desc, local);
  if (normalized != LT_OK) return normalized;
  return lt_draw_text_layout_d2d(frame, layout, local);
}

lt_result __cdecl lt_frame_get_stats(const lt_frame* frame,
                                     lt_frame_stats* out_stats) {
  constexpr size_t required =
      offsetof(lt_frame_stats, compatibility_fallback_runs) + sizeof(uint32_t);
  if (!frame || frame->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(out_stats, required)) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (!owner_thread(frame->renderer)) return LT_E_WRONG_THREAD;
  const uint32_t caller_size = out_stats->struct_size;
  memcpy(out_stats, &frame->stats,
         std::min<size_t>(caller_size, sizeof(frame->stats)));
  out_stats->struct_size = caller_size;
  return LT_OK;
}

lt_result __cdecl lt_frame_flush(lt_frame* frame) {
  if (!frame || frame->magic != lt::kObjectMagic) return LT_E_INVALID_ARGUMENT;
  if (!owner_thread(frame->renderer)) return LT_E_WRONG_THREAD;
  if (frame->ended) return LT_E_INVALID_STATE;
  HRESULT hr = frame->renderer->d2d_target->Flush();
  if (hr == D2DERR_RECREATE_TARGET) {
    frame->renderer->clear_line_cache();
    frame->renderer->a8_bitmap_cache.clear();
    frame->renderer->a8_bitmap_owners.clear();
    frame->renderer->a8_cache_dpi_x = frame->renderer->a8_cache_dpi_y = 0.0f;
    return LT_E_DEVICE_LOST;
  }
  return SUCCEEDED(hr) ? LT_OK : LT_E_INTERNAL;
}

lt_result __cdecl lt_frame_end(lt_frame* frame) {
  if (!frame || frame->magic != lt::kObjectMagic) return LT_E_INVALID_ARGUMENT;
  if (!owner_thread(frame->renderer)) return LT_E_WRONG_THREAD;
  if (frame->ended) return LT_E_INVALID_STATE;
  frame->ended = true;
  frame->renderer->frame_active = false;
  if (!frame->owns_draw) return LT_OK;
  HRESULT hr = frame->renderer->d2d_target->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    frame->renderer->clear_line_cache();
    frame->renderer->a8_bitmap_cache.clear();
    frame->renderer->a8_bitmap_owners.clear();
    frame->renderer->a8_cache_dpi_x = frame->renderer->a8_cache_dpi_y = 0.0f;
    return LT_E_DEVICE_LOST;
  }
  return SUCCEEDED(hr) ? LT_OK : LT_E_INTERNAL;
}

lt_result __cdecl lt_input_create(const lt_input_desc*, lt_input** out_input) {
  if (out_input) *out_input = nullptr;
  return LT_E_UNSUPPORTED;
}

bool __cdecl lt_input_handle_message(lt_input*, HWND, UINT, WPARAM, LPARAM) { return false; }

lt_result __cdecl lt_input_get_visual_state(lt_input*, lt_input_visual_state*) {
  return LT_E_UNSUPPORTED;
}

void __cdecl lt_release(void* object) {
  if (!object) return;
  auto* value = static_cast<lt::Object*>(object);
  if (value->magic == lt::kObjectMagic) value->release();
}

}  // extern "C"
