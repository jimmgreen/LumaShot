#include "internal.hpp"
#include <array>

namespace {

struct ThreadFreeType {
  struct FaceEntry {
    FT_Face face = nullptr;
    std::shared_ptr<const lt::FontBlob> font;
  };
  FT_Library library = nullptr;
  std::unordered_map<uint64_t, FaceEntry> faces;

  ThreadFreeType() { FT_Init_FreeType(&library); }
  ~ThreadFreeType() {
    for (auto& [identity, entry] : faces) {
      (void)identity;
      FT_Done_Face(entry.face);
    }
    if (library) FT_Done_FreeType(library);
  }
};

thread_local ThreadFreeType thread_ft;

uint8_t calibrate(uint8_t coverage, const lt::GlyphKey& key) noexcept {
  if (coverage == 0 || coverage == 255) return coverage;
  const float gamma = std::max(0.25f, key.gamma_64 / 64.0f);
  const float contrast = std::max(0.25f, key.contrast_64 / 64.0f);
  float value = std::pow(coverage / 255.0f, gamma);
  value = (value - 0.5f) * contrast + 0.5f;
  return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

const std::array<uint8_t, 256>& coverage_table(const lt::GlyphKey& key) noexcept {
  thread_local std::array<uint8_t, 256> table{};
  thread_local int gamma = -1;
  thread_local int contrast = -1;
  if (gamma != key.gamma_64 || contrast != key.contrast_64) {
    for (unsigned value = 0; value < table.size(); ++value) {
      table[value] = calibrate(static_cast<uint8_t>(value), key);
    }
    gamma = key.gamma_64;
    contrast = key.contrast_64;
  }
  return table;
}

double mitchell(double value) noexcept {
  constexpr double b = 1.0 / 3.0;
  constexpr double c = 1.0 / 3.0;
  const double x = std::abs(value);
  if (x < 1.0) {
    return ((12.0 - 9.0 * b - 6.0 * c) * x * x * x +
            (-18.0 + 12.0 * b + 6.0 * c) * x * x + (6.0 - 2.0 * b)) / 6.0;
  }
  if (x < 2.0) {
    return ((-b - 6.0 * c) * x * x * x +
            (6.0 * b + 30.0 * c) * x * x +
            (-12.0 * b - 48.0 * c) * x + (8.0 * b + 24.0 * c)) / 6.0;
  }
  return 0.0;
}

double box_area(double value) noexcept {
  // Integrate one 4x source pixel over a destination pixel.
  const double overlap = std::max(0.0, std::min(0.5, value + 0.125) -
      std::max(-0.5, value - 0.125));
  return overlap / 0.25;
}

double filter_weight(double value, uint8_t filter) noexcept {
  return filter == LT_RASTER_FILTER_BOX ? box_area(value) : mitchell(value);
}

std::shared_ptr<lt::GlyphBitmap> direct_bitmap(const FT_GlyphSlot slot,
                                               const lt::GlyphKey& key) {
  const FT_Bitmap& source = slot->bitmap;
  constexpr int padding = 1;
  const auto& coverage = coverage_table(key);
  auto output = std::make_shared<lt::GlyphBitmap>();
  output->left = slot->bitmap_left - padding;
  output->top = slot->bitmap_top + padding;
  output->width = source.width + padding * 2;
  output->height = source.rows + padding * 2;
  output->pixels.assign(static_cast<size_t>(output->width) * output->height, 0);
  const int pitch = source.pitch;
  for (uint32_t row = 0; row < source.rows; ++row) {
    const uint8_t* source_row = pitch >= 0
        ? source.buffer + static_cast<size_t>(row) * pitch
        : source.buffer + static_cast<size_t>(source.rows - 1 - row) *
              static_cast<size_t>(-pitch);
    for (uint32_t column = 0; column < source.width; ++column) {
      output->pixels[static_cast<size_t>(row + padding) * output->width +
                     column + padding] = coverage[source_row[column]];
    }
  }
  return output;
}

std::shared_ptr<lt::GlyphBitmap> downsample(const FT_GlyphSlot slot,
                                            const lt::GlyphKey& key) {
  constexpr int scale = 4;
  constexpr int padding = 2;
  const FT_Bitmap& source = slot->bitmap;
  const int source_left = slot->bitmap_left;
  const int source_top = slot->bitmap_top;
  const int output_left = static_cast<int>(std::floor(source_left / 4.0)) - padding;
  const int output_right = static_cast<int>(std::ceil(
      (source_left + static_cast<int>(source.width)) / 4.0)) + padding;
  const int output_top = static_cast<int>(std::ceil(source_top / 4.0)) + padding;
  const int output_bottom = static_cast<int>(std::floor(
      (source_top - static_cast<int>(source.rows)) / 4.0)) - padding;

  auto output = std::make_shared<lt::GlyphBitmap>();
  output->left = output_left;
  output->top = output_top;
  output->width = static_cast<uint32_t>(std::max(0, output_right - output_left));
  output->height = static_cast<uint32_t>(std::max(0, output_top - output_bottom));
  output->pixels.resize(static_cast<size_t>(output->width) * output->height);
  if (source.width == 0 || source.rows == 0 || output->pixels.empty()) return output;

  std::vector<double> horizontal(static_cast<size_t>(source.rows) * output->width);
  const auto& coverage_lut = coverage_table(key);
  struct Kernel {
    int first = 0;
    int last = 0;
    std::array<double, 19> weights{};
    double total = 0.0;
  };
  // Every source row uses the same horizontal sampling positions.
  std::vector<Kernel> kernels(output->width);
  for (uint32_t column = 0; column < output->width; ++column) {
    auto& kernel = kernels[column];
    const double center = static_cast<double>(output_left) + column + 0.5;
    kernel.first = static_cast<int>(std::floor((center - 2.0) * scale - source_left - 0.5));
    kernel.last = static_cast<int>(std::ceil((center + 2.0) * scale - source_left - 0.5));
    for (int sample = kernel.first; sample <= kernel.last; ++sample) {
      const double weight = filter_weight(
          (source_left + sample + 0.5) / scale - center, key.raster_filter);
      kernel.weights[sample - kernel.first] = weight;
      kernel.total += weight;
    }
  }
  const int pitch = source.pitch;
  for (uint32_t row = 0; row < source.rows; ++row) {
    const uint8_t* source_row = pitch >= 0
        ? source.buffer + static_cast<size_t>(row) * pitch
        : source.buffer + static_cast<size_t>(source.rows - 1 - row) *
              static_cast<size_t>(-pitch);
    for (uint32_t column = 0; column < output->width; ++column) {
      const auto& kernel = kernels[column];
      double sum = 0.0;
      for (int sample = kernel.first; sample <= kernel.last; ++sample) {
        const double weight = kernel.weights[sample - kernel.first];
        if (sample >= 0 && sample < static_cast<int>(source.width)) {
          sum += weight * source_row[sample];
        }
      }
      horizontal[static_cast<size_t>(row) * output->width + column] =
          kernel.total != 0.0 ? sum / kernel.total : 0.0;
    }
  }

  for (uint32_t row = 0; row < output->height; ++row) {
    const double destination_center =
        static_cast<double>(output_top) - static_cast<double>(row) - 0.5;
    const int first = static_cast<int>(std::floor(
        source_top - (destination_center + 2.0) * scale - 0.5));
    const int last = static_cast<int>(std::ceil(
        source_top - (destination_center - 2.0) * scale - 0.5));
    std::array<double, 19> kernel{};
    double weights = 0.0;
    for (int sample = first; sample <= last; ++sample) {
      const double sample_center = source_top / 4.0 - (sample + 0.5) / scale;
      const double weight = filter_weight(sample_center - destination_center,
                                           key.raster_filter);
      kernel[sample - first] = weight;
      weights += weight;
    }
    for (uint32_t column = 0; column < output->width; ++column) {
      double sum = 0.0;
      for (int sample = first; sample <= last; ++sample) {
        const double weight = kernel[sample - first];
        if (sample >= 0 && sample < static_cast<int>(source.rows)) {
          sum += weight * horizontal[static_cast<size_t>(sample) * output->width + column];
        }
      }
      const double value = weights != 0.0 ? sum / weights : 0.0;
      const uint8_t coverage = static_cast<uint8_t>(std::lround(
          std::clamp(value, 0.0, 255.0)));
      output->pixels[static_cast<size_t>(row) * output->width + column] =
          coverage_lut[coverage];
    }
  }
  return output;
}

}  // namespace

lt_result lt::Rasterizer::render(std::shared_ptr<const FontBlob> font, const GlyphKey& key,
                                 std::shared_ptr<const GlyphBitmap>& out) {
  if (!thread_ft.library || !font || font->empty()) return LT_E_FONT_UNAVAILABLE;

  FT_Face face = nullptr;
  auto found = thread_ft.faces.find(font->identity);
  if (found == thread_ft.faces.end()) {
    FT_Error error = FT_New_Memory_Face(
        thread_ft.library, font->data(),
        static_cast<FT_Long>(font->size()), font->face_index, &face);
    if (error != 0 || !face) return LT_E_FONT_UNAVAILABLE;
    if (!font->axes.empty()) {
      FT_MM_Var* variation = nullptr;
      if (FT_Get_MM_Var(face, &variation) != 0 || !variation) {
        FT_Done_Face(face);
        return LT_E_FONT_UNAVAILABLE;
      }
      std::vector<FT_Fixed> coordinates(variation->num_axis);
      for (FT_UInt index = 0; index < variation->num_axis; ++index) {
        coordinates[index] = variation->axis[index].def;
        const auto axis = std::find_if(font->axes.begin(), font->axes.end(),
            [&](const FontBlob::AxisValue& value) {
              return value.tag == variation->axis[index].tag;
            });
        if (axis != font->axes.end()) {
          coordinates[index] = static_cast<FT_Fixed>(
              std::llround(axis->value * 65536.0));
        }
      }
      const FT_Error variation_error = FT_Set_Var_Design_Coordinates(
          face, variation->num_axis, coordinates.data());
      FT_Done_MM_Var(thread_ft.library, variation);
      if (variation_error != 0) {
        FT_Done_Face(face);
        return LT_E_FONT_UNAVAILABLE;
      }
    }
    const uint64_t font_identity = font->identity;
    thread_ft.faces.emplace(
        font_identity, ThreadFreeType::FaceEntry{face, std::move(font)});
  } else {
    face = found->second.face;
  }

  const bool direct = key.raster_filter == LT_RASTER_FILTER_DIRECT;
  const FT_F26Dot6 point_size = static_cast<FT_F26Dot6>(
      std::lround((key.em_size_26_6 / 64.0) * 48.0));
  if (FT_Set_Char_Size(face, 0, point_size,
                       static_cast<FT_UInt>(key.dpi_x) * (direct ? 1 : 4),
                       static_cast<FT_UInt>(key.dpi_y) * (direct ? 1 : 4)) != 0) {
    return LT_E_FONT_UNAVAILABLE;
  }

  FT_Matrix matrix{1L << 16, 0, 0, 1L << 16};
  const FT_Pos phase_scale = direct ? 8 : 32;
  FT_Vector delta{static_cast<FT_Pos>(key.x_phase) * phase_scale,
                  -static_cast<FT_Pos>(key.y_phase) * phase_scale};
  FT_Set_Transform(face, &matrix, &delta);
  FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;
  if (!key.hinted) load_flags |= FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT;
  if (FT_Load_Glyph(face, key.glyph_index, load_flags) != 0) {
    FT_Set_Transform(face, nullptr, nullptr);
    return LT_E_FONT_UNAVAILABLE;
  }

  const FT_Pos legacy_strength = (face->style_flags & FT_STYLE_FLAG_BOLD)
      ? 0 : key.stem_64 + key.synthetic_64;
  if ((legacy_strength != 0 || key.optical_64 != 0) &&
      face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
    const FT_Pos strength = (legacy_strength + key.optical_64) * (direct ? 1 : 4);
    FT_Outline_EmboldenXY(&face->glyph->outline, strength, 0);
  }
  if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
    FT_Set_Transform(face, nullptr, nullptr);
    return LT_E_FONT_UNAVAILABLE;
  }
  FT_Set_Transform(face, nullptr, nullptr);

  const FT_Bitmap& source = face->glyph->bitmap;
  if (source.pixel_mode != FT_PIXEL_MODE_GRAY && source.width != 0 && source.rows != 0) {
    return LT_E_UNSUPPORTED;
  }

  try {
    auto rendered = direct ? direct_bitmap(face->glyph, key)
                           : downsample(face->glyph, key);
    rendered->advance = static_cast<float>(face->glyph->metrics.horiAdvance) /
        (direct ? 64.0f : 256.0f);
    out = std::move(rendered);
  } catch (...) {
    return LT_E_OUT_OF_MEMORY;
  }
  return LT_OK;
}
