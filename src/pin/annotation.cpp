#include "pin/annotation.h"
#include <wincodec.h>
#include <algorithm>
namespace lumashot {
Frame PinAnnotationPreview(const Frame& source,RECT display,RECT canvas){
    auto frame=MakeFrame(canvas,0xfff4f7fb);const int width=std::max(1L,display.right-display.left),height=std::max(1L,display.bottom-display.top);
    ComPtr<IWICImagingFactory> wic;CheckWin32(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"Annotation imaging");
    ComPtr<IWICBitmap> input;CheckWin32(SUCCEEDED(wic->CreateBitmapFromMemory(source.Width(),source.Height(),GUID_WICPixelFormat32bppBGRA,source.Width()*4,UINT(source.pixels.size()*4),reinterpret_cast<BYTE*>(const_cast<uint32_t*>(source.pixels.data())),&input)),"Annotation source");
    ComPtr<IWICBitmapScaler> scaler;wic->CreateBitmapScaler(&scaler);CheckWin32(SUCCEEDED(scaler->Initialize(input.Get(),width,height,WICBitmapInterpolationModeFant)),"Annotation scale");
    RECT clip{};if(IntersectRect(&clip,&canvas,&display)){
        WICRect region{clip.left-display.left,clip.top-display.top,clip.right-clip.left,clip.bottom-clip.top};
        std::vector<uint32_t> row(static_cast<size_t>(region.Width)*region.Height);
        CheckWin32(SUCCEEDED(scaler->CopyPixels(&region,region.Width*4,UINT(row.size()*4),reinterpret_cast<BYTE*>(row.data()))),"Annotation preview");
        for(int y=0;y<region.Height;++y)std::copy_n(row.data()+static_cast<size_t>(y)*region.Width,region.Width,frame.pixels.data()+static_cast<size_t>(clip.top-canvas.top+y)*frame.Width()+clip.left-canvas.left);
    }return frame;
}
static Document TransformDocument(const Document& document,float sx,float sy,float dx,float dy){
    Document result;const float scale=(sx+sy)/2;auto point=[&](Point p){return Point{p.x*sx+dx,p.y*sy+dy};};
    for(auto mark:document.marks){mark.a=point(mark.a);mark.b=point(mark.b);mark.number_target=point(mark.number_target);for(auto& p:mark.points)p=point(p);
        if(mark.number_detail){auto b=*mark.number_detail;auto a=point({b.left,b.top}),z=point({b.right,b.bottom});mark.number_detail=Box{a.x,a.y,z.x,z.y};}
        if(mark.number_leader)for(auto& p:*mark.number_leader)p=point(p);
        if(mark.corner_radii)for(auto& r:*mark.corner_radii)r*=scale;
        mark.text_wrap_width*=sx;
        mark.width*=scale;mark.arrow_size*=scale;mark.font_size*=scale;mark.number_size*=scale;mark.number_text_size*=scale;mark.corner_radius*=scale;mark.mosaic_cell*=scale;mark.mosaic_brush*=scale;result.marks.push_back(std::move(mark));
    }return result;
}
Document PinAnnotationDocument(const Document& document,RECT display,const Frame& source){
    const float sx=float(source.Width())/std::max(1L,display.right-display.left),sy=float(source.Height())/std::max(1L,display.bottom-display.top);
    return TransformDocument(document,sx,sy,-display.left*sx,-display.top*sy);
}
Document PinAnnotationDisplayDocument(const Document& document,const Frame& source,RECT display){return TransformDocument(document,float(display.right-display.left)/source.Width(),float(display.bottom-display.top)/source.Height(),float(display.left),float(display.top));}
}
