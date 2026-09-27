#include "export/png.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace lumashot;
using Microsoft::WRL::ComPtr;
namespace lumashot { void BaselineSaveImageFile(const Frame&,const std::filesystem::path&,int); }
static void Check(HRESULT hr) { if(FAILED(hr)) throw std::runtime_error("WIC failure"); }
static void FilterSave(const Frame& f,const std::filesystem::path& path,int filter) {
 ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
 ComPtr<IWICStream> stream;Check(factory->CreateStream(&stream));Check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
 ComPtr<IWICBitmapEncoder> encoder;Check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
 ComPtr<IWICBitmapFrameEncode> encoded;ComPtr<IPropertyBag2> options;Check(encoder->CreateNewFrame(&encoded,&options));
 PROPBAG2 prop{};prop.pstrName=const_cast<LPOLESTR>(L"FilterOption");VARIANT value{};value.vt=VT_UI1;value.bVal=static_cast<BYTE>(filter);Check(options->Write(1,&prop,&value));
 Check(encoded->Initialize(options.Get()));Check(encoded->SetSize(f.Width(),f.Height()));auto format=GUID_WICPixelFormat32bppBGRA;Check(encoded->SetPixelFormat(&format));
 Check(encoded->WritePixels(f.Height(),f.Width()*4,static_cast<UINT>(f.pixels.size()*4),reinterpret_cast<BYTE*>(const_cast<uint32_t*>(f.pixels.data()))));Check(encoded->Commit());Check(encoder->Commit());
}
namespace lumashot {
void BaselineSaveImageFile(const Frame& frame,const std::filesystem::path& path,int file_format) {
    if(file_format<0||file_format>2)throw std::invalid_argument("Invalid image format");
    ComPtr<IWICImagingFactory> factory; Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream));
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(file_format==1?GUID_ContainerFormatJpeg:(file_format==2?GUID_ContainerFormatBmp:GUID_ContainerFormatPng), nullptr, &encoder));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> encoded;
    Check(encoder->CreateNewFrame(&encoded, nullptr));
    Check(encoded->Initialize(nullptr));
    Check(encoded->SetSize(static_cast<UINT>(frame.Width()), static_cast<UINT>(frame.Height())));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(encoded->SetPixelFormat(&format));
    if(format==GUID_WICPixelFormat32bppBGRA)Check(encoded->WritePixels(static_cast<UINT>(frame.Height()),
        static_cast<UINT>(frame.Width()) * 4, static_cast<UINT>(frame.pixels.size() * 4),
        reinterpret_cast<BYTE*>(const_cast<uint32_t*>(frame.pixels.data()))));
    else {
        ComPtr<IWICBitmap> source;Check(factory->CreateBitmapFromMemory(frame.Width(),frame.Height(),GUID_WICPixelFormat32bppBGRA,frame.Width()*4,static_cast<UINT>(frame.pixels.size()*4),reinterpret_cast<BYTE*>(const_cast<uint32_t*>(frame.pixels.data())),&source));
        ComPtr<IWICFormatConverter> converter;Check(factory->CreateFormatConverter(&converter));
        Check(converter->Initialize(source.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));Check(encoded->WriteSource(converter.Get(),nullptr));
    }
    Check(encoded->Commit());
    Check(encoder->Commit());
}


}
int main() {
 Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
 const auto path=std::filesystem::temp_directory_path()/(L"lumashot-encoding-"+std::to_wstring(GetCurrentProcessId())+L".tmp");
 try {
 for(int scale: {1,2})for(int kind=0;kind<3;++kind) {
 auto f=MakeFrame({0,0,1920*scale,1080*scale});uint32_t seed=123;
 for(int y=0;y<f.Height();++y)for(int x=0;x<f.Width();++x){
 seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
 auto rgb=kind==0?(((x/150+y/30)%7)*0x202020u):kind==1?static_cast<uint32_t>(((x*255/f.Width())<<16)|((y*255/f.Height())<<8)|((x+y)%256)):(seed&0xffffffu);
 f.pixels[static_cast<size_t>(y)*f.Width()+x]=0xff000000u|rgb;
 }
 std::vector<uint32_t> baseline_jpeg;
 for(int mode=0;mode<9;++mode) {
 double total=0;for(int iteration=0;iteration<3;++iteration){
 auto start=std::chrono::steady_clock::now();
 if(mode<3)BaselineSaveImageFile(f,path,mode);else if(mode<6)SaveClipboardImageFile(f,path,mode-3);else FilterSave(f,path,mode==6?WICPngFilterNone:mode==7?WICPngFilterUp:WICPngFilterSub);
 total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 }
 auto decoded=ReadPng(path);if(decoded.Width()!=f.Width()||decoded.Height()!=f.Height())throw std::runtime_error("dimensions changed");
 if(mode==1)baseline_jpeg=decoded.pixels;
 if(mode==4&&decoded.pixels!=baseline_jpeg)throw std::runtime_error("JPEG conversion changed");
 if(mode!=1&&mode!=4&&decoded.pixels!=f.pixels)throw std::runtime_error("lossless pixels changed");
 std::cout<<scale<<","<<kind<<","<<mode<<","<<total/3<<","<<std::filesystem::file_size(path)<<std::endl;
 }
 }
 auto alpha=MakeFrame({0,0,17,13});for(size_t i=0;i<alpha.pixels.size();++i)alpha.pixels[i]=static_cast<uint32_t>(i*1234567);
 SaveClipboardImageFile(alpha,path,0);if(ReadPng(path).pixels!=alpha.pixels)throw std::runtime_error("PNG alpha changed");
 std::filesystem::remove(path);CoUninitialize();return 0;
 }catch(const std::exception& e){std::cerr<<e.what();std::filesystem::remove(path);CoUninitialize();return 1;}
}

