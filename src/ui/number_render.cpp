#include "ui/render.h"
#include "model/number.h"
#include "model/number_label.h"
#include "model/note_color.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace lumashot {
void Renderer::TagBackground(Box box,uint32_t fill,uint32_t border,TagMetrics metrics){
    if(box.right<=box.left||box.bottom<=box.top)return;
    const float inset=std::min({metrics.stroke/2,(box.right-box.left)/2,(box.bottom-box.top)/2});
    const float radius=std::min({metrics.radius,(box.right-box.left)/2,(box.bottom-box.top)/2});
    const auto shape=D2D1::RoundedRect({box.left+inset,box.top+inset,box.right-inset,box.bottom-inset},radius,radius);
    Brush(fill);target_->FillRoundedRectangle(shape,brush_.Get());
    Brush(border);target_->DrawRoundedRectangle(shape,brush_.Get(),metrics.stroke);
}
void Renderer::NumberDetail(const Frame& frame,const Mark& mark){
    if(mark.number_combo==NumberCombo::Plain)return;
    const auto badge=Normalize(mark.a,mark.b),detail=NumberDetailBounds(mark);
    const Point center{(badge.left+badge.right)/2,(badge.top+badge.bottom)/2};
    const float size=mark.number_size,stroke=std::max(1.f,size*.06f);
    Brush(mark.color);
    const auto rect=D2D1::RectF(detail.left,detail.top,detail.right,detail.bottom);
    if(mark.number_combo==NumberCombo::Arrow){const float distance=std::hypot(mark.number_target.x-center.x,mark.number_target.y-center.y);if(distance<=size*.5f)return;Mark arrow;arrow.tool=Tool::Arrow;arrow.a={center.x+(mark.number_target.x-center.x)*size*.5f/distance,center.y+(mark.number_target.y-center.y)*size*.5f/distance};arrow.b=mark.number_target;arrow.color=mark.color;arrow.width=stroke;arrow.arrow_size=size*.65f;MarkShape(frame,arrow,0);return;}
    if(mark.number_combo==NumberCombo::Leader){
        auto properties=D2D1::StrokeStyleProperties();properties.dashStyle=D2D1_DASH_STYLE_DASH;properties.dashCap=D2D1_CAP_STYLE_ROUND;properties.lineJoin=D2D1_LINE_JOIN_ROUND;properties.startCap=properties.endCap=D2D1_CAP_STYLE_ROUND;
        ComPtr<ID2D1StrokeStyle> style;if(FAILED(factory_->CreateStrokeStyle(properties,nullptr,0,&style)))throw std::runtime_error("Number leader failed");
        const auto points=NumberLeaderPoints(mark);
        ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;
        if(FAILED(factory_->CreatePathGeometry(&path))||FAILED(path->Open(&sink)))throw std::runtime_error("Number leader failed");
        sink->BeginFigure({points[0].x,points[0].y},D2D1_FIGURE_BEGIN_HOLLOW);
        for(size_t i=1;i+1<points.size();++i){auto a=points[i-1],p=points[i],b=points[i+1];float l=std::hypot(p.x-a.x,p.y-a.y),r=std::hypot(b.x-p.x,b.y-p.y);const float radius=std::min({size*.24f,l*.35f,r*.35f});
            if(l<.01f||r<.01f){sink->AddLine({p.x,p.y});continue;}
            sink->AddLine({p.x+(a.x-p.x)*radius/l,p.y+(a.y-p.y)*radius/l});sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({p.x,p.y},{p.x+(b.x-p.x)*radius/r,p.y+(b.y-p.y)*radius/r}));
        }
        sink->AddLine({points.back().x,points.back().y});
        sink->EndFigure(D2D1_FIGURE_END_OPEN);if(FAILED(sink->Close()))throw std::runtime_error("Number leader failed");
        target_->DrawGeometry(path.Get(),brush_.Get(),stroke,style.Get());
        target_->FillEllipse(D2D1::Ellipse({points.back().x,points.back().y},stroke*1.8f,stroke*1.8f),brush_.Get());return;
    }
    if(mark.number_combo==NumberCombo::Text){
        const auto appearance=ResolveNoteAppearance(frame,mark);const auto metrics=TextTagMetrics(mark.number_text_size);
        if(appearance.background)TagBackground(detail,*appearance.background,appearance.border,metrics);
        const auto content=TagContentBounds(detail,metrics);const auto text=mark.text.empty()?std::wstring(L"双击输入说明"):mark.text;
        text_renderer_.Draw(target_.Get(),text_factory_.Get(),text,FontFormat(mark.number_text_size,true),{content.left,content.top,content.right,content.bottom},D2D1::ColorF(appearance.ink&0xffffff,1));return;
    }
    if(mark.number_combo==NumberCombo::Highlight||mark.number_combo==NumberCombo::DashedBox){
        Brush(mark.color,mark.number_combo==NumberCombo::DashedBox?.08f:.18f);target_->FillRoundedRectangle(D2D1::RoundedRect(rect,size*.2f,size*.2f),brush_.Get());
        if(mark.number_combo==NumberCombo::DashedBox){auto properties=D2D1::StrokeStyleProperties();properties.dashStyle=D2D1_DASH_STYLE_DASH;ComPtr<ID2D1StrokeStyle> style;if(FAILED(factory_->CreateStrokeStyle(properties,nullptr,0,&style)))throw std::runtime_error("Number border failed");Brush(mark.color);target_->DrawRoundedRectangle(D2D1::RoundedRect(rect,size*.2f,size*.2f),brush_.Get(),stroke,style.Get());}
        return;
    }
    if(mark.number_combo==NumberCombo::Magnify){
        const float radius=std::min(detail.right-detail.left,detail.bottom-detail.top)/2;const auto ellipse=D2D1::Ellipse({(detail.left+detail.right)/2,(detail.top+detail.bottom)/2},(detail.right-detail.left)/2,(detail.bottom-detail.top)/2);
        Brush(mark.color,.15f);ComPtr<ID2D1PathGeometry> connector;ComPtr<ID2D1GeometrySink> sink;
        if(FAILED(factory_->CreatePathGeometry(&connector))||FAILED(connector->Open(&sink)))throw std::runtime_error("Number magnifier failed");
        const float angle=std::atan2(mark.number_target.y-center.y,mark.number_target.x-center.x);
        const Point n{-std::sin(angle)*radius,std::cos(angle)*radius};sink->BeginFigure({center.x,center.y},D2D1_FIGURE_BEGIN_FILLED);sink->AddLine({mark.number_target.x+n.x,mark.number_target.y+n.y});sink->AddLine({mark.number_target.x-n.x,mark.number_target.y-n.y});sink->EndFigure(D2D1_FIGURE_END_CLOSED);sink->Close();target_->FillGeometry(connector.Get(),brush_.Get());
        ComPtr<ID2D1EllipseGeometry> clip;if(FAILED(factory_->CreateEllipseGeometry(ellipse,&clip)))throw std::runtime_error("Number magnifier failed");
        target_->PushLayer(D2D1::LayerParameters(rect,clip.Get()),nullptr);
        try {auto source=Bitmap(frame);const float sample=std::min({radius/2,float(frame.Width())/2,float(frame.Height())/2});const float sx=std::clamp(center.x-frame.bounds.left,sample,float(frame.Width())-sample),sy=std::clamp(center.y-frame.bounds.top,sample,float(frame.Height())-sample);target_->DrawBitmap(source.Get(),rect,1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,D2D1::RectF(sx-sample,sy-sample,sx+sample,sy+sample));}catch(...){target_->PopLayer();throw;}
        target_->PopLayer();Brush(mark.color);target_->DrawEllipse(ellipse,brush_.Get(),stroke);
    }
}
void Renderer::Number(const Frame& frame,const Mark& mark){
    const auto b=Normalize(mark.a,mark.b);const float w=b.right-b.left,h=b.bottom-b.top;
    if(w<=0||h<=0)return;
    const float x=(b.left+b.right)/2,y=(b.top+b.bottom)/2,r=std::min(w,h)/2;
    const float border=std::max(1.f,r*.1f);
    const auto tones=ResolveTagColors(frame,mark,b);
    Brush(tones.background);
    const auto circle=D2D1::Ellipse({x,y},r-border/2,r-border/2);
    const auto shape=mark.number_label.empty()?mark.number_shape:NumberShape::Capsule;
    if(shape==NumberShape::Circle||shape==NumberShape::Outline||shape==NumberShape::DoubleCircle){
        if(shape==NumberShape::Outline){Brush(mark.color);target_->DrawEllipse(circle,brush_.Get(),border);}
        else if(shape==NumberShape::DoubleCircle){target_->DrawEllipse(circle,brush_.Get(),border*.65f);target_->FillEllipse(D2D1::Ellipse({x,y},r-border*2,r-border*2),brush_.Get());}
        else {target_->FillEllipse(circle,brush_.Get());Brush(tones.border);target_->DrawEllipse(circle,brush_.Get(),std::max(.5f,mark.number_size/32));}
    }else if(shape==NumberShape::Square||shape==NumberShape::Rounded||shape==NumberShape::Capsule){
        const float radius=shape==NumberShape::Square?0.f:(shape==NumberShape::Capsule?r:r*.3f);
        auto metrics=TextTagMetrics(mark.number_size*.75f);metrics.radius=radius;
        TagBackground(b,tones.background,tones.border,metrics);
    }else if(shape==NumberShape::Pin){
        ComPtr<ID2D1PathGeometry> geometry;ComPtr<ID2D1GeometrySink> sink;
        if(FAILED(factory_->CreatePathGeometry(&geometry))||FAILED(geometry->Open(&sink)))throw std::runtime_error("Number pin creation failed");
        const auto pin=NumberPin(b);
        sink->BeginFigure({pin.tip.x,pin.tip.y},D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine({pin.left.x,pin.left.y});
        sink->AddArc(D2D1::ArcSegment({pin.right.x,pin.right.y},{pin.radius,pin.radius},0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_LARGE));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);if(FAILED(sink->Close()))throw std::runtime_error("Number pin creation failed");
        target_->FillGeometry(geometry.Get(),brush_.Get());Brush(tones.border);target_->DrawGeometry(geometry.Get(),brush_.Get(),border*.5f);
    }else {
        std::vector<D2D1_POINT_2F> points;
        if(shape==NumberShape::Flag)points={{b.left,b.top},{b.right,b.top},{b.right,b.bottom},{x,b.bottom-r*.5f},{b.left,b.bottom}};
        if(shape==NumberShape::Hexagon)for(int i=0;i<6;++i){const float angle=float(i)*3.14159265f/3-3.14159265f/2;points.push_back({x+r*std::cos(angle),y+r*std::sin(angle)});}
        if(shape==NumberShape::Corner)points={{b.left,b.top},{b.right,b.top},{b.right,b.bottom}};
        ComPtr<ID2D1PathGeometry> geometry;ComPtr<ID2D1GeometrySink> sink;
        if(FAILED(factory_->CreatePathGeometry(&geometry))||FAILED(geometry->Open(&sink)))throw std::runtime_error("Number shape creation failed");
        sink->BeginFigure(points.front(),D2D1_FIGURE_BEGIN_FILLED);for(size_t i=1;i<points.size();++i)sink->AddLine(points[i]);sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if(FAILED(sink->Close()))throw std::runtime_error("Number shape creation failed");target_->FillGeometry(geometry.Get(),brush_.Get());
    }
    const auto digits=NumberCaption(mark);
    auto box=b;
    if(shape==NumberShape::Flag)box.bottom-=r*.45f;
    if(shape==NumberShape::Pin){const auto pin=NumberPin(b);const float inner=pin.radius*.61f;box={pin.center.x-inner,pin.center.y-inner,pin.center.x+inner,pin.center.y+inner};}
    if(shape==NumberShape::Corner){box.left+=w*.4f;box.bottom-=h*.4f;}
    float font=std::min({r*1.12f,(box.right-box.left)*.8f/(float(digits.size())*.65f),(box.bottom-box.top)/1.4f});
    if(!mark.number_label.empty())font=std::min(mark.number_size,r*2)*.56f;
    if(shape==NumberShape::Pin){
        const auto pin=NumberPin(b);const float diameter=pin.radius*1.22f;
        // Font em includes ascender/descender space: fit the visible digits,
        // not the entire line box, into the white center.
        font=std::min(diameter*1.05f,diameter*.88f/(float(digits.size())*.58f));
        box.top=pin.center.y-font*.65f;box.bottom=pin.center.y+font*.7f;
    }else box.top+=(box.bottom-box.top-font*1.3f)/2;
    const uint32_t ink=shape==NumberShape::Outline?mark.color:tones.ink;
    text_renderer_.Draw(target_.Get(),text_factory_.Get(),digits,FontFormat(font,false,true,TextAlign::Center),{box.left,box.top,box.right,box.bottom},D2D1::ColorF(ink&0xffffff,float(ink>>24)/255));
}
}
