#include "model/line_snap.h"
#include "model/selection.h"
#include <algorithm>
#include <cmath>
namespace lumashot {
LineSnap SnapLinePoint(Point pointer,std::optional<Point> anchor,const Document& document,
    std::span<const Point> vertices,Box bounds,float scale,bool enabled){
    pointer.x=std::clamp(pointer.x,bounds.left,bounds.right);
    pointer.y=std::clamp(pointer.y,bounds.top,bounds.bottom);
    LineSnap result{pointer,anchor.value_or(pointer),LineSnapKind::None};
    if(!enabled)return result;
    scale=std::isfinite(scale)&&scale>0?scale:1.f;
    const float radius=8*scale;float nearest=radius+.001f;
    const auto endpoint=[&](Point p){
        if(!Contains(bounds,p)||(anchor&&p==*anchor))return;
        const float distance=std::hypot(p.x-pointer.x,p.y-pointer.y);
        if(distance<=radius&&distance<nearest){nearest=distance;result.point=p;result.kind=LineSnapKind::Endpoint;}
    };
    for(auto it=vertices.rbegin();it!=vertices.rend();++it)endpoint(*it);
    for(auto it=document.marks.rbegin();it!=document.marks.rend();++it){
        const auto& mark=*it;if(mark.tool!=Tool::Pen&&mark.tool!=Tool::Arrow)continue;
        const auto center=mark.rotation==0?Point{}:MarkCenter(mark);
        const auto add=[&](Point p){endpoint(mark.rotation==0?p:RotatePoint(p,center,mark.rotation));};
        if(mark.tool==Tool::Pen&&!mark.points.empty()){
            if(mark.pen_polyline)for(const auto p:mark.points)add(p);
            else{add(mark.points.front());add(mark.points.back());}
        }else{add(mark.a);add(mark.b);}
    }
    if(result.kind==LineSnapKind::Endpoint)return result;
    // Consider only actual straight segments, never the hover ghost or freehand chords.
    const auto segments=[&](const auto& visit){
        for(size_t i=1;i<vertices.size();++i)visit(vertices[i-1],vertices[i]);
        for(auto it=document.marks.rbegin();it!=document.marks.rend();++it){
            const auto& mark=*it;
            if(mark.tool!=Tool::Arrow&&(mark.tool!=Tool::Pen||(!mark.pen_straight&&!mark.pen_polyline)))continue;
            // A curved/hand-drawn arrow has no straight a-b shaft: do not snap to an invisible chord.
            if(mark.tool==Tool::Arrow&&(mark.arrow_type==ArrowType::Curved||mark.arrow_type==ArrowType::HandDrawn))continue;
            const auto center=mark.rotation==0?Point{}:MarkCenter(mark);
            const auto world=[&](Point p){return mark.rotation==0?p:RotatePoint(p,center,mark.rotation);};
            if(mark.tool==Tool::Pen&&mark.points.size()>1){
                for(size_t i=1;i<mark.points.size();++i)visit(world(mark.points[i-1]),world(mark.points[i]));
            }else visit(world(mark.a),world(mark.b));
        }
    };
    const auto candidate=[&](Point p,LineSnapKind kind){
        if(!Contains(bounds,p)||(anchor&&std::hypot(p.x-anchor->x,p.y-anchor->y)<.5f))return;
        const float distance=std::hypot(p.x-pointer.x,p.y-pointer.y);
        if(distance<=radius&&distance<nearest){nearest=distance;result.point=p;result.kind=kind;}
    };
    segments([&](Point a,Point b){
        if(std::hypot(b.x-a.x,b.y-a.y)>.5f)candidate({(a.x+b.x)/2,(a.y+b.y)/2},LineSnapKind::Midpoint);
    });
    if(result.kind==LineSnapKind::Midpoint)return result;
    segments([&](Point a,Point b){
        const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
        if(length2<=.25f)return;
        const float t=std::clamp(((pointer.x-a.x)*dx+(pointer.y-a.y)*dy)/length2,0.f,1.f);
        candidate({a.x+t*dx,a.y+t*dy},LineSnapKind::Nearest);
    });
    if(result.kind==LineSnapKind::Nearest||!anchor)return result;
    const float dx=pointer.x-anchor->x,dy=pointer.y-anchor->y,length=std::hypot(dx,dy);
    if(length<12*scale)return result;
    // Both a pixel-distance limit and a five-degree limit avoid surprising short-line jumps.
    constexpr float diagonal=.7071067811865475f;
    const std::array<Point,4> directions{Point{1,0},Point{0,1},Point{diagonal,diagonal},Point{diagonal,-diagonal}};
    for(size_t i=0;i<directions.size();++i){
        const auto u=directions[i];const float t=dx*u.x+dy*u.y;
        const Point projected{anchor->x+t*u.x,anchor->y+t*u.y};
        const float distance=std::hypot(projected.x-pointer.x,projected.y-pointer.y);
        if(distance<=radius&&distance<=length*.087155743f&&Contains(bounds,projected)&&distance<nearest){
            nearest=distance;result.point=projected;
            result.kind=i==0?LineSnapKind::Horizontal:(i==1?LineSnapKind::Vertical:LineSnapKind::Diagonal);
        }
    }
    return result;
}
}
