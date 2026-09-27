#include "recording/gif_frames.h"
#include <array>

namespace lumashot::recording {
namespace {
void Canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Export canceled");}
void Number(IWICMetadataQueryWriter* writer,const wchar_t* key,unsigned n,VARTYPE type=VT_UI2){
    PROPVARIANT value{};value.vt=type;if(type==VT_UI1)value.bVal=BYTE(n);else value.uiVal=USHORT(n);
    Check(writer->SetMetadataByName(key,&value),"GIF frame metadata");
}
}
GifFrames::GifFrames(IWICImagingFactory* factory,const std::filesystem::path& output,
    int width,int height,std::span<const uint32_t> colors,bool loop,std::stop_token stop,unsigned colorTolerance)
    :width_(width),height_(height),stop_(stop){
    Canceled(stop_);
    if(width<=0||height<=0||width>65535||height>65535||colors.empty()||colors.size()>255||colorTolerance>8)
        throw std::runtime_error("Invalid GIF palette or dimensions");
    reuseSimilar_=colorTolerance!=0;
    if(reuseSimilar_)for(size_t a=0;a<colors.size();++a)for(size_t b=0;b<colors.size();++b){
        bool similar=true;
        for(int channel=0;channel<3;++channel)
            similar&=unsigned(std::abs(int((colors[a]>>(channel*8))&255)-int((colors[b]>>(channel*8))&255)))<=colorTolerance;
        reusable_[a*256+b]=BYTE(similar);
    }
    std::array<WICColor,256> table{};table.fill(0xff000000);
    for(size_t i=0;i<colors.size();++i)table[i]=colors[i]|0xff000000;
    ComPtr<IWICPalette> palette;Check(factory->CreatePalette(&palette),"GIF palette");
    Check(palette->InitializeCustom(table.data(),UINT(table.size())),"GIF palette colors");
    Check(factory->CreateStream(&stream_),"GIF stream");
    Check(stream_->InitializeFromFilename(output.c_str(),GENERIC_WRITE),"GIF output file");
    Check(factory->CreateEncoder(GUID_ContainerFormatGif,nullptr,&encoder_),"GIF encoder");
    Check(encoder_->Initialize(stream_.Get(),WICBitmapEncoderNoCache),"GIF streaming output");
    Check(encoder_->SetPalette(palette.Get()),"GIF global palette");
    if(loop){
        ComPtr<IWICMetadataQueryWriter> meta;Check(encoder_->GetMetadataQueryWriter(&meta),"GIF loop metadata");
        BYTE app[]={'N','E','T','S','C','A','P','E','2','.','0'},data[]={3,1,0,0,0};
        PROPVARIANT value{};value.vt=VT_VECTOR|VT_UI1;value.caub={11,app};
        Check(meta->SetMetadataByName(L"/appext/application",&value),"GIF loop application");
        value.caub={5,data};Check(meta->SetMetadataByName(L"/appext/data",&value),"GIF repeat metadata");
    }
}
void GifFrames::Add(std::span<const BYTE> pixels,uint64_t duration){
    Canceled(stop_);
    if(pixels.size()!=size_t(width_)*height_||std::find(pixels.begin(),pixels.end(),BYTE(255))!=pixels.end())
        throw std::runtime_error("Invalid GIF indexed frame");
    if(!duration)return;
    if(reuseSimilar_&&!pending_.empty()){
        normalized_.assign(pixels.begin(),pixels.end());
        // Compare with the actual displayed/pending canvas, never the previous
        // unfiltered input. This bounds error on slow fades without drift.
        for(size_t i=0;i<normalized_.size();++i){
            if((i&65535)==0)Canceled(stop_);
            if(reusable_[size_t(pending_[i])*256+normalized_[i]])normalized_[i]=pending_[i];
        }
        pixels=normalized_;
    }
    if(!pending_.empty()&&!std::equal(pixels.begin(),pixels.end(),pending_.begin())){Flush();pending_.clear();}
    if(pending_.empty())pending_.assign(pixels.begin(),pixels.end());
    while(duration){
        Canceled(stop_);
        const auto part=unsigned(std::min<uint64_t>(duration,65535-delay_));
        delay_+=part;duration-=part;
        if(delay_==65535)Flush();
    }
}
void GifFrames::Flush(){
    if(!delay_)return;
    Canceled(stop_);
    int left=0,top=0,right=width_,bottom=height_;
    if(!previous_.empty()){
        left=width_;top=height_;right=bottom=0;
        for(int y=0;y<height_;++y){Canceled(stop_);for(int x=0;x<width_;++x){const auto p=size_t(y)*width_+x;
            if(pending_[p]!=previous_[p]){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}}}
        if(right<=left){left=top=0;right=bottom=1;}
    }
    const UINT width=UINT(right-left),height=UINT(bottom-top);
    std::vector<BYTE> patch(size_t(width)*height);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){const auto p=size_t(top+y)*width_+left+x;
        patch[size_t(y)*width+x]=!previous_.empty()&&pending_[p]==previous_[p]?255:pending_[p];}
    ComPtr<IWICBitmapFrameEncode> frame;Check(encoder_->CreateNewFrame(&frame,nullptr),"GIF frame");
    Check(frame->Initialize(nullptr),"GIF frame init");Check(frame->SetSize(width,height),"GIF delta size");
    auto format=GUID_WICPixelFormat8bppIndexed;Check(frame->SetPixelFormat(&format),"GIF indexed format");
    if(format!=GUID_WICPixelFormat8bppIndexed)throw std::runtime_error("Unsupported GIF pixel format");
    ComPtr<IWICMetadataQueryWriter> meta;Check(frame->GetMetadataQueryWriter(&meta),"GIF frame metadata");
    Number(meta.Get(),L"/imgdesc/Left",unsigned(left));Number(meta.Get(),L"/imgdesc/Top",unsigned(top));
    Number(meta.Get(),L"/grctlext/Delay",delay_);Number(meta.Get(),L"/grctlext/Disposal",1,VT_UI1);
    if(!previous_.empty()){
        PROPVARIANT transparent{};transparent.vt=VT_BOOL;transparent.boolVal=VARIANT_TRUE;
        Check(meta->SetMetadataByName(L"/grctlext/TransparencyFlag",&transparent),"GIF transparency");
        Number(meta.Get(),L"/grctlext/TransparentColorIndex",255,VT_UI1);
    }
    Check(frame->WritePixels(height,width,UINT(patch.size()),patch.data()),"Write GIF delta");
    Check(frame->Commit(),"Commit GIF frame");previous_=pending_;delay_=0;
}
void GifFrames::Finish(){
    Flush();Canceled(stop_);
    if(previous_.empty())throw std::runtime_error("The selected range has no frames");
    Check(encoder_->Commit(),"Finish GIF");encoder_.Reset();stream_.Reset();
}
}
