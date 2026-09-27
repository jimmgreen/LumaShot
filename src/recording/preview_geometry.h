#pragma once
#include <d2d1.h>
#include <algorithm>
#include <cmath>

namespace lumashot::recording {
// The container is in DIP; media dimensions are physical pixels. Cap the
// physical output at the source size, even when the panel is scaled for DPI.
inline D2D1_RECT_F PreviewImageBounds(D2D1_RECT_F available,SIZE source,float ui_scale){
    const float cx=(available.left+available.right)/2,cy=(available.top+available.bottom)/2;
    if(source.cx<=0||source.cy<=0||available.right<=available.left||available.bottom<=available.top)return {cx,cy,cx,cy};
    if(!std::isfinite(ui_scale)||ui_scale<=0)ui_scale=1;
    const float width=static_cast<float>(source.cx),height=static_cast<float>(source.cy);
    const float ratio=std::min({1.f/ui_scale,(available.right-available.left)/width,(available.bottom-available.top)/height});
    return {cx-width*ratio/2,cy-height*ratio/2,cx+width*ratio/2,cy+height*ratio/2};
}
}
