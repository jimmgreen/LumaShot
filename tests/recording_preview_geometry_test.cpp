#include "recording/preview_geometry.h"
#include <iostream>

using namespace lumashot::recording;
int main(){
    int failures=0;
    const auto NearlyEqual=[](float a,float b){return std::abs(a-b)<.002f;};
    const auto check=[&](bool good,const char* label){std::cout<<(good?"PASS ":"FAIL ")<<label<<'\n';failures+=!good;};
    const D2D1_RECT_F available{16,48,624,366};
    for(const float dpi:{1.f,1.5f,2.f}){
        for(const SIZE source:{SIZE{240,120},SIZE{610,370},SIZE{1920,1080},SIZE{100,300}}){
            const auto b=PreviewImageBounds(available,source,dpi);
            const float width=b.right-b.left,height=b.bottom-b.top;
            check(width*dpi<=source.cx+.002f&&height*dpi<=source.cy+.002f,"physical preview never enlarges source");
            check(NearlyEqual(width/height,static_cast<float>(source.cx)/source.cy),"source aspect ratio retained");
            check(NearlyEqual(b.left+b.right,available.left+available.right)&&NearlyEqual(b.top+b.bottom,available.top+available.bottom),"preview centered");
            check(b.left>=available.left-.002f&&b.right<=available.right+.002f&&b.top>=available.top-.002f&&b.bottom<=available.bottom+.002f,"preview stays in container");
        }
        const auto tinySource=PreviewImageBounds(available,{240,120},dpi);
        check(NearlyEqual((tinySource.right-tinySource.left)*dpi,240)&&NearlyEqual((tinySource.bottom-tinySource.top)*dpi,120),"tinySource source stays at physical 100 percent at each DPI");
        const auto large=PreviewImageBounds(available,{1920,1080},dpi);
        check(NearlyEqual(large.bottom-large.top,318),"large source fits container height");
    }
    const auto empty=PreviewImageBounds(available,{},2);
    check(NearlyEqual(empty.left,empty.right)&&NearlyEqual(empty.top,empty.bottom),"invalid source produces empty bounds");
    const auto fallback=PreviewImageBounds(available,{240,120},0);
    check(NearlyEqual(fallback.right-fallback.left,240),"invalid scale falls back to 100 percent DPI");
    return failures?1:0;
}
