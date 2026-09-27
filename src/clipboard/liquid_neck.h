#pragma once
#include "clipboard/liquid_motion.h"

namespace lumashot::clipboard {
// Two upper cubics in edge-normal / edge-tangent coordinates. Mirror the
// tangent coordinate for the lower contour. No clocks, pixels or HWNDs here.
inline std::array<D2D1_POINT_2F,7> LiquidNeckProfile(float radial,float half,float radius,float gap,float scale){
    const auto mix=[](D2D1_POINT_2F a,D2D1_POINT_2F b,float t){
        return D2D1_POINT_2F{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
    };
    gap=std::max(0.f,gap);const float edge=radial+gap;
    const float start=radial-std::min(radius,radial*.85f),flare=8*scale;
    constexpr float arc=.55228475f;
    const D2D1_POINT_2F a{start,-half},b{start+(edge-start)*arc,-half};
    const D2D1_POINT_2F c{edge,-half-flare+flare*arc},d{edge,-half-flare};
    const auto ab=mix(a,b,.5f),bc=mix(b,c,.5f),cd=mix(c,d,.5f);
    const auto abc=mix(ab,bc,.5f),bcd=mix(bc,cd,.5f);
    // Exact subdivision preserves the previous resting concave shoulder.
    std::array<D2D1_POINT_2F,7> profile{a,ab,abc,mix(abc,bcd,.5f),bcd,cd,d};
    const float collar=std::min(12*scale,std::max(scale,half-radius)),waist=std::min(2.8f*scale,collar*.4f);
    const float port=9*scale;
    const std::array<D2D1_POINT_2F,7> neck{{
        {radial-std::min(2*scale,radial*.5f),-collar},
        {radial+gap*.15f,-collar*.9f},
        {radial+gap*.28f,-waist},
        {radial+gap*.52f,-waist},
        {radial+gap*.76f,-waist},
        {edge,-port+4*scale},
        {edge,-port}}};
    const float stretch=LiquidSmooth(gap/(14*scale));
    const float remaining=1-LiquidSmooth((gap/scale-28)/(LiquidDetachDistance-28));
    for(size_t i=0;i<profile.size();++i){profile[i]=mix(profile[i],neck[i],stretch);profile[i].y*=remaining;}
    return profile;
}
}
