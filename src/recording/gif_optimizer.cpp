#include "recording/gif_optimizer.h"
#include "recording/gif_frames.h"
#include "recording/storage.h"
#include <fstream>
#include <optional>
#include <array>

namespace lumashot::recording {
namespace {
void Canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Export canceled");}
struct Summary {
    unsigned width{},height{};uint64_t duration{};std::optional<unsigned> loop;
    bool operator==(const Summary&)const=default;
};
// Validate structure without retaining compressed frames or the whole file.
Summary Inspect(const std::filesystem::path& path,std::stop_token stop){
    std::ifstream in(path,std::ios::binary);
    const auto byte=[&](){const int b=in.get();if(b<0)throw std::runtime_error("Truncated GIF");return unsigned(b);};
    const auto word=[&](){const auto low=byte();return low+256*byte();};
    const auto skip=[&](unsigned n){while(n--)(void)byte();};
    const auto blocks=[&](){for(;;){Canceled(stop);const auto n=byte();if(!n)break;skip(n);}};
    std::array<char,6> header{};in.read(header.data(),header.size());
    if(header!=std::array<char,6>{'G','I','F','8','9','a'}&&header!=std::array<char,6>{'G','I','F','8','7','a'})
        throw std::runtime_error("Invalid GIF header");
    Summary result;result.width=word();result.height=word();
    if(!result.width||!result.height)throw std::runtime_error("Empty GIF canvas");
    const auto packed=byte();skip(2);if(packed&128)skip(3u<<(1+(packed&7)));
    unsigned frames=0,delay=0;
    for(;;){
        Canceled(stop);const auto tag=byte();
        if(tag==0x3b){if(!frames||in.peek()!=EOF)throw std::runtime_error("Invalid GIF trailer");break;}
        if(tag==0x21){
            const auto label=byte();
            if(label==0xf9){
                if(byte()!=4)throw std::runtime_error("Invalid GIF control");
                const auto control=byte();if(((control>>2)&7)>3)throw std::runtime_error("Invalid GIF disposal");
                delay=word();skip(1);if(byte()!=0)throw std::runtime_error("Invalid GIF control terminator");
            }else if(label==0xff){
                const auto n=byte();std::string app;for(unsigned i=0;i<n;++i)app.push_back(char(byte()));
                if(app=="NETSCAPE2.0"||app=="ANIMEXTS1.0"){
                    if(byte()!=3||byte()!=1)throw std::runtime_error("Invalid GIF loop");
                    const auto repeat=word();if(result.loop||byte()!=0)throw std::runtime_error("Invalid GIF loop extension");
                    result.loop=repeat;
                }else blocks();
            }else if(label==0xfe)blocks();
            else throw std::runtime_error("Unsupported GIF extension");
        }else if(tag==0x2c){
            const auto x=word(),y=word(),w=word(),h=word();
            if(!w||!h||x+w>result.width||y+h>result.height)throw std::runtime_error("Invalid GIF rectangle");
            const auto flags=byte();if(flags&128)skip(3u<<(1+(flags&7)));
            const auto code=byte();if(code<2||code>8)throw std::runtime_error("Invalid GIF LZW size");
            blocks();result.duration+=delay;delay=0;++frames;
        }else throw std::runtime_error("Invalid GIF block");
    }
    // Ask WIC to actually decode every image, not just trust its metadata.
    ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Validate GIF factory");
    ComPtr<IWICBitmapDecoder> decoder;Check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),"Validate GIF decoder");
    UINT count{};Check(decoder->GetFrameCount(&count),"Validate GIF frame count");
    if(count!=frames)throw std::runtime_error("GIF frame count mismatch");
    for(UINT i=0;i<count;++i){
        Canceled(stop);ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(i,&frame),"Validate GIF frame");
        UINT w{},h{};Check(frame->GetSize(&w,&h),"Validate GIF size");
        std::vector<BYTE> row(size_t(w)*32);
        for(UINT y=0;y<h;y+=32){Canceled(stop);WICRect rect{0,int(y),int(w),int(std::min(32u,h-y))};
            Check(frame->CopyPixels(&rect,w,UINT(row.size()),row.data()),"Validate GIF pixels");}
    }
    return result;
}
struct Handles {
    HANDLE process{},thread{},job{};
    ~Handles(){if(job)CloseHandle(job);if(thread)CloseHandle(thread);if(process)CloseHandle(process);}
};
bool Run(const detail::GifOptimizerTool& tool,const std::filesystem::path& source,
    const std::filesystem::path& output,std::stop_token stop){
    Canceled(stop);Handles h;h.job=CreateJobObjectW(nullptr,nullptr);if(!h.job)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit=tool.memory_bytes;
    if(!SetInformationJobObject(h.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return false;
    std::wstring command=L"\""+tool.executable.native()+L"\" --conserve-memory -O3 "+
        L"\""+source.native()+L"\" -o \""+output.native()+L"\"";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(tool.executable.c_str(),command.data(),nullptr,nullptr,FALSE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|BELOW_NORMAL_PRIORITY_CLASS,nullptr,tool.executable.parent_path().c_str(),&si,&pi))return false;
    h.process=pi.hProcess;h.thread=pi.hThread;
    if(!AssignProcessToJobObject(h.job,h.process)){TerminateProcess(h.process,1);WaitForSingleObject(h.process,INFINITE);return false;}
    if(ResumeThread(h.thread)==DWORD(-1))return false;
    const auto start=GetTickCount64();
    for(;;){
        const auto wait=WaitForSingleObject(h.process,25);
        if(wait==WAIT_OBJECT_0)break;
        if(wait!=WAIT_TIMEOUT||stop.stop_requested()||GetTickCount64()-start>=tool.timeout_ms){
            TerminateJobObject(h.job,1);WaitForSingleObject(h.process,INFINITE);Canceled(stop);return false;
        }
    }
    Canceled(stop);DWORD code{};return GetExitCodeProcess(h.process,&code)&&code==0;
}
}
void detail::OptimizeGifWithTool(const std::filesystem::path& file,bool loop,std::stop_token stop,
    const std::function<void(int)>& progress,const GifOptimizerTool& tool){
    Canceled(stop);progress(0);
    // The public entry may be called after ExportGif has closed its COM apartment.
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Apartment {HRESULT result;~Apartment(){if(SUCCEEDED(result))CoUninitialize();}} cleanup{apartment};
    if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)return;
    try{
        if(!std::filesystem::is_regular_file(tool.executable)){progress(100);return;}
        const auto original=Inspect(file,stop);
        if(original.loop!=(loop?std::optional<unsigned>(0):std::nullopt)){progress(100);return;}
        ExportDirectory directory(file.parent_path());
        auto best=file;auto bytes=std::filesystem::file_size(file);
        {
            Canceled(stop);const auto candidate=directory.Path()/L"lossless.gif";
            try{
                if(Run(tool,file,candidate,stop)&&std::filesystem::file_size(candidate)<bytes&&Inspect(candidate,stop)==original){
                    best=candidate;bytes=std::filesystem::file_size(candidate);
                }
            }catch(const std::exception&){Canceled(stop);}
            progress(90);
        }
        Canceled(stop);
        if(best!=file)SaveOutput(best,file,stop);
    }catch(const std::exception&){Canceled(stop);}
    progress(100);
}
void OptimizeGif(const std::filesystem::path& file,bool loop,std::stop_token stop,const std::function<void(int)>& progress){
    wchar_t module[32768]{};const auto length=GetModuleFileNameW(nullptr,module,32768);
    if(!length||length>=32768){Canceled(stop);return;}
    detail::OptimizeGifWithTool(file,loop,stop,progress,{std::filesystem::path(module).parent_path()/L"gifsicle.exe"});
}
}
