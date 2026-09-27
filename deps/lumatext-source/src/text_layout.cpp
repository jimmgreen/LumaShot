#include "internal.hpp"

#include <map>

namespace {

constexpr size_t layout_required =
    offsetof(lt_text_layout_desc, max_width) + sizeof(float);
constexpr size_t style_required =
    offsetof(lt_text_style, font_size) + sizeof(float);
constexpr size_t metrics_required =
    offsetof(lt_text_metrics, height) + sizeof(float);
constexpr size_t hit_required =
    offsetof(lt_hit_test_metrics, text_position) + sizeof(uint32_t);

struct HbFaceCache {
  std::unordered_map<lt_font_face*, hb_face_t*> faces;

  ~HbFaceCache() {
    for (auto& [key, face] : faces) {
      (void)key;
      hb_face_destroy(face);
    }
  }

  hb_face_t* get(lt_font_face* source) {
    auto found = faces.find(source);
    if (found != faces.end()) return found->second;
    if (!source->blob || source->blob->empty()) return nullptr;
    hb_blob_t* blob = hb_blob_create(
        reinterpret_cast<const char*>(source->blob->data()),
        static_cast<unsigned>(source->blob->size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    if (!blob) return nullptr;
    hb_face_t* face = hb_face_create(blob, source->blob->face_index);
    hb_blob_destroy(blob);
    if (!face || hb_face_get_glyph_count(face) == 0) {
      if (face) hb_face_destroy(face);
      return nullptr;
    }
    faces.emplace(source, face);
    return face;
  }

  hb_font_t* make_font(lt_font_face* source, float size) {
    hb_face_t* face = get(source);
    if (!face) return nullptr;
    hb_font_t* font = hb_font_create(face);
    if (!font) return nullptr;
    hb_ot_font_set_funcs(font);
    const int scale = std::max(1, static_cast<int>(std::lround(size * 64.0f)));
    hb_font_set_scale(font, scale, scale);
    if (!source->blob->axes.empty()) {
      std::vector<hb_variation_t> variations;
      variations.reserve(source->blob->axes.size());
      for (const auto& axis : source->blob->axes) {
        variations.push_back({static_cast<hb_tag_t>(axis.tag), axis.value});
      }
      hb_font_set_variations(font, variations.data(),
                             static_cast<unsigned>(variations.size()));
    }
    return font;
  }
};

class TextAnalysis final : public IDWriteTextAnalysisSource,
                           public IDWriteTextAnalysisSink {
 public:
  TextAnalysis(IDWriteTextAnalyzer1* analyzer, const char16_t* text, uint32_t length,
               DWRITE_READING_DIRECTION direction)
      : text_(reinterpret_cast<const wchar_t*>(text)), length_(length),
        direction_(direction), analyzer_(analyzer),
        levels_(length, direction == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT),
        scripts_(length, HB_SCRIPT_COMMON) {}

  const std::vector<uint8_t>& levels() const noexcept { return levels_; }
  const std::vector<hb_script_t>& scripts() const noexcept { return scripts_; }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteTextAnalysisSource)) {
      *object = static_cast<IDWriteTextAnalysisSource*>(this);
    } else if (iid == __uuidof(IDWriteTextAnalysisSink)) {
      *object = static_cast<IDWriteTextAnalysisSink*>(this);
    } else {
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

  HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text,
                                               UINT32* length) override {
    if (!text || !length) return E_POINTER;
    if (position >= length_) {
      *text = nullptr;
      *length = 0;
    } else {
      *text = text_ + position;
      *length = length_ - position;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text,
                                                   UINT32* length) override {
    if (!text || !length) return E_POINTER;
    if (position == 0 || position > length_) {
      *text = nullptr;
      *length = 0;
    } else {
      *text = text_;
      *length = position;
    }
    return S_OK;
  }

  DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
    return direction_;
  }

  HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* length,
                                           const WCHAR** locale) override {
    if (!length || !locale || position > length_) return E_INVALIDARG;
    *length = length_ - position;
    *locale = L"";
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetNumberSubstitution(
      UINT32 position, UINT32* length, IDWriteNumberSubstitution** substitution) override {
    if (!length || !substitution || position > length_) return E_INVALIDARG;
    *length = length_ - position;
    *substitution = nullptr;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE SetScriptAnalysis(
      UINT32 position, UINT32 length, const DWRITE_SCRIPT_ANALYSIS* analysis) override {
    if (!analysis || position > length_ || length > length_ - position) return E_INVALIDARG;
    DWRITE_SCRIPT_PROPERTIES properties{};
    hb_script_t script = HB_SCRIPT_UNKNOWN;
    if (SUCCEEDED(analyzer_->GetScriptProperties(*analysis, &properties))) {
      script = hb_script_from_iso15924_tag(static_cast<hb_tag_t>(properties.isoScriptCode));
    }
    std::fill(scripts_.begin() + position, scripts_.begin() + position + length, script);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetLineBreakpoints(
      UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetNumberSubstitution(
      UINT32, UINT32, IDWriteNumberSubstitution*) override { return S_OK; }

  HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32 position, UINT32 length,
                                         UINT8, UINT8 resolved_level) override {
    if (position > length_ || length > length_ - position) return E_INVALIDARG;
    std::fill(levels_.begin() + position, levels_.begin() + position + length,
              resolved_level);
    return S_OK;
  }

 private:
  const wchar_t* text_ = nullptr;
  uint32_t length_ = 0;
  DWRITE_READING_DIRECTION direction_ = DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
  IDWriteTextAnalyzer1* analyzer_ = nullptr;
  std::vector<uint8_t> levels_;
  std::vector<hb_script_t> scripts_;
};

DWRITE_READING_DIRECTION paragraph_direction(const char16_t* text, uint32_t length,
                                              lt_text_direction requested) {
  if (requested == LT_TEXT_DIRECTION_RTL) return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
  if (requested == LT_TEXT_DIRECTION_LTR || length == 0) {
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
  }
  std::vector<WORD> types(length);
  if (GetStringTypeW(CT_CTYPE2, reinterpret_cast<const wchar_t*>(text),
                     static_cast<int>(length), types.data())) {
    for (WORD type : types) {
      if (type == C2_LEFTTORIGHT) return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
      if (type == C2_RIGHTTOLEFT) {
        return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
      }
    }
  }
  return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
}

lt_result analyze_text(lt_context* context, const char16_t* text, uint32_t length,
                       lt_text_direction requested, std::vector<lt::unicode::BidiRun>& runs,
                       std::vector<hb_script_t>& scripts) {
  if (length == 0) return LT_OK;
  TextAnalysis analysis(context->text_analyzer.Get(), text, length,
                        paragraph_direction(text, length, requested));
  if (FAILED(context->text_analyzer->AnalyzeScript(&analysis, 0, length, &analysis)) ||
      FAILED(context->text_analyzer->AnalyzeBidi(&analysis, 0, length, &analysis))) {
    return LT_E_INTERNAL;
  }
  runs = lt::unicode::visual_bidi_runs(analysis.levels());
  scripts = analysis.scripts();
  return LT_OK;
}

bool style_valid(const lt_text_style& style) noexcept {
  return lt::descriptor_valid(&style, style_required) && style.cascade &&
      style.cascade->magic == lt::kObjectMagic && std::isfinite(style.font_size) &&
      style.font_size > 0.0f && style.font_size <= 1024.0f;
}

lt::OwnedStyle copy_style(const lt_text_style& source) {
  lt::OwnedStyle result;
  result.cascade = source.cascade;
  result.font_size = source.font_size;
  result.weight = static_cast<uint16_t>(std::clamp<unsigned>(
      source.weight ? source.weight : 400, 1, 1000));
  result.letter_spacing = std::clamp(
      lt::finite_or(source.letter_spacing, 0.0f), -result.font_size, result.font_size * 4.0f);
  result.flags = source.flags;
  if (source.struct_size >= offsetof(lt_text_style, feature_count) + sizeof(uint32_t) &&
      source.feature_count != 0) {
    result.features.assign(source.features, source.features + source.feature_count);
  }
  return result;
}

hb_script_t script_for_cluster(const std::vector<hb_script_t>& scripts,
                               uint32_t start, uint32_t end,
                               hb_script_t inherited) noexcept {
  for (uint32_t index = start; index < end; ++index) {
    const hb_script_t script = scripts[index];
    if (script != HB_SCRIPT_COMMON && script != HB_SCRIPT_INHERITED &&
        script != HB_SCRIPT_UNKNOWN) {
      return script;
    }
  }
  return inherited == HB_SCRIPT_UNKNOWN ? HB_SCRIPT_COMMON : inherited;
}

bool ignorable_for_coverage(char32_t codepoint) noexcept {
  return codepoint == 0x200c || codepoint == 0x200d ||
      (codepoint >= 0xfe00 && codepoint <= 0xfe0f) ||
      (codepoint >= 0xe0100 && codepoint <= 0xe01ef);
}

bool face_covers(HbFaceCache& cache, lt_font_face* face,
                 const char16_t* text, uint32_t start, uint32_t end) {
  hb_font_t* font = cache.make_font(face, 16.0f);
  if (!font) return false;
  bool covered = true;
  uint32_t index = start;
  while (index < end) {
    lt::unicode::Codepoint codepoint;
    if (!lt::unicode::next_utf16(text, end, index, codepoint)) {
      covered = false;
      break;
    }
    hb_codepoint_t glyph = 0;
    if (!ignorable_for_coverage(codepoint.value) &&
        !hb_font_get_nominal_glyph(
            font, static_cast<hb_codepoint_t>(codepoint.value), &glyph)) {
      covered = false;
      break;
    }
  }
  hb_font_destroy(font);
  return covered;
}

lt_font_face* select_face(HbFaceCache& cache, const lt::OwnedStyle& style,
                          const char16_t* text, uint32_t start, uint32_t end) {
  std::vector<size_t> order(style.cascade->entries.size());
  for (size_t index = 0; index < order.size(); ++index) order[index] = index;
  std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
    const auto distance = [&](size_t index) {
      return std::abs(static_cast<int>(style.cascade->entries[index].weight) -
                      static_cast<int>(style.weight));
    };
    return distance(left) < distance(right);
  });
  for (size_t index : order) {
    lt_font_face* face = style.cascade->entries[index].face;
    if (face_covers(cache, face, text, start, end)) return face;
  }
  return nullptr;
}

lt_result shape_segment(HbFaceCache& cache, const char16_t* text, uint32_t full_length,
                        uint32_t start, uint32_t end, bool rtl, hb_script_t script,
                        hb_language_t language, const lt::OwnedStyle& style,
                        lt_font_face* face, float& pen, lt::ShapedRun& output,
                        float& max_ascent, float& max_descent) {
  hb_font_t* font = cache.make_font(face, style.font_size);
  hb_buffer_t* buffer = hb_buffer_create();
  if (!font || !buffer) {
    if (buffer) hb_buffer_destroy(buffer);
    if (font) hb_font_destroy(font);
    return LT_E_OUT_OF_MEMORY;
  }
  hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
  hb_buffer_set_script(buffer, script);
  hb_buffer_set_language(buffer, language);
  hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
  hb_buffer_add_utf16(buffer, reinterpret_cast<const uint16_t*>(text),
                      static_cast<int>(full_length), start, end - start);

  std::vector<hb_feature_t> features;
  features.reserve(style.features.size());
  for (const auto& value : style.features) {
    features.push_back({static_cast<hb_tag_t>(value.tag), value.value, start, end});
  }
  hb_shape(font, buffer, features.data(), static_cast<unsigned>(features.size()));
  unsigned glyph_count = 0;
  const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
  const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
  if ((!infos || !positions) && glyph_count != 0) {
    hb_buffer_destroy(buffer);
    hb_font_destroy(font);
    return LT_E_INTERNAL;
  }

  output.face = face;
  output.font_size = style.font_size;
  output.weight = style.weight;
  output.style_flags = style.flags;
  output.text_start = start;
  output.text_length = end - start;
  output.right_to_left = rtl;
  output.color = face->blob->has_color;
  output.synthetic_bold = style.weight >= 600 && face->blob->weight < 600 &&
      (style.flags & LT_TEXT_STYLE_ALLOW_SYNTHETIC_BOLD) != 0;
  output.glyphs.reserve(glyph_count);
  float local_pen = 0.0f;
  for (unsigned index = 0; index < glyph_count; ++index) {
    const float advance = positions[index].x_advance / 64.0f + style.letter_spacing;
    output.glyphs.push_back({
        infos[index].codepoint,
        infos[index].cluster,
        pen + local_pen + positions[index].x_offset / 64.0f,
        -positions[index].y_offset / 64.0f,
        advance});
    local_pen += advance;
  }
  pen += local_pen;

  hb_font_extents_t extents{};
  if (hb_font_get_h_extents(font, &extents)) {
    max_ascent = std::max(max_ascent, extents.ascender / 64.0f);
    max_descent = std::max(max_descent, -extents.descender / 64.0f);
  } else {
    max_ascent = std::max(max_ascent, style.font_size * 0.8f);
    max_descent = std::max(max_descent, style.font_size * 0.2f);
  }
  hb_buffer_destroy(buffer);
  hb_font_destroy(font);
  return LT_OK;
}

void retain_run_faces(lt_text_layout& layout) {
  std::unordered_set<lt_font_face*> seen;
  for (const auto& run : layout.runs) {
    if (run.face && seen.insert(run.face).second) {
      run.face->retain();
      layout.retained_faces.push_back(run.face);
    }
  }
}

void build_cluster_boxes(lt_text_layout& layout) {
  struct Accumulator {
    float left = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    bool rtl = false;
    uint32_t end = 0;
  };
  std::map<uint32_t, Accumulator> values;
  for (const auto& run : layout.runs) {
    std::vector<uint32_t> boundaries;
    boundaries.reserve(run.glyphs.size() + 1);
    for (const auto& glyph : run.glyphs) boundaries.push_back(glyph.cluster);
    boundaries.push_back(run.text_start + run.text_length);
    std::sort(boundaries.begin(), boundaries.end());
    for (const auto& glyph : run.glyphs) {
      auto& value = values[glyph.cluster];
      value.left = std::min(value.left, glyph.x);
      value.right = std::max(value.right, glyph.x + std::max(0.0f, glyph.advance));
      value.rtl = run.right_to_left;
      value.end = *std::upper_bound(boundaries.begin(), boundaries.end(), glyph.cluster);
    }
  }
  for (const auto& [position, value] : values) {
    layout.clusters.push_back({position, std::max(1u, value.end - position),
        value.left, std::max(0.0f, value.right - value.left), value.rtl});
  }
  std::sort(layout.clusters.begin(), layout.clusters.end(),
      [](const lt::ClusterBox& left, const lt::ClusterBox& right) {
        return left.left < right.left;
      });
}

void apply_alignment(lt_text_layout& layout, lt_text_alignment alignment,
                     float max_width) {
  if (!std::isfinite(max_width) || max_width <= layout.metrics.width) return;
  if (alignment == LT_TEXT_ALIGNMENT_CENTER) {
    layout.alignment_offset = (max_width - layout.metrics.width) * 0.5f;
  } else if (alignment == LT_TEXT_ALIGNMENT_END) {
    layout.alignment_offset = max_width - layout.metrics.width;
  }
  if (layout.alignment_offset == 0.0f) return;
  for (auto& run : layout.runs) {
    for (auto& glyph : run.glyphs) glyph.x += layout.alignment_offset;
  }
  for (auto& cluster : layout.clusters) cluster.left += layout.alignment_offset;
}

lt_hit_test_metrics hit_value(const lt_text_layout& layout,
                              const lt::ClusterBox& cluster,
                              bool trailing, bool inside) noexcept {
  lt_hit_test_metrics result{};
  result.struct_size = sizeof(result);
  result.abi_version = LT_ABI_VERSION;
  result.text_position = cluster.text_position;
  result.text_length = cluster.text_length;
  result.left = cluster.left;
  result.top = 0.0f;
  result.width = cluster.width;
  result.height = layout.metrics.height;
  result.is_trailing = trailing;
  result.is_inside = inside;
  result.is_right_to_left = cluster.right_to_left;
  return result;
}

}  // namespace

extern "C" {

lt_result __cdecl lt_text_layout_create(lt_context* context,
                                        const lt_text_layout_desc* desc,
                                        lt_text_layout** out_layout) {
  if (!out_layout) return LT_E_INVALID_ARGUMENT;
  *out_layout = nullptr;
  if (!context || context->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(desc, layout_required) ||
      (desc->text_length != 0 && !desc->text) || !style_valid(desc->base_style) ||
      desc->base_style.cascade->context != context ||
      desc->base_style.feature_count > 256 ||
      (desc->base_style.feature_count != 0 && !desc->base_style.features) ||
      desc->style_span_count > desc->text_length ||
      (desc->style_span_count != 0 && !desc->style_spans) ||
      !std::isfinite(desc->max_width) || desc->max_width < 0.0f) {
    return LT_E_INVALID_ARGUMENT;
  }
  const auto* input = reinterpret_cast<const char16_t*>(desc->text);
  if (!lt::unicode::validate_utf16(input, desc->text_length)) return LT_E_INVALID_ARGUMENT;
  const auto start_time = std::chrono::steady_clock::now();

  try {
    auto layout = std::make_unique<lt_text_layout>();
    layout->context = context;
    context->retain();
    if (desc->text_length != 0) layout->text.assign(input, input + desc->text_length);
    layout->metrics.struct_size = sizeof(layout->metrics);
    layout->metrics.abi_version = LT_ABI_VERSION;
    layout->metrics.line_count = 1;
    layout->metrics.text_length = desc->text_length;

    std::vector<lt::OwnedStyle> styles;
    styles.reserve(desc->style_span_count + 1);
    styles.push_back(copy_style(desc->base_style));
    std::vector<uint32_t> style_indices(desc->text_length, 0);
    uint32_t previous_end = 0;
    for (uint32_t index = 0; index < desc->style_span_count; ++index) {
      const auto& span = desc->style_spans[index];
      if (span.text_length == 0 || span.text_position < previous_end ||
          span.text_position > desc->text_length ||
          span.text_length > desc->text_length - span.text_position ||
          !style_valid(span.style) || span.style.cascade->context != context ||
          span.style.feature_count > 256 ||
          (span.style.feature_count != 0 && !span.style.features)) {
        return LT_E_INVALID_ARGUMENT;
      }
      previous_end = span.text_position + span.text_length;
      styles.push_back(copy_style(span.style));
      std::fill(style_indices.begin() + span.text_position,
                style_indices.begin() + previous_end,
                static_cast<uint32_t>(styles.size() - 1));
    }

    std::vector<uint32_t> boundaries =
        lt::unicode::grapheme_boundaries(input, desc->text_length);
    if (boundaries.empty()) return LT_E_INTERNAL;

    for (uint32_t index = 0; index < desc->style_span_count; ++index) {
      const auto& span = desc->style_spans[index];
      if (!std::binary_search(boundaries.begin(), boundaries.end(), span.text_position) ||
          !std::binary_search(boundaries.begin(), boundaries.end(),
                              span.text_position + span.text_length)) {
        return LT_E_INVALID_ARGUMENT;
      }
    }

    std::vector<lt::unicode::BidiRun> bidi_runs;
    std::vector<hb_script_t> scripts;
    const lt_result analysis_result = analyze_text(
        context, input, desc->text_length, desc->direction, bidi_runs, scripts);
    if (analysis_result != LT_OK) return analysis_result;

    HbFaceCache cache;
    hb_language_t language = hb_language_from_string(
        desc->locale && *desc->locale ? desc->locale : "und", -1);
    float pen = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    for (const lt::unicode::BidiRun& bidi_run : bidi_runs) {
      const uint32_t run_start = bidi_run.start;
      const uint32_t run_end = bidi_run.end;
      const bool right_to_left = (bidi_run.level & 1u) != 0;
      std::vector<uint32_t> run_boundaries;
      run_boundaries.push_back(run_start);
      for (uint32_t boundary : boundaries) {
        if (boundary > run_start && boundary < run_end) run_boundaries.push_back(boundary);
      }
      run_boundaries.push_back(run_end);

      struct Segment {
        uint32_t start = 0;
        uint32_t end = 0;
        uint32_t style = 0;
        hb_script_t script = HB_SCRIPT_UNKNOWN;
        lt_font_face* face = nullptr;
      };
      std::vector<Segment> segments;
      hb_script_t inherited_script = HB_SCRIPT_UNKNOWN;
      for (size_t cluster = 0; cluster + 1 < run_boundaries.size(); ++cluster) {
        const uint32_t start = run_boundaries[cluster];
        const uint32_t end = run_boundaries[cluster + 1];
        const uint32_t style_index = start < style_indices.size() ? style_indices[start] : 0;
        const hb_script_t script = script_for_cluster(scripts, start, end, inherited_script);
        if (script != HB_SCRIPT_COMMON && script != HB_SCRIPT_INHERITED &&
            script != HB_SCRIPT_UNKNOWN) inherited_script = script;
        lt_font_face* face = select_face(cache, styles[style_index], input, start, end);
        if (!face) {
          context->log(1, "No face in the cascade covers a grapheme cluster");
          return LT_E_FONT_UNAVAILABLE;
        }
        if (!segments.empty() && segments.back().end == start &&
            segments.back().style == style_index && segments.back().script == script &&
            segments.back().face == face) {
          segments.back().end = end;
        } else {
          segments.push_back({start, end, style_index, script, face});
        }
      }
      if (right_to_left) std::reverse(segments.begin(), segments.end());
      for (const auto& segment : segments) {
        lt::ShapedRun shaped;
        const lt_result result = shape_segment(cache, input, desc->text_length,
            segment.start, segment.end, right_to_left, segment.script,
            language, styles[segment.style], segment.face, pen, shaped, ascent, descent);
        if (result != LT_OK) return result;
        layout->metrics.glyph_count += static_cast<uint32_t>(shaped.glyphs.size());
        layout->runs.push_back(std::move(shaped));
      }
    }
    layout->metrics.width = pen;
    layout->metrics.ascent = ascent;
    layout->metrics.descent = descent;
    layout->metrics.leading = 0.0f;
    layout->metrics.height = ascent + descent;
    layout->metrics.run_count = static_cast<uint32_t>(layout->runs.size());
    build_cluster_boxes(*layout);
    apply_alignment(*layout, desc->alignment, desc->max_width);
    retain_run_faces(*layout);
    layout->shaping_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_time).count());
    *out_layout = layout.release();
    return LT_OK;
  } catch (const std::bad_alloc&) {
    return LT_E_OUT_OF_MEMORY;
  } catch (...) {
    return LT_E_INTERNAL;
  }
}

lt_result __cdecl lt_text_layout_get_metrics(const lt_text_layout* layout,
                                             lt_text_metrics* out_metrics) {
  if (!layout || layout->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(out_metrics, metrics_required)) {
    return LT_E_INVALID_ARGUMENT;
  }
  const uint32_t caller_size = out_metrics->struct_size;
  const size_t copy_size = std::min<size_t>(caller_size, sizeof(lt_text_metrics));
  memcpy(out_metrics, &layout->metrics, copy_size);
  out_metrics->struct_size = caller_size;
  return LT_OK;
}

lt_result __cdecl lt_text_layout_hit_test_point(const lt_text_layout* layout,
                                                float x, float y,
                                                lt_hit_test_metrics* out_metrics) {
  if (!layout || layout->magic != lt::kObjectMagic ||
      !lt::descriptor_valid(out_metrics, hit_required) ||
      !std::isfinite(x) || !std::isfinite(y)) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (layout->clusters.empty()) return LT_E_INVALID_STATE;
  const bool vertical_inside = y >= 0.0f && y <= layout->metrics.height;
  const lt::ClusterBox* selected = &layout->clusters.front();
  bool inside = false;
  for (const auto& cluster : layout->clusters) {
    if (x >= cluster.left && x <= cluster.left + cluster.width) {
      selected = &cluster;
      inside = vertical_inside;
      break;
    }
    if (std::abs(x - (cluster.left + cluster.width * 0.5f)) <
        std::abs(x - (selected->left + selected->width * 0.5f))) {
      selected = &cluster;
    }
  }
  bool trailing = x >= selected->left + selected->width * 0.5f;
  if (selected->right_to_left) trailing = !trailing;
  const uint32_t caller_size = out_metrics->struct_size;
  const lt_hit_test_metrics value = hit_value(*layout, *selected, trailing, inside);
  memcpy(out_metrics, &value, std::min<size_t>(caller_size, sizeof(value)));
  out_metrics->struct_size = caller_size;
  return LT_OK;
}

lt_result __cdecl lt_text_layout_hit_test_position(const lt_text_layout* layout,
                                                   uint32_t text_position,
                                                   bool trailing, float* out_x,
                                                   float* out_y,
                                                   lt_hit_test_metrics* out_metrics) {
  if (!layout || layout->magic != lt::kObjectMagic || !out_x || !out_y ||
      !lt::descriptor_valid(out_metrics, hit_required) ||
      text_position > layout->text.size()) {
    return LT_E_INVALID_ARGUMENT;
  }
  if (layout->clusters.empty()) return LT_E_INVALID_STATE;
  const bool at_end = text_position == layout->text.size();
  const uint32_t lookup_position = at_end ? text_position - 1 : text_position;
  if (at_end) trailing = true;
  const lt::ClusterBox* selected = &layout->clusters.back();
  for (const auto& cluster : layout->clusters) {
    if (lookup_position >= cluster.text_position &&
        lookup_position < cluster.text_position + cluster.text_length) {
      selected = &cluster;
      break;
    }
  }
  const bool visual_trailing = selected->right_to_left ? !trailing : trailing;
  *out_x = selected->left + (visual_trailing ? selected->width : 0.0f);
  *out_y = 0.0f;
  const uint32_t caller_size = out_metrics->struct_size;
  const lt_hit_test_metrics value = hit_value(*layout, *selected, trailing, true);
  memcpy(out_metrics, &value, std::min<size_t>(caller_size, sizeof(value)));
  out_metrics->struct_size = caller_size;
  return LT_OK;
}

}  // extern "C"
