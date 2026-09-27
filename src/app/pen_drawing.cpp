#include "app/application.h"
#include "model/mark_properties.h"
#include <cmath>
namespace lumashot {
Point Application::PenSnapPoint(Point point,std::optional<Point> anchor){
    const auto snap=SnapLinePoint(point,anchor,document_,line_vertices_,state_.selection,state_.toolbar.scale,state_.pen_snap);
    state_.line_snap=snap.kind==LineSnapKind::None?std::nullopt:std::optional<LineSnap>{snap};
    return snap.point;
}
void Application::PenHint(){
    if(state_.PropertyTool()!=Tool::Pen)return;
    if(!line_vertices_.empty())state_.hint=L"连续直线 · 单击添点 · 双击 / Enter / 右键完成 · Backspace / Ctrl+Z 退一点 · Esc 取消本条";
    else if(state_.pen_polyline)state_.hint=L"连续直线 · 单击起点，再逐点点击 · 捕捉可开关 · V 编辑已有标注";
    else state_.hint=state_.pen_straight?L"直线 · 拖动起终点 · 捕捉端点 / 中点 / 最近点 / 水平 / 垂直 / 45°，可关闭 · 无需 Shift":L"自由画笔 · 按住左键绘制 · 属性板可切换直线 / 连续直线";
}
void Application::PolylineClick(Point point){
    point=PenSnapPoint(point,line_vertices_.empty()?std::nullopt:std::optional<Point>{line_vertices_.back()});
    if(line_vertices_.empty()){
        document_.selected=-1;document_.selected_part=EditPart::Whole;SyncSelectedProperties();
        Mark mark;mark.tool=Tool::Pen;mark.a=mark.b=point;mark.points={point};
        mark.pen_straight=true;mark.pen_polyline=true;mark.pen_mode=state_.pen_mode;
        mark.color=state_.ActiveColor();mark.width=state_.ActiveWidth()*state_.toolbar.scale;
        mark.pen_opacity=state_.PenOpacity();mark.pen_smoothing=state_.pen_smoothing;
        draft_=std::move(mark);line_vertices_.push_back(point);state_.polyline_active=true;
    }else if(std::hypot(point.x-line_vertices_.back().x,point.y-line_vertices_.back().y)>.5f){
        line_vertices_.push_back(point);
    }
    state_.polyline_confirmed=line_vertices_.size();
    if(draft_){draft_->points=line_vertices_;draft_->a=line_vertices_.front();draft_->b=line_vertices_.back();}
    if(line_vertices_.size()>3&&line_vertices_.front()==line_vertices_.back())EndPolyline(false);
    PenHint();Invalidate();
}
void Application::PolylinePreview(Point point){
    if(line_vertices_.empty()||!draft_)return;
    draft_->points=line_vertices_;draft_->b=line_vertices_.back();
    state_.line_snap.reset();
    if(Contains(state_.selection,point)&&!Contains(state_.toolbar.bounds,point)){
        point=PenSnapPoint(point,line_vertices_.back());
        if(point!=line_vertices_.back())draft_->points.push_back(point);
        draft_->b=point;
    }
}
void Application::EndPolyline(bool cancel){
    if(line_vertices_.empty())return;
    if(!cancel&&draft_&&line_vertices_.size()>1){
        draft_->points=line_vertices_;draft_->a=line_vertices_.front();draft_->b=line_vertices_.back();
        document_.Add(std::move(*draft_));document_.selected=static_cast<int>(document_.marks.size())-1;
        document_.selected_part=EditPart::Whole;
    }
    line_vertices_.clear();state_.polyline_confirmed=0;state_.polyline_active=false;draft_.reset();state_.line_snap.reset();
    SyncSelectedProperties();PenHint();Invalidate();
}
void Application::UndoPolylinePoint(){
    if(line_vertices_.empty())return;
    line_vertices_.pop_back();state_.polyline_confirmed=line_vertices_.size();
    if(line_vertices_.empty()){state_.polyline_active=false;draft_.reset();state_.line_snap.reset();PenHint();}
    else PolylinePreview(state_.pointer);
    Invalidate();
}
}
