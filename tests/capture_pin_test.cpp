#include "app/application.h"
#include <commctrl.h>
#include <iostream>
#include <atomic>
#include <chrono>
#include <algorithm>
using namespace lumashot;
namespace lumashot {
struct CapturePinTest {
    static LPARAM OcrButton(HWND editor) {
        const auto* view=reinterpret_cast<Application::View*>(GetWindowLongPtrW(editor,GWLP_USERDATA));
        const auto button=view->app->state_.toolbar.Button(15);
        return MAKELPARAM(static_cast<short>((button.left+button.right)/2-view->bounds.left),static_cast<short>((button.top+button.bottom)/2-view->bounds.top));
    }
};
}
static HWND OwnWindow(const wchar_t* name) {
    struct Search {const wchar_t* name;HWND result;};Search search{name,nullptr};
    EnumWindows([](HWND window,LPARAM value)->BOOL{auto& s=*reinterpret_cast<Search*>(value);DWORD pid{};GetWindowThreadProcessId(window,&pid);wchar_t cls[128]{};GetClassNameW(window,cls,128);if(pid==GetCurrentProcessId()&&std::wstring(cls)==s.name){s.result=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.result;
}
int main(int argc,char** argv) {
    const bool recognize=argc>1&&std::string_view(argv[1])=="--ocr";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    std::atomic_bool passed{};const DWORD main_thread=GetCurrentThreadId();
    std::jthread driver([&]{
        HWND overlay{};const auto end=GetTickCount64()+10000;
        while(!overlay&&GetTickCount64()<end){auto candidate=OwnWindow(L"LumaShot.Overlay");if(candidate&&IsWindowVisible(candidate))overlay=candidate;else Sleep(10);}
        if(overlay){
            SendMessageW(overlay,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(0,0));
            std::vector<double> latency;bool painted=true;
            for(int i=1;i<=120;++i){auto start=std::chrono::steady_clock::now();
                SendMessageW(overlay,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(i*1279/120,i*719/120));
                // Rendering is scheduled by frame readiness, not guaranteed to finish
                // inside SendMessage. Observe the real completion without forcing paint.
                const auto paint_deadline=GetTickCount64()+250;
                while(GetUpdateRect(overlay,nullptr,FALSE)&&GetTickCount64()<paint_deadline)Sleep(1);
                latency.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
                painted=painted&&!GetUpdateRect(overlay,nullptr,FALSE);
                if(!painted)break;
            }
            SendMessageW(overlay,WM_LBUTTONUP,0,MAKELPARAM(1279,719));
            std::sort(latency.begin(),latency.end());std::cout<<"Synthetic drag event-to-present p95 ms: "<<latency[std::min(size_t(113),latency.size()-1)]<<"; paints completed: "<<painted<<'\n';
            if(!painted){PostMessageW(OwnWindow(L"LumaShot.Host"),WM_CLOSE,0,0);return;}
            const float scale=float(GetDpiForWindow(overlay))/96;
            auto layout=PlaceToolbar({0,0,1279,719},{0,0,1280,800},scale,0);
            const auto click=[&](Box b){auto point=MAKELPARAM(static_cast<short>((b.left+b.right)/2),static_cast<short>((b.top+b.bottom)/2));SendMessageW(overlay,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(overlay,WM_LBUTTONUP,0,point);};
            click(layout.Button(1));Sleep(220);
            // Reverse an in-flight collapse, then allow the final expansion to finish.
            SendMessageW(overlay,WM_KEYDOWN,'V',0);Sleep(35);SendMessageW(overlay,WM_KEYDOWN,'R',0);Sleep(220);
            layout=PlaceToolbar({0,0,1279,719},{0,0,1280,800},scale,1);click(layout.Property(42));
            for(int id:{46,47}) {
                const auto track=ui::SliderTrack(layout.Property(id),scale);const auto startPoint=MAKELPARAM(static_cast<short>(track.left),static_cast<short>(track.top));
                SendMessageW(overlay,WM_LBUTTONDOWN,MK_LBUTTON,startPoint);
                GUITHREADINFO info{sizeof(info)};GetGUIThreadInfo(GetWindowThreadProcessId(overlay,nullptr),&info);
                if(info.hwndCapture!=overlay){std::cout<<"FAIL slider capture "<<id<<'\n';PostMessageW(OwnWindow(L"LumaShot.Host"),WM_CLOSE,0,0);return;}
                const auto endPoint=MAKELPARAM(static_cast<short>(track.right+100),static_cast<short>(track.top));
                SendMessageW(overlay,WM_MOUSEMOVE,MK_LBUTTON,endPoint);SendMessageW(overlay,WM_LBUTTONUP,0,endPoint);
                GetGUIThreadInfo(GetWindowThreadProcessId(overlay,nullptr),&info);
                if(info.hwndCapture==overlay){PostMessageW(OwnWindow(L"LumaShot.Host"),WM_CLOSE,0,0);return;}
            }
            click(layout.Button(4));layout=PlaceToolbar({0,0,1279,719},{0,0,1280,800},scale,1,Tool::Pen);click(layout.Property(28));
            ColorPicker picker;picker.Open(0xffff4d4f,layout.Property(28),{0,0,1280,800},scale);
            click(picker.Part(3));for(wchar_t c:std::wstring(L"#12AB34"))SendMessageW(overlay,WM_CHAR,c,0);
            SendMessageW(overlay,WM_KEYDOWN,VK_ESCAPE,0);
            if(!IsWindow(overlay)){PostThreadMessageW(main_thread,WM_QUIT,1,0);return;}
            click(layout.Property(28));click(picker.Part(0));click(picker.Part(2));
            auto button=layout.Button(recognize?15:13);
            SendMessageW(overlay,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(static_cast<short>((button.left+button.right)/2),static_cast<short>((button.top+button.bottom)/2)));
            HWND pin{};while(!pin&&GetTickCount64()<end){pin=OwnWindow(L"LumaShot.Pin");Sleep(10);}
            if(pin){Sleep(500);passed=IsWindow(pin)&&!OwnWindow(L"LumaShot.Overlay");
                SendMessageW(pin,WM_KEYDOWN,VK_SPACE,0);auto editor=OwnWindow(L"LumaShot.Overlay");
                passed=passed&&editor&&IsWindowVisible(editor);
                if(editor){
                    HRGN region=CreateRectRgn(0,0,0,0);passed=passed&&GetWindowRgn(editor,region)!=ERROR;DeleteObject(region);
                    SendMessageW(editor,WM_KEYDOWN,'R',0);Sleep(220);
                    POINT a{100,100},b{220,180};ScreenToClient(editor,&a);ScreenToClient(editor,&b);
                    SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(a.x,a.y));SendMessageW(editor,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(b.x,b.y));SendMessageW(editor,WM_LBUTTONUP,0,MAKELPARAM(b.x,b.y));
                    const auto ocr=CapturePinTest::OcrButton(editor);
                    SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,ocr);
                    SendMessageW(editor,WM_LBUTTONUP,0,ocr);
                    const auto done=GetTickCount64()+5000;while(IsWindow(editor)&&GetTickCount64()<done)Sleep(10);
                    passed=passed&&!IsWindow(editor)&&IsWindow(pin);
                    SendMessageW(pin,WM_KEYDOWN,VK_SPACE,0);editor=OwnWindow(L"LumaShot.Overlay");passed=passed&&editor;
                    if(editor)SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);
                    passed=passed&&IsWindow(pin)&&!OwnWindow(L"LumaShot.Overlay");
                }
                SendMessageW(pin,WM_CLOSE,0,0);
            }
        }
        if(auto host=OwnWindow(L"LumaShot.Host"))PostMessageW(host,WM_CLOSE,0,0);else PostThreadMessageW(main_thread,WM_QUIT,1,0);
    });
    int result=1;try{Application app;app.Run(true,true);result=passed?0:1;}catch(const std::exception& e){std::cout<<e.what()<<'\n';}
    driver.join();std::cout<<(passed?"PASS":"FAIL")<<" synthetic capture toolbar creates persistent pin and closes overlay\n";CoUninitialize();return result;
}

