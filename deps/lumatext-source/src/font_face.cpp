#include "internal.hpp"

#include <windows.h>

namespace {

constexpr size_t font_source_required =
    offsetof(lt_font_source_desc, memory_size) + sizeof(uint64_t);
constexpr size_t cascade_required =
    offsetof(lt_font_cascade_desc, entry_count) + sizeof(uint32_t);
constexpr size_t profile_required =
    offsetof(lt_render_profile_desc, dark) + sizeof(lt_render_config);

lt_result map_font_file(const wchar_t* path, std::shared_ptr<lt::MappedFile>& out) {
  if (!path || !*path) return LT_E_INVALID_ARGUMENT;
  auto mapped = std::make_shared<lt::MappedFile>();
  mapped->file = CreateFileW(path, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (mapped->file == INVALID_HANDLE_VALUE) return LT_E_FONT_UNAVAILABLE;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(mapped->file, &size) || size.QuadPart <= 0 ||
      static_cast<uint64_t>(size.QuadPart) > static_cast<uint64_t>(SIZE_MAX)) {
    return LT_E_FONT_UNAVAILABLE;
  }
  mapped->size = static_cast<size_t>(size.QuadPart);
  mapped->mapping = CreateFileMappingW(mapped->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!mapped->mapping) return LT_E_FONT_UNAVAILABLE;
  mapped->view = static_cast<const uint8_t*>(
      MapViewOfFile(mapped->mapping, FILE_MAP_READ, 0, 0, 0));
  if (!mapped->view) return LT_E_OUT_OF_MEMORY;
  out = std::move(mapped);
  return LT_OK;
}

lt_render_config normalized_config(const lt_render_config& input) noexcept {
  lt_render_config result{};
  result.struct_size = sizeof(result);
  result.abi_version = LT_ABI_VERSION;
  result.coverage_gamma = std::clamp(lt::finite_or(input.coverage_gamma, 1.0f), 0.25f, 3.0f);
  result.coverage_contrast = std::clamp(lt::finite_or(input.coverage_contrast, 1.0f), 0.25f, 3.0f);
  result.stem_strength = std::clamp(lt::finite_or(input.stem_strength, 0.0f), 0.0f, 1.0f);
  result.flags = input.flags;
  const uint8_t requested_filter = input.struct_size >=
      offsetof(lt_render_config, raster_filter) + sizeof(uint8_t)
      ? input.raster_filter : LT_RASTER_FILTER_DEFAULT;
  result.raster_filter = requested_filter == LT_RASTER_FILTER_DIRECT ||
      requested_filter == LT_RASTER_FILTER_BOX ||
      requested_filter == LT_RASTER_FILTER_MITCHELL
      ? requested_filter : LT_RASTER_FILTER_MITCHELL;
  return result;
}

}  // namespace

lt_result lt_context::get_mapped_file(const wchar_t* path,
                                      std::shared_ptr<lt::MappedFile>& out) {
  if (!path || !*path) return LT_E_INVALID_ARGUMENT;
  const std::wstring key(path);
  {
    std::lock_guard lock(mapped_font_mutex);
    if (const auto found = mapped_fonts.find(key); found != mapped_fonts.end()) {
      out = found->second;
      return LT_OK;
    }
  }
  std::shared_ptr<lt::MappedFile> mapped;
  const lt_result result = map_font_file(path, mapped);
  if (result != LT_OK) return result;
  std::lock_guard lock(mapped_font_mutex);
  const auto [position, inserted] = mapped_fonts.emplace(key, mapped);
  out = inserted ? std::move(mapped) : position->second;
  return LT_OK;
}

lt_font_face::~lt_font_face() {
  if (context) context->release();
}

lt_font_cascade::~lt_font_cascade() {
  for (auto& entry : entries) {
    if (entry.face) entry.face->release();
  }
  if (context) context->release();
}

lt_text_layout::~lt_text_layout() {
  for (lt_font_face* face : retained_faces) {
    if (face) face->release();
  }
  if (context) context->release();
}

extern "C" {

lt_result __cdecl lt_font_face_create(lt_context* context,
                                      const lt_font_source_desc* desc,
                                      lt_font_face** out_face) {
  if (!out_face) return LT_E_INVALID_ARGUMENT;
  *out_face = nullptr;
  if (!context || context->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(desc, font_source_required)) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (desc->source_type != LT_FONT_SOURCE_FILE &&
      desc->source_type != LT_FONT_SOURCE_MEMORY) {
    return LT_E_INVALID_ARGUMENT;
  }
  try {
    auto blob = std::make_shared<lt::FontBlob>();
    blob->identity = lt::next_font_identity();
    blob->face_index = desc->face_index;
    lt_result result = LT_OK;
    if (desc->source_type == LT_FONT_SOURCE_FILE) {
      result = context->get_mapped_file(desc->file_path, blob->mapping);
    } else {
      if (!desc->memory_data || desc->memory_size == 0 ||
          desc->memory_size > static_cast<uint64_t>(SIZE_MAX)) {
        return LT_E_INVALID_ARGUMENT;
      }
      const auto* begin = static_cast<const uint8_t*>(desc->memory_data);
      blob->owned.assign(begin, begin + static_cast<size_t>(desc->memory_size));
    }
    if (result != LT_OK) return result;
    if (blob->empty()) return LT_E_FONT_UNAVAILABLE;

    if (desc->struct_size >= offsetof(lt_font_source_desc, axis_count) + sizeof(uint32_t)) {
      if (desc->axis_count > 64 || (desc->axis_count != 0 && !desc->axes)) {
        return LT_E_INVALID_ARGUMENT;
      }
      blob->axes.reserve(desc->axis_count);
      for (uint32_t index = 0; index < desc->axis_count; ++index) {
        if (!std::isfinite(desc->axes[index].value)) return LT_E_INVALID_ARGUMENT;
        blob->axes.push_back({desc->axes[index].tag, desc->axes[index].value});
      }
    }

    FT_Library library = nullptr;
    FT_Face ft_face = nullptr;
    if (FT_Init_FreeType(&library) != 0 || !library ||
        FT_New_Memory_Face(library, blob->data(),
            static_cast<FT_Long>(blob->size()), blob->face_index, &ft_face) != 0 ||
        !ft_face) {
      if (ft_face) FT_Done_Face(ft_face);
      if (library) FT_Done_FreeType(library);
      return LT_E_FONT_UNAVAILABLE;
    }
    if (const auto* os2 = static_cast<const TT_OS2*>(
            FT_Get_Sfnt_Table(ft_face, ft_sfnt_os2))) {
      blob->weight = static_cast<uint16_t>(std::clamp<unsigned>(
          os2->usWeightClass ? os2->usWeightClass : 400, 1, 1000));
    } else if (ft_face->style_flags & FT_STYLE_FLAG_BOLD) {
      blob->weight = 700;
    }
    blob->has_color = FT_HAS_COLOR(ft_face) != 0;
    FT_Done_Face(ft_face);
    FT_Done_FreeType(library);

    auto face = std::make_unique<lt_font_face>();
    face->context = context;
    context->retain();
    face->blob = std::move(blob);
    if (desc->source_type == LT_FONT_SOURCE_FILE && desc->file_path) {
      face->source_path = desc->file_path;
    }
    *out_face = face.release();
    return LT_OK;
  } catch (const std::bad_alloc&) {
    return LT_E_OUT_OF_MEMORY;
  } catch (...) {
    return LT_E_INTERNAL;
  }
}

lt_result __cdecl lt_font_cascade_create(lt_context* context,
                                         const lt_font_cascade_desc* desc,
                                         lt_font_cascade** out_cascade) {
  if (!out_cascade) return LT_E_INVALID_ARGUMENT;
  *out_cascade = nullptr;
  if (!context || context->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(desc, cascade_required) || desc->entry_count == 0 ||
      !desc->entries || desc->entry_count > 256) {
    return LT_E_INVALID_ARGUMENT;
  }
  auto cascade = std::unique_ptr<lt_font_cascade>(new (std::nothrow) lt_font_cascade());
  if (!cascade) return LT_E_OUT_OF_MEMORY;
  try {
    cascade->context = context;
    context->retain();
    cascade->entries.reserve(desc->entry_count);
    for (uint32_t index = 0; index < desc->entry_count; ++index) {
      lt_font_face* face = desc->entries[index].face;
      if (!face || face->magic != lt::kObjectMagic || face->context != context) {
        return LT_E_INVALID_ARGUMENT;
      }
      face->retain();
      const uint16_t weight = static_cast<uint16_t>(std::clamp<unsigned>(
          desc->entries[index].weight ? desc->entries[index].weight : face->blob->weight,
          1, 1000));
      cascade->entries.push_back({face, weight});
    }
    if (desc->struct_size >= offsetof(lt_font_cascade_desc, allow_system_fallback) + sizeof(bool)) {
      cascade->allow_system_fallback = desc->allow_system_fallback;
    }
    if (desc->struct_size >= offsetof(lt_font_cascade_desc, system_fallback_family) +
        sizeof(const wchar_t*) && desc->system_fallback_family) {
      cascade->system_fallback_family = desc->system_fallback_family;
    }
    *out_cascade = cascade.release();
    return LT_OK;
  } catch (const std::bad_alloc&) {
    return LT_E_OUT_OF_MEMORY;
  } catch (...) {
    return LT_E_INTERNAL;
  }
}

lt_result __cdecl lt_render_profile_create(const lt_render_profile_desc* desc,
                                           lt_render_profile** out_profile) {
  if (!out_profile) return LT_E_INVALID_ARGUMENT;
  *out_profile = nullptr;
  if (!lt::descriptor_valid(desc, profile_required) ||
      !lt::descriptor_valid(&desc->light,
          offsetof(lt_render_config, coverage_gamma) + sizeof(float)) ||
      !lt::descriptor_valid(&desc->dark,
          offsetof(lt_render_config, coverage_gamma) + sizeof(float))) {
    return LT_E_INVALID_ARGUMENT;
  }
  auto profile = std::unique_ptr<lt_render_profile>(new (std::nothrow) lt_render_profile());
  if (!profile) return LT_E_OUT_OF_MEMORY;
  profile->light = normalized_config(desc->light);
  profile->dark = normalized_config(desc->dark);
  if (desc->struct_size >= offsetof(lt_render_profile_desc, regular_optical_weight) + sizeof(float))
    profile->regular_optical_weight = std::clamp(
        lt::finite_or(desc->regular_optical_weight, 0.0f), 0.0f, 1.0f);
  if (desc->struct_size >= offsetof(lt_render_profile_desc, bold_optical_weight) + sizeof(float))
    profile->bold_optical_weight = std::clamp(
        lt::finite_or(desc->bold_optical_weight, 0.0f), 0.0f, 1.0f);
  if (desc->struct_size >= offsetof(lt_render_profile_desc, flags) + sizeof(uint32_t))
    profile->flags = desc->flags;
  *out_profile = profile.release();
  return LT_OK;
}

}  // extern "C"
