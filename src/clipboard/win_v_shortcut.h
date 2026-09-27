#pragma once
#include <windows.h>
#include <atomic>
#include <thread>
#include <utility>
namespace lumashot::clipboard {
// Only the configured Win+V chord is consumed; other keys continue unchanged.
struct WinVChord {
    bool held{};
    bool Consume(UINT key,bool down,bool win,bool other,bool allowed,bool& trigger){
        trigger=false;if(key!='V')return false;
        if(held){if(!down)held=false;return true;}
        if(down&&win&&!other&&allowed){held=true;trigger=true;return true;}
        return false;
    }
};
// Low-level keyboard hooks run on the installing thread and Windows drops them
// silently if a callback exceeds LowLevelHooksTimeout. The hook therefore lives
// on a dedicated thread with its own message loop, so a busy UI thread (render,
// capture hand-off, modal dialogs) cannot delay keyboard input or lose the hook.
// The callback touches no UI state: the owner installs the hook only while
// hotkeys are allowed and re-checks its live policy when WM_HOTKEY arrives.
class WinVShortcut {
    HWND window_{};std::thread thread_;DWORD thread_id_{};
    std::atomic<bool> allowed_{true};
    inline static thread_local WinVShortcut* active_{};
    inline static thread_local WinVChord chord_{};
    static LRESULT CALLBACK Hook(int code,WPARAM message,LPARAM data){
        auto* self=active_;
        if(code<0||!self)return CallNextHookEx(nullptr,code,message,data);
        const auto& key=*reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if(key.vkCode!='V')return CallNextHookEx(nullptr,code,message,data);
        const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
        const bool win=((GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000)!=0;
        const bool other=((GetAsyncKeyState(VK_CONTROL)|GetAsyncKeyState(VK_MENU)|GetAsyncKeyState(VK_SHIFT))&0x8000)!=0;
        bool trigger{};
        if(!chord_.Consume(key.vkCode,down,win,other,self->allowed_.load(std::memory_order_relaxed),trigger))return CallNextHookEx(nullptr,code,message,data);
        if(trigger){
            // Mark the Win chord as used so releasing Win cannot open Start.
            INPUT neutral[2]{};for(auto& input:neutral){input.type=INPUT_KEYBOARD;input.ki.wVk=0xe8;}
            neutral[1].ki.dwFlags=KEYEVENTF_KEYUP;SendInput(2,neutral,sizeof(INPUT));
            PostMessageW(self->window_,WM_HOTKEY,1,0);
        }
        return 1;
    }
    void Run(HANDLE ready,bool& installed){
        MSG msg{};PeekMessageW(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);  // queue exists before Reset can post WM_QUIT
        active_=this;chord_={};
        const HHOOK hook=SetWindowsHookExW(WH_KEYBOARD_LL,Hook,GetModuleHandleW(nullptr),0);
        installed=hook!=nullptr;SetEvent(ready);
        if(hook){while(GetMessageW(&msg,nullptr,0,0)>0)DispatchMessageW(&msg);UnhookWindowsHookEx(hook);}
        active_=nullptr;chord_={};
    }
public:
    WinVShortcut()=default;
    WinVShortcut(const WinVShortcut&)=delete;WinVShortcut& operator=(const WinVShortcut&)=delete;
    ~WinVShortcut(){Reset();}
    bool Installed()const{return thread_.joinable();}
    DWORD HookThreadId()const{return thread_id_;}
    // Cheap veto readable from the hook thread; false lets the system Win+V through.
    void SetAllowed(bool allowed){allowed_.store(allowed,std::memory_order_relaxed);}
    void Reset(){
        if(thread_.joinable()){while(!PostThreadMessageW(thread_id_,WM_QUIT,0,0)&&WaitForSingleObject(thread_.native_handle(),10)==WAIT_TIMEOUT){}thread_.join();}
        thread_id_=0;window_=nullptr;
    }
    bool Install(HWND window){
        Reset();if(!window)return false;
        const HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ready)return false;
        struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{ready};
        bool installed=false;window_=window;
        try{thread_=std::thread([this,ready,&installed]{Run(ready,installed);});}catch(...){window_=nullptr;return false;}
        thread_id_=GetThreadId(thread_.native_handle());
        WaitForSingleObject(ready,INFINITE);
        if(!installed){thread_.join();thread_id_=0;window_=nullptr;return false;}
        return true;
    }
};
}
