#pragma once
#include "model/document.h"
namespace lumashot {
// Persistent tool defaults in DIP; no selection, document or window state.
struct ToolProperties {
    uint32_t color{0xffe0529c};
    float width{3};
    float pen_opacity{1};
    PenMode pen_mode{PenMode::Normal};
    bool pen_straight{};
    bool pen_polyline{};
    bool pen_snap{true};
    uint32_t highlighter_color{0xffe8b339};
    float highlighter_width{24},highlighter_opacity{.4f};
    PenSmoothing pen_smoothing{PenSmoothing::Standard};
    LineStyle line_style{LineStyle::Solid};
    ArrowStyle arrow_style{ArrowStyle::Filled};
    ArrowType arrow_type{ArrowType::Standard};
    ArrowHead arrow_head{ArrowHead::Filled};
    float arrow_size{12};
    float font_size{24};
    std::wstring font_family{L"Segoe UI"};
    float number_size{32};
    std::wstring number_label;
    NumberShape number_shape{NumberShape::Circle};
    NumberCombo number_combo{NumberCombo::Leader};
    uint32_t number_text_color{0xff374151};
    int number_text_preset{-2};
    float number_text_size{16};
    bool text_bold{};
    TextAlign text_align{TextAlign::Left};
    TextBackground text_background{TextBackground::None};
    float mosaic_cell{12};
    float mosaic_brush{32};
    float mosaic_strength{70};
    MosaicMode mosaic_mode{MosaicMode::Pixel};
    MosaicMethod mosaic_method{MosaicMethod::Brush};
    std::optional<uint32_t> fill_color;
    float fill_opacity{0.7f},corner_radius{16};
    bool operator==(const ToolProperties&) const = default;
};
}
