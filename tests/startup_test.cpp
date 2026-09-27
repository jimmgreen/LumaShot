#include "app/application.h"
#include "app/diagnostics.h"
#include <commctrl.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <array>
using namespace lumashot;
namespace {
std::atomic<HWND> overlay{};
std::atomic_bool done{};
std::atomic<double> posted{};
std::vector<double> latency;
int hover_count{}, hover_pending{},drag_count{},drag_pending{};
bool lens_tracks=true,lens_hidden=true;
bool sustained{},burst{};
std::array<std::atomic<double>,120> hover_posted{};
LRESULT CALLBACK ObserveInput(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    const LRESULT result=DefSubclassProc(window,message,wp,lp);
    const int index=int(LOWORD(lp))-250;
    if(message==WM_MOUSEMOVE&&!(wp&MK_LBUTTON)&&HIWORD(lp)==180&&
        (sustained?(index>=0&&index<120&&hover_posted[index]>0):index==0)) {
        ++hover_count;if(GetUpdateRect(window,nullptr,FALSE))++hover_pending;
        latency.push_back(Diagnostics::Now()-(sustained?hover_posted[index].load():posted.load()));
        const HWND lens=FindWindowW(L"STATIC",L"LumaShot.Magnifier");RECT monitor{},actual{};GetWindowRect(window,&monitor);
        const RECT expected=MagnifierBounds(monitor,{monitor.left+250+index,monitor.top+180});
        if(!lens||!IsWindowVisible(lens)||!GetWindowRect(lens,&actual)||!EqualRect(&actual,&expected))lens_tracks=false;
    }
    if(message==WM_MOUSEMOVE&&(wp&MK_LBUTTON)){++drag_count;if(GetUpdateRect(window,nullptr,FALSE))++drag_pending;}
    if(message==WM_LBUTTONDOWN){const HWND lens=FindWindowW(L"STATIC",L"LumaShot.Magnifier");if(lens&&IsWindowVisible(lens))lens_hidden=false;}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,ObserveInput,1);
    return result;
}
LRESULT CALLBACK Observe(int code,WPARAM wp,LPARAM lp) {
    if(code>=0) {
        const auto& message=*reinterpret_cast<CWPRETSTRUCT*>(lp);
        wchar_t name[64]{};GetClassNameW(message.hwnd,name,64);
        if(std::wstring(name)==L"LumaShot.Overlay") {
            if(message.message==WM_SHOWWINDOW&&message.wParam&&!overlay) {
                SetWindowSubclass(message.hwnd,ObserveInput,1,0);overlay=message.hwnd;
            }
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
}
int main(int argc,char** argv) {
    const bool baseline=argc>1&&std::string(argv[1])=="--baseline";
    sustained=argc>1&&std::string(argv[1])=="--hover";
    burst=argc>1&&std::string(argv[1])=="--burst";
    const auto path=std::filesystem::current_path()/(baseline?L"startup-before.csv":(sustained?L"hover-after.csv":(burst?L"burst-after.csv":L"startup-after.csv")));
    SetEnvironmentVariableW(L"LUMASHOT_TRACE",path.c_str());
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    const DWORD main_thread=GetCurrentThreadId();
    const HHOOK hook=SetWindowsHookExW(WH_CALLWNDPROCRET,Observe,nullptr,main_thread);
    if(!hook)return 2;
    std::jthread driver([&] {
        const auto deadline=GetTickCount64()+15000;
        while(!overlay&&GetTickCount64()<deadline)Sleep(1);
        if(const HWND window=overlay.load()) {
            // Start on WM_SHOWWINDOW: deliberately do not wait for initialization,
            // a warm-up paint, focus, or a synchronous SendMessage round trip.
            posted=Diagnostics::Now();
            if(sustained) {
                for(int i=0;i<120;++i){hover_posted[i]=Diagnostics::Now();PostMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(250+i,180));Sleep(8);}
            } else {
                for(int i=0;i<4;++i)PostMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(250,180));
                PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(80,80));
                for(int i=1;i<=40;++i){PostMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(80+i*12,80+i*7));if(!burst)Sleep(8);}
                PostMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(560,360));
                Sleep(250); // The final release must be presented without another mouse move.
            }
            PostMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
        }
        while(!done&&GetTickCount64()<deadline)Sleep(10);
        if(!done)PostThreadMessageW(main_thread,WM_QUIT,1,0);
    });
    int result=1;
    try{Application app;result=app.Run(true,true);}
    catch(const std::exception& e){std::cout<<e.what()<<'\n';}
    done=true;driver.join();UnhookWindowsHookEx(hook);Diagnostics::Get().Flush();
    int initialization_count{},visible_initializations{},paint_count{},lens_count{},released_frames{},presented_frames{};
    std::ifstream input(path);std::string row;
    while(std::getline(input,row)) {
        if(row.starts_with("render_initialize,")){++initialization_count;if(row.substr(row.rfind(',')+1)=="1")++visible_initializations;}
        if(row.starts_with("paint,"))++paint_count;
        if(row.starts_with("magnifier_update,"))++lens_count;
        if(row.starts_with("present,")){++presented_frames;if(row.substr(row.rfind(',')+1)=="2")++released_frames;}
    }
    std::cout<<"Initialization while visible: "<<visible_initializations<<'/'<<initialization_count
        <<"; hover paints pending after input: "<<hover_pending<<'/'<<hover_count
        <<"; drag paints pending: "<<drag_pending<<'/'<<drag_count
        <<"; lens tracking/hides on drag: "<<lens_tracks<<'/'<<lens_hidden
        <<"; full paints/lens updates: "<<paint_count<<'/'<<lens_count
        <<"; presented/final-release frames: "<<presented_frames<<'/'<<released_frames
        <<"; initial posted hover delay ms: "<<(latency.empty()?-1:latency.front())<<'\n';
    if(!latency.empty()){std::sort(latency.begin(),latency.end());std::cout<<"Posted hover to paint-complete ms: p95="<<latency[(latency.size()-1)*95/100]<<" max="<<latency.back()<<'\n';}
    // Timing is reported without a hardware-dependent pass/fail threshold.
    const bool pass=result==0&&hover_count==(sustained?120:4)&&initialization_count>0&&visible_initializations==0&&hover_pending==0
        &&drag_count==(sustained?0:40)&&drag_pending==0;
    const bool lens_pass=lens_tracks&&lens_hidden&&lens_count>=hover_count&&(!sustained||paint_count<=initialization_count*2+4)
        &&(sustained||released_frames>0)&&(!burst||presented_frames<40);
    std::cout<<(pass&&lens_pass?"PASS":"FAIL")<<" cold overlay preparation and first hover/drag\n";
    CoUninitialize();return (pass&&lens_pass)||baseline?0:1;
}
