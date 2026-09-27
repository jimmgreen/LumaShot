#pragma once
#include "model/document.h"
#include <algorithm>
#include <cmath>
namespace lumashot {
struct PinGeometry {Point center,tip,left,right;float radius;};
inline PinGeometry NumberPin(Box box){
    const float radius=std::min((box.right-box.left)/2,(box.bottom-box.top)/2.6f);
    const Point center{(box.left+box.right)/2,box.top+radius},tip{center.x,box.bottom};
    const float distance=tip.y-center.y;
    const float dy=radius*radius/distance,dx=std::sqrt(std::max(0.f,radius*radius-dy*dy));
    return {center,tip,{center.x-dx,center.y+dy},{center.x+dx,center.y+dy},radius};
}
inline std::array<Point,4> NumberLeaderPoints(const Mark& mark){
    if(mark.number_leader)return *mark.number_leader;
    auto b=Normalize(mark.a,mark.b);Point c{(b.left+b.right)/2,(b.top+b.bottom)/2};
    const float direction=mark.number_target.x>=c.x?1.f:-1.f,start=c.x+direction*((b.right-b.left)/2+mark.number_size*.15f),span=mark.number_target.x-start;
    return {Point{start,c.y},Point{start+span*.3f,c.y},Point{start+span*.5f,mark.number_target.y},mark.number_target};
}
}
