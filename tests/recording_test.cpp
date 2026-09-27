#include "recording/core.h"
#include "recording/panel.h"
#include "recording/encoder.h"
#include "recording/quality.h"
#include "app/hotkeys.h"
#include "recording/storage.h"
#include <fstream>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <vector>
using namespace lumashot::recording;
namespace {int failures{};void Expect(bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;failures+=!ok;}
Status Record(const std::filesystem::path& output,bool software,int quality=1,SIZE syntheticSize={640,360},int outputWidth=320,int frameCount=45){std::mutex mutex;std::condition_variable event;Status final;bool done=false;Session session;Options options;options.synthetic=true;options.synthetic_frames=frameCount;options.synthetic_size=syntheticSize;options.software=software;options.quality=quality;options.width=outputWidth;options.system_audio=false;session.Start(options,output,[&](Status s){if(s.state==State::Failed||s.state==State::Preview){std::lock_guard lock(mutex);final=std::move(s);done=true;event.notify_one();}});std::unique_lock lock(mutex);if(!event.wait_for(lock,std::chrono::seconds(20),[&]{return done;})){session.Stop();throw std::runtime_error("Recording timeout");}return final;}
void VerifyVideoPreset(const std::filesystem::path& file){
    ComPtr<IMFAttributes> attributes;Check(MFCreateAttributes(&attributes,1),"Preset reader attributes");
    Check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE),"Preset video processing");
    ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(file.c_str(),attributes.Get(),&reader),"Open preset video");
    ComPtr<IMFMediaType> type;Check(MFCreateMediaType(&type),"Preset media type");
    Check(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"Preset video type");Check(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32),"Preset RGB type");
    Check(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get()),"Decode preset video");
    ComPtr<IMFMediaType> decoded;Check(reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),&decoded),"Preset decoded format");
    UINT width{},height{};Check(MFGetAttributeSize(decoded.Get(),MF_MT_FRAME_SIZE,&width,&height),"Preset decoded dimensions");
    Expect(width==320&&height==180,"video quality preset preserves output dimensions");
    unsigned frames=0;LONGLONG previous=-1;bool increasing=true,complete=true;
    for(;;){DWORD flags{};LONGLONG time{};ComPtr<IMFSample> sample;
        Check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,&time,&sample),"Read preset decoded sample");
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;if(!sample)continue;
        increasing=increasing&&time>previous;previous=time;
        ComPtr<IMFMediaBuffer> buffer;Check(sample->ConvertToContiguousBuffer(&buffer),"Preset decoded pixels");
        DWORD bytes{};Check(buffer->GetCurrentLength(&bytes),"Preset decoded pixel count");complete=complete&&bytes>=width*height*4;++frames;
    }
    Expect(frames==45&&increasing&&complete,"video quality preset decodes every frame with complete pixels and increasing timestamps");
}
void VerifyGifPreset(const std::filesystem::path& source,const std::filesystem::path& file,int quality,UINT expectedWidth=320,UINT expectedHeight=180){
    const auto preset=GifQualityPreset(quality);GifOptions options;options.width=preset.width;options.fps=preset.fps;options.begin=2000000;options.end=12000000;
    int progress=0;ExportGif(source,file,options,{},[&](int value){progress=value;});Expect(progress==100,"GIF quality preset export completes");
    ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Preset GIF factory");
    ComPtr<IWICBitmapDecoder> decoder;Check(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Decode preset GIF");
    UINT count{};Check(decoder->GetFrameCount(&count),"Preset GIF frame count");
    const unsigned expected=quality==0?10u:quality==1?15u:25u;
    // Identical quantized frames merge their delays; stored frame count is not
    // the sampling rate. At 25fps each unmerged tick is exactly 4 centiseconds.
    if(quality!=2)Expect(count>=expected-1&&count<=expected+1,"GIF quality preset uses expected frame rate");
    unsigned duration=0,singleTicks=0,mergedTicks=0;bool dimensions=true,timeGrid=true;
    for(UINT i=0;i<count;++i){ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(i,&frame),"Preset GIF frame");
        UINT width{},height{};Check(frame->GetSize(&width,&height),"Preset GIF frame dimensions");
        // The first frame covers the canvas; later frames may be delta rectangles.
        dimensions=dimensions&&width>0&&height>0&&width<=expectedWidth&&height<=expectedHeight&&(!i?(width==expectedWidth&&height==expectedHeight):true);
        ComPtr<IWICFormatConverter> converter;Check(factory->CreateFormatConverter(&converter),"Preset GIF pixel converter");
        Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Preset GIF pixel format");
        std::vector<BYTE> pixels(size_t(width)*height*4);Check(converter->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()),"Decode preset GIF pixels");
        ComPtr<IWICMetadataQueryReader> metadata;Check(frame->GetMetadataQueryReader(&metadata),"Preset GIF metadata");
        PROPVARIANT delay{};Check(metadata->GetMetadataByName(L"/grctlext/Delay",&delay),"Preset GIF delay");
        const unsigned centiseconds=delay.uiVal;duration+=centiseconds;
        if(quality==2){timeGrid=timeGrid&&delay.vt==VT_UI2&&centiseconds>0&&centiseconds%4==0;singleTicks+=centiseconds==4;mergedTicks+=centiseconds/4;}
        PropVariantClear(&delay);
    }
    Expect(dimensions,"GIF quality preset matches expected canvas and valid delta frames");
    Expect(duration>=90&&duration<=110,"GIF quality preset preserves one-second trim duration");
    if(quality==2){
        std::cout<<"25fps GIF: "<<count<<" stored frames, "<<singleTicks<<" single ticks, "<<mergedTicks<<" total ticks, "<<duration<<" centiseconds"<<std::endl;
        Expect(timeGrid&&singleTicks>0&&mergedTicks==25&&duration==100&&count<=25,
            "25fps GIF delays preserve the 4-centisecond grid and all 25 ticks after frame merging");
    }
}
}
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);
    try{
        Expect(OutputSize(641,359,320).cx==320&&OutputSize(641,359,320).cy%2==0,"output size is bounded and NV12 aligned");
        lumashot::Preferences preferences;Expect(lumashot::UniqueShortcuts(preferences),"default shortcuts distinct");preferences.gif_key=preferences.key;Expect(!lumashot::UniqueShortcuts(preferences),"duplicate GIF binding rejected");preferences.gif_key=0;Expect(lumashot::UniqueShortcuts(preferences),"disabled shortcut does not collide");
        Clock clock;Sleep(30);clock.Pause(true);const auto paused=clock.Now();Sleep(40);Expect(clock.Now()==paused,"pause freezes the media clock");
        // Sleep can overshoot under load: validate against measured elapsed time,
        // not a 35 ms scheduling deadline for a requested 20 ms sleep.
        using Time=std::chrono::steady_clock;
        const auto resumeBefore=Time::now();clock.Pause(false);const auto resumeAfter=Time::now();
        Sleep(20);const auto sampleBefore=Time::now();const auto advanced=clock.Now()-paused;const auto sampleAfter=Time::now();
        const auto low=std::chrono::duration_cast<std::chrono::nanoseconds>(sampleBefore-resumeAfter).count()/100;
        const auto high=std::chrono::duration_cast<std::chrono::nanoseconds>(sampleAfter-resumeBefore).count()/100;
        Expect(advanced>0&&advanced>=low-1&&advanced<=high+1,"resume measures actual elapsed time while excluding the paused interval");
        const auto directory=std::filesystem::current_path()/L"recording-test-output";std::filesystem::create_directories(directory);const auto file=directory/L"synthetic.mp4";
        const auto source=directory/L"save-source.txt",destination=directory/L"save-destination.txt";{std::ofstream(source)<<"new recording";std::ofstream(destination)<<"original";}
        std::stop_source cancellation;cancellation.request_stop();bool canceled=false;try{SaveOutput(source,destination,cancellation.get_token());}catch(...){canceled=true;}std::string contents;{std::ifstream input(destination);std::getline(input,contents);}Expect(canceled&&contents=="original","cancelled save preserves preexisting destination");SaveOutput(source,destination,{});{std::ifstream input(destination);std::getline(input,contents);}Expect(contents=="new recording","completed save commits new contents");
        const auto result=Record(file,true);if(result.state!=State::Preview)std::wcerr<<result.detail<<std::endl;Expect(result.state==State::Preview&&result.frames==45,"synthetic GPU video encodes and finalizes");
        if(result.state==State::Preview){
            ComPtr<IMFAttributes> attrs;MFCreateAttributes(&attrs,1);attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(file.c_str(),attrs.Get(),&reader),"Read test output");ComPtr<IMFMediaType> type;MFCreateMediaType(&type);type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);Check(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get()),"Decode test RGB");
            unsigned frames=0;BYTE first=0,last=0;LONGLONG previous=-1;
            for(;;){DWORD flags{};LONGLONG time{};ComPtr<IMFSample> sample;Check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,&time,&sample),"Decode output sample");if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;if(!sample)continue;Expect(time>previous,"decoded timestamps increase");previous=time;ComPtr<IMFMediaBuffer> buffer;sample->ConvertToContiguousBuffer(&buffer);BYTE* pixels{};buffer->Lock(&pixels,nullptr,nullptr);if(!frames)first=pixels[2];last=pixels[2];buffer->Unlock();++frames;}
            Expect(frames==45&&first!=last,"decoded video contains changing synthetic pixels");reader.Reset();
            std::atomic<int> progress{};GifOptions gif;gif.width=160;gif.fps=15;gif.begin=2000000;gif.end=12000000;const auto image=directory/L"synthetic.gif";std::jthread conversion([&](std::stop_token stop){try{ExportGif(file,image,gif,stop,[&](int n){progress=n;});}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;}});conversion.join();Expect(progress==100,"GIF export finishes");
            if(progress==100){ComPtr<IWICImagingFactory> factory;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));ComPtr<IWICBitmapDecoder> decoder;Check(factory->CreateDecoderFromFilename(image.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Decode GIF");UINT canvasWidth{},canvasHeight{};ComPtr<IWICBitmapFrameDecode> canvasFrame;decoder->GetFrame(0,&canvasFrame);canvasFrame->GetSize(&canvasWidth,&canvasHeight);Expect(canvasWidth==160&&canvasHeight==90,"GIF logical canvas matches requested scale");UINT count{};decoder->GetFrameCount(&count);Expect(count>=14&&count<=16,"GIF crop and frame rate produce expected frame count");unsigned duration=0;for(UINT i=0;i<count;++i){ComPtr<IWICBitmapFrameDecode> frame;decoder->GetFrame(i,&frame);UINT w{},h{};frame->GetSize(&w,&h);Expect(w>0&&h>0&&w<=160&&h<=90,"GIF delta dimensions fit requested canvas");ComPtr<IWICMetadataQueryReader> metadata;frame->GetMetadataQueryReader(&metadata);PROPVARIANT delay{};Check(metadata->GetMetadataByName(L"/grctlext/Delay",&delay),"GIF frame delay");duration+=delay.uiVal;PropVariantClear(&delay);}Expect(duration>=93&&duration<=106,"GIF fractional delays preserve duration");}
        }
        {
            unsigned publications=0;bool posterFirst=false;
            PreviewOptions options;options.publish=[&](const PreviewImages& p){if(!publications)posterFirst=!p.poster.pixels.empty()&&p.thumbnails.empty();++publications;};
            const auto preview=ReadPreview(file,15000000,{},options);
            Expect(posterFirst&&publications==9&&preview.thumbnails.size()==8,"preview publishes poster before eight progressive thumbnails");
            publications=0;options.thumbnails=false;
            const auto poster=ReadPreview(file,15000000,{},options);
            Expect(publications==1&&!poster.poster.pixels.empty()&&poster.thumbnails.empty(),"MP4 poster-only preview skips timeline decoding");
            std::stop_source stop;publications=0;options.thumbnails=true;
            options.publish=[&](const PreviewImages&){++publications;stop.request_stop();};
            ReadPreview(file,15000000,stop.get_token(),options);
            Expect(publications==1,"cancel after poster prevents subsequent thumbnail publication");
            ReadPreview(file,15000000,stop.get_token(),options);
            Expect(publications==1,"pre-canceled preview does not publish");
        }
        for(int quality=0;quality<3;++quality){
            std::cout<<"Quality preset "<<quality<<std::endl;
            const auto video=directory/(L"quality-"+std::to_wstring(quality)+L".mp4");const auto recorded=Record(video,true,quality);
            if(recorded.state!=State::Preview)std::wcerr<<recorded.detail<<std::endl;
            Expect(recorded.state==State::Preview&&recorded.frames==45,"software video quality preset encodes and finalizes");
            if(recorded.state==State::Preview)VerifyVideoPreset(video);
            if(result.state==State::Preview)VerifyGifPreset(file,directory/(L"quality-"+std::to_wstring(quality)+L".gif"),quality);
        }
        const auto largeSource=directory/L"large-synthetic.mp4";
        const auto large=Record(largeSource,true,1,SIZE{1280,720},0,39);
        if(large.state!=State::Preview)std::wcerr<<large.detail<<std::endl;
        Expect(large.state==State::Preview&&large.frames==39,"large synthetic GIF source encodes and finalizes");
        if(large.state==State::Preview){
            constexpr UINT widths[]{640,960,1280},heights[]{360,540,720};
            for(int quality=0;quality<3;++quality){
                std::cout<<"Large GIF quality preset "<<quality<<std::endl;
                VerifyGifPreset(largeSource,directory/(L"large-quality-"+std::to_wstring(quality)+L".gif"),quality,widths[quality],heights[quality]);
            }
        }
        const auto hardware=Record(directory/L"hardware.mp4",false);if(hardware.state==State::Preview)Expect(hardware.frames==45,"verified hardware path encodes frames");else std::wcout<<L"SKIP hardware encoder unavailable on runner: "<<hardware.detail<<std::endl;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}MFShutdown();CoUninitialize();return failures?1:0;
}
