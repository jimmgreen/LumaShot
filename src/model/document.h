#pragma once
#include "capture/frame.h"
#include <optional>
#include <array>
#include <string>

namespace lumashot {

struct Point { float x{}, y{}; bool operator==(const Point&) const = default; };
struct Box { float left{}, top{}, right{}, bottom{};bool operator==(const Box&)const=default; };
enum class EditPart { Whole, Badge, Detail, Leader };
enum class Tool { Select, Rectangle, Ellipse, Arrow, Pen, Text, Mosaic, Number };
enum class NumberShape { Circle, Outline, DoubleCircle, Square, Rounded, Capsule, Flag, Pin, Hexagon, Corner };
enum class NumberCombo { Plain, Leader, Arrow, Text, DashedBox, Highlight, Magnify };
inline constexpr LPCWSTR NumberComboNames[]={L"仅序号",L"引导线",L"箭头",L"文本标签",L"虚线框",L"高亮区域",L"放大说明"};
inline constexpr LPCWSTR NumberShapeNames[]={L"实心圆",L"描边圆",L"双层圆",L"实心方块",L"圆角方块",L"胶囊标签",L"旗标",L"定位针",L"六边形",L"角标标签"};
enum class LineStyle { Solid, Dash, Dot, DashDot };
enum class ArrowStyle { Filled, Open, Double };
enum class ArrowType { Standard, Double, Curved, HandDrawn, Triangle, DotStart };
enum class ArrowHead { Filled, Hollow, Rounded, Diamond, Circle, Short, Wide, Slender, Arc, Slanted, None };
enum class TextAlign { Left, Center, Right };
enum class PenSmoothing { None, Standard, High };
enum class PenMode { Normal, Highlighter };
enum class MosaicMode { Pixel, Blur };
enum class MosaicMethod { Brush, Rectangle };
enum class TextBackground { None, White, Black, Yellow, ToneLight, ToneDark, Automatic, TagAutomatic, TagLight, TagDark };
inline std::optional<uint32_t> TextBackgroundColor(TextBackground background){
    switch(background){case TextBackground::White:return 0xffffffff;case TextBackground::Black:return 0xff243142;case TextBackground::Yellow:return 0xffffe58f;default:return std::nullopt;}
}
struct Mark {
    float rotation{};
    std::optional<std::array<float,4>> corner_radii;
    std::optional<Box> number_detail;
    std::optional<std::array<Point,4>> number_leader;
    Tool tool{Tool::Rectangle};
    Point a{}, b{};
    uint32_t color{0xffe0529c};
    float width{3};
    float pen_opacity{1};
    PenMode pen_mode{PenMode::Normal};
    bool pen_straight{};
    bool pen_polyline{};
    PenSmoothing pen_smoothing{PenSmoothing::Standard};
    LineStyle line_style{LineStyle::Solid};
    ArrowStyle arrow_style{ArrowStyle::Filled};
    ArrowType arrow_type{ArrowType::Standard};
    ArrowHead arrow_head{ArrowHead::Filled};
    float arrow_size{12};
    float font_size{24};
    int number{1};
    std::wstring number_label;
    float number_size{32};
    NumberShape number_shape{NumberShape::Circle};
    NumberCombo number_combo{NumberCombo::Plain};
    uint32_t number_text_color{0xff172b43};
    int number_text_preset{-2};
    float number_text_size{16};
    Point number_target{};
    bool text_bold{};
    std::wstring font_family{L"Segoe UI"};
    bool text_auto_size{};
    float text_wrap_width{};
    TextAlign text_align{TextAlign::Left};
    TextBackground text_background{TextBackground::None};
    float mosaic_cell{12};
    float mosaic_brush{32};
    MosaicMode mosaic_mode{MosaicMode::Pixel};
    MosaicMethod mosaic_method{MosaicMethod::Brush};
    std::optional<uint32_t> fill_color;
    float fill_opacity{0.3f},corner_radius{};
    std::wstring text;
    std::vector<Point> points;
    bool operator==(const Mark&) const = default;
};

Box LocalBounds(const Mark& mark);
Box Bounds(const Mark& mark);
Box NumberDetailBounds(const Mark& mark);
Box Normalize(Point a, Point b);
inline bool Contains(Box box,Point p,float padding=0){return p.x>=box.left-padding&&p.x<=box.right+padding&&p.y>=box.top-padding&&p.y<=box.bottom+padding;}
void Translate(Mark& mark, Point delta);
RECT PixelRect(Box box);
Point ConstrainStraightLine(Point origin, Point current);

class Document {
public:
    std::vector<Mark> marks;
    int selected{-1};
    EditPart selected_part{EditPart::Whole};
    // Current editable state only: export jobs must not retain undo/redo history.
    Document Snapshot() const;
    void Reset();
    void Add(Mark mark);
    void Checkpoint();
    void Undo();
    void Redo();
    void DeleteSelected();
    int HitTest(Point point) const;
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
private:
    std::vector<std::vector<Mark>> undo_, redo_;
};

}

