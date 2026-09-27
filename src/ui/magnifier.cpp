#include "ui/magnifier.h"
#include "app/diagnostics.h"
#include <algorithm>
#include <stdexcept>

namespace lumashot {
RECT MagnifierBounds(RECT monitor,POINT pointer) {
    const LONG x=std::clamp(pointer.x+22,monitor.left,std::max(monitor.left,monitor.right-kMagnifierSize));
    const LONG y=std::clamp(pointer.y+24,monitor.top,std::max(monitor.top,monitor.bottom-kMagnifierSize));
    return {x,y,x+kMagnifierSize,y+kMagnifierSize};
}
void MagnifierPixels(const Frame& frame,RECT monitor,POINT pointer,std::span<uint32_t> output) {
    if(output.size()!=kMagnifierSize*kMagnifierSize)throw std::invalid_argument("Invalid magnifier surface");
    RECT source{};
    if(!IntersectRect(&source,&monitor,&frame.bounds))throw std::invalid_argument("Magnifier outside screenshot");
    const LONG sx=std::clamp(pointer.x-10,source.left,std::max(source.left,source.right-20));
    const LONG sy=std::clamp(pointer.y-10,source.top,std::max(source.top,source.bottom-20));
    for(int y=0;y<kMagnifierSize;++y)for(int x=0;x<kMagnifierSize;++x) {
        uint32_t color=0xff1686ff;
        if(x>0&&y>0&&x<121&&y<121&&x!=61&&y!=61) {
            const LONG px=std::min(sx+(x-1)/6,source.right-1)-frame.bounds.left;
            const LONG py=std::min(sy+(y-1)/6,source.bottom-1)-frame.bounds.top;
            color=frame.pixels[static_cast<size_t>(py)*frame.Width()+px]|0xff000000;
        }
        output[static_cast<size_t>(y)*kMagnifierSize+x]=color;
    }
}
void Magnifier::Show(HWND owner,const Frame& frame,RECT monitor,POINT pointer) {
    TraceScope trace("magnifier_update");
    if(!surface_)surface_=std::make_unique<DibSurface>(kMagnifierSize,kMagnifierSize);
    if(!IsWindow(window_)) {
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
            L"STATIC",L"LumaShot.Magnifier",WS_POPUP,0,0,kMagnifierSize,kMagnifierSize,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
        CheckWin32(window_!=nullptr,"Create magnifier");
    }
    MagnifierPixels(frame,monitor,pointer,{surface_->Pixels(),kMagnifierSize*kMagnifierSize});
    const RECT bounds=MagnifierBounds(monitor,pointer);POINT position{bounds.left,bounds.top},source{};
    SIZE size{kMagnifierSize,kMagnifierSize};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    CheckWin32(UpdateLayeredWindow(window_,nullptr,&position,&size,surface_->Dc(),&source,0,&blend,ULW_ALPHA)!=FALSE,"Update magnifier");
    if(!IsWindowVisible(window_))ShowWindow(window_,SW_SHOWNOACTIVATE);
}
void Magnifier::Hide(){if(IsWindow(window_)&&IsWindowVisible(window_))ShowWindow(window_,SW_HIDE);}
void Magnifier::Close(){if(IsWindow(window_))DestroyWindow(window_);window_=nullptr;surface_.reset();}
}
