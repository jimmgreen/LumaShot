#include "model/selection.h"
#include "model/number.h"
#include "model/number_label.h"
#include <algorithm>
#include <cmath>
namespace lumashot {
constexpr float Pi=3.14159265358979323846f;
Point RotatePoint(Point p,Point c,float degrees){const float a=degrees*Pi/180,cs=std::cos(a),sn=std::sin(a);return {c.x+(p.x-c.x)*cs-(p.y-c.y)*sn,c.y+(p.x-c.x)*sn+(p.y-c.y)*cs};}
Point MarkCenter(const Mark& m){auto b=LocalBounds(m);return {(b.left+b.right)/2,(b.top+b.bottom)/2};}
Box PartBounds(const Mark& m,EditPart part){
    if(m.tool!=Tool::Number||part==EditPart::Whole){if(m.tool==Tool::Rectangle||m.tool==Tool::Ellipse||m.tool==Tool::Text)return Normalize(m.a,m.b);return LocalBounds(m);}
    if(part==EditPart::Badge)return Normalize(m.a,m.b);
    if(part==EditPart::Leader){auto points=NumberLeaderPoints(m);Box b=Normalize(points[0],points[3]);for(auto p:points){b.left=std::min(b.left,p.x);b.top=std::min(b.top,p.y);b.right=std::max(b.right,p.x);b.bottom=std::max(b.bottom,p.y);}return b;}
    return NumberDetailBounds(m);
}
static std::array<Point,8> BoxPoints(Box b){const float x=(b.left+b.right)/2,y=(b.top+b.bottom)/2;return {Point{b.left,b.top},{x,b.top},{b.right,b.top},{b.right,y},{b.right,b.bottom},{x,b.bottom},{b.left,b.bottom},{b.left,y}};}
std::vector<EditHandle> EditHandles(const Mark& m,EditPart part,float s){
    std::vector<EditHandle> result;const auto center=MarkCenter(m);auto add=[&](int id,Point p){result.push_back({id,RotatePoint(p,center,m.rotation)});};
    if(part==EditPart::Leader){auto points=NumberLeaderPoints(m);for(int i=0;i<4;++i)add(20+i,points[i]);return result;}
    auto b=PartBounds(m,part);auto points=BoxPoints(b);for(int i=0;i<8;++i)add(i,points[i]);if(part==EditPart::Whole)add(8,{(b.left+b.right)/2,b.top-26*s});
    if(m.tool==Tool::Rectangle){const auto radii=m.corner_radii.value_or(std::array<float,4>{m.corner_radius,m.corner_radius,m.corner_radius,m.corner_radius});const float limit=std::min(b.right-b.left,b.bottom-b.top)/2;
        for(int i=0;i<4;++i){float inset=std::clamp(radii[i],std::min(12*s,limit),std::max(limit,0.f));const auto corner=points[i*2];add(9+i,{corner.x+(i==0||i==3?inset:-inset),corner.y+(i<2?inset:-inset)});}
    }
    if(m.tool==Tool::Arrow){add(24,m.a);add(25,m.b);}return result;
}
int HitEditHandle(const Mark& m,EditPart part,Point p,float s){auto handles=EditHandles(m,part,s);for(auto it=handles.rbegin();it!=handles.rend();++it)if(std::hypot(it->point.x-p.x,it->point.y-p.y)<=6*s)return it->id;return -1;}
static float SegmentDistance(Point p,Point a,Point b){const float dx=b.x-a.x,dy=b.y-a.y,t=std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/std::max(.0001f,dx*dx+dy*dy),0.f,1.f);return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);}
EditPart HitMarkPart(const Mark& m,Point p,float tolerance){
    if(m.tool!=Tool::Number)return EditPart::Whole;p=RotatePoint(p,MarkCenter(m),-m.rotation);
    if(Contains(Normalize(m.a,m.b),p,tolerance))return EditPart::Badge;
    if(m.number_combo==NumberCombo::Leader)return EditPart::Leader;return EditPart::Detail;
}
bool HitMark(const Mark& m,Point p,float tolerance){
    p=RotatePoint(p,MarkCenter(m),-m.rotation);const auto b=Normalize(m.a,m.b);const float t=std::max(tolerance,m.width/2+2);
    if(m.tool==Tool::Arrow)return SegmentDistance(p,m.a,m.b)<=std::max(t,m.arrow_size*.6f)||(!m.points.empty()&&Contains(LocalBounds(m),p,t));
    if(m.tool==Tool::Number){if(Contains(b,p,t))return true;if(m.number_combo==NumberCombo::Plain)return false;if(m.number_combo==NumberCombo::Leader){auto ps=NumberLeaderPoints(m);for(size_t i=1;i<ps.size();++i)if(SegmentDistance(p,ps[i-1],ps[i])<=t)return true;return false;}return Contains(NumberDetailBounds(m),p,t);}
    if(m.tool==Tool::Ellipse){const float rx=(b.right-b.left)/2,ry=(b.bottom-b.top)/2;if(rx<=0||ry<=0)return false;const float d=std::hypot((p.x-(b.left+b.right)/2)/rx,(p.y-(b.top+b.bottom)/2)/ry);return m.fill_color?d<=1+t/std::min(rx,ry):std::abs(d-1)<=t/std::min(rx,ry);}
    if(m.tool==Tool::Pen){if(m.points.size()==1)return SegmentDistance(p,m.points[0],m.points[0])<=t;for(size_t i=1;i<m.points.size();++i)if(SegmentDistance(p,m.points[i-1],m.points[i])<=t)return true;return false;}
    if(m.tool==Tool::Rectangle&&!m.fill_color){return Contains(b,p,t)&&(!Contains(b,p,-t));}
    return Contains(LocalBounds(m),p,t);
}
// Storage is badge -> adjacent elbow -> remote elbow -> target. In the
// user's target-to-badge drawing order, points[1] is the penultimate point.
static Point LeaderBadgeAnchor(const Mark& m,const std::array<Point,4>& points){
    const auto badge=Normalize(m.a,m.b);const Point center{(badge.left+badge.right)/2,(badge.top+badge.bottom)/2};
    const float delta=points[1].x-center.x;
    // On the center line retain the previous side, never consult remote points.
    const float direction=std::abs(delta)>.01f?(delta>0?1.f:-1.f):(points[0].x>=center.x?1.f:-1.f);
    return {center.x+direction*((badge.right-badge.left)/2+m.number_size*.15f),center.y};
}
// Moving/resizing the badge changes the attachment, not the annotated target.
// Inferred geometry must stay inferred; reconnect explicit, user-edited parts.
void ReconnectNumberBadge(Mark& m){
    if(m.tool!=Tool::Number)return;
    const auto badge=Normalize(m.a,m.b);const Point center{(badge.left+badge.right)/2,(badge.top+badge.bottom)/2};
    const float direction=m.number_target.x>=center.x?1.f:-1.f;
    if(m.number_combo==NumberCombo::Leader&&m.number_leader){
        auto& points=*m.number_leader;
        const Point anchor=LeaderBadgeAnchor(m,points);
        const float leader_direction=anchor.x>=center.x?1.f:-1.f;
        const Point offset{points[1].x-points[0].x,points[1].y-points[0].y};
        points[0]=anchor;points[1]={anchor.x+leader_direction*std::abs(offset.x),anchor.y+offset.y};
        // Keep the remote elbow and endpoint, including manually edited ones.
        m.number_target=points.back();
    }
    if(!m.number_detail)return;
    const auto detail=*m.number_detail;
    if(m.number_combo==NumberCombo::Text){
        // A note keeps its edited dimensions and docks beside the badge.
        const float width=detail.right-detail.left,height=detail.bottom-detail.top,gap=m.number_size*.2f;
        const float left=direction>0?badge.right+gap:badge.left-gap-width;
        m.number_detail=Box{left,badge.top,left+width,badge.top+height};
    }else if(m.number_combo==NumberCombo::Highlight||m.number_combo==NumberCombo::DashedBox){
        // The far corner remains on the annotated area; only the attached corner moves.
        const Point fixedCorner{m.number_target.x>=(detail.left+detail.right)/2?detail.right:detail.left,
                        m.number_target.y>=(detail.top+detail.bottom)/2?detail.bottom:detail.top};
        m.number_detail=Normalize(center,fixedCorner);
    }
}
Mark EditMark(const Mark& original,EditPart part,int handle,Point start,Point current,bool proportional,bool snap,float){
    Mark m=original;const auto center=MarkCenter(original);const Point p=RotatePoint(current,center,-original.rotation),q=RotatePoint(start,center,-original.rotation);
    const auto b=PartBounds(original,part);
    const bool attached_detail=m.tool==Tool::Number&&part==EditPart::Detail&&
        (m.number_combo==NumberCombo::Text||m.number_combo==NumberCombo::DashedBox);
    if((part==EditPart::Badge||attached_detail)&&current==start)return m;
    if(handle==8){float a=std::atan2(current.y-center.y,current.x-center.x)-std::atan2(start.y-center.y,start.x-center.x);m.rotation=original.rotation+a*180/Pi;if(snap)m.rotation=std::round(m.rotation/15)*15;return m;}
    auto preserve=[&]{auto c=MarkCenter(m);auto rotated=RotatePoint(c,center,original.rotation);Translate(m,{rotated.x-c.x,rotated.y-c.y});};
    if(handle>=9&&handle<=12){const int corner=handle-9;auto radii=m.corner_radii.value_or(std::array<float,4>{m.corner_radius,m.corner_radius,m.corner_radius,m.corner_radius});float value=std::min(corner==0||corner==3?p.x-b.left:b.right-p.x,corner<2?p.y-b.top:b.bottom-p.y);value=std::clamp(value,0.f,std::min(b.right-b.left,b.bottom-b.top)/2);if(proportional)radii.fill(value);else radii[corner]=value;m.corner_radii=radii;return m;}
    if(handle>=20&&handle<=23){
        if(current==start)return m;
        // The first vertex is attached to the badge, not an independent endpoint.
        // Reuse badge movement so rotated edits keep the far target in world space.
        if(handle==20)return EditMark(original,EditPart::Badge,-1,start,current,proportional,snap);
        auto points=NumberLeaderPoints(m);points[handle-20]=p;m.number_target=points[3];
        // Only the adjacent elbow chooses the attachment side; remote edits cannot flip it.
        points[0]=LeaderBadgeAnchor(m,points);m.number_leader=points;
        preserve();return m;
    }
    if(handle==24||handle==25){(handle==24?m.a:m.b)=p;
        if(m.tool==Tool::Arrow&&m.arrow_type==ArrowType::HandDrawn&&!m.points.empty())(handle==24?m.points.front():m.points.back())=p;
        preserve();return m;}
    auto transform=[&](auto point){if(handle<0)return Point{point.x+p.x-q.x,point.y+p.y-q.y};return point;};
    if(handle<0){
        if(part==EditPart::Whole){Translate(m,{current.x-start.x,current.y-start.y});return m;}
        // Dragging a leader, text note or dashed frame moves its badge and detail as
        // one combination. Linked detail resizing keeps its badge offset below.
        if(part==EditPart::Leader||attached_detail){Translate(m,{current.x-start.x,current.y-start.y});return m;}
        // Freeze the independently edited detail, but keep a badge attachment live.
        if(part!=EditPart::Badge){if(m.number_combo==NumberCombo::Leader)m.number_leader=NumberLeaderPoints(m);else m.number_detail=NumberDetailBounds(m);}
        if(part==EditPart::Badge){m.a=transform(m.a);m.b=transform(m.b);}
        else {auto d=*m.number_detail;auto a=transform(Point{d.left,d.top}),z=transform(Point{d.right,d.bottom});m.number_detail=Box{a.x,a.y,z.x,z.y};m.number_target=transform(m.number_target);}
        if(part==EditPart::Badge)ReconnectNumberBadge(m);
        preserve();return m;
    }
    Box next=b;const float dx=p.x-q.x,dy=p.y-q.y;
    // Text handles scale the complete text object by default, rather than
    // changing only its wrapping box. Keep glyph proportions intact.
    if(m.tool==Tool::Text){
        m.text_auto_size=true;
        if(m.text_wrap_width<=0)m.text_wrap_width=std::max(1.f,b.right-b.left);
        proportional=true;
    }
    const bool left=handle==0||handle==6||handle==7,right=handle==2||handle==3||handle==4,top=handle<=2,bottom=handle>=4&&handle<=6;
    if(left)next.left=std::min(b.right-2,b.left+dx);if(right)next.right=std::max(b.left+2,b.right+dx);if(top)next.top=std::min(b.bottom-2,b.top+dy);if(bottom)next.bottom=std::max(b.top+2,b.bottom+dy);
    const bool label_badge=m.tool==Tool::Number&&part==EditPart::Badge&&!m.number_label.empty();
    if(proportional||label_badge){const float w=std::max(2.f,b.right-b.left),h=std::max(2.f,b.bottom-b.top);float ratio=left||right?(next.right-next.left)/w:(next.bottom-next.top)/h;
        if(m.tool==Tool::Text||label_badge){
            // Project corner motion onto the diagonal so either mouse axis can
            // enlarge or shrink text without a stationary axis blocking it.
            if((left||right)&&(top||bottom))ratio=((next.right-next.left)*w+(next.bottom-next.top)*h)/(w*w+h*h);
            ratio=std::max(ratio,std::max({2/w,2/h,1/std::max(1.f,label_badge?m.number_size:m.font_size)}));
        }else if(top||bottom)ratio=left||right?std::max(ratio,(next.bottom-next.top)/h):ratio;
        if(left)next.left=next.right-w*ratio;else if(right)next.right=next.left+w*ratio;else {next.left=(b.left+b.right-w*ratio)/2;next.right=next.left+w*ratio;}
        if(top)next.top=next.bottom-h*ratio;else if(bottom)next.bottom=next.top+h*ratio;else{next.top=(b.top+b.bottom-h*ratio)/2;next.bottom=next.top+h*ratio;}
    }
    const float sx=(next.right-next.left)/std::max(1.f,b.right-b.left),sy=(next.bottom-next.top)/std::max(1.f,b.bottom-b.top);
    auto map=[&](Point pt){return Point{next.left+(pt.x-b.left)*sx,next.top+(pt.y-b.top)*sy};};
    auto mapbox=[&](Box box){auto a=map({box.left,box.top}),z=map({box.right,box.bottom});return Box{a.x,a.y,z.x,z.y};};
    if(m.tool==Tool::Number&&part!=EditPart::Whole&&part!=EditPart::Badge){if(m.number_combo==NumberCombo::Leader)m.number_leader=NumberLeaderPoints(m);else m.number_detail=NumberDetailBounds(m);}
    if(part==EditPart::Whole||part==EditPart::Badge){m.a=map(m.a);m.b=map(m.b);for(auto& pt:m.points)pt=map(pt);if(m.tool==Tool::Number)m.number_size*=std::min(sx,sy);}
    if(part==EditPart::Whole){m.number_target=map(m.number_target);if(m.number_detail)m.number_detail=mapbox(*m.number_detail);if(m.number_leader)for(auto& pt:*m.number_leader)pt=map(pt);}
    if(part==EditPart::Detail){
        m.number_detail=next;m.number_target=map(m.number_target);
        if(attached_detail){
            // Follow the nearest linked-detail corner, preserving the badge's physical
            // offset and size, not scaling its gap with the detail dimensions.
            // Choose from the original gesture geometry so the anchor cannot
            // switch while the pointer moves. preserve() handles rotation below.
            const auto badge=Normalize(original.a,original.b);
            const bool anchor_right=badge.left+badge.right>b.left+b.right;
            const bool anchor_bottom=badge.top+badge.bottom>b.top+b.bottom;
            const Point delta{anchor_right?next.right-b.right:next.left-b.left,
                              anchor_bottom?next.bottom-b.bottom:next.top-b.top};
            m.a={m.a.x+delta.x,m.a.y+delta.y};m.b={m.b.x+delta.x,m.b.y+delta.y};
        }
    }
    if(proportional){const float factor=std::min(sx,sy);m.font_size*=factor;m.text_wrap_width*=factor;m.number_text_size*=factor;m.width*=factor;m.arrow_size*=factor;m.corner_radius*=factor;if(m.corner_radii)for(auto& r:*m.corner_radii)r*=factor;}
    if(part==EditPart::Badge){if(!m.number_label.empty())FitNumberBadge(m);ReconnectNumberBadge(m);}
    preserve();return m;
}
}
