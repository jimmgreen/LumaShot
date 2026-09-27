#include "recording/encoder.h"
#include "recording/pixels.h"
#include "recording/gif_frames.h"
#include "recording/gif_quantizer.h"
#include "recording/gif_palette.h"


#include <wincodec.h>
#include <propvarutil.h>
#include <vector>
namespace lumashot::recording {
void ExportGif(const std::filesystem::path& source,const std::filesystem::path& output,const GifOptions& options,std::stop_token stop,const std::function<void(int)>& progress){
    if(options.fps<1||options.fps>50||options.width<0||options.width==1||options.begin<0||options.end<=options.begin)throw std::runtime_error("Invalid GIF export range");
    Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"GIF apartment");struct Cleanup{~Cleanup(){MFShutdown();CoUninitialize();}} cleanup;Check(MFStartup(MF_VERSION),"GIF decoder startup");
    const auto canceled=[&]{if(stop.stop_requested())throw std::runtime_error("Export canceled");};
    ComPtr<IMFAttributes> attributes;Check(MFCreateAttributes(&attributes,1),"GIF attributes");attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
    ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(source.c_str(),attributes.Get(),&reader),"Open recorded video");reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS),FALSE);reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),TRUE);
    ComPtr<IMFMediaType> native;Check(reader->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&native),"GIF source size");UINT w{},h{};Check(MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&w,&h),"Video dimensions");const auto size=OutputSize(w,h,options.width);
    ComPtr<IMFMediaType> type;MFCreateMediaType(&type);type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);Check(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get()),"GIF decode");
    ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"GIF imaging factory");
    std::vector<BYTE> normalized(size_t(w)*h*4);
    const auto pixels=[&](IMFSample* sample,int width,int height){
        CopyDecodedRgb32(reader.Get(),sample,w,h,normalized);
        ComPtr<IWICBitmap> bitmap;Check(factory->CreateBitmapFromMemory(w,h,GUID_WICPixelFormat32bppBGR,w*4,UINT(normalized.size()),normalized.data(),&bitmap),"GIF source bitmap");
        ComPtr<IWICBitmapScaler> scaler;Check(factory->CreateBitmapScaler(&scaler),"GIF scaler");Check(scaler->Initialize(bitmap.Get(),width,height,WICBitmapInterpolationModeFant),"GIF scale");return scaler;
    };
    const auto seek=[&](long long time){PROPVARIANT value{};value.vt=VT_I8;value.hVal.QuadPart=time;Check(reader->SetCurrentPosition(GUID_NULL,value),"Seek GIF source");};
    // Sample original pixel colors across time and spatial strata (384 KiB maximum).
    // One content-adaptive palette prevents palette flicker and permits exact
    // index comparisons for frame coalescing and delta rectangles.
    constexpr UINT sampleW=128,sampleH=32,sampleCount=24;
    std::vector<BYTE> contact;contact.reserve(sampleW*sampleH*sampleCount*4);
    for(UINT i=0;i<sampleCount;++i){canceled();const auto desired=options.begin+(options.end-options.begin)*i/sampleCount;seek(desired);
        for(;;){canceled();DWORD flags{};LONGLONG time{};ComPtr<IMFSample> sample;Check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,&time,&sample),"Sample GIF colors");if(flags&MF_SOURCE_READERF_ENDOFSTREAM||time>=options.end)break;if(!sample||time<desired)continue;CopyDecodedRgb32(reader.Get(),sample.Get(),w,h,normalized);
            for(UINT sy=0;sy<sampleH;++sy)for(UINT sx=0;sx<sampleW;++sx){
                const UINT x=UINT((uint64_t(sx)*w+(uint64_t(i)*37+sy*13)%w)/sampleW);
                const UINT y=UINT((uint64_t(sy)*h+(uint64_t(i)*19+sx*7)%h)/sampleH);
                const auto pos=(size_t(y)*w+x)*4;contact.insert(contact.end(),normalized.data()+pos,normalized.data()+pos+4);
            }break;}progress(int((i+1)*15/sampleCount));
    }
    if(contact.empty())throw std::runtime_error("The selected range has no frames");
    const auto colors=GifPalette(contact);contact.clear();contact.shrink_to_fit();canceled();
    GifQuantizer quantizer{std::span<const uint32_t>(colors)};
    GifFrames frames(factory.Get(),output,size.cx,size.cy,std::span<const uint32_t>(colors),options.loop,stop);
    std::vector<BYTE> indices(size_t(size.cx)*size.cy),scaled;
    const bool originalSize=w==UINT(size.cx)&&h==UINT(size.cy);
    // Original-size export can reuse the sampling buffer directly.
    if(originalSize)scaled=std::move(normalized);else scaled.resize(indices.size()*4);
    long long ticks{};
    seek(options.begin);long long next=options.begin;
    for(;;){canceled();DWORD flags{};LONGLONG time{};ComPtr<IMFSample> sample;Check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,&time,&sample),"Decode GIF frame");if(flags&MF_SOURCE_READERF_ENDOFSTREAM||time>=options.end)break;if(!sample||time<next)continue;
        if(originalSize)CopyDecodedRgb32(reader.Get(),sample.Get(),w,h,scaled);
        else{
            auto scaler=pixels(sample.Get(),size.cx,size.cy);
            Check(scaler->CopyPixels(nullptr,UINT(size.cx)*4,UINT(scaled.size()),scaled.data()),"GIF scaled pixels");
        }
        quantizer.Map(scaled.data(),size.cx,size.cy,indices.data());
        const unsigned duration=unsigned(((ticks+1)*100/options.fps)-(ticks*100/options.fps));
        frames.Add(indices,duration);++ticks;next=options.begin+ticks*10000000/options.fps;
        progress(15+int(std::min(84LL,(time-options.begin)*84/(options.end-options.begin))));
    }
    frames.Finish();progress(100);
}
}







