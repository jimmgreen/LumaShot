#include "recording/panel.h"
#include "recording/encoder.h"
#include "recording/pixels.h"
#include <wincodec.h>
namespace lumashot::recording {
PreviewImages ReadPreview(const std::filesystem::path& path,long long duration,std::stop_token stop,const PreviewOptions& options){
    if(stop.stop_requested())return {};
    Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Preview apartment");struct Cleanup{~Cleanup(){MFShutdown();CoUninitialize();}} cleanup;Check(MFStartup(MF_VERSION),"Preview startup");
    ComPtr<IMFAttributes> attributes;MFCreateAttributes(&attributes,1);attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE);ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(path.c_str(),attributes.Get(),&reader),"Read preview");reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS),FALSE);reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),TRUE);
    ComPtr<IMFMediaType> native;Check(reader->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&native),"Preview visible type");UINT sourceWidth{},sourceHeight{};Check(MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&sourceWidth,&sourceHeight),"Preview visible size");
    const auto previewSize=OutputSize(sourceWidth,sourceHeight,608);UINT width=UINT(previewSize.cx),height=UINT(previewSize.cy);
    ComPtr<IMFMediaType> type;MFCreateMediaType(&type);type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
    Check(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,width,height),"Preview decode size");
    if(FAILED(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get()))){
        type->DeleteItem(MF_MT_FRAME_SIZE);Check(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get()),"Preview pixels");width=sourceWidth;height=sourceHeight;
    }
    ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Preview scaler");PreviewImages result;
    // Reuse nearby samples; seek sparse targets instead of decoding seconds of 4K/60 video.
    // Always publish the poster before doing any optional timeline work.
    ComPtr<IMFSample> sample;LONGLONG timestamp=-1;
    for(int i=0;i<(options.thumbnails?9:1)&&!stop.stop_requested();++i){const long long position=i?std::max(0LL,duration-1000000)*(i-1)/8:0;
        if(!sample||position-timestamp>20000000){PROPVARIANT seek{};seek.vt=VT_I8;seek.hVal.QuadPart=position;Check(reader->SetCurrentPosition(GUID_NULL,seek),"Seek thumbnail");sample.Reset();}
        const auto deadline=GetTickCount64()+30000;
        while((!sample||timestamp<position)&&!stop.stop_requested()){
            if(GetTickCount64()>deadline)throw std::runtime_error("Thumbnail decode timed out");
            DWORD flags{};sample.Reset();Check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,&timestamp,&sample),"Read thumbnail frame");
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;
        }
        if(stop.stop_requested())return {};
        if(!sample||timestamp<position)throw std::runtime_error("Incomplete preview timeline");
        std::vector<BYTE> normalized;CopyDecodedRgb32(reader.Get(),sample.Get(),width,height,normalized);
        ComPtr<IWICBitmap> bitmap;factory->CreateBitmapFromMemory(width,height,GUID_WICPixelFormat32bppBGR,width*4,UINT(normalized.size()),normalized.data(),&bitmap);ComPtr<IWICBitmapScaler> scaler;factory->CreateBitmapScaler(&scaler);const auto size=OutputSize(width,height,i?96:608);Check(scaler->Initialize(bitmap.Get(),size.cx,size.cy,WICBitmapInterpolationModeFant),"Scale thumbnail");PreviewPixels image;image.width=size.cx;image.height=size.cy;image.pixels.resize(size_t(size.cx)*size.cy);Check(scaler->CopyPixels(nullptr,size.cx*4,UINT(image.pixels.size()*4),reinterpret_cast<BYTE*>(image.pixels.data())),"Thumbnail output");if(!i)result.poster=std::move(image);else result.thumbnails.push_back(std::move(image));
        if(options.publish&&!stop.stop_requested())options.publish(result);
    }return result;
}
}

