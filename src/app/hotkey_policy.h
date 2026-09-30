#pragma once
#include "app/hotkeys.h"
#include <shellapi.h>
namespace lumashot {
constexpr UINT LaunchCommandMessage=WM_APP+86;
constexpr UINT GameHotkeyMenu=9,DisableHotkeyMenu=10;
// Deliberately not QUNS_BUSY, PRESENTATION_MODE or APP: ordinary fullscreen
// windows and presentations are not games. Windows' D3D classification is only
// a best-effort signal; borderless games may not be reported by the shell.
inline bool GameNotificationState(HRESULT result,QUERY_USER_NOTIFICATION_STATE state){
    return SUCCEEDED(result)&&state==QUNS_RUNNING_D3D_FULL_SCREEN;
}
inline bool FullScreenGameActive(){
    QUERY_USER_NOTIFICATION_STATE state=QUNS_ACCEPTS_NOTIFICATIONS;
    const HRESULT result=SHQueryUserNotificationState(&state);
    if(!GameNotificationState(result,state))return false;
    HWND foreground=GetForegroundWindow();DWORD process{};
    if(!foreground||!IsWindowVisible(foreground)||IsIconic(foreground)||foreground==GetShellWindow()||foreground==GetDesktopWindow())return false;
    GetWindowThreadProcessId(foreground,&process);if(process==GetCurrentProcessId())return false;
    RECT bounds{};MONITORINFO monitor{sizeof(monitor)};
    if(!GetWindowRect(foreground,&bounds)||!GetMonitorInfoW(MonitorFromWindow(foreground,MONITOR_DEFAULTTONEAREST),&monitor))return false;
    return bounds.left<=monitor.rcMonitor.left&&bounds.top<=monitor.rcMonitor.top&&bounds.right>=monitor.rcMonitor.right&&bounds.bottom>=monitor.rcMonitor.bottom;
}
inline bool HotkeysSuspended(const Preferences& p,bool game){return p.hotkeys_disabled||(p.disable_hotkeys_in_game&&game);}
inline Preferences EffectiveHotkeys(Preferences p,bool suspended){if(suspended)p.key=p.gif_key=p.video_key=p.translate_key=0;return p;}
inline bool AllowLaunch(UINT message,bool suspended){return message==LaunchCommandMessage||(message==WM_HOTKEY&&!suspended);}
inline void AppendHotkeyPolicyMenu(HMENU menu,const Preferences& p){
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(p.disable_hotkeys_in_game?MF_CHECKED:0),GameHotkeyMenu,L"全屏游戏时禁用快捷键");
    AppendMenuW(menu,MF_STRING|(p.hotkeys_disabled?MF_CHECKED:0),DisableHotkeyMenu,L"禁用快捷键");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
}
}