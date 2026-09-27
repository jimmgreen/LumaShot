#include "capture/elements_protocol.h"
#include <algorithm>
#include <dwmapi.h>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

namespace lumashot {
std::optional<RECT> HitTestElements(POINT p, std::span<const ElementWindow> windows, std::span<const ElementRegion> regions) {
    for(size_t i=0;i<windows.size();++i) {
        if(!PtInRect(&windows[i].bounds,p)) continue;
        std::optional<RECT> best;
        int64_t area=INT64_MAX;
        for(const auto& region:regions) {
            RECT r{};
            if(region.window_index!=i || !IntersectRect(&r,&region.bounds,&windows[i].bounds)) continue;
            const int64_t width=int64_t(r.right)-r.left, height=int64_t(r.bottom)-r.top;
            if(width<6 || height<6 || !PtInRect(&r,p) || width*height>=area) continue;
            best=r; area=width*height;
        }
        return best;
    }
    return {};
}
struct ElementScanner::Impl {
    struct Request { HWND notify{}; UINT message{}; uint64_t generation{}, serial{}; std::vector<ElementWindow> windows; POINT pointer{}; };
    std::mutex mutex;
    std::condition_variable wake;
    bool stop{};
    uint64_t serial{};
    std::optional<Request> pending;
    std::optional<ElementScanResult> result;
    elements::Handle cancel{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::thread thread;
    Impl():thread([this]{Run();}) {}
    ~Impl() {
        {std::lock_guard lock(mutex);stop=true;SetEvent(cancel.value);}
        wake.notify_one();thread.join();
    }
    std::vector<ElementRegion> Scan(const Request& request) {
        using namespace elements;
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(Shared),nullptr));
        if(!mapping.value) return {};
        View view(mapping.value);if(!view.value) return {};
        auto& shared=*view.value;
        shared.window_count=static_cast<DWORD>(std::min(request.windows.size(),MaxWindows));
        shared.pointer=request.pointer;
        std::copy_n(request.windows.begin(),shared.window_count,shared.windows);
        SIZE_T bytes{};InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> storage(bytes);
        auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes)) return {};
        struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Cleanup(){DeleteProcThreadAttributeList(p);} } cleanup{attributes};
        if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&mapping.value,sizeof(HANDLE),nullptr,nullptr)) return {};
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
        const auto exe=std::filesystem::path(module).parent_path()/L"lumashot_elements_worker.exe";
        auto command=L"\""+exe.native()+L"\" "+std::to_wstring(reinterpret_cast<uintptr_t>(mapping.value));
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
        PROCESS_INFORMATION pi{};
        Handle job(CreateJobObjectW(nullptr,nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job.value || !SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) return {};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT|BELOW_NORMAL_PRIORITY_CLASS,nullptr,nullptr,&startup.StartupInfo,&pi)) return {};
        Handle process(pi.hProcess), primary(pi.hThread);
        if(!AssignProcessToJobObject(job.value,process.value)) {TerminateProcess(process.value,1);return {};}
        if(ResumeThread(primary.value)==DWORD(-1)) {TerminateProcess(process.value,1);return {};}
        HANDLE waits[]{cancel.value,process.value};
        WaitForMultipleObjects(2,waits,FALSE,600);
        TerminateJobObject(job.value,0);
        WaitForSingleObject(process.value,100);
        const auto count=static_cast<size_t>(std::clamp(InterlockedCompareExchange(&shared.published,0,0),0L,static_cast<LONG>(MaxRegions)));
        if(WaitForSingleObject(cancel.value,0)==WAIT_OBJECT_0)return {};
        std::vector<ElementRegion> regions(shared.regions,shared.regions+count);
        bool valid[MaxWindows]{};
        for(size_t i=0;i<shared.window_count;++i) {
            if(WaitForSingleObject(cancel.value,0)==WAIT_OBJECT_0)return {};
            const auto& window=request.windows[i];RECT current{};
            if(!IsWindowVisible(window.window))continue;
            if(FAILED(DwmGetWindowAttribute(window.window,DWMWA_EXTENDED_FRAME_BOUNDS,&current,sizeof(current)))&&!GetWindowRect(window.window,&current))continue;
            valid[i]=SameElementGeometry(window.bounds,current);
        }
        std::erase_if(regions,[&](const ElementRegion& region) {
            return region.window_index>=shared.window_count||!valid[region.window_index];
        });
        return regions;
    }
    void Run() {
        for(;;) {
            Request request;
            {std::unique_lock lock(mutex);wake.wait(lock,[this]{return stop||pending.has_value();});if(stop)return;
             request=std::move(*pending);pending.reset();ResetEvent(cancel.value);}
            std::vector<ElementRegion> regions;
            try {regions=Scan(request);} catch(...) {}
            {std::lock_guard lock(mutex);if(stop)return;if(request.serial!=serial)continue;
             result=ElementScanResult{request.generation,std::move(regions)};
             PostMessageW(request.notify,request.message,0,0);}
        }
    }
};
ElementScanner::ElementScanner():impl_(std::make_unique<Impl>()) {}
ElementScanner::~ElementScanner()=default;
void ElementScanner::Start(HWND notify,UINT message,uint64_t generation,std::vector<ElementWindow> windows,POINT pointer) {
    try {std::lock_guard lock(impl_->mutex);++impl_->serial;SetEvent(impl_->cancel.value);impl_->result.reset();
        impl_->pending=Impl::Request{notify,message,generation,impl_->serial,std::move(windows),pointer};impl_->wake.notify_one();} catch(...) {Cancel();}
}
void ElementScanner::Cancel() {std::lock_guard lock(impl_->mutex);++impl_->serial;impl_->pending.reset();impl_->result.reset();SetEvent(impl_->cancel.value);}
std::optional<ElementScanResult> ElementScanner::Take() {std::lock_guard lock(impl_->mutex);return std::exchange(impl_->result,{});}
}



