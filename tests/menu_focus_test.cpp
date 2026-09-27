#include "app/application.h"
#include <iostream>
#include <string>
namespace lumashot {
// A separate foreground process keeps a Win32 popup menu open (the situation
// with CAD context menus). The production hotkey path must still give the
// overlay keyboard focus so Esc cancels the session instead of reaching the
// covered program. Synthetic fixture windows only; no desktop pixels are kept.
struct MenuFocusTest {
    static inline Application* app{};
    static inline int failures{},phase{};
    static inline bool escaped{};
    static inline ULONGLONG next{},deadline{};
    static inline HWND fixture{},host{};
    static inline PROCESS_INFORMATION child{};
    static void Expect(bool condition,const char* label){std::cout<<(condition?"PASS ":"FAIL ")<<label<<'\n';if(!condition)++failures;}
    static void Finish(){KillTimer(fixture,1);PostMessageW(app->main_,WM_CLOSE,0,0);phase=9;}
    static void Tap(WORD vk){INPUT k[2]{};k[0].type=k[1].type=INPUT_KEYBOARD;k[0].ki.wVk=k[1].ki.wVk=vk;k[1].ki.dwFlags=KEYEVENTF_KEYUP;SendInput(2,k,sizeof(INPUT));}
    static bool InMenu(){if(!host)host=FindWindowW(L"LumaShot.MenuFocusHost",nullptr);if(!host)return false;GUITHREADINFO info{sizeof(info)};return GetGUIThreadInfo(GetWindowThreadProcessId(host,nullptr),&info)&&(info.flags&GUI_INMENUMODE);}
    static void Tick(){
        const auto now=GetTickCount64();if(now>deadline){Expect(false,"bounded completion");Finish();return;}
        if(!app->main_||now<next)return;
        if(phase==0){
            if(!InMenu()||GetForegroundWindow()!=host)return;
            Expect(true,"foreign process holds a popup menu in the foreground");
            SendMessageW(app->main_,WM_HOTKEY,1,0);phase=1;return;
        }
        if(phase==1){
            if(app->pending_||!app->active_||app->views_.empty())return;
            for(const auto& view:app->views_)if(!IsWindowVisible(view->window))return;
            HWND focus=GetForegroundWindow();bool ours=false;for(const auto& view:app->views_)ours|=view->window==focus;
            Expect(ours,"overlay takes foreground focus while a foreign menu is open");
            Expect(InMenu(),"foreign popup menu is still open when the overlay appears");
            Tap(VK_ESCAPE);phase=2;next=now+150;return;
        }
        if(phase==2){
            // Diagnostic sessions close the host window on cancel, so Run()
            // normally returns before this tick; it only runs if Esc was lost.
            Expect(false,"real Escape key cancels the capture session");Finish();
        }
    }
    static LRESULT CALLBACK FixtureProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_TIMER){Tick();return 0;}
        if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);const HBRUSH brush=CreateSolidBrush(RGB(0x23,0x47,0x65));FillRect(dc,&r,brush);DeleteObject(brush);EndPaint(window,&paint);return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
    static LRESULT CALLBACK HostProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_TIMER&&wp==1){KillTimer(window,1);
            const DWORD fg=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),me=GetCurrentThreadId();const bool attached=fg!=me&&AttachThreadInput(me,fg,TRUE);SetForegroundWindow(window);if(attached)AttachThreadInput(me,fg,FALSE);
            SetTimer(window,2,8000,nullptr);
            HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,10,L"Synthetic menu item");
            TrackPopupMenu(menu,TPM_RETURNCMD,240,240,0,window,nullptr);DestroyMenu(menu);PostQuitMessage(0);return 0;}
        if(message==WM_TIMER&&wp==2){KillTimer(window,2);PostMessageW(window,WM_CANCELMODE,0,0);return 0;}
        if(message==WM_CLOSE){PostMessageW(window,WM_CANCELMODE,0,0);DestroyWindow(window);return 0;}
        if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
    static int HostMain(){
        WNDCLASSW cls{};cls.lpfnWndProc=HostProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LumaShot.MenuFocusHost";cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
        const HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Synthetic menu host",WS_POPUP|WS_VISIBLE,200,200,300,200,nullptr,nullptr,cls.hInstance,nullptr);
        if(!window)return 2;SetTimer(window,1,200,nullptr);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
    }
    static int Run(){
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
        std::wstring command=L"\""+std::wstring(module)+L"\" --menu-host";STARTUPINFOW startup{sizeof(startup)};
        if(!CreateProcessW(module,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child)){std::cout<<"FAIL menu host process\n";return 1;}
        WNDCLASSW cls{};cls.lpfnWndProc=FixtureProc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"LumaShot.MenuFocusFixture";RegisterClassW(&cls);
        RECT r{};GetWindowRect(GetDesktopWindow(),&r);
        fixture=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Synthetic capture fixture",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,cls.hInstance,nullptr);
        ShowWindow(fixture,SW_SHOWNOACTIVATE);UpdateWindow(fixture);
        int result=1;
        {Application application;app=&application;next=GetTickCount64()+300;deadline=next+12000;SetTimer(fixture,1,20,nullptr);
            try{result=application.Run(false,false,true);}catch(const std::exception& error){std::cout<<error.what()<<'\n';++failures;}
            escaped=phase==2&&!application.active_&&application.views_.empty();KillTimer(fixture,1);}
        Expect(escaped,"real Escape key cancels the capture session");
        if(host&&IsWindow(host))PostMessageW(host,WM_CLOSE,0,0);
        if(WaitForSingleObject(child.hProcess,3000)!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,1);
        CloseHandle(child.hThread);CloseHandle(child.hProcess);DestroyWindow(fixture);
        const bool ok=result==0&&!failures&&escaped;
        std::cout<<(ok?"PASS":"FAIL")<<" overlay keeps keyboard focus and Escape over a foreign popup menu\n";return ok?0:1;
    }
};
}
int main(int argc,char** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--menu-host")return lumashot::MenuFocusTest::HostMain();
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=lumashot::MenuFocusTest::Run();CoUninitialize();return result;
}
