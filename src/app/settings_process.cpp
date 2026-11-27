#include "app/settings_process.h"
#include "app/settings_dialog.h"
#include "app/hotkeys.h"
#include <algorithm>
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
struct Values {UINT modifiers,key,cursor,theme,paste_as_file,paste_file_format,gif_modifiers,gif_key,video_modifiers,video_key,pin_style,clipboard_enabled,start_with_windows,clipboard_modifiers,clipboard_key,clipboard_persist,update_auto_check,clipboard_strip_visible,translate_modifiers,translate_key,translate_auto_show,settings_page,ocr_modifiers,ocr_key,pin_modifiers,pin_key,ocr_remove_whitespace,clothesline_enabled,selection_modifiers,selection_key;wchar_t ocr_model_directory[32768];
    UINT toolbar_order[std::size(CustomizableToolbarIds)],toolbar_overflow[std::size(CustomizableToolbarIds)],toolbar_overflow_count,toolbar_labels,toolbar_auto_properties,toolbar_property_style,toolbar_surface_style;
    UINT clipboard_max_entries,clipboard_classic_strip,clipboard_card_preview;};
// command: 0 = save request, 1 = "check for updates now" (host answers immediately).
struct Exchange {UINT version;Values values;UINT accepted;LONG recording;UINT command;};
thread_local Exchange* active_exchange{};
struct ActiveExchange {
    Exchange* previous{active_exchange};
    explicit ActiveExchange(Exchange* value){active_exchange=value;}
    ~ActiveExchange(){active_exchange=previous;}
};
struct Mapping {Exchange* value{};~Mapping(){if(value)UnmapViewOfFile(value);}};
Values Encode(const Preferences& p){Values values{p.modifiers,p.key,p.include_cursor?1u:0u,static_cast<UINT>(p.theme),p.paste_as_file?1u:0u,static_cast<UINT>(p.paste_file_format),p.gif_modifiers,p.gif_key,p.video_modifiers,p.video_key,static_cast<UINT>(p.pin_style),p.clipboard_enabled?1u:0u,p.start_with_windows?1u:0u,p.clipboard_modifiers,p.clipboard_key,p.clipboard_persist?1u:0u,p.update_auto_check?1u:0u,p.clipboard_strip_visible?1u:0u,p.translate_modifiers,p.translate_key,p.translate_auto_show?1u:0u,static_cast<UINT>(std::clamp(p.settings_page,0,7)),p.ocr_modifiers,p.ocr_key,p.pin_modifiers,p.pin_key,p.ocr_remove_whitespace?1u:0u,p.clothesline_enabled?1u:0u,p.selection_modifiers,p.selection_key,{}};
    if(p.ocr_model_directory.native().size()>=std::size(values.ocr_model_directory))throw std::length_error("OCR model directory too long");
    std::copy(p.ocr_model_directory.native().begin(),p.ocr_model_directory.native().end(),values.ocr_model_directory);
    auto layout=p.toolbar;layout.Normalize();
    for(size_t i=0;i<layout.order.size()&&i<std::size(values.toolbar_order);++i)values.toolbar_order[i]=static_cast<UINT>(layout.order[i]);
    for(size_t i=0;i<layout.overflow.size()&&i<std::size(values.toolbar_overflow);++i)values.toolbar_overflow[i]=static_cast<UINT>(layout.overflow[i]);
    values.toolbar_overflow_count=static_cast<UINT>(std::min(layout.overflow.size(),std::size(values.toolbar_overflow)));
    values.toolbar_labels=static_cast<UINT>(layout.labels);values.toolbar_auto_properties=layout.auto_properties?1u:0u;
    values.toolbar_property_style=static_cast<UINT>(layout.property_style);
    values.toolbar_surface_style=static_cast<UINT>(layout.surface_style);
    values.clipboard_max_entries=static_cast<UINT>(p.clipboard_max_entries);values.clipboard_classic_strip=p.clipboard_classic_strip?1u:0u;values.clipboard_card_preview=p.clipboard_card_preview?1u:0u;
    return values;
}
// The order must be a permutation of the customizable ids; overflow a duplicate-free subset.
bool DecodeToolbar(const Values& values,ToolbarCustomization& out){
    if(values.toolbar_overflow_count>std::size(values.toolbar_overflow)||values.toolbar_labels>2||values.toolbar_auto_properties>1||values.toolbar_property_style>1||values.toolbar_surface_style>1)return false;
    std::vector<int> order,overflow;
    for(UINT raw:values.toolbar_order){const int id=static_cast<int>(raw);if(!IsCustomizableToolbarId(id)||std::find(order.begin(),order.end(),id)!=order.end())return false;order.push_back(id);}
    for(UINT i=0;i<values.toolbar_overflow_count;++i){const int id=static_cast<int>(values.toolbar_overflow[i]);if(!IsCustomizableToolbarId(id)||std::find(overflow.begin(),overflow.end(),id)!=overflow.end())return false;overflow.push_back(id);}
    out.order=std::move(order);out.overflow=std::move(overflow);out.labels=static_cast<ToolbarLabels>(values.toolbar_labels);out.auto_properties=values.toolbar_auto_properties!=0;out.property_style=static_cast<PropertyBarStyle>(values.toolbar_property_style);out.surface_style=static_cast<ToolbarSurfaceStyle>(values.toolbar_surface_style);out.Normalize();
    return true;
}
bool Decode(const Values& values,Preferences& p){
    if(std::find(std::begin(values.ocr_model_directory),std::end(values.ocr_model_directory),L'\0')==std::end(values.ocr_model_directory)||values.ocr_remove_whitespace>1||values.clothesline_enabled>1)return false;
    if(!ValidShortcut(values.modifiers,values.key)||ShortcutModifier(values.key)||values.start_with_windows>1||values.clipboard_enabled>1||values.clipboard_persist>1||values.update_auto_check>1||values.clipboard_strip_visible>1||values.translate_auto_show>1||values.settings_page>7||values.cursor>1||values.theme>2||values.paste_as_file>1||values.paste_file_format>2||values.pin_style>static_cast<UINT>(PinStyle::Curl))return false;
    for(auto pair:{std::pair{values.gif_modifiers,values.gif_key},std::pair{values.video_modifiers,values.video_key},std::pair{values.clipboard_modifiers,values.clipboard_key},std::pair{values.translate_modifiers,values.translate_key},std::pair{values.ocr_modifiers,values.ocr_key},std::pair{values.pin_modifiers,values.pin_key},std::pair{values.selection_modifiers,values.selection_key}})if(pair.second&&(!ValidShortcut(pair.first,pair.second)||ShortcutModifier(pair.second)))return false;
    if(!ValidClipboardLimit(static_cast<int>(values.clipboard_max_entries))||values.clipboard_classic_strip>1||values.clipboard_card_preview>1)return false;
    ToolbarCustomization toolbar;if(!DecodeToolbar(values,toolbar))return false;
    p.ocr_modifiers=values.ocr_modifiers;p.ocr_key=values.ocr_key;p.pin_modifiers=values.pin_modifiers;p.pin_key=values.pin_key;p.selection_modifiers=values.selection_modifiers;p.selection_key=values.selection_key;
    p.ocr_remove_whitespace=values.ocr_remove_whitespace!=0;p.clothesline_enabled=values.clothesline_enabled!=0;p.ocr_model_directory=values.ocr_model_directory;
    p.start_with_windows=values.start_with_windows!=0;
    p.clipboard_modifiers=values.clipboard_modifiers;p.clipboard_key=values.clipboard_key;
    p.translate_modifiers=values.translate_modifiers;p.translate_key=values.translate_key;
    p.clipboard_enabled=values.clipboard_enabled!=0;
    p.clipboard_persist=values.clipboard_persist!=0;
    p.update_auto_check=values.update_auto_check!=0;
    p.clipboard_strip_visible=values.clipboard_strip_visible!=0;
    p.clipboard_max_entries=static_cast<int>(values.clipboard_max_entries);p.clipboard_classic_strip=values.clipboard_classic_strip!=0;p.clipboard_card_preview=values.clipboard_card_preview!=0;
    p.translate_auto_show=values.translate_auto_show!=0;p.settings_page=static_cast<int>(values.settings_page);p.toolbar=toolbar;
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
    if(!exchange.value||exchange.value->version!=21||!Decode(exchange.value->values,draft))return 2;
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
    *exchange.value={21,Encode(value),0,0,0};
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
