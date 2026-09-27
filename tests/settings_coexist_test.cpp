#include "app/application.h"
#include "app/settings_process.h"
#include "export/png.h"
#include <tlhelp32.h>
#include <iostream>
namespace lumashot {
struct SettingsCoexistTest {
    inline static Application* app{};
    inline static HWND fixture{},settings{};
    inline static int phase{},moves{},failures{};
    inline static ULONGLONG deadline{},paint_deadline{};
    inline static Application::View* view{};
    inline static RECT settings_bounds{};
    static void Expect(bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;failures+=!ok;}
    static HWND ChildSettings(){
        std::vector<DWORD> children;ocr::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W entry{sizeof(entry)};
        if(Process32FirstW(snapshot.Get(),&entry))do{if(entry.th32ParentProcessID==GetCurrentProcessId()&&std::wstring(entry.szExeFile)==L"LumaShot.exe")children.push_back(entry.th32ProcessID);}while(Process32NextW(snapshot.Get(),&entry));
        struct Search{const std::vector<DWORD>& children;HWND result{};} search{children};
        EnumWindows([](HWND window,LPARAM data)->BOOL{auto& s=*reinterpret_cast<Search*>(data);DWORD pid{};GetWindowThreadProcessId(window,&pid);
            if(std::find(s.children.begin(),s.children.end(),pid)!=s.children.end()&&GetDlgItem(window,103)&&IsWindowVisible(window)){s.result=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.result;
    }
    static void Foreground(HWND window){
        const DWORD foreground=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),current=GetCurrentThreadId();
        const bool attached=foreground&&foreground!=current&&AttachThreadInput(current,foreground,TRUE);SetForegroundWindow(window);if(attached)AttachThreadInput(current,foreground,FALSE);
    }
    static void Tap(WORD code){INPUT keys[2]{};keys[0].type=keys[1].type=INPUT_KEYBOARD;keys[0].ki.wVk=keys[1].ki.wVk=code;keys[1].ki.dwFlags=KEYEVENTF_KEYUP;Expect(SendInput(2,keys,sizeof(INPUT))==2,"input injected into synthetic settings window");}
    static void Shortcut(){
        INPUT keys[8]{};const WORD codes[]={VK_LCONTROL,VK_LMENU,VK_LSHIFT,VK_F9,VK_F9,VK_LSHIFT,VK_LMENU,VK_LCONTROL};
        for(int i=0;i<8;++i){keys[i].type=INPUT_KEYBOARD;keys[i].ki.wVk=codes[i];keys[i].ki.dwFlags=i>=4?KEYEVENTF_KEYUP:0;}
        Expect(SendInput(8,keys,sizeof(INPUT))==8,"real registered screenshot shortcut sent while settings is foreground");
    }
    static LPARAM PointAt(int step){
        const int x=settings_bounds.left+12+(settings_bounds.right-settings_bounds.left-24)*step/30-view->bounds.left;
        const int y=settings_bounds.top+12+(settings_bounds.bottom-settings_bounds.top-24)*step/30-view->bounds.top;
        return MAKELPARAM(x,y);
    }
    static void Finish(){
        phase=99;if(settings&&IsWindow(settings))PostMessageW(settings,WM_COMMAND,IDCANCEL,0);
        if(app->active_||app->pending_)app->Cancel(false);
        PostMessageW(app->main_,WM_CLOSE,0,0);
    }
    static void Tick(){
        if(!app||!app->main_||phase==99)return;
        if(GetTickCount64()>deadline){Expect(false,"settings coexistence finishes within deadline");Finish();return;}
        if(phase==0){
            app->preferences_.theme=1;app->preferences_.include_cursor=false;app->preferences_.modifiers=MOD_CONTROL|MOD_ALT|MOD_SHIFT;app->preferences_.key=VK_F9;
            if(!RegisterHotKey(app->main_,1,app->preferences_.modifiers|MOD_NOREPEAT,VK_F9)){Expect(false,"synthetic test shortcut is available");Finish();return;}
            phase=1;app->Settings();
            Expect(phase==7&&!app->settings_open_&&!SettingsShortcutRecording(),"closing settings releases scoped input guard");
            Expect(app->preferences_.pin_style==DefaultPinStyle,"cancel preserves saved preference instead of applying draft");
            Finish();return;
        }
        if(phase==1){
            settings=ChildSettings();if(!settings)return;
            // The fixture covers the entire virtual desktop, including the taskbar.
            // Only our settings child is raised above it; no personal pixels are saved.
            SetWindowPos(settings,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);Foreground(settings);
            if(GetForegroundWindow()!=settings){Expect(false,"settings obtains foreground for real shortcut test");Finish();return;}
            Expect(IsWindowEnabled(app->main_)&&!SettingsShortcutRecording(),"settings leaves host enabled and normal capture unblocked");
            SendMessageW(settings,WM_COMMAND,124,0);GetWindowRect(settings,&settings_bounds);
            phase=2;Shortcut();return;
        }
        if(phase==2){
            if(app->pending_||!app->active_||app->views_.empty())return;
            Expect(app->settings_open_&&IsWindow(settings)&&IsWindowVisible(settings),"capture starts without hiding or closing settings");
            const POINT middle{(settings_bounds.left+settings_bounds.right)/2,(settings_bounds.top+settings_bounds.bottom)/2};
            for(auto& candidate:app->views_)if(PtInRect(&candidate->bounds,middle))view=candidate.get();
            if(!view){Expect(false,"settings lies on a captured monitor");Finish();return;}
            Expect(GetForegroundWindow()==view->window&&IsWindowEnabled(view->window),"capture overlay owns keyboard focus and is enabled");
            RECT crop{};IntersectRect(&crop,&settings_bounds,&app->frame_->bounds);auto captured=Crop(*app->frame_,crop);
            bool varied=false;for(auto pixel:captured.pixels)if(pixel!=captured.pixels.front()){varied=true;break;}
            Expect(varied&&captured.Width()>300&&captured.Height()>500,"frozen screenshot contains the settings window");
            SavePng(captured,L"build/settings-captured-preview.png");
            SendMessageW(view->window,WM_LBUTTONDOWN,MK_LBUTTON,PointAt(0));moves=0;phase=3;paint_deadline=GetTickCount64()+1500;return;
        }
        if(phase==3){
            if(view->needs_paint||GetUpdateRect(view->window,nullptr,FALSE)){
                if(GetTickCount64()>paint_deadline){Expect(false,"capture frames keep presenting inside settings wait");Finish();}return;
            }
            if(moves<30){SendMessageW(view->window,WM_MOUSEMOVE,MK_LBUTTON,PointAt(++moves));paint_deadline=GetTickCount64()+500;return;}
            SendMessageW(view->window,WM_LBUTTONUP,0,PointAt(30));
            Expect(app->state_.selected&&app->state_.selection.right-app->state_.selection.left>200,"drag selection renders through thirty frame-ready updates");
            app->Cancel(false);Expect(!app->active_&&IsWindow(settings),"canceling capture leaves the original settings window alive");
            Foreground(settings);SendMessageW(settings,WM_COMMAND,103,0);
            Expect(SettingsShortcutRecording(),"real child publishes shortcut recording state synchronously");
            for(int id=1;id<=3;++id)SendMessageW(app->main_,WM_HOTKEY,id,0);
            Expect(!app->active_&&!app->pending_&&!app->recording_process_.Active(),"screenshot GIF and video triggers are ignored only during shortcut recording");
            phase=4;Tap(VK_ESCAPE);return;
        }
        if(phase==4){
            if(SettingsShortcutRecording())return;
            Expect(IsWindow(settings),"Escape exits shortcut recording without closing settings");
            phase=5;SendMessageW(app->main_,WM_HOTKEY,1,0);return;
        }
        if(phase==5){
            if(app->pending_||!app->active_)return;
            Expect(true,"capture works again immediately after recording ends");app->Cancel(false);
            phase=6;Foreground(settings);SendMessageW(settings,WM_COMMAND,119,0);
            Expect(SettingsShortcutRecording(),"GIF shortcut editing also activates input guard");
            // Focus loss aborts recording and restores its uncommitted chord.
            Foreground(fixture);return;
        }
        if(phase==6){
            if(SettingsShortcutRecording())return;
            Expect(true,"focus loss clears shortcut recording guard");phase=7;PostMessageW(settings,WM_COMMAND,IDCANCEL,0);
        }
    }
    static LRESULT CALLBACK Fixture(HWND window,UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_TIMER){try{Tick();}catch(const std::exception& error){std::cout<<error.what()<<std::endl;Expect(false,"bounded test execution");Finish();}return 0;}
        if(message==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);const HBRUSH brush=CreateSolidBrush(RGB(35,71,101));FillRect(dc,&r,brush);DeleteObject(brush);EndPaint(window,&ps);return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
    static int Run(){
        WNDCLASSW wc{};wc.lpfnWndProc=Fixture;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumaShot.SettingsFixture";RegisterClassW(&wc);
        fixture=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Synthetic settings capture fixture",WS_POPUP,
            GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN),nullptr,nullptr,wc.hInstance,nullptr);
        ShowWindow(fixture,SW_SHOW);UpdateWindow(fixture);int result=1;
        {Application application;app=&application;deadline=GetTickCount64()+30000;SetTimer(fixture,1,30,nullptr);result=application.Run(false,false,true);KillTimer(fixture,1);}
        app=nullptr;DestroyWindow(fixture);return failures||result?1:0;
    }
};
}
int main(){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const auto result=lumashot::SettingsCoexistTest::Run();CoUninitialize();return result;}
