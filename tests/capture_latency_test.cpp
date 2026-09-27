#include "app/application.h"
#include "app/diagnostics.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <psapi.h>
#include <iostream>
#include <algorithm>

namespace lumashot {
// Exercises the actual hotkey/capture path behind opaque, owned fixtures on
// every monitor. It never writes desktop pixels, preferences or clipboard data.
struct CaptureLatencyTest {
    struct Memory {SIZE_T private_bytes{},working_set{};DWORD gdi{},user{};};
    static inline Application* app{};
    static inline int failures{},cycles{},phase{};
    static inline ULONGLONG next{},deadline{},idle_start{},cpu_start{};
    static inline double started{};
    static inline std::vector<RECT> monitors;
    static inline std::vector<double> latency;
    static inline std::vector<HWND> fixtures;
    static inline HWND button{};
    static inline Memory baseline{},warmed{},peak{};
    static constexpr uint32_t color=0xff234765;
    static void Expect(bool condition,const char* label){if(!condition){++failures;std::cout<<"FAIL "<<label<<'\n';}}
    static Memory Sample(){
        PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
        GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters));
        return {counters.PrivateUsage,counters.WorkingSetSize,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)};
    }
    static ULONGLONG Cpu(){FILETIME created{},exited{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
        ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;}
    static void Finish(){KillTimer(fixtures.front(),1);PostMessageW(app->main_,WM_CLOSE,0,0);phase=3;}
    static void Tick(){
        const auto now=GetTickCount64();if(now>deadline){std::cout<<"phase="<<phase<<" active="<<app->active_<<" pending="<<app->pending_<<" views="<<app->views_.size()<<" cycles="<<cycles<<'\n';Expect(false,"bounded capture completion");Finish();return;}
        if(!app->main_||now<next)return;
        const HWND popup=GetLastActivePopup(app->main_);wchar_t popup_class[32]{};
        if(popup!=app->main_&&IsWindowVisible(popup)&&GetClassNameW(popup,popup_class,32)&&std::wstring(popup_class)==L"LumaShot.ThemedMessage"){
            EnumChildWindows(popup,[](HWND child,LPARAM)->BOOL{char text[1024]{};if(GetWindowTextA(child,text,1024))std::cerr<<"capture dialog: "<<text<<'\n';return TRUE;},0);
            Expect(false,"capture reported an error");PostMessageW(popup,WM_KEYDOWN,VK_RETURN,0);Finish();return;
        }
        if(phase==0){
            if(app->pending_)return;
            const auto memory=Sample();
            if(!cycles)baseline=memory;
            else {
                std::cout<<"idle_cycle="<<cycles<<" private_MiB="<<double(memory.private_bytes)/1048576<<" working_MiB="<<double(memory.working_set)/1048576<<" gdi="<<memory.gdi<<" user="<<memory.user<<'\n';
                Expect(!app->frame_&&!app->acrylic_&&app->views_.empty()&&app->element_regions_.empty(),"session image and region resources released");
                if(cycles==1)warmed=memory;
                // Input/accessibility libraries can create a few lazy helper
                // windows after first focus; repeated overlays must stay bounded.
                else Expect(memory.gdi<=warmed.gdi+4&&memory.user<=warmed.user+4,"no accumulating window/GDI resources after first use");
            }
            if(cycles==6){
                Expect(memory.private_bytes<=warmed.private_bytes+16*1024*1024,"repeated captures keep idle private memory bounded after first use");
                std::cout<<"idle_cpu_ms="<<double(Cpu()-cpu_start)/10000<<" idle_wall_ms="<<now-idle_start
                    <<" idle_private_MiB="<<double(memory.private_bytes)/1048576<<" idle_working_MiB="<<double(memory.working_set)/1048576
                    <<" initial_private_MiB="<<double(baseline.private_bytes)/1048576<<" active_private_MiB="<<double(peak.private_bytes)/1048576
                    <<" active_working_MiB="<<double(peak.working_set)/1048576<<'\n';
                phase=4;SendMessageW(app->main_,WM_HOTKEY,1,0);app->Cancel(false);next=GetTickCount64()+100;return;
            }
            SetCursor(LoadCursorW(nullptr,IDC_ARROW));app->preferences_.include_cursor=cycles%2!=0;app->preferences_.theme=1;started=Diagnostics::Now();
            // Deliberately use the production WM_HOTKEY dispatch.
            phase=1;SendMessageW(app->main_,WM_HOTKEY,1,0);
            Expect(app->capture_started_>0,"hotkey dispatch reaches capture start");
        }else if(phase==1&&app->active_&&!app->views_.empty()){
            for(const auto& view:app->views_)if(!IsWindowVisible(view->window))return;
            latency.push_back(Diagnostics::Now()-started);
            const auto memory=Sample();peak.private_bytes=std::max(peak.private_bytes,memory.private_bytes);peak.working_set=std::max(peak.working_set,memory.working_set);
            Expect(bool(app->frame_),"real capture delivered a frame");
            if(app->frame_)for(const auto r:monitors){const int x=r.left+12-app->frame_->bounds.left,y=r.bottom-12-app->frame_->bounds.top;
                Expect(app->frame_->pixels[static_cast<size_t>(y)*app->frame_->Width()+x]==color,"real capture reads only opaque synthetic fixture pixels");}
            const auto& view=*app->views_.front();
            const Point a{float(view.bounds.left+120),float(view.bounds.top+100)},b{a.x+180,a.y+100};
            app->PointerDown(*app->views_.front(),a);app->PointerMove(*app->views_.front(),b,MK_LBUTTON);app->PointerUp(*app->views_.front(),b);
            Expect(app->state_.selected&&!app->state_.dragging&&app->state_.selection.right-app->state_.selection.left==180,"first visible frame accepts immediate drag");
            // Keep the first session briefly for the asynchronous control scan.
            next=now+(cycles==0?750:30);phase=2;
        }else if(phase==4){
            if(app->pending_)return;
            Expect(!app->active_&&!app->frame_&&app->views_.empty()&&app->element_regions_.empty(),"canceled preparation cannot reopen an overlay or retain its frame");Finish();
        }else if(phase==2){
            if(cycles==0){
                RECT bounds{};GetWindowRect(button,&bounds);const POINT point{(bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2};
                const auto selected=PixelRect(app->WindowAt(point));
                Expect(EqualRect(&selected,&bounds)!=FALSE,"production capture can snap to a synthetic button");
                Expect(app->state_.selection.right-app->state_.selection.left==180,"late discovery does not replace a completed selection");
                std::cout<<"detected_regions="<<app->element_regions_.size()<<'\n';
            }
            app->Cancel(false);++cycles;phase=0;next=now+300;idle_start=now;cpu_start=Cpu();
        }
    }
    static LRESULT CALLBACK FixtureProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_TIMER){Tick();return 0;}
        if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);const HBRUSH brush=CreateSolidBrush(RGB(0x23,0x47,0x65));FillRect(dc,&r,brush);DeleteObject(brush);EndPaint(window,&paint);return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
    static int Run(){
        WNDCLASSW cls{};cls.lpfnWndProc=FixtureProc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"LumaShot.LatencyFixture";RegisterClassW(&cls);
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR,HDC,LPRECT r,LPARAM)->BOOL{monitors.push_back(*r);return TRUE;},0);
        for(const auto r:monitors){const HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Synthetic capture fixture",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,cls.hInstance,nullptr);fixtures.push_back(window);ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);}
        button=CreateWindowExW(0,L"BUTTON",L"Synthetic button",WS_CHILD|WS_VISIBLE,30,30,160,40,fixtures.front(),nullptr,cls.hInstance,nullptr);
        CreateWindowExW(0,L"EDIT",L"Synthetic text",WS_CHILD|WS_VISIBLE|WS_BORDER,30,80,200,40,fixtures.front(),nullptr,cls.hInstance,nullptr);
        DwmFlush();
        int result=1;
        {Application application;app=&application;next=GetTickCount64()+300;deadline=next+15000;SetTimer(fixtures.front(),1,10,nullptr);
            try{result=application.Run(false,false,true);}catch(const std::exception& error){std::cout<<error.what()<<'\n';++failures;}}
        for(HWND window:fixtures)DestroyWindow(window);
        if(!latency.empty()){std::cout<<"hotkey_to_interactive_ms cold="<<latency.front()<<" repeats=";for(size_t i=1;i<latency.size();++i)std::cout<<latency[i]<<',';std::cout<<'\n';}
        std::cout<<(result==0&&!failures&&cycles==6?"PASS":"FAIL")<<" real capture startup, immediate interaction, controls, and resource release\n";
        return result==0&&!failures&&cycles==6?0:1;
    }
};
}
int main(){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    const int result=lumashot::CaptureLatencyTest::Run();CoUninitialize();return result;}
