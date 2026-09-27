#include "recording/encoder.h"
#include "recording/pixels.h"
#include "gif_test_helpers.h"
#include <d3d10.h>
#include <iostream>
using namespace lumashot::recording;
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);int result=0;
    try{
        constexpr int w=1280,h=720,fps=30,frames=45;
        const auto folder=std::filesystem::current_path()/
#ifdef LUMASHOT_MP4_BASELINE
            L"mp4-quality-baseline";
#else
            L"mp4-quality-fixture";
#endif
std::filesystem::create_directories(folder);
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Quality fixture GPU");
        ComPtr<ID3D10Multithread> guard;context.As(&guard);guard->SetMultithreadProtected(TRUE);
        std::vector<uint32_t> base(size_t(w)*h,0xfff3f5f7),pixels;
        BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        void* bits{};HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);memcpy(bits,base.data(),base.size()*4);
        HFONT font=CreateFontW(-20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(20,24,30));
        const wchar_t* text=L"LumaShot  清晰文字  AaBb 0123456789";for(int y=24;y<h-24;y+=36)TextOutW(dc,16,y,text,lstrlenW(text));GdiFlush();memcpy(base.data(),bits,base.size()*4);for(auto& p:base)p|=0xff000000;
        SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
        const auto scene=[&](int frame){pixels=base;for(int y=0;y<h;++y)for(int x=w/2;x<w;++x){const int shift=(x-w/2+frame*5)%(w/2);const uint32_t r=40+shift*170/(w/2),g=55+y*155/h,b=80+(shift+y)*110/(w/2+h);pixels[size_t(y)*w+x]=0xff000000|(r<<16)|(g<<8)|b;}
            const int cx=w/2+20+frame*7;for(int y=240;y<340;++y)for(int x=cx;x<cx+96;++x)pixels[size_t(y)*w+x]=((x-cx)/8+(y-240)/8)%2?0xff101010:0xffeeeeee;
        };
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&d,nullptr,&texture),"Quality texture");
        for(bool software:{true,false}){
            const auto name=software?L"cpu":L"gpu";const auto path=folder/(std::wstring(name)+L".mp4");
            {Encoder encoder(device.Get(),path,{w,h},{w,h},fps,software);std::cout<<(software?"CPU":"GPU")<<" quality_control="<<encoder.UsesQualityControl()<<std::endl;
#ifndef LUMASHOT_MP4_BASELINE
                if(software&&!encoder.UsesQualityControl())throw std::runtime_error("Software quality mode did not survive BeginWriting");
#endif
                for(int i=0;i<frames;++i){scene(i);context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),w*4,0);encoder.Frame(texture.Get(),{0,0,w,h},i*10000000LL/fps,10000000LL/fps);}encoder.Finish();}
            ComPtr<IMFAttributes> attrs;MFCreateAttributes(&attrs,1);attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
            ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(path.c_str(),attrs.Get(),&reader),"Quality reader");
            ComPtr<IMFMediaType> native;reader->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&native);UINT width{},height{};MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&width,&height);
            if(width!=w||height!=h)throw std::runtime_error("Original MP4 dimensions changed");
            ComPtr<IMFMediaType> type;MFCreateMediaType(&type);type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);Check(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get()),"Quality RGB");
            int count=0;double total=0,textError=0;std::vector<BYTE> rgb;
            for(;;){DWORD flags{};LONGLONG time{};ComPtr<IMFSample> sample;Check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,&time,&sample),"Quality decode");if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;if(!sample)continue;
                if(count>=frames||std::abs(time-count*10000000LL/fps)>10000){std::cerr<<"frame="<<count<<" timestamp="<<time<<std::endl;throw std::runtime_error("MP4 frame timing changed");}
                CopyDecodedRgb32(reader.Get(),sample.Get(),w,h,rgb);scene(count);
                for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto i=size_t(y)*w+x;for(int c=0;c<3;++c){const int delta=int(rgb[i*4+c])-int((pixels[i]>>(8*c))&255);total+=delta*delta;if(x<w/2)textError+=delta*delta;}}
                if(count==0||count==frames-1)gif_test::Png(folder/(std::wstring(name)+L"-"+std::to_wstring(count)+L".png"),rgb,w,h);++count;
            }
            const double mse=total/(double(w)*h*3*frames),textMse=textError/(double(w/2)*h*3*frames);
            std::cout<<"frames="<<count<<" bytes="<<std::filesystem::file_size(path)<<" MSE="<<mse<<" text_MSE="<<textMse<<std::endl;
            if(count!=frames||mse>100||textMse>25)throw std::runtime_error("MP4 frame count or quality regression");
        }
        std::cout<<"PASS original resolution, frame timing, moving detail and text quality"<<std::endl;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;result=1;}
    MFShutdown();CoUninitialize();return result;
}
