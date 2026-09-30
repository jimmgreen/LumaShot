// Exercise the actual modal window and scoped keyboard hook with synthetic input.
#include "../src/app/settings_dialog.cpp"
#include "../src/app/settings_process.cpp"
#include "capture/frame.h"
#include "export/png.h"
#include "app/launch_options.h"
#include <iostream>
#include <chrono>
#include <fstream>
using namespace lumashot;
namespace {
int failures{},scenario{},phase{};bool callback_called{},input_blocked{};
void Expect(bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;failures+=!ok;}
void Key(Settings& s,UINT key,bool down){s.Input(key,down);Expect(input_blocked,"capture remains blocked during recording and key release");Expect(s.hook!=nullptr,"hook stays scoped to active recording until release");}
void Finish(Settings& s){SendMessageW(s.window,Finished,0,0);Expect(!s.hook&&!s.recording&&!s.draining&&!input_blocked,"hook removed after complete chord");}
void Snapshot(Settings& s,const wchar_t* file,float scale){
    RECT previous{};GetWindowRect(s.window,&previous);const float old=s.scale;
    std::array<RECT,27> rects{};constexpr int ids[]={128,119,120,103,101,115,126,132,129,127,130,131,116,117,118,121,122,123,124,125,110,111,112,113,IDOK,IDCANCEL,114};
    for(int i=0;i<27;++i){GetWindowRect(GetDlgItem(s.window,ids[i]),&rects[i]);MapWindowPoints(nullptr,s.window,reinterpret_cast<POINT*>(&rects[i]),2);}
    SetWindowPos(s.window,nullptr,0,0,int(PanelWidth*scale),int(PanelHeight*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);s.scale=scale;
    for(int i=0;i<27;++i){const auto r=rects[i];SetWindowPos(GetDlgItem(s.window,ids[i]),nullptr,int(r.left/old*scale),int(r.top/old*scale),int((r.right-r.left)/old*scale),int((r.bottom-r.top)/old*scale),SWP_NOZORDER|SWP_NOACTIVATE);}
    s.RenderSurface();auto frame=MakeFrame({0,0,s.surface_width,s.surface_height});
    const auto* pixels=s.surface->Pixels();Expect((pixels[0]>>24)==0,"production surface has fully transparent outer corner");
    const auto hasInk=[&](RECT bounds,UINT32 ink){
        unsigned count=0;
        for(int y=std::max(0L,bounds.top);y<std::min(LONG(frame.Height()),bounds.bottom);++y)
            for(int x=std::max(0L,bounds.left);x<std::min(LONG(frame.Width()),bounds.right);++x){
                // Small antialiased glyphs need not contain fully covered pixels.
                // Unpremultiply and require the sample to be closer to ink than
                // the panel background (at least half foreground coverage).
                const auto pixel=pixels[y*frame.Width()+x];const UINT alpha=pixel>>24;if(alpha<64)continue;
                int foreground_distance=0,background_distance=0;
                for(UINT shift:{0u,8u,16u}){const int channel=int(std::min(255u,(((pixel>>shift)&255)*255+alpha/2)/alpha));
                    foreground_distance+=std::abs(channel-int((ink>>shift)&255));
                    background_distance+=std::abs(channel-int((PanelBackground(s.material_dark)>>shift)&255));}
                if(foreground_distance<background_distance)++count;
            }
        return count>10;
    };
    const UINT32 ink=s.material_dark?0xedf4ff:0x243142;
    Expect(hasInk({LONG(60*scale),LONG(18*scale),LONG(220*scale),LONG(48*scale)},ink),"LumaText paints settings title at current DPI");
    for(int id:{103,IDCANCEL}){
        if(id==103&&s.page!=0)continue;
        RECT bounds{};GetWindowRect(GetDlgItem(s.window,id),&bounds);MapWindowPoints(nullptr,s.window,reinterpret_cast<POINT*>(&bounds),2);
        Expect(hasInk(bounds,ink),"LumaText paints translated shortcut and footer control text");
    }
    if(s.acrylic)Expect((pixels[int(185*scale)*frame.Width()+int(20*scale)]>>24)<255,"acrylic background retains alpha for DWM blur");
    // Synthetic backdrop only; no personal desktop capture. Shows the real alpha
    // surface over a smooth colored background, not a claim about DWM blur output.
    for(int y=0;y<frame.Height();++y)for(int x=0;x<frame.Width();++x){const auto source=pixels[y*frame.Width()+x];const UINT inverse=255-(source>>24);
        const UINT red=180+UINT(x*45/frame.Width()),green=195+UINT(y*35/frame.Height()),blue=225;
        const uint32_t bg=(red<<16)|(green<<8)|blue;uint32_t out=0xff000000;
        for(UINT shift:{0u,8u,16u})out|=std::min(255u,((source>>shift)&255)+((((bg>>shift)&255)*inverse+127)/255))<<shift;
        frame.pixels[y*frame.Width()+x]=out;
    }
    SavePng(frame,file);SetWindowPos(s.window,nullptr,0,0,previous.right-previous.left,previous.bottom-previous.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);s.Layout();
}
void CALLBACK Drive(HWND,UINT,UINT_PTR timer,DWORD){
    HWND w=GetActiveWindow();if(!w||!GetDlgItem(w,103))return;KillTimer(nullptr,timer);
    auto& s=*reinterpret_cast<Settings*>(GetWindowLongPtrW(w,DWLP_USER));
    Expect(s.prepared_before_show&&s.presents>0,"complete frame uploaded before dialog is shown");
    SetForegroundWindow(w);    Expect(s.Present(),"real layered window accepts complete alpha surface");
    RECT outer{},client{};GetWindowRect(w,&outer);GetClientRect(w,&client);POINT origin{};ClientToScreen(w,&origin);
    Expect(!(GetWindowLongPtrW(w,GWL_STYLE)&(WS_CAPTION|WS_THICKFRAME|WS_BORDER)),"actual dialog has no system caption or border");
    Expect(origin.x==outer.left&&origin.y==outer.top&&client.right==outer.right-outer.left&&client.bottom==outer.bottom-outer.top,"actual client and window extents agree");
    for(int id:{128,115,116,117,118,121,122,123,124,125,126,132,129,127,130,131,113,IDCANCEL,IDOK}){RECT r{};GetWindowRect(GetDlgItem(w,id),&r);Expect(r.left>=outer.left&&r.right<=outer.right&&r.top>=outer.top&&r.bottom<=outer.bottom,"file paste and footer controls fully inside real window bounds");}
    if(scenario==4){
        if(phase++==0){
            const DWORD foreground=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),current=GetCurrentThreadId();
            const bool attached=foreground!=current&&AttachThreadInput(current,foreground,TRUE);SetForegroundWindow(w);if(attached)AttachThreadInput(current,foreground,FALSE);
            if(GetForegroundWindow()!=w)std::cout<<"SKIP real SendInput integration: runner has no foreground input desktop; synthetic input state tests completed"<<std::endl;
            if(GetForegroundWindow()!=w){SendMessageW(w,WM_COMMAND,IDCANCEL,0);return;}
            SendMessageW(w,WM_COMMAND,103,0);
            INPUT keys[6]{};const WORD codes[]={VK_LWIN,VK_LCONTROL,VK_F11,VK_F11,VK_LCONTROL,VK_LWIN};
            for(int i=0;i<6;++i){keys[i].type=INPUT_KEYBOARD;keys[i].ki.wVk=codes[i];keys[i].ki.dwFlags=i>=3?KEYEVENTF_KEYUP:0;}
            Expect(SendInput(6,keys,sizeof(INPUT))==6,"synthetic Win chord sent through real hook");SetTimer(nullptr,0,100,Drive);return;
        }
        Expect(s.draft.modifiers==(MOD_WIN|MOD_CONTROL)&&s.draft.key==VK_F11&&!s.hook,"real hook records Win chord and releases after key-up");
        SendMessageW(w,WM_COMMAND,IDCANCEL,0);
    }else if(scenario==0){
        const auto count=s.formats.size();const auto* surface=s.surface.get();
        const auto render_start=std::chrono::steady_clock::now();
        for(int i=0;i<21;++i)s.RenderSurface();
        std::cout<<"Settings full-frame average ms: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-render_start).count()/20<<std::endl;
        Expect(s.formats.size()==count&&s.surface.get()==surface,"full redraw reuses text formats and a single surface");
        Expect(s.draft.start_with_windows,"login startup defaults on");SendMessageW(w,WM_COMMAND,127,0);Expect(!s.draft.start_with_windows,"startup switch disables draft");SendMessageW(w,WM_COMMAND,127,0);Expect(s.draft.start_with_windows,"startup switch enables draft");
        Expect(!s.draft.clipboard_enabled,"clipboard history defaults off");SendMessageW(w,WM_COMMAND,126,0);Expect(s.draft.clipboard_enabled,"clipboard switch enables draft");SendMessageW(w,WM_COMMAND,126,0);Expect(!s.draft.clipboard_enabled,"clipboard switch disables draft");
        Expect(!s.draft.clipboard_persist,"clipboard history persistence defaults off");SendMessageW(w,WM_COMMAND,129,0);Expect(s.draft.clipboard_persist,"persistence switch enables draft");SendMessageW(w,WM_COMMAND,113,0);Expect(!s.draft.clipboard_persist,"restore defaults turns persistence off");
        Expect(!s.draft.clipboard_enabled&&!IsWindowEnabled(GetDlgItem(w,132)),"strip switch is disabled while the clipboard is off");
        SendMessageW(w,WM_COMMAND,126,0);Expect(IsWindowEnabled(GetDlgItem(w,132)),"enabling the clipboard enables the strip switch");SendMessageW(w,WM_COMMAND,126,0);
        Expect(s.draft.clipboard_strip_visible,"clipboard strip defaults visible");SendMessageW(w,WM_COMMAND,132,0);Expect(!s.draft.clipboard_strip_visible,"strip switch hides draft strip");
        SendMessageW(w,WM_COMMAND,113,0);Expect(s.draft.clipboard_strip_visible,"restore defaults shows the strip again");
        {RECT a{},b{};GetWindowRect(GetDlgItem(w,126),&a);GetWindowRect(GetDlgItem(w,132),&b);RECT c{};GetWindowRect(GetDlgItem(w,129),&c);Expect(a.bottom<=b.top&&b.bottom<=c.top,"strip switch sits between clipboard and history switches");}
        Expect(s.draft.pin_style==DefaultPinStyle,"sticker default is simple border and shadow");
        for(int id=121;id<=125;++id){SendMessageW(w,WM_COMMAND,id,0);Expect(static_cast<int>(s.draft.pin_style)==id-121,"each sticker choice updates draft");}
        SendMessageW(w,WM_COMMAND,124,0);
        Expect(s.draft.paste_as_file&&s.draft.paste_file_format==0,"file paste defaults to enabled PNG");
        SendMessageW(w,WM_COMMAND,117,0);Expect(s.draft.paste_file_format==1,"JPEG selection updates draft");
        SendMessageW(w,WM_COMMAND,115,0);Expect(!s.draft.paste_as_file&&!IsWindowEnabled(GetDlgItem(w,116))&&!IsWindowEnabled(GetDlgItem(w,117))&&!IsWindowEnabled(GetDlgItem(w,118)),"disabling file paste disables all format controls");
        SendMessageW(w,WM_COMMAND,118,0);Expect(s.draft.paste_file_format==1,"disabled format cannot change draft");
        SendMessageW(w,WM_COMMAND,136,0);{const auto shown=[&](int id){return (GetWindowLongPtrW(GetDlgItem(w,id),GWL_STYLE)&WS_VISIBLE)!=0;};Expect(s.page==1&&shown(115)&&shown(116)&&!shown(103)&&!shown(126)&&shown(135)&&shown(IDOK),"sidebar shows one page at a time");}
        Snapshot(s,L"settings-file-disabled.png",1);
        SendMessageW(w,WM_COMMAND,115,0);SendMessageW(w,WM_COMMAND,118,0);Expect(s.draft.paste_as_file&&s.draft.paste_file_format==2&&IsWindowEnabled(GetDlgItem(w,118)),"reenabling file paste permits BMP selection");
        {const auto pages=[&](const wchar_t* prefix){for(int page=0;page<6;++page){SendMessageW(w,WM_COMMAND,135+page,0);Snapshot(s,(std::wstring(prefix)+std::to_wstring(page)+L".png").c_str(),1);}};
         SendMessageW(w,WM_COMMAND,111,0);pages(L"settings-light-p");SendMessageW(w,WM_COMMAND,112,0);Expect(s.draft.theme==2,"dark theme preview updates draft");pages(L"settings-dark-p");}
        SendMessageW(w,WM_COMMAND,138,0);Expect(s.tr_name==L"DeepSeek"&&!s.tr_ready,"translation page summarises the isolated engine config");
        Expect(s.draft.translate_auto_show,"translated pin switch defaults on");SendMessageW(w,WM_COMMAND,141,0);Expect(!s.draft.translate_auto_show,"translated pin switch toggles draft");SendMessageW(w,WM_COMMAND,141,0);
        SendMessageW(w,WM_COMMAND,135,0);Snapshot(s,L"settings-dark-150.png",1.5f);
        SendMessageW(w,WM_COMMAND,103,0);Expect(s.recording&&s.hook,"recording installs hook on demand");
        Key(s,VK_LWIN,true);Key(s,VK_LCONTROL,true);Key(s,VK_F11,true);Expect(s.draft.modifiers==(MOD_WIN|MOD_CONTROL)&&s.draft.key==VK_F11,"Win plus Ctrl plus F11 captured");
        Key(s,VK_F11,false);Key(s,VK_LCONTROL,false);Key(s,VK_LWIN,false);Finish(s);
        SendMessageW(w,WM_COMMAND,IDOK,0);
    }else if(scenario==1){
        const auto mods=s.draft.modifiers,key=s.draft.key;SendMessageW(w,WM_COMMAND,103,0);Key(s,VK_LWIN,true);Key(s,VK_ESCAPE,true);Key(s,VK_ESCAPE,false);Key(s,VK_LWIN,false);Finish(s);
        Expect(s.draft.modifiers==mods&&s.draft.key==key,"Escape restores shortcut and leaves dialog open");
        SendMessageW(w,WM_COMMAND,103,0);Key(s,'B',true);Key(s,'B',false);Expect(s.recording&&s.draft.key==key,"unmodified letter rejected");
        Key(s,VK_LCONTROL,true);Key(s,VK_F12,true);Key(s,VK_F12,false);Expect(s.recording&&s.draft.key==key,"reserved F12 rejected");
        Key(s,VK_LCONTROL,false);Key(s,VK_F1,true);Expect(!s.recording&&s.draining&&s.draft.key==VK_F1&&s.draft.modifiers==0,"standalone F1 accepted without modifier");
        Key(s,VK_F1,false);Finish(s);Expect(ShortcutLabel(s.draft.modifiers,s.draft.key)==L"F1","standalone shortcut label has no modifier prefix");
        SendMessageW(w,WM_COMMAND,103,0);Key(s,VK_LCONTROL,true);
        SendMessageW(w,WM_ACTIVATE,WA_INACTIVE,0);Expect(!s.hook&&!s.recording&&!input_blocked&&s.draft.key==VK_F1&&s.draft.modifiers==0,"focus loss removes keyboard hook and keeps committed standalone key");
        SendMessageW(w,WM_COMMAND,125,0);SendMessageW(w,WM_COMMAND,101,0);SendMessageW(w,WM_COMMAND,IDCANCEL,0);
    }else if(scenario==2){
        bool registered=RegisterHotKey(nullptr,88,MOD_CONTROL|MOD_ALT|MOD_SHIFT,VK_F10)!=FALSE;Expect(registered,"synthetic conflict registered");
        s.draft.modifiers=MOD_CONTROL|MOD_ALT|MOD_SHIFT;s.draft.key=VK_F10;SendMessageW(w,WM_COMMAND,IDOK,0);
        Expect(s.error&&!IsWindowEnabled(GetDlgItem(w,IDOK))&&!callback_called,"conflict stays inline and blocks save");Snapshot(s,L"settings-conflict.png",1);
        if(registered)UnregisterHotKey(nullptr,88);SendMessageW(w,WM_COMMAND,113,0);Expect(s.draft.key=='A'&&s.draft.modifiers==(MOD_CONTROL|MOD_ALT)&&s.draft.theme==0&&s.draft.include_cursor&&s.draft.paste_as_file&&s.draft.paste_file_format==0&&s.draft.pin_style==DefaultPinStyle,"restore defaults changes dialog settings only");
        SendMessageW(w,WM_COMMAND,IDCANCEL,0);
    }else if(scenario==5){
        SendMessageW(w,WM_COMMAND,128,0);Key(s,VK_LCONTROL,true);Key(s,VK_F23,true);Key(s,VK_F23,false);Key(s,VK_LCONTROL,false);Finish(s);
        Expect(s.draft.clipboard_key==VK_F23&&s.draft.clipboard_modifiers==MOD_CONTROL&&s.draft.key=='A',"clipboard shortcut recorder edits independent chord");
        s.draft.clipboard_key=s.draft.key;s.draft.clipboard_modifiers=s.draft.modifiers;Expect(!s.Validate(),"clipboard cannot duplicate capture shortcut");
        SendMessageW(w,WM_COMMAND,128,0);Key(s,VK_BACK,true);Key(s,VK_BACK,false);Finish(s);Expect(s.draft.clipboard_key==0,"clipboard shortcut can be cleared");
        SendMessageW(w,WM_COMMAND,119,0);Key(s,VK_LCONTROL,true);Key(s,VK_LSHIFT,true);Key(s,'G',true);Key(s,'G',false);Key(s,VK_LSHIFT,false);Key(s,VK_LCONTROL,false);Finish(s);
        Expect(s.draft.gif_key=='G'&&s.draft.gif_modifiers==(MOD_CONTROL|MOD_SHIFT)&&s.draft.key=='A',"GIF recorder edits independent shortcut");
        SendMessageW(w,WM_COMMAND,120,0);Key(s,VK_BACK,true);Key(s,VK_BACK,false);Finish(s);Expect(s.draft.video_key==0,"recording shortcut can be disabled");
        s.draft.gif_key=s.draft.key;s.draft.gif_modifiers=s.draft.modifiers;Expect(!s.Validate(),"duplicate shortcuts blocked before parent submission");
        SendMessageW(w,WM_COMMAND,IDCANCEL,0);
    }else{
        SendMessageW(w,WM_COMMAND,IDOK,0);Expect(s.error&&IsWindow(w)&&callback_called,"registration race keeps dialog open");SendMessageW(w,WM_COMMAND,IDCANCEL,0);
    }
}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    // Synthetic engine config: the translation page never reads the user's file.
    const auto translation_config=std::filesystem::current_path()/L"settings-translation-test.ini";
    {translate::Config config;config.provider="deepseek";translate::SaveConfig(translation_config,config);}
    SetEnvironmentVariableW(L"LUMASHOT_TRANSLATION_CONFIG",translation_config.c_str());
    Expect(Preferences{}.clipboard_key=='V'&&Preferences{}.clipboard_modifiers==MOD_WIN,"clipboard defaults to Win+V");
    Expect(!CaptureOnLaunch(L"--background")&&!ActivateExistingOnLaunch(L"--background")&&CaptureOnLaunch(L"--capture")&&CaptureOnLaunch(L""),"installer background launch neither captures nor activates existing instance");
    Expect(ShortcutLabel(MOD_WIN|MOD_SHIFT,'S').find(L"Win")!=std::wstring::npos,"shortcut label retains Win modifier");
    Expect(ValidShortcut(0,VK_F1)&&ValidShortcut(0,VK_F11)&&ValidShortcut(0,VK_SNAPSHOT)&&ValidShortcut(0,VK_PAUSE)&&ValidShortcut(MOD_SHIFT,VK_F1),"function keys valid alone or with modifiers");
    Expect(!ValidShortcut(0,'A')&&!ValidShortcut(0,VK_SPACE)&&!ValidShortcut(0,VK_F12)&&!ValidShortcut(MOD_CONTROL,VK_F12),"plain characters and F12 rejected");
    Expect(ShortcutLabel(0,VK_SNAPSHOT)==L"PrtSc"&&ShortcutLabel(0,VK_PAUSE)==L"Pause"&&ShortcutLabel(0,VK_F13)==L"F13"&&ShortcutLabel(MOD_SHIFT,VK_F24)==L"Shift + F24","standalone keys have readable labels");
    for(scenario=0;scenario<6;++scenario){Preferences p;p.theme=1;p.tools.width=9;const auto original=p;callback_called=false;SetTimer(nullptr,0,60,Drive);
        const bool saved=ShowSettingsDialog(nullptr,p,[&](const Preferences&){callback_called=true;return scenario!=3;},[](bool active){input_blocked=active;});
        Expect(saved==(scenario==0),"modal save/cancel result");if(scenario==0)Expect(p.modifiers==(MOD_WIN|MOD_CONTROL)&&p.key==VK_F11&&p.theme==2&&p.paste_as_file&&p.paste_file_format==2&&p.pin_style==PinStyle::Polaroid,"saved draft preserves Win modifier and theme");else Expect(p.modifiers==original.modifiers&&p.key==original.key&&p.include_cursor==original.include_cursor&&p.theme==original.theme&&p.paste_as_file==original.paste_as_file&&p.paste_file_format==original.paste_file_format&&p.pin_style==original.pin_style,"cancel leaves caller preferences unchanged");
        Expect(p.tools.width==9&&Settings::recorder==nullptr&&!input_blocked,"tool properties retained and hook lifetime closed");
    }
    const auto path=std::filesystem::current_path()/L"settings-roundtrip-test.ini";
    WritePrivateProfileStringW(L"Unrelated",L"Keep",L"yes",path.c_str());Preferences stored;stored.clipboard_enabled=true;stored.key=VK_F1;stored.modifiers=0;stored.gif_key='T';stored.gif_modifiers=MOD_CONTROL|MOD_SHIFT;stored.video_key=0;stored.clipboard_key=VK_F23;stored.clipboard_modifiers=MOD_WIN|MOD_SHIFT;stored.SaveTo(path);const auto loaded=Preferences::LoadFrom(path);Expect(loaded.clipboard_key==VK_F23&&loaded.clipboard_modifiers==(MOD_WIN|MOD_SHIFT),"clipboard shortcut file roundtrip");Expect(loaded.clipboard_enabled,"clipboard enable preference roundtrip");Preferences clipboard_decoded;Expect(Decode(Encode(stored),clipboard_decoded)&&clipboard_decoded.clipboard_enabled&&clipboard_decoded.clipboard_key==VK_F23&&clipboard_decoded.clipboard_modifiers==(MOD_WIN|MOD_SHIFT),"clipboard enable IPC roundtrip");
    Expect(loaded.key==VK_F1&&loaded.modifiers==0,"standalone function key survives settings reload without modifier fallback");
    Expect(!loaded.clipboard_persist,"clipboard history persistence defaults off on disk");
    {std::ifstream bom(path,std::ios::binary);char head[2]{};bom.read(head,2);Expect(bom.gcount()==2&&static_cast<unsigned char>(head[0])==0xFF&&static_cast<unsigned char>(head[1])==0xFE,"legacy ANSI settings migrate to UTF-16 file on save");}
    {Preferences unicode=stored;unicode.clipboard_persist=true;unicode.save_directory=L"C:\\Synthetic\\截图 Ünïcødé \U0001F308";unicode.SaveTo(path);const auto reread=Preferences::LoadFrom(path);
     Expect(reread.save_directory==unicode.save_directory,"non-ANSI save folder survives settings reload");Expect(reread.clipboard_persist,"clipboard persistence preference roundtrip");
     Preferences persist_decoded;Expect(Decode(Encode(unicode),persist_decoded)&&persist_decoded.clipboard_persist,"isolated settings IPC carries clipboard persistence");}
    {Expect(loaded.clipboard_strip_visible&&!loaded.clipboard_strip_hint_shown,"strip visible and hint unseen by default on disk");
     Preferences hidden=stored;hidden.clipboard_strip_visible=false;hidden.clipboard_strip_hint_shown=true;hidden.SaveTo(path);const auto reread=Preferences::LoadFrom(path);
     Expect(!reread.clipboard_strip_visible&&reread.clipboard_strip_hint_shown,"hidden strip and seen hint survive settings reload");
     Preferences strip_decoded;Expect(Decode(Encode(hidden),strip_decoded)&&!strip_decoded.clipboard_strip_visible,"isolated settings IPC carries strip visibility");
     auto bad=Encode(hidden);bad.clipboard_strip_visible=2;Preferences rejected;Expect(!Decode(bad,rejected),"IPC rejects invalid strip visibility");}
    {const auto fresh=std::filesystem::current_path()/L"settings-fresh-unicode-test.ini";std::filesystem::remove(fresh);Preferences created;created.save_directory=L"D:\\合成\\ß";created.SaveTo(fresh);
     Expect(Preferences::LoadFrom(fresh).save_directory==created.save_directory,"new settings file is created as Unicode");std::filesystem::remove(fresh);}
    Expect(loaded.gif_key=='T'&&loaded.gif_modifiers==(MOD_CONTROL|MOD_SHIFT)&&loaded.video_key==0,"new bindings persist through isolated settings file");
    for(int style=0;style<=4;++style){
        stored.pin_style=static_cast<PinStyle>(style);stored.SaveTo(path);
        Expect(Preferences::LoadFrom(path).pin_style==stored.pin_style,"every sticker style survives settings reload");
        Preferences decoded;Expect(Decode(Encode(stored),decoded)&&decoded.pin_style==stored.pin_style,"isolated settings IPC roundtrips every sticker style");
    }
    for(const wchar_t* invalid:{L"-1",L"5",L"999"}){WritePrivateProfileStringW(L"General",L"PinStyle",invalid,path.c_str());Expect(Preferences::LoadFrom(path).pin_style==DefaultPinStyle,"invalid persisted sticker style falls back to simple");}
    WritePrivateProfileStringW(L"General",L"PinStyle",nullptr,path.c_str());Expect(Preferences::LoadFrom(path).pin_style==DefaultPinStyle,"old settings without PinStyle migrate to simple");
    auto invalid=Encode(stored);invalid.pin_style=5;Preferences decoded;Expect(!Decode(invalid,decoded),"IPC rejects invalid sticker style");
    wchar_t keep[8]{};GetPrivateProfileStringW(L"Unrelated",L"Keep",L"",keep,8,path.c_str());Expect(std::wstring(keep)==L"yes","atomic settings save preserves unknown fields");std::filesystem::remove(path);
    {Preferences page;page.settings_page=3;page.translate_auto_show=false;page.SaveTo(path);const auto reread=Preferences::LoadFrom(path);
     Expect(reread.settings_page==3&&!reread.translate_auto_show,"settings page and translated pin switch survive settings reload");
     Preferences decoded_page;Expect(Decode(Encode(page),decoded_page)&&decoded_page.settings_page==3&&!decoded_page.translate_auto_show,"isolated settings IPC carries settings page and translated pin switch");
     auto bad=Encode(page);bad.settings_page=6;Preferences rejected;Expect(!Decode(bad,rejected),"IPC rejects invalid settings page");std::filesystem::remove(path);}
    std::filesystem::remove(translation_config);
    CoUninitialize();return failures?1:0;
}



