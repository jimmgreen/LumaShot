#include "ui/selection_cursor.h"
#include "ui/resize_cursor.h"
#include <tuple>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
namespace lumashot::ui {
namespace {
struct BitmapOwner {HBITMAP handle{};~BitmapOwner(){if(handle)DeleteObject(handle);}};
Point NativeResizeScale(bool diagonal){
    // Preserve each local cursor's visible dimensions as its coordinate frame rotates.
    static const std::array<Point,2> scales=[](){
        std::array<Point,2> result{Point{1,1},Point{1,1}};
        DibSurface sample(32,32);
        for(size_t kind=0;kind<2;++kind){
            std::fill_n(sample.Pixels(),32*32,0xffffffff);
            if(!DrawIconEx(sample.Dc(),0,0,LoadCursorW(nullptr,kind?IDC_SIZENWSE:IDC_SIZEWE),32,32,0,nullptr,DI_NORMAL))continue;
            GdiFlush();float left=100,top=100,right=-100,bottom=-100;
            for(int y=0;y<32;++y)for(int x=0;x<32;++x){const auto pixel=sample.Pixels()[y*32+x];
                if((pixel&255)<96&&((pixel>>8)&255)<96&&((pixel>>16)&255)<96){
                    const float along=kind?(float(x)+float(y))*.70710678118f:float(x);
                    const float across=kind?(-float(x)+float(y))*.70710678118f:float(y);
                    left=std::min(left,along);right=std::max(right,along);top=std::min(top,across);bottom=std::max(bottom,across);
                }}
            if(right>=left&&bottom>=top)result[kind]={std::clamp((right-left+1)/16,.75f,1.75f),std::clamp((bottom-top+1)/8,.75f,1.75f)};
        }
        return result;
    }();
    return scales[diagonal?1:0];
}
float SegmentDistance(Point p,Point a,Point b){
    const float dx=b.x-a.x,dy=b.y-a.y;
    const float t=std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/(dx*dx+dy*dy),0.f,1.f);
    return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
HCURSOR CreateResizeCursor(float degrees,int size,bool diagonal){
    // Native-sized, black double arrow with a thin white contrast edge.
    // Transform the vector outline BEFORE rasterization; never rotate a low-res bitmap.
    constexpr std::array<Point,10> outline{{{-8,0},{-3,-4},{-3,-1},{3,-1},{3,-4},{8,0},{3,4},{3,1},{-3,1},{-3,4}}};
    const float angle=degrees*3.14159265358979323846f/180,cs=std::cos(angle),sn=std::sin(angle);
    const auto proportions=NativeResizeScale(diagonal);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;
    info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* data{};BitmapOwner color{CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&data,nullptr,0)};
    if(!color.handle||!data)return nullptr;
    auto* pixels=static_cast<uint32_t*>(data);
    const int stride=((size+15)/16)*2;std::vector<BYTE> mask(static_cast<size_t>(stride)*size,0xff);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
        unsigned covered=0,white=0;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
            const float dx=(float(x)+(float(sx)+.5f)/4)*32/size-16,dy=(float(y)+(float(sy)+.5f)/4)*32/size-16;
            const Point p{(dx*cs+dy*sn)/proportions.x,(-dx*sn+dy*cs)/proportions.y};
            bool inside=false;float edge=100;
            for(size_t i=0,j=outline.size()-1;i<outline.size();j=i++){
                const auto a=outline[i],b=outline[j];
                if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
                edge=std::min(edge,SegmentDistance(p,a,b));
            }
            if(inside||edge<=.85f){++covered;if(!inside)++white;}
        }
        const uint32_t alpha=(covered*255+8)/16,ink=(white*255+8)/16;
        pixels[static_cast<size_t>(y)*size+x]=(alpha<<24)|(ink<<16)|(ink<<8)|ink;
        if(alpha)mask[static_cast<size_t>(y)*stride+x/8]&=static_cast<BYTE>(~(0x80u>>(x%8)));
    }
    BitmapOwner mono{CreateBitmap(size,size,1,1,mask.data())};if(!mono.handle)return nullptr;
    ICONINFO icon{};icon.xHotspot=icon.yHotspot=static_cast<DWORD>(size/2);icon.hbmColor=color.handle;icon.hbmMask=mono.handle;
    return reinterpret_cast<HCURSOR>(CreateIconIndirect(&icon));
}
struct Entry {HCURSOR cursor{};uint64_t age{};};
struct Cache {
    std::mutex mutex;std::map<std::tuple<int,float,bool>,Entry> entries;uint64_t clock{};
    ~Cache(){for(const auto& [key,entry]:entries){(void)key;DestroyCursor(entry.cursor);}}
};
}
static HCURSOR ResizeAxisCursor(float degrees,UINT dpi,bool diagonal){
    if(!std::isfinite(degrees))degrees=0;
    degrees=std::fmod(degrees,180.f);if(degrees<0)degrees+=180;
    // Preserve actual Windows cursors only when they match EXACTLY, not by rounding.
    if(!diagonal&&degrees==0)return LoadCursorW(nullptr,IDC_SIZEWE);
    if(diagonal&&degrees==45)return LoadCursorW(nullptr,IDC_SIZENWSE);
    if(!diagonal&&degrees==90)return LoadCursorW(nullptr,IDC_SIZENS);
    if(diagonal&&degrees==135)return LoadCursorW(nullptr,IDC_SIZENESW);
    if(!dpi)dpi=96;
    const int size=std::clamp(MulDiv(32,static_cast<int>(std::clamp(dpi,72u,384u)),96),24,128);
    static Cache cache;const std::lock_guard lock(cache.mutex);const auto key=std::tuple{size,degrees,diagonal};
    if(auto it=cache.entries.find(key);it!=cache.entries.end()){it->second.age=++cache.clock;return it->second.cursor;}
    const auto cursor=CreateResizeCursor(degrees,size,diagonal);if(!cursor)return LoadCursorW(nullptr,IDC_SIZEALL);
    if(cache.entries.size()>=64){
        auto oldest=cache.entries.end();const auto active=GetCursor();
        for(auto it=cache.entries.begin();it!=cache.entries.end();++it)
            if(it->second.cursor!=active&&(oldest==cache.entries.end()||it->second.age<oldest->second.age))oldest=it;
        if(oldest!=cache.entries.end()){DestroyCursor(oldest->second.cursor);cache.entries.erase(oldest);}
    }
    cache.entries.emplace(key,Entry{cursor,++cache.clock});return cursor;
}
HCURSOR AngledResizeCursor(float degrees,UINT dpi){return ResizeAxisCursor(degrees,dpi,false);}
HCURSOR ExactResizeHandleCursor(int handle,float rotation,UINT dpi){
    constexpr std::array<float,8> axes{45,90,135,0,45,90,135,0};
    return ResizeAxisCursor(axes[static_cast<size_t>(handle)]+rotation,dpi,handle%2==0);
}
}
