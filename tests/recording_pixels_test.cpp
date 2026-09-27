#include "recording/pixels.h"
#include "recording/panel.h"
#include <iostream>
#include <d3d9types.h>
using namespace lumashot::recording;
int main(){CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);int failures{};try{for(const UINT width:{1918u,3838u,3840u})for(BOOL bottom:{FALSE,TRUE}){const UINT height=2160;ComPtr<IMFMediaBuffer> buffer;Check(MFCreate2DMediaBuffer(width,height,D3DFMT_X8R8G8B8,bottom,&buffer),"Padded fixture");ComPtr<IMF2DBuffer> two;buffer.As(&two);BYTE* top{};LONG pitch{};two->Lock2D(&top,&pitch);for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){auto* p=top+ptrdiff_t(y)*pitch+x*4;p[0]=BYTE(x);p[1]=BYTE(y);p[2]=BYTE(x^y);p[3]=255;}two->Unlock2D();ComPtr<IMFSample> sample;MFCreateSample(&sample);sample->AddBuffer(buffer.Get());std::vector<BYTE> pixels;CopyRgb32(sample.Get(),width,height,LONG(width*4),pixels);bool okay=true;for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){const auto* p=pixels.data()+(size_t(y)*width+x)*4;if(p[0]!=BYTE(x)||p[1]!=BYTE(y)||p[2]!=BYTE(x^y))okay=false;}std::cout<<(okay?"PASS ":"FAIL ")<<width<<" x "<<height<<" signed pitch "<<pitch<<std::endl;failures+=!okay;}
// Linear buffers have no IMF2DBuffer interface: use the current negotiated
// storage stride and storage height even when cropping a bottom-up image.
for(bool bottom:{false,true}){
    constexpr UINT width=16,height=10,pitch=80;ComPtr<IMFMediaBuffer> buffer;Check(MFCreateMemoryBuffer(pitch*height,&buffer),"Linear fixture");
    BYTE* data{};Check(buffer->Lock(&data,nullptr,nullptr),"Linear fixture lock");memset(data,0,pitch*height);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){auto* pixel=data+size_t(bottom?height-1-y:y)*pitch+x*4;pixel[0]=BYTE(x);pixel[1]=BYTE(y);pixel[2]=BYTE(x^y);pixel[3]=255;}
    buffer->Unlock();buffer->SetCurrentLength(pitch*height);ComPtr<IMFSample> sample;MFCreateSample(&sample);sample->AddBuffer(buffer.Get());
    std::vector<BYTE> pixels;CopyRgb32Region(sample.Get(),width,height,bottom?-LONG(pitch):LONG(pitch),{1,2,13,9},pixels);
    bool good=pixels.size()==12*7*4;for(UINT y=0;y<7;++y)for(UINT x=0;x<12;++x){const auto* pixel=pixels.data()+(size_t(y)*12+x)*4;good&=pixel[0]==x+1&&pixel[1]==y+2&&pixel[2]==((x+1)^(y+2));}
    std::cout<<(good?"PASS ":"FAIL ")<<"linear padded crop with signed stride "<<(bottom?-LONG(pitch):LONG(pitch))<<std::endl;failures+=!good;
    bool rejected=false;buffer->SetCurrentLength(32);try{CopyRgb32Region(sample.Get(),width,height,LONG(pitch),{0,0,16,10},pixels);}catch(const std::exception&){rejected=true;}
    std::cout<<(rejected?"PASS ":"FAIL ")<<"truncated linear buffer rejected"<<std::endl;failures+=!rejected;
}
const auto fixture=std::filesystem::current_path()/L"gif-4k-fixture"/L"motion.mp4";if(std::filesystem::exists(fixture)){const auto preview=ReadPreview(fixture,100000000,{});const auto& image=preview.poster;double error=0;size_t count=0;for(int y=2;y<image.height-2;y+=5)for(int x=2;x<image.width-2;x+=5){const double sx=double(x)*3840/image.width,sy=double(y)*2160/image.height;const auto pixel=image.pixels[size_t(y)*image.width+x];error+=std::abs(int((pixel>>16)&255)-(50+sx*170/3840))+std::abs(int((pixel>>8)&255)-(40+sy*170/2160))+std::abs(int(pixel&255)-(30+(sx+sy)*120/6000));count+=3;}const bool okay=count&&error/count<12;std::cout<<(okay?"PASS ":"FAIL ")<<"actual 4K MP4 preview row geometry, mean channel error "<<(count?error/count:999)<<std::endl;failures+=!okay;}
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}MFShutdown();CoUninitialize();return failures?1:0;}


