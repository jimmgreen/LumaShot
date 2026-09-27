#pragma once
#include "recording/gif_frames.h"
#include <fstream>
#include <functional>
namespace gif_test {
using namespace lumashot::recording;
inline unsigned Meta(IWICMetadataQueryReader* meta,const wchar_t* key,unsigned fallback=0){
    PROPVARIANT v{};if(FAILED(meta->GetMetadataByName(key,&v)))return fallback;
    const auto n=v.vt==VT_UI1?unsigned(v.bVal):v.vt==VT_BOOL?unsigned(v.boolVal!=VARIANT_FALSE):unsigned(v.uiVal);
    PropVariantClear(&v);return n;
}
struct Info{UINT width{},height{},count{};uint64_t duration{};bool transparent{},cropped{};};
inline Info Decode(const std::filesystem::path& path,const std::function<void(const std::vector<BYTE>&,unsigned,UINT)>& visit={}){
    ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Test factory");
    ComPtr<IWICBitmapDecoder> decoder;Check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),"Test decoder");
    ComPtr<IWICMetadataQueryReader> meta;Check(decoder->GetMetadataQueryReader(&meta),"Test canvas metadata");
    Info info;info.width=Meta(meta.Get(),L"/logscrdesc/Width");info.height=Meta(meta.Get(),L"/logscrdesc/Height");
    Check(decoder->GetFrameCount(&info.count),"Test count");
    std::vector<BYTE> canvas(size_t(info.width)*info.height*4,0),saved;
    std::array<WICColor,256> colors{};UINT colorCount{};ComPtr<IWICPalette> pal;factory->CreatePalette(&pal);
    if(SUCCEEDED(decoder->CopyPalette(pal.Get())))pal->GetColors(256,colors.data(),&colorCount);
    const auto background=Meta(meta.Get(),L"/logscrdesc/BackgroundColorIndex");
    const uint32_t clear=background<colorCount?colors[background]:0;
    for(UINT i=0;i<info.count;++i){
        ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(i,&frame),"Test frame");
        UINT w{},h{};frame->GetSize(&w,&h);frame->GetMetadataQueryReader(&meta);
        const auto x=Meta(meta.Get(),L"/imgdesc/Left"),y=Meta(meta.Get(),L"/imgdesc/Top");
        if(x+w>info.width||y+h>info.height)throw std::runtime_error("Frame outside canvas");
        const auto disposal=Meta(meta.Get(),L"/grctlext/Disposal"),delay=Meta(meta.Get(),L"/grctlext/Delay");
        if(disposal==3)saved=canvas;
        info.cropped|=w<info.width||h<info.height;info.transparent|=Meta(meta.Get(),L"/grctlext/TransparencyFlag")!=0;
        ComPtr<IWICFormatConverter> converter;factory->CreateFormatConverter(&converter);
        Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Test frame RGB");
        std::vector<BYTE> patch(size_t(w)*h*4);Check(converter->CopyPixels(nullptr,w*4,UINT(patch.size()),patch.data()),"Test decode pixels");
        for(UINT row=0;row<h;++row)for(UINT col=0;col<w;++col){const auto p=(size_t(row)*w+col)*4;
            if(patch[p+3])memcpy(canvas.data()+((size_t(y+row)*info.width)+x+col)*4,patch.data()+p,4);}
        info.duration+=delay;if(visit)visit(canvas,delay,i);
        if(disposal==2){for(UINT row=0;row<h;++row)for(UINT col=0;col<w;++col)memcpy(canvas.data()+((size_t(y+row)*info.width)+x+col)*4,&clear,4);}
        else if(disposal==3)canvas=saved;
    }
    return info;
}
inline std::vector<BYTE> Bytes(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
inline void Write(const std::filesystem::path& p,const std::vector<BYTE>& bytes){std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
inline void Png(const std::filesystem::path& path,const std::vector<BYTE>& pixels,UINT w,UINT h){
    ComPtr<IWICImagingFactory> f;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)),"PNG factory");
    ComPtr<IWICStream> stream;f->CreateStream(&stream);Check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"PNG file");
    ComPtr<IWICBitmapEncoder> enc;f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc);enc->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;enc->CreateNewFrame(&frame,nullptr);frame->Initialize(nullptr);frame->SetSize(w,h);
    auto format=GUID_WICPixelFormat32bppBGRA;frame->SetPixelFormat(&format);
    Check(frame->WritePixels(h,w*4,UINT(pixels.size()),const_cast<BYTE*>(pixels.data())),"PNG pixels");frame->Commit();enc->Commit();
}
}
