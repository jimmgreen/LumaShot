#include "ui/render.h"
#include <array>
#include <stdexcept>
namespace lumashot {
void Renderer::Picker(const Frame& frame,const ViewState& state) {
    const auto& p=state.picker;if(!p.open)return;const float s=p.scale;
    const auto rect=[](Box b){return D2D1::RectF(b.left,b.top,b.right,b.bottom);};
    const auto ink=state.dark?0xffedf4ffu:0xff243142u,muted=state.dark?0xffb6c8dfu:0xff62748au;
    Panel(p.bounds,acrylic_bitmap_.Get(),frame,state.dark,14*s);
    Text(L"自定义颜色",{p.bounds.left+16*s,p.bounds.top+16*s,p.bounds.right-44*s,p.bounds.top+40*s},14*s,ink);
    Icon(11,p.Part(2),muted);
    const auto square=p.Part(0),hue=p.Part(1);
    const auto gradient=[&](Box b,const D2D1_GRADIENT_STOP* stops,UINT count,bool vertical){
        ComPtr<ID2D1GradientStopCollection> collection;ComPtr<ID2D1LinearGradientBrush> gradient_brush;
        if(FAILED(target_->CreateGradientStopCollection(stops,count,D2D1_GAMMA_2_2,D2D1_EXTEND_MODE_CLAMP,collection.GetAddressOf())))throw std::runtime_error("Create color gradient");
        const auto props=D2D1::LinearGradientBrushProperties({b.left,b.top},vertical?D2D1_POINT_2F{b.left,b.bottom}:D2D1_POINT_2F{b.right,b.top});
        if(FAILED(target_->CreateLinearGradientBrush(props,collection.Get(),gradient_brush.GetAddressOf())))throw std::runtime_error("Create color brush");
        target_->FillRectangle(rect(b),gradient_brush.Get());
    };
    ComPtr<ID2D1RoundedRectangleGeometry> clip;factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect(square),5*s,5*s),clip.GetAddressOf());
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip.Get()),nullptr);
    const uint32_t saturated=FromHsv({p.hsv.h,1,1});
    const D2D1_GRADIENT_STOP horizontal[]={{0,D2D1::ColorF(0xffffff)},{1,D2D1::ColorF(saturated&0xffffff)}};
    gradient(square,horizontal,2,false);
    const D2D1_GRADIENT_STOP vertical[]={{0,D2D1::ColorF(0,0.0f)},{1,D2D1::ColorF(0,1.0f)}};
    gradient(square,vertical,2,true);target_->PopLayer();
    std::array<D2D1_GRADIENT_STOP,7> rainbow{};for(size_t i=0;i<rainbow.size();++i)rainbow[i]={float(i)/6,D2D1::ColorF(FromHsv({float(i)*60,1,1})&0xffffff)};
    clip.Reset();factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect(hue),8*s,8*s),clip.GetAddressOf());
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip.Get()),nullptr);gradient(hue,rainbow.data(),7,true);target_->PopLayer();
    const D2D1_POINT_2F position{square.left+p.hsv.s*(square.right-square.left),square.top+(1-p.hsv.v)*(square.bottom-square.top)};
    Brush(0x77000000);target_->DrawEllipse(D2D1::Ellipse(position,6*s,6*s),brush_.Get(),3*s);Brush(0xffffffff);target_->DrawEllipse(D2D1::Ellipse(position,6*s,6*s),brush_.Get(),1.5f*s);
    const D2D1_POINT_2F handle{(hue.left+hue.right)/2,hue.top+p.hsv.h/360*(hue.bottom-hue.top)};
    Brush(0xffffffff);target_->FillEllipse(D2D1::Ellipse(handle,7*s,7*s),brush_.Get());Brush(saturated);target_->FillEllipse(D2D1::Ellipse(handle,4*s,4*s),brush_.Get());
    Brush(p.color);target_->FillRoundedRectangle(D2D1::RoundedRect(rect(p.Part(7)),5*s,5*s),brush_.Get());
    for(int id=3;id<=6;++id) {
        const auto b=p.Part(id);Brush(state.dark?0x55415470:0x99ffffff);target_->FillRoundedRectangle(D2D1::RoundedRect(rect(b),4*s,4*s),brush_.Get());
        if(p.field==id){Brush(p.invalid?0xffff4d4f:0xff1686ff);target_->DrawRoundedRectangle(D2D1::RoundedRect(rect(b),4*s,4*s),brush_.Get(),1*s);}
        if(id>=4)Text(std::wstring(1,L"RGB"[id-4]),{b.left-20*s,b.top+3*s,b.left-4*s,b.bottom},12*s,muted);
        const auto text=p.field==id?p.input:(id==3?ColorHex(p.color):std::to_wstring((p.color>>((6-id)*8))&255));
        if(p.field==id&&p.replace){Brush(0x441686ff);target_->FillRectangle({b.left+3*s,b.top+3*s,b.right-16*s,b.bottom-3*s},brush_.Get());}
        Text(text,{b.left+5*s,b.top+4*s,b.right-15*s,b.bottom},11*s,ink);
        const float x=b.right-8*s,y=(b.top+b.bottom)/2;Brush(muted);
        target_->DrawLine({x-2*s,y-3*s},{x,y-5*s},brush_.Get(),1*s);target_->DrawLine({x,y-5*s},{x+2*s,y-3*s},brush_.Get(),1*s);
        target_->DrawLine({x-2*s,y+3*s},{x,y+5*s},brush_.Get(),1*s);target_->DrawLine({x,y+5*s},{x+2*s,y+3*s},brush_.Get(),1*s);
    }
    Text(p.invalid?L"请输入有效的 HEX 或 0–255":L"最近使用的颜色",{p.bounds.left+16*s,p.bounds.top+222*s,p.bounds.right-16*s,p.bounds.top+244*s},11*s,p.invalid?0xffff4d4f:muted);
    for(int i=0;i<8;++i){const auto b=p.Part(10+i);const D2D1_POINT_2F c{(b.left+b.right)/2,(b.top+b.bottom)/2};
        if(i<7&&size_t(i)<p.recent.size()){Brush(p.recent[i]);target_->FillEllipse(D2D1::Ellipse(c,10*s,10*s),brush_.Get());if(p.recent[i]==p.color){Brush(0xff1686ff);target_->DrawEllipse(D2D1::Ellipse(c,13*s,13*s),brush_.Get(),1*s);}}
        else if(i==7){Brush(state.dark?0x55415470:0xaaffffff);target_->FillEllipse(D2D1::Ellipse(c,12*s,12*s),brush_.Get());Brush(muted);target_->DrawLine({c.x-5*s,c.y},{c.x+5*s,c.y},brush_.Get(),1.5f*s);target_->DrawLine({c.x,c.y-5*s},{c.x,c.y+5*s},brush_.Get(),1.5f*s);}
    }
}
}
