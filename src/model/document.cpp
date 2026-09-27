#include "model/document.h"
#include "model/arrow.h"
#include "model/selection.h"
#include "model/number.h"
#include <algorithm>
#include <cmath>

namespace lumashot {

Box Normalize(Point a, Point b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}
RECT PixelRect(Box box) {
    return {static_cast<LONG>(std::floor(box.left)), static_cast<LONG>(std::floor(box.top)),
        static_cast<LONG>(std::ceil(box.right)), static_cast<LONG>(std::ceil(box.bottom))};
}
Box NumberDetailBounds(const Mark& mark){
    if(mark.number_detail)return *mark.number_detail;
    const auto badge=Normalize(mark.a,mark.b);const Point center{(badge.left+badge.right)/2,(badge.top+badge.bottom)/2};
    if(mark.number_combo==NumberCombo::Magnify){const float r=mark.number_size*1.25f;return {mark.number_target.x-r,mark.number_target.y-r,mark.number_target.x+r,mark.number_target.y+r};}
    auto box=Normalize(center,mark.number_target);
    if(mark.number_combo==NumberCombo::Text){
        const float gap=mark.number_size*.2f;
        if(mark.number_target.x>=center.x){box.left=badge.right+gap;box.right=std::max(box.right,box.left+mark.number_size*4);}
        else {box.right=badge.left-gap;box.left=std::min(box.left,box.right-mark.number_size*4);}
        box.top=badge.top;box.bottom=std::max(box.bottom,box.top+std::max(mark.number_size*1.5f,mark.number_text_size*1.5f+mark.number_size*.5f));
    }
    return box;
}
Box LocalBounds(const Mark& mark) {
    Box box = Normalize(mark.a, mark.b);
    if(mark.tool==Tool::Number&&mark.number_combo!=NumberCombo::Plain){const auto detail=NumberDetailBounds(mark);box.left=std::min(box.left,detail.left);box.top=std::min(box.top,detail.top);box.right=std::max(box.right,detail.right);box.bottom=std::max(box.bottom,detail.bottom);}
    if(mark.tool==Tool::Arrow){
        for(const auto& path:BuildArrow(mark))for(const auto p:path.points){
            box.left=std::min(box.left,p.x);box.top=std::min(box.top,p.y);box.right=std::max(box.right,p.x);box.bottom=std::max(box.bottom,p.y);
        }
    }
    for (const auto point : mark.points) {
        box.left = std::min(box.left, point.x);
        box.top = std::min(box.top, point.y);
        box.right = std::max(box.right, point.x);
        box.bottom = std::max(box.bottom, point.y);
    }
    if (mark.tool == Tool::Mosaic && !mark.points.empty()) {
        const float padding = mark.mosaic_brush / 2 + mark.mosaic_cell;
        box.left -= padding; box.top -= padding;
        box.right += padding; box.bottom += padding;
    }
    if(mark.number_leader)for(auto p:*mark.number_leader){box.left=std::min(box.left,p.x);box.top=std::min(box.top,p.y);box.right=std::max(box.right,p.x);box.bottom=std::max(box.bottom,p.y);}
    return box;
}
Box Bounds(const Mark& mark){auto b=LocalBounds(mark);const auto center=MarkCenter(mark);Box result{center.x,center.y,center.x,center.y};for(auto p:{Point{b.left,b.top},Point{b.right,b.top},Point{b.right,b.bottom},Point{b.left,b.bottom}}){p=RotatePoint(p,center,mark.rotation);result.left=std::min(result.left,p.x);result.top=std::min(result.top,p.y);result.right=std::max(result.right,p.x);result.bottom=std::max(result.bottom,p.y);}return result;}
void Translate(Mark& mark, Point delta) {
    if(mark.number_detail){auto& b=*mark.number_detail;b.left+=delta.x;b.right+=delta.x;b.top+=delta.y;b.bottom+=delta.y;}
    if(mark.number_leader)for(auto& p:*mark.number_leader){p.x+=delta.x;p.y+=delta.y;}
    if(mark.tool==Tool::Number){mark.number_target.x+=delta.x;mark.number_target.y+=delta.y;}
    mark.a.x += delta.x; mark.a.y += delta.y;
    mark.b.x += delta.x; mark.b.y += delta.y;
    for (auto& point : mark.points) { point.x += delta.x; point.y += delta.y; }
}
Document Document::Snapshot() const {
    Document result;result.marks=marks;result.selected=selected;result.selected_part=selected_part;return result;
}
void Document::Reset() {
    marks.clear(); undo_.clear(); redo_.clear(); selected = -1;
}
void Document::Checkpoint() {
    if (undo_.size() >= 100) undo_.erase(undo_.begin());
    undo_.push_back(marks);
    redo_.clear();
}
void Document::Add(Mark mark) {
    Checkpoint(); marks.push_back(std::move(mark)); selected = -1;
}
void Document::Undo() {
    if (undo_.empty()) return;
    redo_.push_back(std::move(marks)); marks = std::move(undo_.back()); undo_.pop_back(); selected = -1;
}
void Document::Redo() {
    if (redo_.empty()) return;
    undo_.push_back(std::move(marks)); marks = std::move(redo_.back()); redo_.pop_back(); selected = -1;
}
void Document::DeleteSelected() {
    if (selected < 0 || selected >= static_cast<int>(marks.size())) return;
    Checkpoint(); marks.erase(marks.begin() + selected); selected = -1;
}
int Document::HitTest(Point p) const {
    for (size_t i = marks.size(); i-- > 0;) {
        if (HitMark(marks[i],p)) return static_cast<int>(i);
    }
    return -1;
}

Point ConstrainStraightLine(Point origin, Point current) {
    const float dx = current.x - origin.x;
    const float dy = current.y - origin.y;
    const float dist = std::hypot(dx, dy);
    if (dist < 1.0f) return current;

    const float rad = std::atan2(dy, dx);
    const float pi = 3.14159265358979323846f;
    const float deg = rad * 180.0f / pi;

    const float snapped_deg = std::round(deg / 45.0f) * 45.0f;
    const float snapped_rad = snapped_deg * pi / 180.0f;

    if (std::abs(std::sin(snapped_rad)) < 1e-4f) {
        return {origin.x + (dx >= 0 ? dist : -dist), origin.y};
    }
    if (std::abs(std::cos(snapped_rad)) < 1e-4f) {
        return {origin.x, origin.y + (dy >= 0 ? dist : -dist)};
    }
    const float d = dist * 0.7071067811865475f;
    return {origin.x + (dx >= 0 ? d : -d), origin.y + (dy >= 0 ? d : -d)};
}
}

