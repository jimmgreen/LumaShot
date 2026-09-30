#include "app/application.h"
namespace lumashot {
void Application::ConfigureHotkeyPolicy(){
    KillTimer(main_,20);
    if(preferences_.disable_hotkeys_in_game&&!preferences_.hotkeys_disabled)SetTimer(main_,20,500,nullptr);
    RefreshHotkeys();
}
void Application::RefreshHotkeys(){
    const bool suspended=HotkeysSuspended(preferences_,preferences_.disable_hotkeys_in_game&&!preferences_.hotkeys_disabled&&game_active_());
    if(!hotkeys_initialized_||suspended!=hotkeys_suspended_){
        for(int id=1;id<=4;++id)UnregisterHotKey(main_,id);
        hotkeys_initialized_=true;hotkeys_suspended_=suspended;
        bool failed=false;
        if(!suspended){const auto keys=Shortcuts(preferences_);for(int i=0;i<4;++i)if(keys[i].key&&!RegisterHotKey(main_,i+1,keys[i].modifiers|MOD_NOREPEAT,keys[i].key))failed=true;}
        if(failed){
            // Never steal focus with a modal dialog during automatic recovery.
            NOTIFYICONDATAW info{sizeof(info)};info.hWnd=main_;info.uID=1;info.uFlags=NIF_INFO;info.dwInfoFlags=NIIF_WARNING;
            wcscpy_s(info.szInfoTitle,L"LumaShot 快捷键");wcscpy_s(info.szInfo,L"部分快捷键已被其他程序占用，可从托盘操作或在设置中更换快捷键。");Shell_NotifyIconW(NIM_MODIFY,&info);
        }
    }
    if(clipboard_panel_)clipboard_panel_->SetHotkeysSuspended(suspended);
}
void Application::ToggleHotkeyPolicy(UINT choice){
    auto next=preferences_;
    if(choice==GameHotkeyMenu)next.disable_hotkeys_in_game=!next.disable_hotkeys_in_game;
    else if(choice==DisableHotkeyMenu)next.hotkeys_disabled=!next.hotkeys_disabled;
    else return;
    settings_writer_.Request(next);
    if(!settings_writer_.Flush()){settings_writer_.DropPending();Notice(L"快捷键设置保存失败，未更改当前选项。");return;}
    preferences_=std::move(next);ConfigureHotkeyPolicy();
}
}