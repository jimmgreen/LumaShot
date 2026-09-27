#include "ui/brand_icon.h"
#include "capture/frame.h"
#include "export/png.h"
#include <wincodec.h>
#include <filesystem>
#include <iostream>
using namespace lumashot;
using Microsoft::WRL::ComPtr;
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int failures{};
    const auto check=[&](bool good,const char* label){std::cout<<(good?"PASS ":"FAIL ")<<label<<'\n';failures+=!good;};
    {
        ComPtr<IWICImagingFactory> wic;ComPtr<ID2D1Factory> factory;
        CheckWin32(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"WIC factory");
        CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf())),"D2D factory");
        std::filesystem::create_directories(L"build/brand-icon");
        for(int size:{16,20,24,28,32,40,48,64,96,128,256,1254}){
            ComPtr<IWICBitmap> bitmap;ComPtr<ID2D1RenderTarget> target;
            CheckWin32(SUCCEEDED(wic->CreateBitmap(size,size,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap)),"WIC bitmap");
            auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            CheckWin32(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(),props,&target)),"Vector target");
            BrandIcon icon;target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));
            // Windows allocates a tiny square slot. Fit the visible artwork,
            // preserving its aspect ratio, instead of shrinking the SVG margins.
            const float width=static_cast<float>(size),height=width*960.f/1004.f;
            if(size<=256)icon.Draw(target.Get(),{0,(width-height)/2,width,(width+height)/2},true);
            else icon.Draw(target.Get(),{0,0,width,width});
            CheckWin32(SUCCEEDED(target->EndDraw()),"Render SVG icon");
            auto frame=MakeFrame({0,0,size,size});
            CheckWin32(SUCCEEDED(bitmap->CopyPixels(nullptr,size*4,size*size*4,reinterpret_cast<BYTE*>(frame.pixels.data()))),"Read icon pixels");
            check(frame.pixels.front()==0,"transparent outer corner");
            if(size<=256){
                int left=size,right=-1,top=size,bottom=-1;
                for(int y=0;y<size;++y)for(int x=0;x<size;++x)if((frame.pixels[static_cast<size_t>(y)*size+x]>>24)>128){left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
                check(right-left+1>=size-1&&bottom-top+1>=size*9/10,"Windows icon artwork fills its slot without inherited SVG padding");
            }
            const auto center=frame.pixels[static_cast<size_t>(size/2)*size+size/2];
            check((center>>24)>240&&(center&255)>((center>>16)&255),"cyan star rendered at original center");
            for(auto& pixel:frame.pixels){const auto alpha=pixel>>24;if(alpha&&alpha<255){UINT32 straight=alpha<<24;for(UINT shift:{0u,8u,16u})straight|=std::min(255u,(((pixel>>shift)&255)*255+alpha/2)/alpha)<<shift;pixel=straight;}}
            SavePng(frame,std::filesystem::path(L"build/brand-icon")/(std::to_wstring(size)+L".png"));
            icon.Reset();check(!icon,"target resources release on reset");
        }
    }
    CoUninitialize();return failures?1:0;
}

