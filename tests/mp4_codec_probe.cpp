#include "recording/encoder.h"
#include "recording/panel.h"
#include "recording/mp4_export.h"
#include <d3d10.h>
#include <iostream>
#include <cmath>
using namespace lumashot::recording;
int main(int argc,char** argv){
 if(argc>=4&&argc<=6&&std::string(argv[1])=="export"){
  try{
   if(argc==6&&std::string(argv[5])!="h264")throw std::runtime_error("Expected optional h264 mode");
   const auto start=GetTickCount64();
   const auto progress=[&](int p){std::cout<<"ms="<<GetTickCount64()-start<<" progress="<<p<<std::endl;};
    const auto stage=[&](const Mp4StageProgress& p){std::cout<<"ms="<<GetTickCount64()-start<<" stage="<<int(p.stage)<<" attempt="<<p.attempt<<" percent="<<p.percent<<std::endl;};
   const auto result=argc>=5
       ?detail::ExportMp4WithTool(argv[2],argv[3],{},progress,{argv[4],900000,3ull*1024*1024*1024,argc!=6},stage)
       :ExportMp4ToFile(argv[2],argv[3],{},progress,stage);
   std::cout<<"encoding="<<int(result.encoding)<<" original_bytes="<<result.original_bytes
            <<" saved_bytes="<<std::filesystem::file_size(argv[3])<<" elapsed_ms="<<GetTickCount64()-start<<std::endl;
   return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
 }

  if(argc>=3&&std::string(argv[1])=="preview"){
    try{
      const auto duration=argc>3?std::stoll(argv[3])*10000000LL:200000000LL;
      PreviewOptions options;options.thumbnails=argc<5||std::string(argv[4])!="poster";
      const auto start=GetTickCount64();ULONGLONG first{};unsigned updates=0;
      options.publish=[&](const PreviewImages& p){if(!updates)first=GetTickCount64()-start;++updates;if(p.poster.pixels.empty())throw std::runtime_error("Empty published poster");};
      const auto p=ReadPreview(std::filesystem::path(argv[2]),duration,{},options);
      if(p.poster.pixels.empty()||p.thumbnails.size()!=(options.thumbnails?8u:0u))throw std::runtime_error("Incomplete preview");
      std::cout<<"poster_ms="<<first<<" total_ms="<<GetTickCount64()-start<<" updates="<<updates<<" thumbnails="<<p.thumbnails.size()<<std::endl;
      return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
  }
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);int result=0;
 try{
  if(argc!=4||std::string(argv[1])!="generate")throw std::runtime_error("Usage: generate 1920|3840 output.mp4 OR preview file.mp4");
  const int w=std::stoi(argv[2]),h=w*9/16,fps=30,frames=600;if(w!=1920&&w!=3840)throw std::runtime_error("Invalid size");
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Quality fixture GPU");
        ComPtr<ID3D10Multithread> guard;context.As(&guard);guard->SetMultithreadProtected(TRUE);
        std::vector<uint32_t> base(size_t(w)*h,0xfff3f5f7),pixels;
        BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        void* bits{};HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);memcpy(bits,base.data(),base.size()*4);
        HFONT font=CreateFontW(-20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(20,24,30));
        const wchar_t* text=L"LumaShot  清晰文字  AaBb 0123456789";for(int y=24;y<h-24;y+=36)TextOutW(dc,16,y,text,lstrlenW(text));GdiFlush();memcpy(base.data(),bits,base.size()*4);for(auto& p:base)p|=0xff000000;
        SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
        const auto scene=[&](int frame){pixels=base;const int phase=frame/150;
            if(phase==1)for(int y=0;y<h;++y)memcpy(pixels.data()+size_t(y)*w,base.data()+size_t((y+frame*3)%h)*w,size_t(w/2)*4);
            for(int y=0;y<h;++y)for(int x=w/2;x<w;++x){const int shift=(x-w/2+(phase==2?frame*17:0))%(w/2);const uint32_t r=40+shift*170/(w/2),g=55+y*155/h,b=80+(shift+y)*110/(w/2+h);pixels[size_t(y)*w+x]=0xff000000|(r<<16)|(g<<8)|b;}
            const int cx=w/2+20+(frame*7)%(w/2-130);for(int y=240;y<340;++y)for(int x=cx;x<cx+96;++x)pixels[size_t(y)*w+x]=((x-cx)/8+(y-240)/8)%2?0xff101010:0xffeeeeee;
            if(phase==2)for(int y=h/2;y<h-50;++y)for(int x=w/2;x<w;++x){const uint32_t v=uint32_t(((x+frame*17)/13+(y+frame*11)/17)%2?210:40);pixels[size_t(y)*w+x]=0xff000000|v*0x010101;}
            for(int y=h-64;y<h;++y)for(int x=0;x<64;++x)pixels[size_t(y)*w+x]=frame%150<3?0xffffffff:0xff000000;
        };
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&d,nullptr,&texture),"Quality texture");
        Encoder encoder(device.Get(),std::filesystem::path(argv[3]),{w,h},{w,h},fps,false,true);
        std::vector<short> audio(1600*2);const auto start=GetTickCount64();
        for(int i=0;i<frames;++i){scene(i);context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),w*4,0);encoder.Frame(texture.Get(),{0,0,w,h},i*10000000LL/fps,10000000LL/fps);
            for(int j=0;j<1600;++j){const auto sample=int64_t(i)*1600+j;const short v=sample%240000<4800?short(10000*std::sin(double(sample)*6.283185307179586*880/48000)):0;audio[j*2]=audio[j*2+1]=v;}encoder.AudioFrame(audio.data(),1600,int64_t(i)*1600);
        }encoder.Finish();std::cout<<"generated "<<w<<"x"<<h<<" 600 frames / 20 seconds / sync pulses, ms="<<GetTickCount64()-start<<std::endl;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;result=1;}MFShutdown();CoUninitialize();return result;
}
