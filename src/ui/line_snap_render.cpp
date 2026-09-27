#include "ui/render.h"
#include <algorithm>
namespace lumashot {
void Renderer::SnapGuide(const ViewState& state,const std::optional<Mark>& draft){
    if(state.busy||state.tool!=Tool::Pen)return;
    const float s=state.toolbar.scale;
    struct RestoreAntialias {
        ID2D1RenderTarget* target;D2D1_ANTIALIAS_MODE mode;
        ~RestoreAntialias(){target->SetAntialiasMode(mode);}
    }restore{target_.Get(),target_->GetAntialiasMode()};
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Single filled vector dots, without a second contrasting outline.
    if(state.polyline_active&&draft){
        Brush(0xff087eff);
        for(size_t i=0;i<std::min(state.polyline_confirmed,draft->points.size());++i){
            const auto p=draft->points[i];
            target_->FillEllipse(D2D1::Ellipse({p.x,p.y},2.2f*s,2.2f*s),brush_.Get());
        }
    }
    if(!state.line_snap||!state.pen_snap)return;
    const auto& snap=*state.line_snap;const auto p=snap.point;const float r=4.5f*s;
    if(snap.kind==LineSnapKind::None)return;
    const bool object=snap.kind==LineSnapKind::Endpoint||snap.kind==LineSnapKind::Midpoint||snap.kind==LineSnapKind::Nearest;
    if(!object){
        Brush(0x88087eff);target_->DrawLine({snap.origin.x,snap.origin.y},{p.x,p.y},brush_.Get(),s);
    }
    // One vector path and one stroke, equivalent to an unfilled SVG path with
    // stroke-linejoin/linecap="round". No white halo, no overlapping line caps.
    const auto check=[](HRESULT hr){CheckWin32(SUCCEEDED(hr),"Snap vector geometry");};
    ComPtr<ID2D1PathGeometry> path;check(factory_->CreatePathGeometry(&path));
    ComPtr<ID2D1GeometrySink> sink;check(path->Open(&sink));
    const auto point=[&](float x,float y){return D2D1::Point2F(p.x+x*r,p.y+y*r);};
    const auto begin=[&](float x,float y){sink->BeginFigure(point(x,y),D2D1_FIGURE_BEGIN_HOLLOW);};
    const auto line=[&](float x,float y){sink->AddLine(point(x,y));};
    if(snap.kind==LineSnapKind::Midpoint){begin(0,-1);line(-.9f,.8f);line(.9f,.8f);}
    else if(snap.kind==LineSnapKind::Nearest){begin(-1,-1);line(1,-1);line(-1,1);line(1,1);}
    else if(snap.kind==LineSnapKind::Endpoint){begin(-1,-1);line(1,-1);line(1,1);line(-1,1);}
    else {begin(-1,0);line(1,0);sink->EndFigure(D2D1_FIGURE_END_OPEN);begin(0,-1);line(0,1);}
    sink->EndFigure(object?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);check(sink->Close());
    auto properties=D2D1::StrokeStyleProperties();properties.lineJoin=D2D1_LINE_JOIN_ROUND;
    properties.startCap=properties.endCap=D2D1_CAP_STYLE_ROUND;
    ComPtr<ID2D1StrokeStyle> stroke;check(factory_->CreateStrokeStyle(properties,nullptr,0,&stroke));
    Brush(object?0xff008a70:0xff087eff);
    target_->DrawGeometry(path.Get(),brush_.Get(),1.25f*s,stroke.Get());
}
}
