#include "ui/render.h"
#include <algorithm>
#include <cmath>
namespace lumashot {
void Renderer::Toolbar(const Frame& frame,const Document& document,const ViewState& state) {
    const bool acrylic_surface=state.toolbar.custom.surface_style==ToolbarSurfaceStyle::Acrylic;
    const bool toolbar_dark=state.dark&&acrylic_surface;
    ui::PaintContext paint;
    paint.target=target_.Get();
    paint.brush=[this](uint32_t color){Brush(color);return brush_.Get();};
    paint.text=[this](const std::wstring& text,Box b,float size,uint32_t color){Text(text,b,size,color,false);};
    paint.measure_text=[this](const std::wstring& text,float size){return MeasureText(text,size);};
    paint.icon=[this,&state,&paint](int id,Box b,uint32_t color){
        if(id>=0&&id<=19){
            const float size=std::min({b.right-b.left,b.bottom-b.top,28*state.toolbar.scale});
            const float cx=(b.left+b.right)/2,cy=(b.top+b.bottom)/2;
            b={cx-size/2,cy-size/2,cx+size/2,cy+size/2};
        }
        const auto& motion=state.toolbar_visual;
        if(motion.ready&&motion.effects&&id>=0&&id<int(ui::ToolbarSlots)){
            const float hot=motion.hover[size_t(id)],down=motion.press[size_t(id)],zoom=1+.06f*hot-.1f*down;
            const float cx=(b.left+b.right)/2,cy=(b.top+b.bottom)/2+(-1.5f*hot+.8f*down)*state.toolbar.scale;
            const float w=(b.right-b.left)*zoom/2,h=(b.bottom-b.top)*zoom/2;b={cx-w,cy-h,cx+w,cy+h};
        }
        if(id==15){
            const float u=std::min(b.right-b.left,b.bottom-b.top)/32,cx=(b.left+b.right)/2,cy=(b.top+b.bottom)/2;
            const auto line=[&](float x,float y,float x2,float y2){target_->DrawLine({cx+x*u,cy+y*u},{cx+x2*u,cy+y2*u},paint.brush(color),1.65f*u);};
            for(float side:{-1.f,1.f}){line(side*9,-4,side*9,-9);line(side*9,-9,side*4,-9);line(side*9,4,side*9,9);line(side*9,9,side*4,9);}
            line(-4,-3,4,-3);line(-4,1,4,1);line(-4,5,1,5);
        }else if(id==ToolbarPropertiesId){
            // Three sliders: the "属性" (properties) toggle.
            const float u=std::min(b.right-b.left,b.bottom-b.top)/32,cx=(b.left+b.right)/2,cy=(b.top+b.bottom)/2;
            for(int k=0;k<3;++k){const float y=cy+float(k-1)*6*u,knob=cx+(k==1?4.f:-4.f)*u;
                target_->DrawLine({cx-9*u,y},{cx+9*u,y},paint.brush(color),1.6f*u);target_->FillEllipse(D2D1::Ellipse({knob,y},2.6f*u,2.6f*u),paint.brush(color));}
        }else Icon(id,b,color);
    };
    paint.surface=[this,&frame,toolbar_dark,acrylic_surface](Box b,float radius){Panel(b,acrylic_bitmap_.Get(),frame,toolbar_dark,radius,acrylic_surface);};
    paint.color_wheel=[this](Box b){
        if(!color_wheel_bitmap_){
            // Generate once per render target, with premultiplied edge pixels.
            Frame wheel;wheel.bounds={0,0,64,64};wheel.pixels.resize(64*64);
            for(int y=0;y<64;++y)for(int x=0;x<64;++x){
                const float dx=x-31.5f,dy=y-31.5f,r=std::sqrt(dx*dx+dy*dy);
                const float a=std::clamp(32-r,0.f,1.f);
                const uint32_t rgb=FromHsv({std::atan2(dy,dx)*180/3.14159265f+180,std::min(r/22,1.f),1});
                const auto channel=[&](int shift){return uint32_t(std::lround(float((rgb>>shift)&255)*a));};
                wheel.pixels[y*64+x]=(uint32_t(std::lround(a*255))<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);
            }
            color_wheel_bitmap_=Bitmap(wheel,D2D1_ALPHA_MODE_PREMULTIPLIED);
        }
        target_->DrawBitmap(color_wheel_bitmap_.Get(),D2D1::RectF(b.left,b.top,b.right,b.bottom),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    const auto controls=ToolbarControls(state,document);
    const ui::Theme theme{state.toolbar.scale,toolbar_dark};
    ui::DrawControls(paint,theme,std::span(controls).first(1));
    const auto toolbar_bounds=state.toolbar.bounds;const auto& card_full=state.toolbar.card_full;
    const auto in_card=[&card_full](const ui::Control& c){return card_full.right>card_full.left&&c.bounds.left>=card_full.left-.5f&&c.bounds.right<=card_full.right+.5f&&
        c.bounds.top>=card_full.top-.5f&&c.bounds.bottom<=card_full.bottom+.5f;};
    target_->PushAxisAlignedClip(D2D1::RectF(toolbar_bounds.left,toolbar_bounds.top,toolbar_bounds.right,toolbar_bounds.bottom),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto& motion=state.toolbar_visual;
    if(motion.ready){
        const auto pill=motion.indicator;const float radius=theme.Radius();
        const auto rounded=D2D1::RoundedRect(D2D1::RectF(pill.left,pill.top,pill.right,pill.bottom),radius,radius);
        target_->FillRoundedRectangle(rounded,paint.brush(0x3364acff));
        target_->DrawRoundedRectangle(rounded,paint.brush(0x6692c4ff),theme.scale);
    }
    for(size_t i=1;i<controls.size();++i){
        auto c=controls[i];int hovered=state.hover;if(in_card(c))continue;
        if(motion.ready&&c.id>=0&&c.id<int(ui::ToolbarSlots)&&c.kind==ui::Kind::Button){
            const float hot=motion.hover[size_t(c.id)],down=motion.press[size_t(c.id)];
            const auto r=D2D1::RoundedRect(D2D1::RectF(c.bounds.left,c.bounds.top,c.bounds.right,c.bounds.bottom),theme.Radius(),theme.Radius());
            if(c.enabled&&!c.primary){
                if(hot>0){const auto a=uint32_t(std::lround(hot*(theme.dark?28:90)));target_->FillRoundedRectangle(r,paint.brush((a<<24)|(theme.dark?0x3e526e:0xe1e7ef)));}
                if(down>0){const auto a=uint32_t(std::lround(down*65));target_->FillRoundedRectangle(r,paint.brush((a<<24)|0x488fe0));}
                c.indicator_backed=true;
            }
            hovered=-1;
        }
        ui::DrawControls(paint,theme,std::span(&c,1),hovered,state.property_drag);
    }
    target_->PopAxisAlignedClip();
    if(state.toolbar.CardVisible()){
        // 方案 C: the property card, its own acrylic panel; it grows out of the bar while expanding.
        const auto card=state.toolbar.card;
        paint.surface(card,std::min(14*theme.scale,(card.bottom-card.top)/2));
        target_->PushAxisAlignedClip(D2D1::RectF(card.left,card.top,card.right,card.bottom),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for(size_t i=1;i<controls.size();++i)if(in_card(controls[i]))ui::DrawControls(paint,theme,std::span(&controls[i],1),state.hover,state.property_drag);
        target_->PopAxisAlignedClip();
    }
    if(state.dropdown.Open()){
        const auto& popup=state.dropdown;const float s=popup.scale;const auto b=popup.bounds;
        const ui::Theme menu{s,toolbar_dark};
        Panel(b,acrylic_bitmap_.Get(),frame,toolbar_dark,8*s,acrylic_surface);
        for(int i=0;i<static_cast<int>(popup.items.size());++i){
            const auto row=popup.Row(i);
            float text_left=ui::DrawDropdownRow(paint,menu,row,i==popup.hover,i==popup.selected);
            if(popup.property==ToolbarMoreId){
                // Tool icon, label, and a pin at the row end that puts the entry back on the panel.
                const float cy=(row.top+row.bottom)/2;const auto pin=ToolbarMorePinBox(popup,i);
                paint.icon(popup.items[i].second,{text_left,cy-9*s,text_left+18*s,cy+9*s},menu.Ink());text_left+=26*s;
                const float px=(pin.left+pin.right)/2,py=(pin.top+pin.bottom)/2;
                Icon(13,{px-8*s,py-8*s,px+8*s,py+8*s},i==popup.hover?menu.Accent():menu.Muted());
                Text(popup.items[i].first,{text_left,row.top+5*s,pin.left-4*s,row.bottom},13*s,menu.Ink(),false);
                continue;
            }
            if(popup.property==49||popup.property==66){
                Mark sample;sample.tool=Tool::Arrow;sample.color=menu.Accent();sample.width=1.5f*s;sample.arrow_size=11*s;
                sample.a={text_left+3*s,row.top+17*s};sample.b={text_left+53*s,row.top+13*s};
                if(popup.property==49)sample.arrow_type=static_cast<ArrowType>(popup.items[i].second);
                else sample.arrow_head=static_cast<ArrowHead>(popup.items[i].second);
                MarkShape(frame,sample,0);text_left+=66*s;
            }
            Text(popup.items[i].first,{text_left,row.top+5*s,row.right-8*s,row.bottom},13*s,menu.Ink(),false);
        }
        return;
    }
    if(state.property_drag<0&&((state.hover>=0&&state.hover<=18)||state.hover==28||state.hover==70)) {
        const float s=state.toolbar.scale;const Box anchor=(state.hover==28||state.hover==70)?state.toolbar.Property(state.hover):state.toolbar.Button(state.hover);
        const RECT monitor=monitor_.right>monitor_.left?monitor_:frame.bounds;
        ui::DrawTooltip(paint,{s,toolbar_dark},(state.hover==28||state.hover==70)?L"自定义颜色":ToolbarActionName(state.hover),anchor,
            {float(monitor.left),float(monitor.top),float(monitor.right),float(monitor.bottom)});
    }
}
}
