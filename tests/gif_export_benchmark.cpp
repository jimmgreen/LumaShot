#include "gif_test_helpers.h"
#include "recording/gif_optimizer.h"
#include <d3d10.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>
#ifdef LUMASHOT_GIF_BASELINE
namespace lumashot::recording {void ExportGifBaseline(const std::filesystem::path&,const std::filesystem::path&,const GifOptions&,std::stop_token,const std::function<void(int)>&);}
#endif
using namespace lumashot::recording;
int main(int argc,char** argv){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);
    try{
        if(argc!=3&&argc!=4)throw std::runtime_error("Usage: benchmark generate|before|after|inspect|compare|compare-exact 1920|3840 [output-width]");
        const std::string mode=argv[1];const UINT w=UINT(std::stoi(argv[2])),h=w*9/16;
        const int outputWidth=argc==4?std::stoi(argv[3]):0;const auto outputSize=OutputSize(int(w),int(h),outputWidth);
        if(w!=1920&&w!=3840)throw std::runtime_error("Fixture width must be 1920 or 3840");
        const auto folder=std::filesystem::current_path()/(L"gif-benchmark-"+std::to_wstring(w));std::filesystem::create_directories(folder);
        const auto source=folder/L"source.mp4";
        if(mode=="generate"){
            std::vector<uint32_t> base(size_t(w)*h,0xfff3f5f7),pixels;
            for(UINT y=0;y<h;++y)for(UINT x=w/2;x<w;++x){const UINT r=40+(x-w/2)*170/(w/2),g=55+y*155/h,b=80+(x+y)*110/(w+h);base[size_t(y)*w+x]=0xff000000|(r<<16)|(g<<8)|b;}
            BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=LONG(w);bi.bmiHeader.biHeight=-LONG(h);bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
            void* bits{};HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);memcpy(bits,base.data(),base.size()*4);
            HFONT font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(20,24,30));
            const wchar_t* text=L"LumaShot  高清录屏  AaBb 0123456789 / File Edit View";
            for(int y=32;y<int(h)-32;y+=48)TextOutW(dc,32,y,text,lstrlenW(text));
            GdiFlush();memcpy(base.data(),bits,base.size()*4);for(auto& p:base)p|=0xff000000;
            SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
            gif_test::Png(folder/L"reference.png",std::vector<BYTE>(reinterpret_cast<BYTE*>(base.data()),reinterpret_cast<BYTE*>(base.data()+base.size())),w,h);
            ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
            Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Fixture GPU");
            ComPtr<ID3D10Multithread> threaded;context.As(&threaded);threaded->SetMultithreadProtected(TRUE);
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&d,nullptr,&texture),"Fixture texture");
            Encoder enc(device.Get(),source,{LONG(w),LONG(h)},{LONG(w),LONG(h)},15,true);
            std::cout<<"quality_control="<<enc.UsesQualityControl()<<std::endl;
            for(int i=0;i<30;++i){pixels=base;
                if(i>=10){const UINT cx=80+UINT(i-10)*12,cy=h-100;for(UINT y=0;y<22;++y)for(UINT x=0;x<=y/2;++x)pixels[size_t(cy+y)*w+cx+x]=x==0||x==y/2?0xffffffff:0xff101010;}
                if(i>=20)for(UINT y=20;y<40;++y)for(UINT x=w-40;x<w-20;++x)pixels[size_t(y)*w+x]=0xfff0a020;
                context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),w*4,0);
                enc.Frame(texture.Get(),{0,0,LONG(w),LONG(h)},i*10000000LL/15,10000000LL/15);
            }enc.Finish();std::cout<<"generated "<<w<<"x"<<h<<" 30 frames"<<std::endl;
        }else if(mode=="before"||mode=="after"){
            GifOptions options;options.fps=15;options.end=20000000;options.width=outputWidth;const auto file=folder/(mode=="before"?L"before.gif":L"after.gif");
            const auto start=GetTickCount64();
            if(mode=="before"){
#ifdef LUMASHOT_GIF_BASELINE
                ExportGifBaseline(source,file,options,{},[](int){});
#else
                throw std::runtime_error("Configure LUMASHOT_GIF_BASELINE_SOURCE to benchmark the original encoder");
#endif
            }else ExportGif(source,file,options,{},[](int){});
            const auto encoded=GetTickCount64();const auto rawSize=std::filesystem::file_size(file);
            if(mode=="after"){
                std::filesystem::copy_file(file,folder/L"before-optimizer.gif",std::filesystem::copy_options::overwrite_existing);
                OptimizeGif(file,true,{},[](int){});
            }
            PROCESS_MEMORY_COUNTERS pm{};GetProcessMemoryInfo(GetCurrentProcess(),&pm,sizeof(pm));
            std::cout<<mode<<","<<w<<","<<rawSize<<","<<std::filesystem::file_size(file)<<","<<std::fixed<<std::setprecision(3)
                <<(encoded-start)/1000.<<","<<(GetTickCount64()-encoded)/1000.<<","<<pm.PeakWorkingSetSize/1048576.<<std::endl;
        }else if(mode=="compare"||mode=="compare-exact"){
            std::vector<std::pair<std::vector<BYTE>,unsigned>> reference;
            const auto before=gif_test::Decode(folder/L"before.gif",[&](const auto& pixels,unsigned delay,UINT){reference.emplace_back(pixels,delay);});
            if(reference.empty())throw std::runtime_error("Empty baseline");
            size_t frame=0;unsigned remaining=reference.front().second;bool same=true;double total=0,maxMse=0;uint64_t count=0;
            const auto after=gif_test::Decode(folder/L"before-optimizer.gif",[&](const auto& pixels,unsigned delay,UINT index){
                if(index==0)gif_test::Png(folder/L"after-scaled.png",pixels,UINT(outputSize.cx),UINT(outputSize.cy));
                while(delay){
                    if(frame>=reference.size()||pixels.size()!=reference[frame].first.size()){same=false;break;}
                    const auto overlap=std::min(delay,remaining);double error=0;
                    for(size_t i=0;i<pixels.size();++i)if(i%4!=3){const double d=int(pixels[i])-int(reference[frame].first[i]);error+=d*d;}
                    const auto samples=pixels.size()/4*3;const double mse=error/double(samples);maxMse=std::max(maxMse,mse);total+=error*overlap;count+=samples*overlap;
                    std::cout<<"frame="<<index<<" reference="<<frame<<" duration_cs="<<overlap<<" mse="<<mse<<std::endl;
                    delay-=overlap;remaining-=overlap;if(!remaining&&++frame<reference.size())remaining=reference[frame].second;
                }
            });
            gif_test::Png(folder/L"before-scaled.png",reference.front().first,UINT(outputSize.cx),UINT(outputSize.cy));
            same&=frame==reference.size()&&before.duration==after.duration&&before.width==after.width&&before.height==after.height;
            const double mse=count?total/double(count):1e9;
            std::cout<<"timeline="<<same<<" mse="<<mse<<" max_frame_mse="<<maxMse<<std::endl;
            if(!same||maxMse>(mode=="compare-exact"?0:25))throw std::runtime_error("GIF comparison exceeded timeline or per-frame quality tolerance");
        }else if(mode=="inspect"){
            for(const wchar_t* name:{L"before",L"after"}){
                std::vector<BYTE> previous;uint64_t changed=0;
                const auto info=gif_test::Decode(folder/(std::wstring(name)+L".gif"),[&](const auto& canvas,unsigned,UINT i){
                    if(i==0||i==10)gif_test::Png(folder/(std::wstring(name)+L"-"+std::to_wstring(i)+L".png"),canvas,UINT(outputSize.cx),UINT(outputSize.cy));
                    if(!previous.empty())for(size_t p=0;p<canvas.size();p+=4)changed+=memcmp(canvas.data()+p,previous.data()+p,4)!=0;
                    previous=canvas;
                });
                gif_test::Png(folder/(std::wstring(name)+L"-last.png"),previous,UINT(outputSize.cx),UINT(outputSize.cy));
                std::wcout<<name<<L" frames="<<info.count<<L" duration_cs="<<info.duration<<L" changed_pixels="<<changed<<std::endl;
            }
        }else throw std::runtime_error("Unknown benchmark mode");
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;MFShutdown();CoUninitialize();return 1;}
    MFShutdown();CoUninitialize();return 0;
}
