#include "app/application.h"
#include <iostream>
namespace lumashot {
struct HotkeyPolicyTest {
static int Run(){
    int failures=0;const auto expect=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;failures+=!ok;};
    Preferences defaults;expect(!defaults.hotkeys_disabled&&!defaults.disable_hotkeys_in_game,"both tray switches default off");
    for(int value=0;value<=6;++value)expect(GameNotificationState(S_OK,static_cast<QUERY_USER_NOTIFICATION_STATE>(value))==(value==QUNS_RUNNING_D3D_FULL_SCREEN),"only shell D3D full screen state matches, not busy/presentation/fullscreen app");
    expect(!GameNotificationState(E_FAIL,QUNS_RUNNING_D3D_FULL_SCREEN),"failed detection does not disable shortcuts");
    const auto path=std::filesystem::temp_directory_path()/(L"LumaShot-hotkey-policy-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}}cleanup{path};
    for(bool manual:{false,true})for(bool automatic:{false,true}){
        auto pref=defaults;pref.hotkeys_disabled=manual;pref.disable_hotkeys_in_game=automatic;pref.SaveTo(path);const auto loaded=Preferences::LoadFrom(path);
        expect(loaded.hotkeys_disabled==manual&&loaded.disable_hotkeys_in_game==automatic,"both policy switches persist independently");
        expect(Shortcuts(loaded)==Shortcuts(pref),"policy save preserves configured shortcut combinations");
        for(bool game:{false,true})expect(HotkeysSuspended(pref,game)==(manual||(automatic&&game)),"manual disable takes priority and survives game exit");
        const HMENU menu=CreatePopupMenu();AppendHotkeyPolicyMenu(menu,pref);
        expect(((GetMenuState(menu,GameHotkeyMenu,MF_BYCOMMAND)&MF_CHECKED)!=0)==automatic&&((GetMenuState(menu,DisableHotkeyMenu,MF_BYCOMMAND)&MF_CHECKED)!=0)==manual,"real tray items reflect independent checked states");
        wchar_t label[80]{};GetMenuStringW(menu,GameHotkeyMenu,label,80,MF_BYCOMMAND);expect(std::wstring(label)==L"全屏游戏时禁用快捷键","game-only tray label");DestroyMenu(menu);
    }
    expect(!AllowLaunch(WM_HOTKEY,true)&&AllowLaunch(LaunchCommandMessage,true)&&AllowLaunch(WM_HOTKEY,false),"manual tray/timer/startup launches bypass only the hotkey suspension gate");
    Application app;bool game=false;app.game_active_=[&]{return game;};
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumaShot.HotkeyPolicyTest";wc.lpfnWndProc=Application::MainProc;RegisterClassW(&wc);
    app.main_=CreateWindowExW(0,wc.lpszClassName,L"Synthetic hotkey host",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,&app);
    HWND probe=CreateWindowExW(0,L"STATIC",L"Synthetic key ownership probe",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    expect(app.main_&&probe,"isolated message-only test windows created");
    auto& pref=app.preferences_;pref.modifiers=pref.gif_modifiers=pref.video_modifiers=MOD_CONTROL|MOD_ALT|MOD_SHIFT;pref.key=VK_F20;pref.gif_key=VK_F21;pref.video_key=VK_F22;pref.translate_modifiers=MOD_CONTROL|MOD_ALT|MOD_SHIFT;pref.translate_key=VK_F19;
    const auto available=[&](Shortcut key){const bool free=RegisterHotKey(probe,77,key.modifiers|MOD_NOREPEAT,key.key)!=FALSE;if(free)UnregisterHotKey(probe,77);return free;};
    const auto owns=[&](bool registered){bool ok=true;for(auto key:Shortcuts(pref))if(key.key)ok&=available(key)!=registered;return ok;};
    expect(owns(false),"synthetic shortcut combinations initially free");
    pref.disable_hotkeys_in_game=true;app.ConfigureHotkeyPolicy();expect(owns(true)&&!app.hotkeys_suspended_,"normal mode registers screenshot GIF and video keys");
    game=true;Application::MainProc(app.main_,WM_TIMER,20,0);expect(owns(false)&&app.hotkeys_suspended_,"game transition unregisters all keys rather than merely ignoring them");
    Application::MainProc(app.main_,WM_HOTKEY,1,0);expect(!app.pending_&&!app.active_,"queued screenshot hotkey cannot launch while suspended");
    pref.hotkeys_disabled=true;app.ConfigureHotkeyPolicy();game=false;app.RefreshHotkeys();expect(owns(false)&&app.hotkeys_suspended_,"game exit does not override manual disable");
    // Settings may change keys while suspended without claiming them early.
    auto changed=pref;changed.key=VK_F23;
    expect(ApplyShortcuts(app.main_,EffectiveHotkeys(pref,true),EffectiveHotkeys(changed,true)),"editing shortcuts while suspended succeeds without registration");pref=changed;
    expect(owns(false),"edited shortcuts remain released until enabled");
    pref.hotkeys_disabled=false;app.ConfigureHotkeyPolicy();expect(owns(true)&&!app.hotkeys_suspended_,"reenable restores latest configured keys");
    game=true;app.RefreshHotkeys();pref.disable_hotkeys_in_game=false;app.ConfigureHotkeyPolicy();expect(owns(true),"disabling automatic policy restores keys even while game signal remains true");
    pref.hotkeys_disabled=true;app.ConfigureHotkeyPolicy();const auto conflict=Shortcuts(pref)[0];expect(RegisterHotKey(probe,78,conflict.modifiers|MOD_NOREPEAT,conflict.key)!=FALSE,"another app can claim a released shortcut");
    pref.hotkeys_disabled=false;app.ConfigureHotkeyPolicy();UnregisterHotKey(probe,78);
    expect(available(conflict)&&!available(Shortcuts(pref)[1])&&!available(Shortcuts(pref)[2]),"resume conflict leaves other shortcuts operational without modal UI");
    pref.hotkeys_disabled=true;app.ConfigureHotkeyPolicy();pref.hotkeys_disabled=false;app.ConfigureHotkeyPolicy();expect(owns(true),"toggle retries registration after conflict is cleared");
    pref.hotkeys_disabled=true;app.ConfigureHotkeyPolicy();DestroyWindow(probe);DestroyWindow(app.main_);app.main_=nullptr;
    return failures?1:0;
}
};
}
int main(){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=lumashot::HotkeyPolicyTest::Run();CoUninitialize();return result;}