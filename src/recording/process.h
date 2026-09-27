#pragma once
#include <windows.h>
#include "recording/launch_options.h"
#include <filesystem>
#include <string>
namespace lumashot {
// No recording libraries or GPU resources are loaded in the tray process.
class RecordingProcess {
    HANDLE process_{},job_{};
public:
    ~RecordingProcess(){if(job_)CloseHandle(job_);if(process_)CloseHandle(process_);}
    bool Active()const{return process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT;}
    void Show()const{if(!Active())return;const DWORD pid=GetProcessId(process_);AllowSetForegroundWindow(pid);EnumWindows([](HWND w,LPARAM data)->BOOL{DWORD owner{};GetWindowThreadProcessId(w,&owner);if(owner==static_cast<DWORD>(data)&&IsWindowVisible(w)&&!GetWindow(w,GW_OWNER)){ShowWindow(w,SW_SHOW);SetForegroundWindow(w);return FALSE;}return TRUE;},static_cast<LPARAM>(pid));}
    bool Start(bool gif,bool dark,std::optional<RECT> region={}){
        if(Active()){Show();return true;}
        if(process_){CloseHandle(process_);process_=nullptr;}if(job_){CloseHandle(job_);job_=nullptr;}
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
        const auto exe=std::filesystem::path(module).parent_path()/L"lumashot_recording_worker.exe";
        std::wstring command=L"\""+exe.native()+L"\" "+(gif?L"--gif":L"--video")+(dark?L" --dark":L"");
        if(region)command+=recording::RecordingRegionArgument(*region);
        job_=CreateJobObjectW(nullptr,nullptr);if(!job_)return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return false;
        STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION pi{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,exe.parent_path().c_str(),&start,&pi))return false;
        process_=pi.hProcess;
        if(!AssignProcessToJobObject(job_,process_)){TerminateProcess(process_,1);CloseHandle(pi.hThread);return false;}
        AllowSetForegroundWindow(pi.dwProcessId);const auto resumed=ResumeThread(pi.hThread);CloseHandle(pi.hThread);
        if(resumed==DWORD(-1)){TerminateProcess(process_,1);return false;}return true;
    }
};
}
