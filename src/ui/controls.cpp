#include "ui/controls.h"
#include <algorithm>

namespace lumashot::ui {
float LinearTransition::Value(uint64_t now)const {
    const float progress=std::min(1.f,float(now>=started_?now-started_:0)/float(duration_));
    return from_+(to_-from_)*progress;
}
bool LinearTransition::Active(uint64_t now)const{return from_!=to_&&now>=started_&&now-started_<duration_;}
void LinearTransition::Target(float value,uint64_t now){from_=Value(now);to_=std::clamp(value,0.f,1.f);started_=now;}
bool Control::Interactive()const{return enabled&&id>=0&&kind!=Kind::Surface&&kind!=Kind::Group&&kind!=Kind::Label&&kind!=Kind::Separator;}
int HitTest(std::span<const Control> controls,Point point) {
    for(auto i=controls.rbegin();i!=controls.rend();++i)if(i->Interactive()&&Contains(i->bounds,point))return i->id;
    return -1;
}
Box SliderTrack(Box b,float s){const float y=(b.top+b.bottom)/2;return {b.left+9*s,y-2*s,std::max(b.left+10*s,b.right-49*s),y+2*s};}
float SliderValue(Box b,Point p,float s){const auto t=SliderTrack(b,s);return std::clamp((p.x-t.left)/(t.right-t.left),0.f,1.f);}
Point SliderThumb(Box b,float value,float s){const auto t=SliderTrack(b,s);return {t.left+(t.right-t.left)*std::clamp(value,0.f,1.f),(t.top+t.bottom)/2};}
Box TooltipBounds(Box anchor,Box viewport,Point text_size,float s) {
    const float width=std::min(text_size.x+18*s,viewport.right-viewport.left-8*s),height=text_size.y+12*s;
    const float left=std::clamp((anchor.left+anchor.right-width)/2,viewport.left+4*s,viewport.right-width-4*s);
    float top=anchor.top-height-8*s;
    if(top<viewport.top+4*s)top=anchor.bottom+8*s;
    top=std::clamp(top,viewport.top+4*s,std::max(viewport.top+4*s,viewport.bottom-height-4*s));
    return {left,top,left+width,top+height};
}
void DrawTooltip(const PaintContext& p,const Theme& theme,const std::wstring& text,Box anchor,Box viewport) {
    const float s=theme.scale;const auto size=p.measure_text(text,12*s);const auto b=TooltipBounds(anchor,viewport,size,s);
    Control surface;surface.kind=Kind::Surface;surface.bounds=b;
    Control label;label.kind=Kind::Label;label.bounds={b.left+9*s,b.top+6*s,b.right-9*s,b.bottom-6*s};label.text=text;
    const Control controls[]{surface,label};DrawControls(p,theme,controls);
}
void DrawControls(const PaintContext& p,const Theme& theme,std::span<const Control> controls,int hovered,int pressed) {
    const float s=theme.scale;
    const auto rect=[](Box b){return D2D1::RectF(b.left,b.top,b.right,b.bottom);};
    const auto fill=[&](Box b,uint32_t c,float radius){p.target->FillRoundedRectangle(D2D1::RoundedRect(rect(b),radius,radius),p.brush(c));};
    const auto stroke=[&](Box b,uint32_t c,float radius){p.target->DrawRoundedRectangle(D2D1::RoundedRect(rect(b),radius,radius),p.brush(c),s);};
    for(const auto& c:controls) {
        const Box b=c.bounds;const bool hot=c.enabled&&hovered==c.id,down=c.enabled&&pressed==c.id;
        const auto ink=!c.enabled?theme.Muted():(c.primary?0xffffffff:(c.selected?theme.Accent():theme.Ink()));
        if(c.kind==Kind::Surface){p.surface(b,14*s);continue;}
        if(c.kind==Kind::Group){fill(b,theme.dark?0x1823344d:0x60ffffff,10*s);stroke(b,theme.dark?0x225f7697:0x60ffffff,10*s);continue;}
        if(c.kind==Kind::Label){p.text(c.text,b,12*s,c.enabled?theme.Ink():theme.Muted());continue;}
        if(c.kind==Kind::Separator){const float x=(b.left+b.right)/2;p.target->DrawLine({x,b.top},{x,b.bottom},p.brush(theme.Border()),s);continue;}
        if(c.kind==Kind::Slider) {
            const auto track=SliderTrack(b,s);const Point knob=SliderThumb(b,c.value,s);
            fill(track,theme.dark?0xff42516a:0xffc7ced9,2*s);
            fill({track.left,track.top,knob.x,track.bottom},c.enabled?theme.Accent():theme.Muted(),2*s);
            p.target->FillEllipse(D2D1::Ellipse({knob.x,knob.y},(down?7.f:6.f)*s,(down?7.f:6.f)*s),p.brush(0xffffffff));
            p.target->FillEllipse(D2D1::Ellipse({knob.x,knob.y},4*s,4*s),p.brush(c.enabled?theme.Accent():theme.Muted()));
            p.text(c.text,{b.right-42*s,b.top+5*s,b.right,b.bottom},12*s,ink);continue;
        }
        if(c.kind==Kind::Swatch) {
            const float x=(b.left+b.right)/2,y=(b.top+b.bottom)/2;
            const float r=std::min(b.right-b.left,b.bottom-b.top)/2-2*s;
            if(c.selected||hot||down){fill(b,down?0x553080ed:0x2285baff,6*s);stroke(b,c.selected?theme.Accent():theme.Border(),6*s);}
            if(c.rainbow){if(p.color_wheel)p.color_wheel({x-r,y-r,x+r,y+r});}
            else if(c.square){const Box tile{x-r,y-r,x+r,y+r};fill(tile,c.color,4*s);stroke(tile,theme.Border(),4*s);}
            else {p.target->FillEllipse(D2D1::Ellipse({x,y},r,r),p.brush(c.color));p.target->DrawEllipse(D2D1::Ellipse({x,y},r,r),p.brush(theme.Border()),s);}
            continue;
        }
        if(!c.indicator_backed&&(c.kind==Kind::Dropdown||c.kind==Kind::TextField||c.selected||c.primary||hot||down)) {
            const uint32_t background=c.primary?theme.Accent():(down?0x55488fe0:(c.selected?0x3364acff:(theme.dark?0x223e526e:0x50ffffff)));
            fill(b,background,theme.Radius());stroke(b,c.selected?0x6692c4ff:theme.Border(),theme.Radius());
        }
        float left=b.left+8*s;
        if(c.icon>=0){Box icon{b.left+4*s,b.top+(b.bottom-b.top-28*s)/2,b.left+32*s,b.top+(b.bottom-b.top+28*s)/2};if(c.text.empty())icon=b;p.icon(c.icon,icon,ink);left=b.left+36*s;}
        if(c.icon==-2){const float x=b.left+11*s,y=(b.top+b.bottom)/2;p.target->DrawEllipse(D2D1::Ellipse({x,y},5*s,5*s),p.brush(ink),1.2f*s);p.target->DrawLine({x-3*s,y-4*s},{x+3*s,y+4*s},p.brush(ink),1.2f*s);left=b.left+22*s;}
        if(!c.text.empty()){
            Box text_bounds{left,b.top+(b.bottom-b.top-19*s)/2,b.right-(c.kind==Kind::Dropdown?15:3)*s,b.bottom};
            if(c.kind==Kind::Button&&c.icon==-1){
                const auto size=p.measure_text(c.text,13*s);
                text_bounds.left=b.left+std::max(0.f,(b.right-b.left-size.x)/2);
                text_bounds.top=b.top+std::max(0.f,(b.bottom-b.top-size.y)/2);
                text_bounds.right=b.right;
            }
            p.text(c.text,text_bounds,13*s,ink);
        }
        if(c.kind==Kind::Dropdown){const float x=b.right-8*s,y=(b.top+b.bottom)/2;p.target->DrawLine({x-3*s,y-2*s},{x,y+1*s},p.brush(ink),1.2f*s);p.target->DrawLine({x,y+1*s},{x+3*s,y-2*s},p.brush(ink),1.2f*s);}
    }
}
}
