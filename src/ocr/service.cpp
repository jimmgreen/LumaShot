#include "ocr/service.h"
#include <filesystem>
#include <chrono>
namespace lumashot::ocr {
Service::Service(HWND notify,UINT message):notify_(notify),message_(message),thread_([this]{Run();}){}
Service::~Service(){stopping_=true;cv_.notify_one();if(thread_.joinable())thread_.join();}
void Service::Submit(uint64_t id,uint64_t version,std::shared_ptr<const Frame> image) {
    Cancel(id);{std::lock_guard lock(mutex_);jobs_.push_back({id,version,std::move(image),std::make_shared<std::atomic_bool>(false)});}cv_.notify_one();
}
void Service::Cancel(uint64_t id) {
    std::lock_guard lock(mutex_);
    if(active_&&active_->id==id)*active_->canceled=true;
    std::erase_if(jobs_,[&](const Job& j){return j.id==id;});std::erase_if(completed_,[&](const Completion& c){return c.id==id;});
}
std::vector<Completion> Service::Take(){std::lock_guard lock(mutex_);return std::exchange(completed_,{});}
void Service::StartChild() {
    if(process_.Get())return;
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE a{},b{};
    CheckWin32(CreatePipe(&a,&b,&sa,65536)!=FALSE,"Create OCR input pipe");Handle child_input(a);input_.Reset(b);
    CheckWin32(CreatePipe(&a,&b,&sa,65536)!=FALSE,"Create OCR output pipe");output_.Reset(a);Handle child_output(b);
    CheckWin32(SetHandleInformation(input_.Get(),HANDLE_FLAG_INHERIT,0)!=FALSE,"Protect OCR pipe");
    CheckWin32(SetHandleInformation(output_.Get(),HANDLE_FLAG_INHERIT,0)!=FALSE,"Protect OCR pipe");
    Handle null_error(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));
    const HANDLE inherited[]={child_input.Get(),child_output.Get(),null_error.Get()};
    SIZE_T bytes{};InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<uint8_t> storage(bytes);
    auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    CheckWin32(InitializeProcThreadAttributeList(attributes,1,0,&bytes)!=FALSE,"Initialize OCR process attributes");
    struct AttributeCleanup {LPPROC_THREAD_ATTRIBUTE_LIST p;~AttributeCleanup(){DeleteProcThreadAttributeList(p);}} cleanup{attributes};
    CheckWin32(UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(inherited),sizeof(inherited),nullptr,nullptr)!=FALSE,"Set OCR inherited handles");
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
    const auto exe=std::filesystem::path(module).parent_path()/L"lumashot_ocr_worker.exe";
    std::wstring command=L"\""+exe.native()+L"\"";
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow=SW_HIDE;si.StartupInfo.hStdInput=child_input.Get();si.StartupInfo.hStdOutput=child_output.Get();si.StartupInfo.hStdError=null_error.Get();si.lpAttributeList=attributes;
    PROCESS_INFORMATION pi{};
    CheckWin32(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,exe.parent_path().c_str(),&si.StartupInfo,&pi)!=FALSE,"Start offline OCR worker");
    process_.Reset(pi.hProcess);Handle primary_thread(pi.hThread);
    job_object_.Reset(CreateJobObjectW(nullptr,nullptr));CheckWin32(job_object_.Get()!=nullptr,"Create OCR job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CheckWin32(SetInformationJobObject(job_object_.Get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"Configure OCR job");
    CheckWin32(AssignProcessToJobObject(job_object_.Get(),process_.Get())!=FALSE,"Assign OCR process");
    CheckWin32(ResumeThread(primary_thread.Get())!=DWORD(-1),"Resume OCR process");
}
void Service::StopChild(){if(process_.Get()){TerminateProcess(process_.Get(),0);WaitForSingleObject(process_.Get(),2000);}process_.Reset();input_.Reset();output_.Reset();job_object_.Reset();}
void Service::Read(void* buffer,size_t size,const Job& job) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);auto* p=static_cast<uint8_t*>(buffer);
    while(size) {
        if(stopping_||*job.canceled)throw std::runtime_error("OCR canceled");
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("OCR timed out; retry with a smaller image");
        DWORD available{};CheckWin32(PeekNamedPipe(output_.Get(),nullptr,0,nullptr,&available,nullptr)!=FALSE,"Read OCR worker");
        if(!available){if(WaitForSingleObject(process_.Get(),10)==WAIT_OBJECT_0)throw std::runtime_error("OCR worker exited");continue;}
        const size_t n=std::min(size,size_t(available));if(!ReadExact(output_.Get(),p,n))throw std::runtime_error("Incomplete OCR result");p+=n;size-=n;
    }
}
void Service::Run() {
    while(!stopping_) {
        Job job;
        {std::unique_lock lock(mutex_);
            if(!cv_.wait_for(lock,std::chrono::seconds(30),[&]{return stopping_||!jobs_.empty();})){lock.unlock();StopChild();continue;}
            if(stopping_)break;job=std::move(jobs_.front());jobs_.pop_front();active_=job;
        }
        Completion completion;completion.id=job.id;completion.version=job.version;
        try {
            StartChild();const auto& f=*job.image;const uint64_t size=f.pixels.size()*4;
            Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,static_cast<DWORD>(size>>32),static_cast<DWORD>(size),nullptr));
            CheckWin32(mapping.Get()!=nullptr,"Create OCR image mapping");
            {MappingView view(mapping.Get(),FILE_MAP_WRITE);std::memcpy(view.data,f.pixels.data(),static_cast<size_t>(size));}
            HANDLE remote{};CheckWin32(DuplicateHandle(GetCurrentProcess(),mapping.Get(),process_.Get(),&remote,FILE_MAP_READ,FALSE,0)!=FALSE,"Share OCR image");
            Request request;request.width=static_cast<uint32_t>(f.Width());request.height=static_cast<uint32_t>(f.Height());request.mapping=reinterpret_cast<uintptr_t>(remote);
            if(!WriteExact(input_.Get(),&request,sizeof(request)))throw std::runtime_error("Cannot send OCR request");
            uint32_t n{};Read(&n,sizeof(n),job);if(n>kMaxReply||n<12)throw std::runtime_error("Invalid OCR reply size");
            Bytes bytes;bytes.data.resize(n);Read(bytes.data.data(),n,job);completion.text=Decode(bytes,completion.error);
        }catch(const std::exception& e){completion.error=ErrorMessage(e);StopChild();}
        {std::lock_guard lock(mutex_);active_.reset();if(!stopping_&&!*job.canceled){completed_.push_back(std::move(completion));PostMessageW(notify_,message_,0,0);}}
    }StopChild();
}
}
