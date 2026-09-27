#include "recording/encoder.h"
#include "recording/pixels.h"
#include "recording/panel.h"
#include "gif_test_helpers.h"
#include <d3d10.h>
#include <iostream>
using namespace lumashot::recording;
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);int failures=0;
    try{
        const auto folder=std::filesystem::current_path()/L"recording-geometry-fixture";std::filesystem::create_directories(folder);
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Fixture device");
        ComPtr<ID3D10Multithread> guard;context.As(&guard);guard->SetMultithreadProtected(TRUE);
        for(const SIZE size:{SIZE{638,358},SIZE{1918,1078},SIZE{3838,2158}})for(bool software:{true,false}){
            const int w=size.cx,h=size.cy;const auto name=std::to_wstring(w)+(software?L"-cpu":L"-gpu");const auto file=folder/(name+L".mp4");
            std::vector<uint32_t> pixels(size_t(w)*h);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)pixels[size_t(y)*w+x]=0xff000000|uint32_t(30+x*180/w)<<16|uint32_t(30+y*180/h)<<8|80;
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
            D3D11_SUBRESOURCE_DATA data{pixels.data(),UINT(w)*4,0};ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&d,&data,&texture),"Fixture texture");
            {Encoder encoder(device.Get(),file,size,size,15,software);for(int i=0;i<6;++i)encoder.Frame(texture.Get(),{0,0,w,h},i*10000000LL/15,10000000LL/15);encoder.Finish();}
            // Log negotiation before and after the first actual decoded sample.
            ComPtr<IMFAttributes> attributes;MFCreateAttributes(&attributes,1);attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
            ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(file.c_str(),attributes.Get(),&reader),"Reader");
            ComPtr<IMFMediaType> type;MFCreateMediaType(&type);type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get());reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),&type);
            UINT prior{};type->GetUINT32(MF_MT_DEFAULT_STRIDE,&prior);DWORD flags{};ComPtr<IMFSample> sample;
            while(!sample){Check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,nullptr,&sample),"Decode");if(flags&MF_SOURCE_READERF_ENDOFSTREAM)throw std::runtime_error("No fixture frame");}
            reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),&type);UINT current{},dw{},dh{};type->GetUINT32(MF_MT_DEFAULT_STRIDE,&current);MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&dw,&dh);
            ComPtr<IMFMediaBuffer> buffer;sample->GetBufferByIndex(0,&buffer);ComPtr<IMF2DBuffer> two;buffer.As(&two);DWORD length{};buffer->GetCurrentLength(&length);
            std::wcout<<name<<L" stride="<<prior<<L" -> "<<current<<L" decoded="<<dw<<L"x"<<dh<<L" flags="<<flags<<L" twoD="<<bool(two)<<L" length="<<length<<std::endl;
            const auto preview=ReadPreview(file,4000000,{});
            auto check=[&](const PreviewPixels& image){double error=0;size_t n=0;for(int y=3;y<image.height-3;y+=7)for(int x=3;x<image.width-3;x+=7){auto p=image.pixels[size_t(y)*image.width+x];error+=std::abs(int((p>>16)&255)-(30+double(x)*180/image.width))+std::abs(int((p>>8)&255)-(30+double(y)*180/image.height))+std::abs(int(p&255)-80);n+=3;}return n?error/double(n):999.;};
            const auto error=check(preview.poster);bool good=error<8&&preview.thumbnails.size()==8;for(const auto& thumbnail:preview.thumbnails)good&=check(thumbnail)<8;
            std::cout<<(good?"PASS ":"FAIL ")<<"preview geometry MAE="<<error<<std::endl;failures+=!good;
            GifOptions options;options.end=4000000;options.fps=15;const auto gif=folder/(name+L".gif");ExportGif(file,gif,options,{},[](int){});
            bool gifGood=true;double gifError=0;
            const auto info=gif_test::Decode(gif,[&](const auto& canvas,unsigned,UINT index){
                PreviewPixels decoded;decoded.width=w;decoded.height=h;decoded.pixels.resize(size_t(w)*h);memcpy(decoded.pixels.data(),canvas.data(),canvas.size());
                const auto frameError=check(decoded);gifError=std::max(gifError,frameError);gifGood&=frameError<10;
                if(index==0)gif_test::Png(folder/(name+L"-gif.png"),canvas,UINT(w),UINT(h));
            });
            gifGood&=info.width==UINT(w)&&info.height==UINT(h)&&info.duration==40;
            std::cout<<(gifGood?"PASS ":"FAIL ")<<"GIF visible dimensions, timeline and geometry MAE="<<gifError<<std::endl;failures+=!gifGood;
            const auto& image=preview.poster;gif_test::Png(folder/(name+L".png"),std::vector<BYTE>(reinterpret_cast<const BYTE*>(image.pixels.data()),reinterpret_cast<const BYTE*>(image.pixels.data()+image.pixels.size())),UINT(image.width),UINT(image.height));
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}
    MFShutdown();CoUninitialize();return failures?1:0;
}
