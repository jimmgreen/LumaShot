#include "recording/encoder.h"
#include "recording/gif_export.h"
#include "gif_test_helpers.h"
#include <d3d10.h>
#include <wincodec.h>
#include <iostream>
#include <vector>
using namespace lumashot::recording;
namespace {
int failures{};void Expect(bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;failures+=!ok;}
void SavePng(IWICImagingFactory* f,const wchar_t* path,IWICBitmapSource* source){ComPtr<IWICStream> stream;f->CreateStream(&stream);Check(stream->InitializeFromFilename(path,GENERIC_WRITE),"PNG file");ComPtr<IWICBitmapEncoder> encoder;f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);ComPtr<IWICBitmapFrameEncode> frame;encoder->CreateNewFrame(&frame,nullptr);frame->Initialize(nullptr);frame->WriteSource(source,nullptr);frame->Commit();encoder->Commit();}
}
int main(){CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);try{
    constexpr UINT w=320,h=180;const auto folder=std::filesystem::current_path()/L"gif-quality-fixture";std::filesystem::create_directories(folder);const auto mp4=folder/L"gradient.mp4",gif=folder/L"gradient.gif";
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Fixture GPU");ComPtr<ID3D10Multithread> threaded;context.As(&threaded);threaded->SetMultithreadProtected(TRUE);
    std::vector<UINT32> pixels(w*h);for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const UINT r=70+x*170/w,g=30+x*145/w,b=20+x*115/w;pixels[y*w+x]=0xff000000|r<<16|g<<8|b;}
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&d,nullptr,&texture),"Fixture texture");
    {Encoder encoder(device.Get(),mp4,{w,h},{w,h},30,true);for(int i=0;i<45;++i){if(i==30)for(UINT y=164;y<180;++y)for(UINT x=304;x<320;++x)pixels[y*w+x]=0xffedbb95;context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),w*4,0);encoder.Frame(texture.Get(),{0,0,w,h},i*10000000LL/30,10000000LL/30);}encoder.Finish();}
    GifOptions options;options.width=0;options.fps=15;options.end=15000000;int progress{};ExportGif(mp4,gif,options,{},[&](int p){progress=p;});Expect(progress==100,"original-size GIF exports");
    ComPtr<IWICImagingFactory> factory;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));ComPtr<IWICBitmapDecoder> decoder;Check(factory->CreateDecoderFromFilename(gif.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Read adaptive GIF");UINT count{};decoder->GetFrameCount(&count);unsigned duration{};bool cropped=false;std::vector<BYTE> canvas(w*h*4);ComPtr<IWICBitmapFrameDecode> first;
    for(UINT i=0;i<count;++i){ComPtr<IWICBitmapFrameDecode> frame;decoder->GetFrame(i,&frame);if(!i)first=frame;UINT fw{},fh{};frame->GetSize(&fw,&fh);ComPtr<IWICMetadataQueryReader> meta;frame->GetMetadataQueryReader(&meta);const auto read=[&](const wchar_t* name){PROPVARIANT v{};Check(meta->GetMetadataByName(name,&v),"Delta metadata");const unsigned value=v.uiVal;PropVariantClear(&v);return value;};const auto left=read(L"/imgdesc/Left"),top=read(L"/imgdesc/Top");duration+=read(L"/grctlext/Delay");Expect(left+fw<=w&&top+fh<=h,"delta remains within original canvas");cropped|=fw<w||fh<h;ComPtr<IWICFormatConverter> converter;factory->CreateFormatConverter(&converter);converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);std::vector<BYTE> patch(fw*fh*4);converter->CopyPixels(nullptr,fw*4,UINT(patch.size()),patch.data());for(UINT y=0;y<fh;++y)for(UINT x=0;x<fw;++x)if(patch[(y*fw+x)*4+3])memcpy(canvas.data()+((y+top)*w+left+x)*4,patch.data()+(y*fw+x)*4,4);}
    Expect(count<20,"static frames merge instead of storing 23 full frames");Expect(cropped,"small change writes a cropped delta frame");Expect(duration>=146&&duration<=154,"merged frames preserve playback duration");
    ComPtr<IWICFormatConverter> rgb;factory->CreateFormatConverter(&rgb);rgb->Initialize(first.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);std::vector<BYTE> decoded(w*h*4);rgb->CopyPixels(nullptr,w*4,UINT(decoded.size()),decoded.data());double adaptive{},fixed{};for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const int channels[]={int(20+x*115/w),int(30+x*145/w),int(70+x*170/w)};for(int c=0;c<3;++c){const int value=channels[c],error=int(decoded[(y*w+x)*4+c])-value,old=((value+25)/51)*51-value;adaptive+=error*error;fixed+=old*old;}}Expect(adaptive<fixed*.5,"adaptive palette halves fixed-cube color error on skin-tone gradient");Expect(adaptive/(w*h*3)<22,"full precision palette keeps gradient error below previous noisy encoder");std::cout<<"Gradient MSE adaptive="<<adaptive/(w*h*3)<<" fixed="<<fixed/(w*h*3)<<" frames="<<count<<" bytes="<<std::filesystem::file_size(gif)<<std::endl;
    Expect(std::abs(int(canvas[(172*w+310)*4+2])-237)<20,"composited delta displays new patch without stale pixels");
    SavePng(factory.Get(),(folder/L"adaptive.png").c_str(),rgb.Get());
    // Baseline: same palette and same first image repeated, without delta or
    // frame merging. This isolates size savings from resolution and palette.
    const auto baseline=folder/L"full-frames.gif";ComPtr<IWICStream> stream;factory->CreateStream(&stream);stream->InitializeFromFilename(baseline.c_str(),GENERIC_WRITE);ComPtr<IWICBitmapEncoder> full;factory->CreateEncoder(GUID_ContainerFormatGif,nullptr,&full);full->Initialize(stream.Get(),WICBitmapEncoderNoCache);ComPtr<IWICPalette> pal;factory->CreatePalette(&pal);first->CopyPalette(pal.Get());UINT colorCount{};pal->GetColorCount(&colorCount);std::cout<<"Palette entries="<<colorCount<<std::endl;full->SetPalette(pal.Get());std::vector<BYTE> index(w*h);first->CopyPixels(nullptr,w,UINT(index.size()),index.data());for(int i=0;i<23;++i){ComPtr<IWICBitmapFrameEncode> frame;full->CreateNewFrame(&frame,nullptr);frame->Initialize(nullptr);frame->SetSize(w,h);auto format=GUID_WICPixelFormat8bppIndexed;frame->SetPixelFormat(&format);frame->WritePixels(h,w,UINT(index.size()),index.data());frame->Commit();}full->Commit();full.Reset();stream.Reset();Expect(std::filesystem::file_size(gif)<std::filesystem::file_size(baseline)/2,"delta and merging cut fixture size by more than half");
    const auto destination=folder/L"transaction.gif";gif_test::Write(destination,{1,2,3});
    std::stop_source stagedCancel;bool canceledSave=false;
    try{ExportGifToFile(mp4,destination,options,stagedCancel.get_token(),[&](int p){if(p>=80)stagedCancel.request_stop();});}catch(const std::exception&){canceledSave=true;}
    Expect(canceledSave&&gif_test::Bytes(destination)==std::vector<BYTE>({1,2,3}),"cancellation between encode and optimize preserves destination");
    int lastProgress=-1;bool monotonic=true,committed=false;
    ExportGifToFile(mp4,destination,options,{},[&](int p){monotonic&=p>=lastProgress;lastProgress=p;if(p==100)committed=gif_test::Decode(destination).duration>0;});
    Expect(monotonic&&lastProgress==100&&committed,"production export progress reaches 100 only after valid file commit");
    std::vector<std::pair<std::vector<BYTE>,unsigned>> originalFrames;
    gif_test::Decode(gif,[&](const auto& image,unsigned duration,UINT){originalFrames.emplace_back(image,duration);});
    size_t originalFrame=0;unsigned remaining=originalFrames.front().second;double squaredError=0;uint64_t samples=0;bool sameTimeline=true;
    const auto finalInfo=gif_test::Decode(destination,[&](const auto& image,unsigned duration,UINT frame){
        if(frame==0)gif_test::Png(folder/L"optimized.png",image,w,h);
        while(duration){
            if(originalFrame>=originalFrames.size()){sameTimeline=false;break;}
            const auto overlap=std::min(duration,remaining);const auto& reference=originalFrames[originalFrame].first;
            for(size_t i=0;i<image.size();++i)if(i%4!=3){const int error=int(image[i])-int(reference[i]);squaredError+=double(error*error)*overlap;}
            samples+=uint64_t(w)*h*3*overlap;duration-=overlap;remaining-=overlap;
            if(!remaining&&++originalFrame<originalFrames.size())remaining=originalFrames[originalFrame].second;
        }
    });
    const double optimizerMse=samples?squaredError/double(samples):10000;
    Expect(sameTimeline&&originalFrame==originalFrames.size()&&finalInfo.duration==duration,"optimized animation retains full presentation timeline");
    Expect(optimizerMse==0,"production optimization preserves every displayed pixel exactly");
    std::cout<<"Optimizer MSE="<<optimizerMse<<" final bytes="<<std::filesystem::file_size(destination)<<std::endl;
    size_t leftovers=0;for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.is_directory()&&entry.path().filename().native().starts_with(L"gif-export-"))++leftovers;
    Expect(leftovers==0,"success and canceled exports clean unique temporary directories");
    std::stop_source cancel;cancel.request_stop();bool stopped=false;try{ExportGif(mp4,folder/L"cancel.gif",options,cancel.get_token(),[](int){});}catch(...){stopped=true;}Expect(stopped,"palette sampling respects cancellation");
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}MFShutdown();CoUninitialize();return failures?1:0;}


