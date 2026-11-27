#pragma once
#include "model/document.h"
#include "model/line_snap.h"
#include "model/tool_properties.h"
#include "model/toolbar_customization.h"
#include "model/mosaic.h"
#include "ui/color_picker.h"
#include "ui/window_surface.h"
#include "ui/controls.h"
#include "ui/toolbar_motion.h"
#include "ui/dropdown_view.h"
#include "ui/text_renderer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <memory>
#include <array>

namespace lumashot {
using Microsoft::WRL::ComPtr;

struct ToolbarLayout {
    // bounds: the bar itself (one row; it never changes with the active tool). colors: the property
    // controls area inside card_full, the separate property card (方案 C) beside the bar on the side away
    // from the selection. card: the part of it revealed by the expansion animation (grows from the bar).
    Box bounds{},tools{},colors{},actions{},card{},card_full{};
    float card_reveal{};bool card_above{};
    Tool property_tool{Tool::Rectangle};
    NumberCombo number_combo{NumberCombo::Plain};
    float scale{1};
    int columns{14};
    bool ocr_available{true},recording_available{true};
    bool properties_open{};
    // Icon modes that fit the monitor: content-sized bar with fixed slots and group dividers.
    bool grouped{};
    ToolbarCustomization custom;
    // Hidden entries that are currently available, in panel order (the "更多" menu).
    std::vector<int> OverflowItems() const;
    // Tool-grid slots in display order, including the "更多" button when it has items.
    std::vector<int> PanelToolSlots() const;
    // Grid slots reserved by the layout (independent of runtime availability).
    int PanelSlotCount() const;
    int Hit(Point point,Tool tool=Tool::Rectangle) const;
    std::vector<ui::Control> Controls(Tool tool)const;
    Box Button(int index) const;
    Box Property(int index,Tool tool=Tool::Select) const;
    bool CardVisible() const{return card.bottom-card.top>.5f&&card.right>card.left;}
    // 属性栏样式 = 卡片: property captions above their controls (taller card); otherwise the one-row strip.
    bool PropertyCardStyle() const{return custom.property_style==PropertyBarStyle::Card;}
    // Union of the bar and the visible card.
    Box Extent() const;
};
// The bar or the visible property card.
inline bool Contains(const ToolbarLayout& toolbar,Point p){return Contains(toolbar.bounds,p)||(toolbar.CardVisible()&&Contains(toolbar.card,p));}
// Screen area a toolbar tooltip can take for this hover (ui::TooltipBounds: up to ~36 DIP above or
// below its anchor, centred on it and clamped sideways). Empty (left>=right) when no tooltip shows.
Box ToolbarTooltipReserve(const ToolbarLayout& toolbar,int hover,int property_drag=-1);
// ocr_available / recording_available: quick actions that are unavailable take no space in the bar.
ToolbarLayout PlaceToolbar(Box selection, RECT monitor, float scale,float expansion=1,Tool tool=Tool::Rectangle,NumberCombo combo=NumberCombo::Plain,const ToolbarCustomization& custom=ToolbarCustomization{},bool ocr_available=true,bool recording_available=true);
// Toolbar action id of an annotation tool (Number is 14).
int ToolbarToolId(Tool tool);

// Live in-place text editing (文字工具): the edited annotation with its current
// text (IME preedit spliced in) and the native EDIT's selection/caret, painted
// directly on the canvas in the final style. Indices are UTF-16 positions.
struct TextEditPreview {
    std::optional<Mark> mark;
    int selection_start{},selection_end{},caret{};
    int composition_start{-1},composition_length{};
    bool caret_visible{true};
    bool SameContent(const TextEditPreview& other)const{
        return mark==other.mark&&selection_start==other.selection_start&&selection_end==other.selection_end&&caret==other.caret&&
            composition_start==other.composition_start&&composition_length==other.composition_length;
    }
};

struct ViewState : ToolProperties {
    using Dropdown=ui::Dropdown; Dropdown dropdown;
    TextEditPreview text_edit;
    Box selection{};
    bool selected{}, dragging{}, busy{}, dark{};
    bool external_magnifier{};
    // Pin annotation over a live screen copy: nothing dims the outside, so window-region margins
    // around the toolbar show exactly the screen behind it (clean anti-aliased corners and shadow).
    bool clear_outside{};
    bool polyline_active{};
    size_t polyline_confirmed{};
    Tool tool{Tool::Select};
    Tool closing_tool{Tool::Select};
    Tool selected_tool{Tool::Select};
    Tool PropertyTool()const{return tool==Tool::Select?selected_tool:tool;}
    bool Highlighting()const{return PropertyTool()==Tool::Pen&&pen_mode==PenMode::Highlighter;}
    uint32_t& ActiveColor(){return Highlighting()?highlighter_color:color;}
    uint32_t ActiveColor()const{return Highlighting()?highlighter_color:color;}
    float& ActiveWidth(){return Highlighting()?highlighter_width:width;}
    float ActiveWidth()const{return Highlighting()?highlighter_width:width;}
    float& PenOpacity(){return Highlighting()?highlighter_opacity:pen_opacity;}
    float PenOpacity()const{return Highlighting()?highlighter_opacity:pen_opacity;}
    int picker_original_preset{-2};
    int property_drag{-1};
    int hover{-1};
    Point pointer{};
    std::optional<LineSnap> line_snap;
    ToolbarLayout toolbar;
    ui::ToolbarMotionFrame toolbar_visual;
    ColorPicker picker;
    bool picker_text_color{};
    uint32_t& PickerColor(){return picker_text_color?number_text_color:ActiveColor();}
    std::wstring hint;
};
bool AdjustPropertyValue(ViewState& state,int id,int steps);
bool SetToolbarSlider(ViewState& state,int id,Point point);
bool OpenToolbarDropdown(ViewState& state,int id,RECT monitor);
// Opens the "更多" menu listing hidden toolbar entries; the item value is the action id.
bool OpenToolbarMoreMenu(ViewState& state,RECT monitor);
// Right-click menu for a customizable panel button: "收进“更多”菜单" / "全部放回面板".
bool OpenToolbarContextMenu(ViewState& state,int id,RECT monitor);
// Clickable pin glyph at the right end of a "更多" menu row ("固定到面板").
Box ToolbarMorePinBox(const ViewState::Dropdown& popup,int index);
void ChooseToolbarDropdown(ViewState& state,int index);
std::vector<ui::Control> ToolbarControls(const ViewState& state,const Document& document);
const wchar_t* ToolbarActionName(int id);
std::optional<uint32_t> ToolbarColor(int id);
std::optional<uint32_t> PenToolbarColor(int id,const ViewState& state);

struct TagMetrics;
class Renderer {
    friend struct SelectedPropertiesTest;
    friend struct StartupRenderTest;
    friend struct RenderOptimizationTest;
    friend struct ToolbarMotionTest;
public:
    Renderer();
    void Prepare(HWND window,RECT monitor);
    bool Paint(HWND window, RECT monitor, const Frame& frame, const Frame& acrylic,
        const Document& document, const std::optional<Mark>& draft, const ViewState& state,int editing_text=-1);
    HANDLE FrameEvent()const{return window_surface_.FrameEvent();}
    void FrameReady(){window_surface_.FrameReady();}
    Frame Flatten(const Frame& frame, const Document& document, RECT selection);
    // Offscreen copy of the interactive canvas (marks, live text editing,
    // selection chrome) for previews and tests. Use a dedicated renderer: it
    // replaces the presentation target.
    Frame Canvas(const Frame& frame, const Document& document, const ViewState& state, int editing_text=-1);
    Frame Demo(bool chrome = true, bool dark = false, Tool preview_tool = Tool::Arrow,bool color_picker = false,int preview_hover=-1,int preview_dropdown=-1,bool highlighter=false,const ToolbarCustomization* customization=nullptr);
private:
    void ResetTarget();
    void Brush(uint32_t color, float opacity = 1);
    void Text(const std::wstring& text, Box rect, float size, uint32_t color,bool wrap=true);
    IDWriteTextFormat* FontFormat(float size,bool wrap,bool bold=false,TextAlign align=TextAlign::Left,const std::wstring& family=L"Segoe UI");
    Point MeasureText(const std::wstring& text,float size);
    void Marks(const Frame& frame, const Document& document, const std::optional<Mark>& draft,int editing_text=-1);
    void MarkShape(const Frame& frame, const Mark& mark, size_t index);
    void Number(const Frame& frame,const Mark& mark);
    void TagBackground(Box box,uint32_t fill,uint32_t border,TagMetrics metrics);
    void NumberDetail(const Frame& frame,const Mark& mark);
    void Panel(Box rect, ID2D1Bitmap* acrylic, const Frame& frame, bool dark, float radius,bool acrylic_surface=true);
    void Icon(int index, Box rect, uint32_t color);
    void Picker(const Frame& frame,const ViewState& state);
    void SnapGuide(const ViewState& state,const std::optional<Mark>& draft);
    // Dashed edit outline, selection, IME underline and caret over the live text.
    void TextEditOverlay(const Frame& frame,const ViewState& state);
    void Toolbar(const Frame& frame,const Document& document,const ViewState& state);
    void Chrome(const Frame& frame, const Frame& acrylic, const Document& document,
        const std::optional<Mark>& draft, const ViewState& state, RECT monitor,int editing_text=-1);
    ComPtr<ID2D1Bitmap> Bitmap(const Frame& frame,D2D1_ALPHA_MODE alpha=D2D1_ALPHA_MODE_IGNORE);
    ComPtr<ID2D1Bitmap> BitmapRegion(const Frame& frame,RECT region);
    ComPtr<ID2D1Factory1> factory_;
    ComPtr<IDWriteFactory> text_factory_;
    TextRenderer text_renderer_;
    struct TextFormat {float size{};bool wrap{},bold{};TextAlign align{};std::wstring family;ComPtr<IDWriteTextFormat> format;};
    std::vector<TextFormat> text_formats_;
    // Destroy brushes/bitmaps and the target before releasing the surface device.
    WindowSurface window_surface_;
    ComPtr<ID2D1RenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1Bitmap> screen_bitmap_, acrylic_bitmap_, color_wheel_bitmap_;
    RECT monitor_{};
    struct MosaicCache {
        Mark mark;
        const uint32_t* pixels{};
        std::vector<MosaicTile> tiles;
        Frame blur;
        ComPtr<ID2D1Bitmap> bitmap; // Belongs to target_; discard on every target change.
    };
    std::vector<MosaicCache> mosaic_cache_;
};
Frame BlurBackdrop(const Frame& frame);
uint32_t AcrylicSaturate(unsigned r,unsigned g,unsigned b);

}
