#include "pin/paper.h"
#include "pin/selection_tools.h"
#include "ui/render.h"
#include "export/png.h"
#include <wincodec.h>
#include <chrono>
#include <iostream>
using namespace lumashot;
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;
    auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;};
    for(float dpi:{96.f,144.f,192.f})for(POINT size:{POINT{1,1},POINT{1,720},POINT{720,1},POINT{720,410}}){
        const auto p=MakePaperLayout(size.x,size.y,dpi);HRGN region=PaperRegion(p);
        expect(PtInRegion(region,p.inset,p.inset)&&PtInRegion(region,p.inset+size.x-1,p.inset+size.y-1)&&!PtInRegion(region,p.width-1,p.height-1),"rounded paper and folded corner preserve image corners");DeleteObject(region);
        auto point=p.ToImage(p.ToWindow({-24.5f,35.25f}));expect(point.x==-24.5f&&point.y==35.25f,"paper mapping roundtrips at mixed DPI");
        auto shadow=PaperShadowImage(p);expect(shadow.pixels[static_cast<size_t>(p.shadow+p.inset)*shadow.Width()+p.shadow+p.inset]==0,"shadow is transparent over image");
        bool clear=true;for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x)clear=clear&&shadow.pixels[static_cast<size_t>(p.shadow+p.inset+y)*shadow.Width()+p.shadow+p.inset+x]==0;
        expect(clear,"curl shadow never darkens any screenshot pixel, including narrow images");
        expect(p.fold<=std::min(p.width,p.height)/2&&p.fold<=p.inset*2,"curl is bounded by the blank paper margin");
        bool transparent_border=true;for(int x=0;x<shadow.Width();++x)transparent_border&=shadow.pixels[x]==0&&shadow.pixels[static_cast<size_t>(shadow.Height()-1)*shadow.Width()+x]==0;
        for(int y=0;y<shadow.Height();++y)transparent_border&=shadow.pixels[static_cast<size_t>(y)*shadow.Width()]==0&&shadow.pixels[static_cast<size_t>(y)*shadow.Width()+shadow.Width()-1]==0;
        expect(transparent_border,"shadow fades to zero on every bitmap boundary");
        bool has_shadow=false;for(auto pixel:shadow.pixels)has_shadow|=(pixel>>24)>0;expect(has_shadow,"soft shadow has visible exterior pixels");
    }
    try{
        auto image=MakeFrame({0,0,720,410},0xfff8fbff);Document doc;Mark mark;mark.tool=Tool::Text;mark.a={24,20};mark.b={670,80};mark.color=0xff183451;mark.font_size=32;mark.text=L"项目笔记";doc.Add(mark);
        mark.a={25,75};mark.b={690,105};mark.font_size=15;mark.text=L"LumaShot · 轻巧地记录每一个细节";doc.Add(mark);
        mark.a={25,138};mark.b={690,176};mark.font_size=20;mark.text=L"梳理产品核心流程，聚焦截图与标注的极简体验。";doc.Add(mark);
        mark.a={25,178};mark.b={690,216};mark.text=L"需要进一步优化启动速度和内存占用，保持轻量。";doc.Add(mark);
        mark.a={25,237};mark.b={670,270};mark.text=L"待办事项";doc.Add(mark);mark.a={25,272};mark.b={670,308};mark.font_size=17;mark.text=L"✓ 完善核心截图功能";doc.Add(mark);
        mark.tool=Tool::Rectangle;mark.a={22,178};mark.b={587,211};mark.width=1.5f;mark.color=0xffff4545;doc.Add(mark);
        Renderer renderer;image=renderer.Flatten(image,doc,image.bounds);const auto p=MakePaperLayout(image.Width(),image.Height(),144);const auto shadow=PaperShadowImage(p);
        auto preview=MakeFrame(shadow.bounds,0xff1a354f);ComPtr<IWICImagingFactory> wic;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));ComPtr<IWICBitmap> bitmap;
        wic->CreateBitmapFromMemory(preview.Width(),preview.Height(),GUID_WICPixelFormat32bppPBGRA,preview.Width()*4,static_cast<UINT>(preview.pixels.size()*4),reinterpret_cast<BYTE*>(preview.pixels.data()),&bitmap);
        ComPtr<ID2D1Factory> factory;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf());ComPtr<ID2D1RenderTarget> target;
        factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target);
        const auto upload=[&](const Frame& f,D2D1_ALPHA_MODE alpha){ComPtr<ID2D1Bitmap> out;target->CreateBitmap(D2D1::SizeU(f.Width(),f.Height()),f.pixels.data(),f.Width()*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,alpha),96,96),&out);return out;};
        auto shadow_bitmap=upload(shadow,D2D1_ALPHA_MODE_PREMULTIPLIED),image_bitmap=upload(image,D2D1_ALPHA_MODE_IGNORE);
        target->BeginDraw();target->DrawBitmap(shadow_bitmap.Get());target->SetTransform(D2D1::Matrix3x2F::Translation(float(p.shadow),float(p.shadow)));DrawPaper(target.Get(),p);
        target->DrawBitmap(image_bitmap.Get(),D2D1::RectF(float(p.inset),float(p.inset),float(p.inset+image.Width()),float(p.inset+image.Height())),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        target->SetTransform(D2D1::Matrix3x2F::Translation(float(p.shadow+p.inset+287),float(p.shadow+p.inset+315)));DrawSelectionBar(target.Get(),415,83,1.0375f,-1);target->EndDraw();
        bitmap->CopyPixels(nullptr,preview.Width()*4,static_cast<UINT>(preview.pixels.size()*4),reinterpret_cast<BYTE*>(preview.pixels.data()));SavePng(preview,L"paper-b-preview.png");
        SavePng(Crop(preview,{p.shadow+p.width-p.fold-24,p.shadow+p.height-p.fold-24,preview.Width(),preview.Height()}),L"paper-curl-detail.png");
        expect(true,"paper curl rendered using production paper and shadow code");
        auto start=std::chrono::steady_clock::now();auto large=PaperShadowImage(MakePaperLayout(3840,2160,144));
        std::cout<<"4K shadow one-time generation ms: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"; alpha bitmap MiB: "<<double(large.pixels.size()*4)/(1024*1024)<<'\n';
    }catch(const std::exception& e){std::cout<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}
