#include "app/application.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <iostream>
namespace lumashot {
struct ColdStartMemoryTest {
 inline static Application* app{};inline static int stage{},failures{};inline static bool baseline{};inline static std::filesystem::path directory;
 static void Expect(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failures+=!ok;}
 static void Sample(const char* label){
  PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
  std::vector<unsigned char> buffer(1024*1024);size_t private_ws=0;if(QueryWorkingSet(GetCurrentProcess(),buffer.data(),static_cast<DWORD>(buffer.size()))){auto* info=reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(buffer.data());for(ULONG_PTR i=0;i<info->NumberOfEntries;++i)if(!info->WorkingSetInfo[i].Shared)private_ws+=4096;}
  DWORD handles{};GetProcessHandleCount(GetCurrentProcess(),&handles);int threads=0;HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 entry{sizeof(entry)};if(snapshot!=INVALID_HANDLE_VALUE){if(Thread32First(snapshot,&entry))do{if(entry.th32OwnerProcessID==GetCurrentProcessId())++threads;}while(Thread32Next(snapshot,&entry));CloseHandle(snapshot);}
  std::cout<<"MEMORY "<<label<<" private_commit="<<memory.PrivateUsage<<" working_set="<<memory.WorkingSetSize<<" private_working_set="<<private_ws<<" threads="<<threads<<" handles="<<handles<<std::endl;
  for(const auto* name:{L"lumatext.dll",L"d3d11.dll",L"dxgi.dll",L"d2d1.dll",L"DWrite.dll",L"onnxruntime.dll"})std::wcout<<L"MODULE "<<name<<L" loaded="<<(GetModuleHandleW(name)!=nullptr)<<std::endl;
 }
 static void CALLBACK Tick(HWND,UINT,UINT_PTR timer,DWORD){
  try{
   if(stage==0){Expect(app->pins_->EnableSession(directory),"empty synthetic pin session restores without touching personal pins");stage=1;return;}
   if(stage==1){
    Sample("cold_app");Expect(!app->active_&&!app->pending_&&app->views_.empty()&&!app->frame_&&!app->acrylic_,"whole app idle has no screenshot buffers or overlay renderers");
    Expect(GetModuleHandleW(L"onnxruntime.dll")==nullptr,"OCR engine is not loaded into tray app");if(!baseline)Expect(GetModuleHandleW(L"lumatext.dll")==nullptr,"text engine is deferred until actual rendering");
    const auto start=GetTickCount64();{Renderer renderer;auto frame=renderer.Demo(true,false);Expect(frame.Width()>0&&frame.Height()>0&&!frame.pixels.empty(),"first-use synthetic rendering works after deferred loading");}
    std::cout<<"FIRST_RENDER_MS "<<GetTickCount64()-start<<std::endl;Expect(GetModuleHandleW(L"lumatext.dll")!=nullptr,"text engine loads on first real rendering request");stage=2;return;
   }
   Sample("after_synthetic_render");KillTimer(nullptr,timer);PostMessageW(app->main_,WM_CLOSE,0,0);
  }catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<std::endl;++failures;KillTimer(nullptr,timer);PostMessageW(app->main_,WM_CLOSE,0,0);}
 }
 static int Run(bool before){baseline=before;directory=std::filesystem::temp_directory_path()/(L"LumaShot-cold-memory-"+std::to_wstring(GetCurrentProcessId()));
  {Application instance;app=&instance;SetTimer(nullptr,0,1000,Tick);instance.Run(false,false,true);app=nullptr;}
  std::error_code ec;std::filesystem::remove_all(directory,ec);return failures?1:0;
 }
};
}
int main(int argc,char**){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);const int result=lumashot::ColdStartMemoryTest::Run(argc>1);CoUninitialize();return result;}
