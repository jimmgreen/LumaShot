#pragma once
#include "pin/paper.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lumashot {
// Resource limits for the *composited surface*, not an arbitrary zoom ratio.
// A 64M-pixel BGRA surface consumes 256 MiB; long thin images can exceed 4096px.
inline constexpr int PinSurfaceMaxEdge=16384;
inline constexpr uint64_t PinSurfaceMaxPixels=64ull*1024*1024;
inline float PinZoomLimit(int width,int height,float dpi,PinStyle style){
    if(width<=0||height<=0||!std::isfinite(dpi)||dpi<=0)return 1;
    double low=0,high=double(PinSurfaceMaxEdge)/std::max(width,height);
    for(int i=0;i<40;++i){
        const double zoom=(low+high)/2;
        // Ceil plus a small guard also covers float rounding in the render path.
        const auto paper=MakePaperLayout(std::max(1,int(std::ceil(width*zoom))+2),std::max(1,int(std::ceil(height*zoom))+2),dpi,style);
        const uint64_t w=uint64_t(paper.width)+2*paper.shadow+2,h=uint64_t(paper.height)+2*paper.shadow+2;
        if(w<=PinSurfaceMaxEdge&&h<=PinSurfaceMaxEdge&&w*h<=PinSurfaceMaxPixels)low=zoom;else high=zoom;
    }
    return std::nextafter(static_cast<float>(low),0.f);
}
inline float PinZoomGoal(float current,int delta,int width,int height,float dpi,PinStyle style){
    const double limit=PinZoomLimit(width,height,dpi,style);
    if(limit<=0)return current;
    const double minimum=std::min(.1,limit);
    const double base=std::isfinite(current)&&current>0?current:1.;
    // Clamp in log space before exponentiation: even extreme wheel input stays finite.
    const double value=std::clamp(std::log(base)+double(delta)/WHEEL_DELTA*std::log(1.12),std::log(minimum),std::log(limit));
    return std::clamp(static_cast<float>(std::exp(value)),static_cast<float>(minimum),static_cast<float>(limit));
}
}
