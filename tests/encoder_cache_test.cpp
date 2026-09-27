#include "recording/encoder.h"
#include <d3d10.h>
#include <iostream>
using namespace lumashot::recording;
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);int result=0;
    try{
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Device");
        ComPtr<ID3D10Multithread> guard;Check(context.As(&guard),"Multithread");guard->SetMultithreadProtected(TRUE);
        constexpr int width=128,height=64,frames=30;
        // Neutral levels isolate view/crop identity from decoder color-matrix defaults.
        const std::array<uint32_t,5> colors{0xff222222,0xff666666,0xffaaaaaa,0xffdddddd,0xff222222};
        std::array<ComPtr<ID3D11Texture2D>,2> textures;
        for(size_t i=0;i<textures.size();++i){
            std::vector<uint32_t> pixels(width*height);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels[size_t(y)*width+x]=colors[i*2+(x>=width/2?1:0)];
            D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
            D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};Check(device->CreateTexture2D(&d,&data,&textures[i]),"Texture");
        }
        for(bool software:{true,false}){
            const auto file=std::filesystem::current_path()/(software?L"encoder-cache-cpu.mp4":L"encoder-cache-gpu.mp4");
            {Encoder encoder(device.Get(),file,{width,height},{64,64},30,software);
                for(int i=0;i<frames;++i){const int phase=i/6;const int left=phase%2?64:0;
                    encoder.Frame(textures[phase==2||phase==3?1:0].Get(),{left,0,left+64,64},i*10000000LL/30,10000000LL/30);
                }encoder.Finish();}
            ComPtr<IMFAttributes> attributes;Check(MFCreateAttributes(&attributes,1),"Attributes");attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
            ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(file.c_str(),attributes.Get(),&reader),"Reader");
            ComPtr<IMFMediaType> type;Check(MFCreateMediaType(&type),"Type");type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
            Check(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get()),"RGB output");int count=0;
            for(;;){DWORD flags{};ComPtr<IMFSample> sample;Check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,nullptr,&sample),"Decode");
                if(sample){if(count>=frames)throw std::runtime_error("Extra frames");ComPtr<IMFMediaBuffer> buffer;Check(sample->ConvertToContiguousBuffer(&buffer),"Buffer");BYTE* data{};DWORD length{};Check(buffer->Lock(&data,nullptr,&length),"Lock");
                    // Every cropped frame is uniform, so the first pixel also avoids stride assumptions.
                    const uint32_t expected=colors[size_t(count/6)];bool good=length>=4;
                    if(good)for(int c=0;c<3;++c)good&=std::abs(int(data[c])-int((expected>>(c*8))&255))<20;
                    if(!good){std::cerr<<(software?"CPU":"GPU")<<" frame="<<count<<" bytes="<<length<<" expected RGB="<<((expected>>16)&255)<<","<<((expected>>8)&255)<<","<<(expected&255);if(length>=4)std::cerr<<" actual RGB="<<int(data[2])<<","<<int(data[1])<<","<<int(data[0]);std::cerr<<std::endl;}
                    buffer->Unlock();if(!good)throw std::runtime_error("Cached source/crop produced the wrong color");++count;
                }if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;
            }
            if(count!=frames)throw std::runtime_error("Missing frames");
            std::cout<<"PASS "<<(software?"CPU":"GPU")<<" repeated source, source replacement, changing crop, and returning to original source: "<<count<<" decoded frames\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;result=1;}
    MFShutdown();CoUninitialize();return result;
}
