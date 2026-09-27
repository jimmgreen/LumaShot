#include "app/application.h"
#include <tlhelp32.h>
#include <iostream>
#include <atomic>

namespace lumashot {
namespace {
int failures{};std::atomic_bool failure_notice{};
void Expect(bool value,const char* name){std::cout<<(value?"PASS ":"FAIL ")<<name<<std::endl;failures+=!value;}
void Pump(DWORD milliseconds){const auto end=GetTickCount64()+milliseconds;do{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}while(GetTickCount64()<end);}
HWND WorkerWindow(){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return nullptr;
    PROCESSENTRY32W entry{sizeof(entry)};DWORD child{};
    if(Process32FirstW(snapshot,&entry))do{if(entry.th32ParentProcessID==GetCurrentProcessId()&&std::wstring_view(entry.szExeFile)==L"lumashot_recording_worker.exe"){child=entry.th32ProcessID;break;}}while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);if(!child)return nullptr;
    struct Search{DWORD pid;HWND window{};} search{child};
    EnumWindows([](HWND w,LPARAM context)->BOOL{auto& found=*reinterpret_cast<Search*>(context);DWORD pid{};GetWindowThreadProcessId(w,&pid);wchar_t name[128]{};GetClassNameW(w,name,128);if(pid==found.pid&&std::wstring_view(name)==L"LumaShot.Recording"&&IsWindowVisible(w)){found.window=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
    return search.window;
}
HWND WaitWorker(){const auto end=GetTickCount64()+8000;do{if(HWND w=WorkerWindow())return w;Pump(40);}while(GetTickCount64()<end);return nullptr;}
// The worker is custom drawn: readiness is a real owned FPS menu, not an EDIT HWND.
bool ConfigurationReady(HWND worker){
    RECT client{};GetClientRect(worker,&client);const float scale=float(client.right)/544.f;
    // Video-mode FPS button is {220,91,362,121} in src/recording/panel.cpp; click its center.
    const LPARAM point=MAKELPARAM(int(291*scale),int(106*scale));
    SendMessageTimeoutW(worker,WM_PAINT,0,0,SMTO_ABORTIFHUNG,1000,nullptr);
    PostMessageW(worker,WM_LBUTTONDOWN,0,point);PostMessageW(worker,WM_LBUTTONUP,0,point);
    struct Search{HWND owner{},menu{};} found{worker};
    const auto end=GetTickCount64()+3000;
    do{
        EnumWindows([](HWND w,LPARAM context)->BOOL{
            auto& result=*reinterpret_cast<Search*>(context);wchar_t name[80]{};GetClassNameW(w,name,80);
            if(GetWindow(w,GW_OWNER)==result.owner&&std::wstring_view(name)==L"LumaShot.SharedDropdown"&&IsWindowVisible(w)){result.menu=w;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&found));
        if(found.menu)break;Pump(20);
    }while(GetTickCount64()<end);
    if(!found.menu)return false;
    PostMessageW(found.menu,WM_KEYDOWN,VK_ESCAPE,0);
    const auto closed=GetTickCount64()+2000;while(IsWindow(found.menu)&&GetTickCount64()<closed)Pump(20);
    return !IsWindow(found.menu)&&IsWindow(worker);
}
void DismissFailure(std::stop_token stop){while(!stop.stop_requested()){EnumWindows([](HWND w,LPARAM)->BOOL{DWORD pid{};GetWindowThreadProcessId(w,&pid);wchar_t name[64]{};GetClassNameW(w,name,64);if(pid==GetCurrentProcessId()&&std::wstring_view(name)==L"LumaShot.ThemedMessage"){failure_notice=true;PostMessageW(w,WM_KEYDOWN,VK_RETURN,0);}return TRUE;},0);Sleep(20);}}
}
struct RecordingEntryTest {
    static void Select(Application& app){
        app.Start();Expect(app.active_&&!app.views_.empty(),"synthetic screenshot opens");
        if(app.views_.empty())throw std::runtime_error("No synthetic overlay");
        const RECT m=app.monitors_.front();app.state_.selection={float(m.left+30),float(m.top+40),float(m.left+350),float(m.top+220)};app.state_.selected=true;
        app.UpdateToolbar({m.left+350,m.top+220});
    }
    static void Open(Application& app){const auto b=app.state_.toolbar.Button(16);app.PointerDown(*app.views_.front(),{(b.left+b.right)/2,(b.top+b.bottom)/2});}
    static int Run(bool missing){
        Application app;app.demo_=true;app.diagnostic_session_=true;
        const auto instance=GetModuleHandleW(nullptr);WNDCLASSW host{};host.hInstance=instance;host.lpfnWndProc=Application::MainProc;host.lpszClassName=L"LumaShot.RecordingEntryHost";RegisterClassW(&host);
        app.main_=CreateWindowW(host.lpszClassName,L"Synthetic recording-entry host",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,&app);
        WNDCLASSW overlay{};overlay.hInstance=instance;overlay.lpfnWndProc=Application::OverlayProc;overlay.lpszClassName=L"LumaShot.Overlay";RegisterClassW(&overlay);
        Select(app);auto original=app.frame_;const auto selection=app.state_.selection;
        app.state_.busy=true;app.Command(16);Expect(app.active_&&!app.recording_process_.Active(),"busy command is guarded");app.state_.busy=false;
        app.pin_edit_id_=1;app.Command(16);Expect(app.active_&&!app.recording_process_.Active(),"pinned image cannot launch desktop recording");app.pin_edit_id_=0;
        app.state_.selection.right=app.state_.selection.left+7;app.Command(16);Expect(app.active_&&!app.recording_process_.Active(),"tiny selection cannot launch recording");app.state_.selection=selection;
        if(missing){
            std::jthread dismiss(DismissFailure);Open(app);dismiss.request_stop();dismiss.join();
            Expect(failure_notice&&!app.recording_process_.Active()&&app.active_&&app.frame_==original&&app.state_.selected&&!app.state_.dropdown.Open(),"missing worker reports failure without losing screenshot");app.Cancel(false);return failures?1:0;
        }
        {
            Open(app);
            Expect(!app.state_.dropdown.Open(),"record icon jumps directly without opening a menu");
            HWND worker=WaitWorker();Expect(worker&&app.recording_process_.Active()&&!app.active_&&app.views_.empty()&&!app.frame_,"one icon click opens video preparation and releases screenshot");
            if(!worker)return 1;
            Expect(ConfigurationReady(worker),"actual custom-drawn FPS control opens and dismisses its owned menu");
            RECT before{};GetWindowRect(worker,&before);
            DWORD pid{};GetWindowThreadProcessId(worker,&pid);
            Select(app);app.state_.selection.right+=80;app.state_.selection.bottom+=40;Open(app);Pump(80);
            HWND reused=WorkerWindow();DWORD reused_pid{};GetWindowThreadProcessId(reused,&reused_pid);RECT after{};GetWindowRect(reused,&after);
            Expect(reused==worker&&reused_pid==pid&&EqualRect(&before,&after)&&!app.active_,"existing preparation is raised without replacement or relocation for a different selection");
            Expect(ConfigurationReady(reused),"reused preparation retains working configuration controls");
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);PostMessageW(worker,WM_CLOSE,0,0);const auto end=GetTickCount64()+5000;while(app.recording_process_.Active()&&GetTickCount64()<end)Pump(40);
            Expect(!app.recording_process_.Active(),"preparation window closes without starting or saving a recording");if(process)CloseHandle(process);
        }
        app.Cancel(false);return failures?1:0;
    }
};
}
int main(int argc,char** argv){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;try{result=lumashot::RecordingEntryTest::Run(argc==2&&std::string_view(argv[1])=="--missing-worker");}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;}CoUninitialize();return result;}
