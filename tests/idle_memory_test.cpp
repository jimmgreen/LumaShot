#include "app/application.h"
#include <psapi.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
using namespace lumashot;
namespace {
int stage{},mode{},requests{},failures{};SIZE_T baseline_ws{},baseline_private{};
void Expect(bool value,const char* message){std::cout<<(value?"PASS ":"FAIL ")<<message<<std::endl;failures+=!value;}
void Sample(const char* label){static std::vector<unsigned char> buffer(512*1024);PROCESS_MEMORY_COUNTERS_EX memory{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
 SIZE_T private_ws=0;if(QueryWorkingSet(GetCurrentProcess(),buffer.data(),static_cast<DWORD>(buffer.size()))){auto* ws=reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(buffer.data());for(ULONG_PTR i=0;i<ws->NumberOfEntries;++i)if(!ws->WorkingSetInfo[i].Shared)private_ws+=4096;}
 std::cout<<label<<" working_set_MiB="<<memory.WorkingSetSize/1048576.0<<" private_commit_MiB="<<memory.PrivateUsage/1048576.0<<" private_ws_MiB="<<private_ws/1048576.0<<std::endl;
 if(!baseline_ws){baseline_ws=memory.WorkingSetSize;baseline_private=memory.PrivateUsage;}
 else {Expect(memory.WorkingSetSize<baseline_ws+5*1024*1024,"settings does not retain graphics working set in tray process");Expect(memory.PrivateUsage<baseline_private+3*1024*1024,"settings does not retain large private allocations in tray process");}
}
HWND ChildDialog(){
 HWND dialog{};const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return nullptr;
 PROCESSENTRY32W entry{sizeof(entry)};
 if(Process32FirstW(snapshot,&entry))do{if(entry.th32ParentProcessID!=GetCurrentProcessId())continue;
  struct Search {DWORD process;HWND window;} search{entry.th32ProcessID,nullptr};
  EnumWindows([](HWND w,LPARAM lp)->BOOL{auto& s=*reinterpret_cast<Search*>(lp);DWORD pid{};GetWindowThreadProcessId(w,&pid);if(pid==s.process&&GetDlgItem(w,103)){s.window=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
  if(search.window){dialog=search.window;break;}
 }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return dialog;
}
void CALLBACK Tick(HWND,UINT,UINT_PTR id,DWORD){
 if(stage==0){KillTimer(nullptr,id);Sample("cold_idle");stage=1;SetTimer(nullptr,0,400,Tick);
  for(mode=0;mode<3;++mode){requests=0;Preferences synthetic;synthetic.tools.width=9;
   const bool saved=EditPreferences(nullptr,synthetic,[&](const Preferences& candidate){++requests;return candidate.theme==2&&candidate.paste_as_file&&candidate.paste_file_format==2&&(mode!=2||requests>1);});
   Expect(saved==(mode!=0),"isolated cancel/save result");Expect(synthetic.tools.width==9,"worker preserves unrelated tool preferences");
   if(mode)Expect(synthetic.theme==2&&synthetic.paste_as_file&&synthetic.paste_file_format==2&&requests==(mode==2?2:1),"save validation executes on parent and rejected request retries inline");
   Expect(!ChildDialog(),"settings child exits before returning");
  }stage=2;
 }else if(stage==1){if(const HWND dialog=ChildDialog()){
    if(!mode)PostMessageW(dialog,WM_CLOSE,0,0);
    else {PostMessageW(dialog,WM_COMMAND,112,0);PostMessageW(dialog,WM_COMMAND,118,0);PostMessageW(dialog,WM_COMMAND,IDOK,0);}
 }}else {KillTimer(nullptr,id);Sample("settings_closed");PostMessageW(FindWindowW(L"LumaShot.Host",L"LumaShot"),WM_CLOSE,0,0);}
}
}
int main(){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);SetTimer(nullptr,0,500,Tick);{Application app;app.Run(false,false,true);}CoUninitialize();return failures?1:0;}
