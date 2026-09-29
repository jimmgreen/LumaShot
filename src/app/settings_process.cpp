#include "app/settings_process.h"
#include "app/settings_dialog.h"
#include "app/hotkeys.h"
#include <array>
#include <vector>
#include <string>
#include <stdexcept>

namespace lumashot {
namespace {
struct Handle {
    HANDLE value{};
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Values {UINT modifiers,key,cursor,theme,paste_as_file,paste_file_format,gif_modifiers,gif_key,video_modifiers,video_key,pin_style,clipboard_enabled,start_with_windows,clipboard_modifiers,clipboard_key,clipboard_persist,update_auto_check;};
// command: 0 = save request, 1 = "check for updates now" (host answers immediately).
struct Exchange {UINT version;Values values;UINT accepted;LONG recording;UINT command;};
thread_local Exchange* active_exchange{};
struct ActiveExchange {
    Exchange* previous{active_exchange};
    explicit ActiveExchange(Exchange* value){active_exchange=value;}
    ~ActiveExchange(){active_exchange=previous;}
};
struct Mapping {Exchange* value{};~Mapping(){if(value)UnmapViewOfFile(value);}};
Values Encode(const Preferences& p){return {p.modifiers,p.key,p.include_cursor?1u:0u,static_cast<UINT>(p.theme),p.paste_as_file?1u:0u,static_cast<UINT>(p.paste_file_format),p.gif_modifiers,p.gif_key,p.video_modifiers,p.video_key,static_cast<UINT>(p.pin_style),p.clipboard_enabled?1u:0u,p.start_with_windows?1u:0u,p.clipboard_modifiers,p.clipboard_key,p.clipboard_persist?1u:0u,p.update_auto_check?1u:0u};}
bool Decode(const Values& values,Preferences& p){
    if(!ValidShortcut(values.modifiers,values.key)||ShortcutModifier(values.key)||values.start_with_windows>1||values.clipboard_enabled>1||values.clipboard_persist>1||values.update_auto_check>1||values.cursor>1||values.theme>2||values.paste_as_file>1||values.paste_file_format>2||values.pin_style>static_cast<UINT>(PinStyle::Curl))return false;
    for(auto pair:{std::pair{values.gif_modifiers,values.gif_key},std::pair{values.video_modifiers,values.video_key},std::pair{values.clipboard_modifiers,values.clipboard_key}})if(pair.second&&(!ValidShortcut(pair.first,pair.second)||ShortcutModifier(pair.second)))return false;
    p.start_with_windows=values.start_with_windows!=0;
    p.clipboard_modifiers=values.clipboard_modifiers;p.clipboard_key=values.clipboard_key;
    p.clipboard_enabled=values.clipboard_enabled!=0;
    p.clipboard_persist=values.clipboard_persist!=0;
    p.update_auto_check=values.update_auto_check!=0;
    p.pin_style=static_cast<PinStyle>(values.pin_style);
    p.gif_modifiers=values.gif_modifiers;p.gif_key=values.gif_key;p.video_modifiers=values.video_modifiers;p.video_key=values.video_key;
    p.modifiers=values.modifiers;p.key=values.key;p.include_cursor=values.cursor!=0;p.theme=static_cast<int>(values.theme);p.paste_as_file=values.paste_as_file!=0;p.paste_file_format=static_cast<int>(values.paste_file_format);return true;
}
HANDLE Argument(const wchar_t* text){wchar_t* end{};const auto number=_wcstoui64(text,&end,10);return end!=text&&*end==0?reinterpret_cast<HANDLE>(static_cast<uintptr_t>(number)):nullptr;}
}
bool SettingsShortcutRecording(){return active_exchange&&InterlockedCompareExchange(&active_exchange->recording,0,0)!=0;}
int SettingsWorkerMain(int count,wchar_t** args){
    if(count!=5)return 2;
    Handle map{Argument(args[2])},request{Argument(args[3])},response{Argument(args[4])};
    if(!map.value||!request.value||!response.value)return 2;
    Mapping exchange{static_cast<Exchange*>(MapViewOfFile(map.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(Exchange)))};
    Preferences draft;
    if(!exchange.value||exchange.value->version!=10||!Decode(exchange.value->values,draft))return 2;
    return ShowSettingsDialog(nullptr,draft,[&](const Preferences& candidate){
        exchange.value->values=Encode(candidate);exchange.value->accepted=0;exchange.value->command=0;
        if(!SetEvent(request.value)||WaitForSingleObject(response.value,INFINITE)!=WAIT_OBJECT_0)return false;
        return exchange.value->accepted==1;
    },[&](bool recording){InterlockedExchange(&exchange.value->recording,recording?1:0);},[&]{
        // Let the host's result prompt come to the foreground over this dialog.
        AllowSetForegroundWindow(ASFW_ANY);
        exchange.value->accepted=0;exchange.value->command=1;
        if(SetEvent(request.value))WaitForSingleObject(response.value,INFINITE);
        exchange.value->command=0;
    })?0:1;
}
bool EditPreferencesIsolated(HWND owner,Preferences& value,const std::function<bool(const Preferences&)>& accept,const SettingsWait& wait_events,const std::function<void()>& check_updates){
    // Keep all font, acrylic and third-party input-method caches out of the tray
    // process. The job closes the short-lived child even if the parent exits.
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle map{CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(Exchange),nullptr)};
    Handle request{CreateEventW(&security,FALSE,FALSE,nullptr)},response{CreateEventW(&security,FALSE,FALSE,nullptr)};
    Handle job{CreateJobObjectW(nullptr,nullptr)};
    if(!map.value||!request.value||!response.value||!job.value)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return false;
    Mapping exchange{static_cast<Exchange*>(MapViewOfFile(map.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(Exchange)))};
    if(!exchange.value)return false;
    *exchange.value={10,Encode(value),0,0,0};
    ActiveExchange active(exchange.value);
    SIZE_T bytes{};InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> storage(bytes);
    auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes))return false;
    struct AttributeCleanup {LPPROC_THREAD_ATTRIBUTE_LIST list;~AttributeCleanup(){DeleteProcThreadAttributeList(list);}} cleanup{attributes};
    std::array<HANDLE,3> inherited{map.value,request.value,response.value};
    if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited.data(),sizeof(inherited),nullptr,nullptr))return false;
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
    const auto exe=std::filesystem::path(module).parent_path()/L"LumaShot.exe";
    std::wstring command=L"\""+exe.native()+L"\" --settings-worker";
    for(HANDLE handle:inherited)command+=L" "+std::to_wstring(reinterpret_cast<uintptr_t>(handle));
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED,nullptr,exe.parent_path().c_str(),&startup.StartupInfo,&process))return false;
    Handle child{process.hProcess},thread{process.hThread};
    if(!AssignProcessToJobObject(job.value,child.value)){TerminateProcess(child.value,2);WaitForSingleObject(child.value,5000);return false;}
    AllowSetForegroundWindow(process.dwProcessId);
    if(ResumeThread(thread.value)==static_cast<DWORD>(-1))return false;
    // Keep the host enabled: tray commands and owned capture overlays must work
    // while this child is open. Only active shortcut recording blocks hotkeys.
    (void)owner;
    bool saved=false,quit=false;int quit_code{};
    const HANDLE waits[]={child.value,request.value};
    for(;;){
        const DWORD ready=wait_events?wait_events(waits):MsgWaitForMultipleObjectsEx(2,waits,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(ready==WAIT_OBJECT_0||ready==WAIT_FAILED)break;
        if(ready==WAIT_OBJECT_0+1&&exchange.value->command==1){
            exchange.value->accepted=1;SetEvent(response.value);
            if(check_updates)check_updates();
        }
        else if(ready==WAIT_OBJECT_0+1){
            auto candidate=value;
            saved=Decode(exchange.value->values,candidate)&&(!accept||accept(candidate));
            if(saved)value=candidate;
            exchange.value->accepted=saved?1:0;SetEvent(response.value);
        }
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT){quit=true;quit_code=static_cast<int>(message.wParam);break;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(quit){TerminateJobObject(job.value,1);WaitForSingleObject(child.value,5000);break;}
    }
    if(quit)PostQuitMessage(quit_code);
    return saved;
}
}