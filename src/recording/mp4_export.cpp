#include "recording/mp4_export.h"
#include "recording/encoder.h"
#include "recording/storage.h"
#include "recording/progress_file.h"
#include <bcrypt.h>
#include <mferror.h>
#include <fstream>
#include <array>
#include <cmath>
#include <future>
#include <thread>
namespace lumashot::recording {
namespace {
void Canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Export canceled");}
std::wstring Quote(const std::filesystem::path& p){return L"\""+std::filesystem::absolute(p).native()+L"\"";}
struct Digest {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};std::vector<BYTE> object;
    Digest(){
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Audio hash provider");
        DWORD size{},read{};
        if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<BYTE*>(&size),sizeof(size),&read,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;throw std::runtime_error("Audio hash size");}
        object.resize(size);
        if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;throw std::runtime_error("Audio hash");}
    }
    ~Digest(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    void Add(IMFSample* sample){ComPtr<IMFMediaBuffer> buffer;Check(sample->ConvertToContiguousBuffer(&buffer),"Audio bytes");BYTE* data{};DWORD size{};Check(buffer->Lock(&data,nullptr,&size),"Audio lock");const auto result=BCryptHashData(hash,data,size,0);buffer->Unlock();if(result<0)throw std::runtime_error("Hash audio packets");}
    std::array<BYTE,32> Finish(){std::array<BYTE,32> result{};if(BCryptFinishHash(hash,result.data(),ULONG(result.size()),0)<0)throw std::runtime_error("Audio digest");return result;}
};
struct Summary {
    UINT width{},height{},rateN{},rateD{},audioRate{},channels{};bool audio{};GUID codec{};
    std::vector<LONGLONG> videoTimes,audioTimes;std::array<BYTE,32> digest{};LONGLONG duration{};
};
Summary Inspect(const std::filesystem::path& path,std::stop_token stop,bool packets=true){
    Canceled(stop);ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader),"Inspect MP4");
    Summary s;DWORD video=DWORD(-1),audio=DWORD(-1);
    for(DWORD i=0;;++i){ComPtr<IMFMediaType> type;const auto result=reader->GetNativeMediaType(i,0,&type);if(result==MF_E_INVALIDSTREAMNUMBER)break;Check(result,"MP4 stream type");
        GUID major{},subtype{};Check(type->GetGUID(MF_MT_MAJOR_TYPE,&major),"MP4 major type");Check(type->GetGUID(MF_MT_SUBTYPE,&subtype),"MP4 codec");
        if(major==MFMediaType_Video&&video==DWORD(-1)&&(subtype==MFVideoFormat_H264||subtype==MFVideoFormat_AV1)){video=i;s.codec=subtype;Check(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&s.width,&s.height),"MP4 size");Check(MFGetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,&s.rateN,&s.rateD),"MP4 rate");}
        else if(major==MFMediaType_Audio&&audio==DWORD(-1)&&subtype==MFAudioFormat_AAC){audio=i;s.audio=true;Check(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&s.audioRate),"MP4 audio rate");Check(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&s.channels),"MP4 channels");}
        else throw std::runtime_error("Unsupported MP4 stream layout");
        if(i>=8)throw std::runtime_error("Excess MP4 streams");
    }
    if(video==DWORD(-1)||!s.width||!s.height||!s.rateN||!s.rateD)throw std::runtime_error("Missing MP4 video");
    PROPVARIANT value{};Check(reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE),MF_PD_DURATION,&value),"MP4 duration");s.duration=value.hVal.QuadPart;PropVariantClear(&value);
    if(!packets)return s;
    Digest digest;
    for(DWORD stream:{video,audio}){
        if(stream==DWORD(-1))continue;
        Check(reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS),FALSE),"Deselect MP4 streams");
        Check(reader->SetStreamSelection(stream,TRUE),"Select MP4 stream");PROPVARIANT seek{};seek.vt=VT_I8;seek.hVal.QuadPart=0;Check(reader->SetCurrentPosition(GUID_NULL,seek),"Inspect MP4 from start");
        for(;;){Canceled(stop);DWORD flags{};LONGLONG time{};ComPtr<IMFSample> sample;Check(reader->ReadSample(stream,0,nullptr,&flags,&time,&sample),"Inspect MP4 packet");
            if(sample){auto& times=stream==video?s.videoTimes:s.audioTimes;if(times.size()>=3000000)throw std::runtime_error("MP4 validation limit");times.push_back(time);if(stream==audio)digest.Add(sample.Get());}
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;
        }
    }
    if(s.videoTimes.empty()||(s.audio&&s.audioTimes.empty()))throw std::runtime_error("Empty MP4 stream");
    std::sort(s.videoTimes.begin(),s.videoTimes.end());s.digest=digest.Finish();return s;
}
bool Same(const Summary& a,const Summary& b){
    const auto times=[](const auto& x,const auto& y){if(x.size()!=y.size())return false;for(size_t i=0;i<x.size();++i)if(std::abs(x[i]-y[i])>10000)return false;return true;};
    // MF estimates nominal FPS differently for remuxed VFR files. Exact frame
    // counts and presentation timestamps below are the authoritative timeline.
    return a.width==b.width&&a.height==b.height&&std::abs(a.duration-b.duration)<=10000&&
        a.audio==b.audio&&a.audioRate==b.audioRate&&a.channels==b.channels&&a.digest==b.digest&&times(a.videoTimes,b.videoTimes)&&times(a.audioTimes,b.audioTimes);
}
struct Handles {HANDLE process{},thread{},job{};~Handles(){if(job)CloseHandle(job);if(thread)CloseHandle(thread);if(process)CloseHandle(process);}};
bool Run(const detail::Mp4OptimizerTool& tool,const std::wstring& arguments,const std::filesystem::path& folder,
    LONGLONG duration,int low,int high,std::stop_token stop,const std::function<void(int)>& progress,size_t frames=0){
    Canceled(stop);Handles h;h.job=CreateJobObjectW(nullptr,nullptr);if(!h.job)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.ProcessMemoryLimit=tool.memory_bytes;
    if(!SetInformationJobObject(h.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return false;
    const auto progressFile=folder/L"progress.txt";{std::error_code e;std::filesystem::remove(progressFile,e);}
    std::wstring command=Quote(tool.executable)+L" -hide_banner -nostdin -v error -nostats -y -progress progress.txt "+arguments;
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(tool.executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED|BELOW_NORMAL_PRIORITY_CLASS,nullptr,folder.c_str(),&si,&pi))return false;
    h.process=pi.hProcess;h.thread=pi.hThread;
    if(!AssignProcessToJobObject(h.job,h.process)){TerminateProcess(h.process,1);WaitForSingleObject(h.process,INFINITE);return false;}
    if(ResumeThread(h.thread)==DWORD(-1))return false;
    const auto start=GetTickCount64();auto lastRead=start;int last=low;progress(last);
    // ffmpeg -progress only appends; parse newly added bytes instead of
    // re-reading the whole file on every 250ms poll.
    ProgressTail progress_tail;
    for(;;){const auto wait=WaitForSingleObject(h.process,25);if(wait==WAIT_OBJECT_0)break;
        if(wait!=WAIT_TIMEOUT||stop.stop_requested()||GetTickCount64()-start>=tool.timeout_ms){TerminateJobObject(h.job,1);WaitForSingleObject(h.process,INFINITE);Canceled(stop);return false;}
        if(GetTickCount64()-lastRead>250){lastRead=GetTickCount64();
            // SSIM uses setpts=N in AVTB: its timestamps are frame indices in
            // microseconds, not clip time. Use completed frames for validation.
            if(const auto done=progress_tail.Poll(progressFile,frames?"frame=":"out_time_us=")){
                const double fraction=frames?double(*done)/double(frames):double(*done)*10/std::max(1LL,duration);
                const int value=low+int(std::clamp(fraction,0.,1.)*(high-low));if(value>last){last=value;progress(last);}
            }
        }
    }
    Canceled(stop);DWORD code{};const bool success=GetExitCodeProcess(h.process,&code)&&code==0;if(success)progress(high);return success;
}
// Keep the whole clip and 99% of frames close to the reference, while
// tolerating brief local SSIM dips. An absolute floor still rejects bad frames.
bool Quality(const std::filesystem::path& file,size_t expected){
    std::ifstream in(file);std::string line;size_t count=0,belowFloor=0;double sum=0;
    while(std::getline(in,line)){const auto pos=line.find("All:");if(pos==std::string::npos)return false;double value{};try{value=std::stod(line.substr(pos+4));}catch(...){return false;}
        if(!std::isfinite(value)||value<.96||value>1)return false;sum+=value;++count;if(value<.98)++belowFloor;
    }
    return count==expected&&count&&sum/double(count)>=.99&&belowFloor<=count/100;
}
}
Mp4ExportResult detail::ExportMp4WithTool(const std::filesystem::path& source,const std::filesystem::path& destination,
    std::stop_token stop,const std::function<void(int)>& progress,const Mp4OptimizerTool& tool,const Mp4StageCallback& stage){
    const auto notify=[&](Mp4Stage phase,int percent,int attempt=0){if(stage)stage({phase,percent,attempt});};
    Canceled(stop);notify(Mp4Stage::Inspect,-1);progress(0);Mp4ExportResult outcome;outcome.original_bytes=std::filesystem::file_size(source);
    const auto apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct ComCleanup{HRESULT hr;~ComCleanup(){if(SUCCEEDED(hr))CoUninitialize();}} com{apartment};
    const auto mf=MFStartup(MF_VERSION);struct MfCleanup{HRESULT hr;~MfCleanup(){if(SUCCEEDED(hr))MFShutdown();}} media{mf};
    std::unique_ptr<ExportDirectory> scratch;auto selected=source;
    try{
        if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)throw std::runtime_error("MP4 apartment");Check(mf,"MP4 inspection startup");
        if(std::filesystem::is_regular_file(tool.executable)){
            scratch=std::make_unique<ExportDirectory>(std::filesystem::absolute(source).parent_path());
            const auto metadata=Inspect(source,stop,false);progress(2);
            // Packet timestamps and the compressed audio digest are independent
            // of encoding. Join before validation, cleanup, or MFShutdown.
            std::promise<Summary> inspected;
            const auto inspection=inspected.get_future().share();
            std::jthread inspector([&](std::stop_token worker_stop){
                const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
                ComCleanup worker_com{hr};
                try{Check(hr,"MP4 inspection apartment");inspected.set_value(Inspect(source,worker_stop));}
                catch(...){inspected.set_exception(std::current_exception());}
            });
            std::stop_callback cancel_inspection(stop,[&]{inspector.request_stop();});
            // Faster AV1 first pass, a higher-quality retry, then compatible H.264.
            // Preview still uses the untouched recording, not an AV1 system decoder.
            // Bound parallelism, retain CRF and all whole-clip quality guards.
            // Retain the previous concurrency above 1080p: a larger pool can
            // exhaust the existing bounded-memory job before encoding starts.
            const DWORD thread_limit=uint64_t(metadata.width)*metadata.height>1920ull*1080?2:4;
            const auto parallelism=std::clamp(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),DWORD{1},thread_limit);
            const auto av1_parameters=L" -svtav1-params tune=0:lp="+std::to_wstring(parallelism)+L":lookahead=16";
            struct Attempt {Mp4Encoding encoding;std::wstring options;};
            const std::array<Attempt,3> attempts{{
                {Mp4Encoding::Av1,L"-c:v libsvtav1 -preset 8 -crf 36"+av1_parameters},
                {Mp4Encoding::Av1,L"-c:v libsvtav1 -preset 6 -crf 28"+av1_parameters},
                // No B-frames: reordering delay makes MP4 readers shift every video PTS by
                // two frames (~67 ms A/V offset), so Same() correctly rejected it.
                {Mp4Encoding::H264,L"-c:v libx264 -preset slow -crf 18 -bf 0 -threads 4"}}};
            int last=2;
            const auto report=[&](int value){if(value>last){last=value;progress(value);}};
            for(size_t i=tool.prefer_av1?0:2;i<attempts.size();++i){
                Canceled(stop);
                // Surface a failed source scan before starting another retry.
                if(inspection.wait_for(std::chrono::seconds(0))==std::future_status::ready)inspection.get();
                try{
                    const auto candidate=scratch->Path()/(L"candidate-"+std::to_wstring(i)+L".mp4");
                    const auto& attempt=attempts[i];
                    // passthrough alone quantizes VFR timestamps to 1/nominal-FPS.
                    // Keep the demuxer timebase as well; copy compressed AAC bytes.
                    const auto encode=L"-threads 4 -i "+Quote(source)+L" -map 0:v:0 -map 0:a:0? "+attempt.options+
                        L" -pix_fmt yuv420p -fps_mode passthrough -enc_time_base demux -c:a copy -movflags +faststart "+Quote(candidate);
                    // Legacy aggregate callback stays monotonic for existing callers.
                    // The UI uses explicit per-phase progress, without reserving
                    // two thirds of a progress bar for retries that may not run.
                    const int low=tool.prefer_av1?2+int(i)*31:2,high=tool.prefer_av1?low+31:97;
                    const int encoded=low+(high-low)*3/4;
                    const int attempt_number=int(i)+1;
                    const auto encoding_progress=[&](int value){notify(Mp4Stage::Encode,value,attempt_number);report(low+(encoded-low)*value/100);};
                    if(!Run(tool,encode,scratch->Path(),metadata.duration,0,100,stop,encoding_progress)||!std::filesystem::is_regular_file(candidate)||
                        std::filesystem::file_size(candidate)>=outcome.original_bytes)continue;
                    notify(Mp4Stage::Inspect,-1,attempt_number);
                    const auto& original=inspection.get();Canceled(stop);
                    const auto result=Inspect(candidate,stop);
                    const auto expected=attempt.encoding==Mp4Encoding::Av1?MFVideoFormat_AV1:MFVideoFormat_H264;
                    if(result.codec!=expected||!Same(original,result))continue;
                    std::error_code ignored;std::filesystem::remove(scratch->Path()/L"quality.txt",ignored);
                    // Compare corresponding decoded frames, never adjacent frames
                    // chosen by filter timestamp rounding. Same() validated timing.
                    const auto validate=L"-xerror -threads 4 -i "+Quote(source)+L" -threads 4 -i "+Quote(candidate)+
                        L" -filter_complex_threads 2 -lavfi \"[0:v]settb=AVTB,setpts=N[r];[1:v]settb=AVTB,setpts=N[d];[r][d]ssim=shortest=1:stats_file=quality.txt\" -an -fps_mode passthrough -f null -";
                    const auto quality_progress=[&](int value){notify(Mp4Stage::Verify,value,attempt_number);report(encoded+(high-encoded)*value/100);};
                    if(Run(tool,validate,scratch->Path(),original.duration,0,100,stop,quality_progress,original.videoTimes.size())&&Quality(scratch->Path()/L"quality.txt",original.videoTimes.size())){
                        selected=candidate;outcome.encoding=attempt.encoding;break;
                    }
                }catch(const std::exception&){Canceled(stop);}
            }
        }
    }catch(const std::exception&){Canceled(stop);}
    Canceled(stop);outcome.saved_bytes=std::filesystem::file_size(selected);notify(Mp4Stage::Save,-1);progress(99);SaveOutput(selected,destination,stop);progress(100);notify(Mp4Stage::Complete,100);return outcome;
}
Mp4ExportResult ExportMp4ToFile(const std::filesystem::path& source,const std::filesystem::path& destination,
    std::stop_token stop,const std::function<void(int)>& progress,const Mp4StageCallback& stage){
    wchar_t path[32768]{};const auto size=GetModuleFileNameW(nullptr,path,32768);if(!size||size>=32768)throw std::runtime_error("MP4 optimizer path");
    return detail::ExportMp4WithTool(source,destination,stop,progress,{std::filesystem::path(path).parent_path()/L"ffmpeg.exe"},stage);
}
}
