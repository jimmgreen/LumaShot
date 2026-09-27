#pragma once
#include "model/document.h"
#include <d2d1.h>
#include <functional>
#include <span>

namespace lumashot::ui {
// Event-driven transition; the owner schedules ticks only while Active().
class LinearTransition {
public:
    float Value(uint64_t now)const;
    bool Active(uint64_t now)const;
    void Target(float value,uint64_t now);
    void Reset(float value=0){from_=to_=value;started_=0;}
private:
    float from_{},to_{};uint64_t started_{};
    static constexpr uint64_t duration_=160;
};
enum class Kind { Surface, Group, Label, Separator, Button, Swatch, Dropdown, Slider, TextField };
struct Control {
    int id{-1};Kind kind{Kind::Label};Box bounds{};std::wstring text;
    int icon{-1};uint32_t color{};float value{};
    bool selected{},enabled{true},primary{},square{},rainbow{};
    bool indicator_backed{}; // The toolbar paints its shared animated backdrop.
    bool Interactive()const;
};
struct Theme {
    float scale{1};bool dark{};
    float Radius()const{return 7*scale;}
    uint32_t Ink()const{return dark?0xffedf4ff:0xff243142;}
    uint32_t Muted()const{return dark?0xff8090a6:0xff8793a4;}
    uint32_t Border()const{return dark?0x556f839d:0x448294ae;}
    uint32_t Accent()const{return 0xff0784ff;}
};
// Bind existing text/icon/acrylic resources once. Widgets own no rendering
// targets and use the same geometry for painting and input at every DPI.
struct PaintContext {
    ID2D1RenderTarget* target{};
    std::function<ID2D1SolidColorBrush*(uint32_t)> brush;
    std::function<void(const std::wstring&,Box,float,uint32_t)> text;
    std::function<void(int,Box,uint32_t)> icon;
    std::function<void(Box,float)> surface;
    std::function<void(Box)> color_wheel;
    std::function<Point(const std::wstring&,float)> measure_text;
};
int HitTest(std::span<const Control> controls,Point point);
Box SliderTrack(Box bounds,float scale);
float SliderValue(Box bounds,Point point,float scale);
Point SliderThumb(Box bounds,float value,float scale);
void DrawControls(const PaintContext& paint,const Theme& theme,std::span<const Control> controls,int hovered=-1,int pressed=-1);
Box TooltipBounds(Box anchor,Box viewport,Point text_size,float scale);
void DrawTooltip(const PaintContext& paint,const Theme& theme,const std::wstring& text,Box anchor,Box viewport);
}
