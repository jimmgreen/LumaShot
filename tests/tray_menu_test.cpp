// Exercise the real popup and renderer with synthetic command data only.
#include "../src/ui/tray_menu.cpp"
#include "app/hotkey_policy.h"
#include "export/png.h"
#include <iostream>
using namespace lumashot;
namespace {
int failures{},mode{},seen{};
void Expect(bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;failures+=!ok;}
HMENU Model(){
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"开始截图");AppendMenuW(menu,MF_STRING,2,L"3 秒后截图");
    AppendMenuW(menu,MF_STRING,6,L"录制 GIF…");AppendMenuW(menu,MF_STRING,7,L"录制屏幕…");AppendMenuW(menu,MF_STRING|MF_CHECKED,3,L"包含鼠标指针");AppendMenuW(menu,MF_STRING,8,L"剪贴板（Ctrl+Shift+V）");
    Preferences pref;pref.disable_hotkeys_in_game=true;AppendHotkeyPolicyMenu(menu,pref);AppendMenuW(menu,MF_STRING,4,L"设置…");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,5,L"退出");return menu;
}
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD){
    HWND window=FindWindowW(L"LumaShot.TrayMenu",nullptr);if(!window)return;
    ++seen;Expect((GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION)==0,"popup has no native caption or menu chrome");
    DWORD affinity=0xffffffff;Expect(GetWindowDisplayAffinity(window,&affinity)&&affinity==WDA_NONE,"themed tray menu remains capturable");
    RECT bounds{};GetWindowRect(window,&bounds);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    Expect(bounds.left>=monitor.rcWork.left&&bounds.right<=monitor.rcWork.right&&bounds.top>=monitor.rcWork.top&&bounds.bottom<=monitor.rcWork.bottom,"popup stays inside monitor work area");
    if(mode==0)PostMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
    if(mode==1){PostMessageW(window,WM_KEYDOWN,VK_HOME,0);PostMessageW(window,WM_KEYDOWN,VK_RETURN,0);}
    if(mode==2){PostMessageW(window,WM_KEYDOWN,VK_END,0);PostMessageW(window,WM_KEYDOWN,VK_RETURN,0);}
    if(mode==3)PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(-8,-8));
    if(mode==4){auto* popup=reinterpret_cast<ui::tray_detail::Popup*>(GetWindowLongPtrW(window,GWLP_USERDATA));const auto row=popup->Row(7);const LPARAM point=MAKELPARAM(int(70*popup->scale),int((row.top+row.bottom)*.5f*popup->scale));PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,point);PostMessageW(window,WM_LBUTTONUP,0,point);}
    if(mode==5)PostMessageW(window,WM_CANCELMODE,0,0);
    if(mode==6)PostMessageW(window,WM_ACTIVATE,WA_INACTIVE,0);
    if(mode==7){auto* popup=reinterpret_cast<ui::tray_detail::Popup*>(GetWindowLongPtrW(window,GWLP_USERDATA));const auto row=popup->Row(6);const LPARAM point=MAKELPARAM(int(70*popup->scale),int((row.top+row.bottom)*.5f*popup->scale));SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(window,WM_LBUTTONUP,0,point);Expect(!popup->done&&!popup->choice,"clicking divider neither selects nor closes menu");PostMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);}
    if(mode==8){auto* popup=reinterpret_cast<ui::tray_detail::Popup*>(GetWindowLongPtrW(window,GWLP_USERDATA));const auto a=popup->Row(0),b=popup->Row(1);SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(70*popup->scale),int((a.top+a.bottom)*.5f*popup->scale)));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(int(70*popup->scale),int((b.top+b.bottom)*.5f*popup->scale)));Expect(!popup->choice&&!popup->done,"drag release on another row does not trigger accidental command");PostMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);}
}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    HMENU menu=Model();ui::tray_detail::Popup popup;popup.items=ui::tray_detail::Items(menu);
    Expect(popup.items.size()==13,"all ten commands and three dividers retained");
    Expect(popup.items[4].checked&&popup.items[7].checked&&!popup.items[8].checked,"cursor and automatic/manual shortcut states remain independent");
    Expect(popup.items[5].label==L"剪贴板"&&popup.items[5].hint==L"Ctrl+Shift+V","clipboard hint moves into aligned secondary column");
    Expect(popup.Hit(40,popup.Row(6).top+3)==-1,"divider is not selectable");popup.hover=5;popup.Move(1);Expect(popup.hover==7,"keyboard skips divider");popup.hover=0;popup.Move(-1);Expect(popup.hover==12,"keyboard navigation wraps");
    popup.items[1].enabled=false;popup.hover=0;popup.Move(1);Expect(popup.hover==2,"keyboard skips disabled rows");popup.Choose(1);Expect(!popup.done,"disabled row cannot execute");popup.items[1].enabled=true;
    for(const RECT work:{RECT{0,0,1920,1040},RECT{-1920,-100,0,940}})for(const POINT anchor:{POINT{work.left,work.top},POINT{work.right,work.bottom},POINT{work.left+40,work.bottom-12}}){const auto rect=ui::tray_detail::Placement(anchor,work,{312,404});Expect(rect.left>=work.left&&rect.right<=work.right&&rect.top>=work.top&&rect.bottom<=work.bottom,"placement clamps screen corners and negative monitor origins");}
    for(bool dark:{false,true})for(float scale:{1.f,1.5f,2.f}){
        popup.dark=dark;popup.scale=scale;popup.hover=0;
        const int width=int(std::ceil(ui::tray_detail::Width*scale)),height=int(std::ceil(ui::tray_detail::Height(popup.items)*scale));DibSurface surface(width,height);popup.Draw(surface,width,height);
        auto frame=MakeFrame({0,0,width,height});std::copy_n(surface.Pixels(),frame.pixels.size(),frame.pixels.begin());
        SavePng(frame,(std::wstring(L"tray-menu-")+(dark?L"dark-":L"light-")+std::to_wstring(int(scale*100))+L".png").c_str());
        Expect(frame.pixels[0]==0,"transparent outer corner preserves rounded popup silhouette");
        Expect((frame.pixels[int(18*scale)*width+int(150*scale)]>>24)==255,"menu surface stays opaque for readable content");
        for(int i=0;i<int(popup.items.size());++i)if(!popup.items[i].separator){const auto r=popup.Row(i);Expect(popup.Hit(70,(r.top+r.bottom)/2)==i,"every rendered command has a matching hit target at all DPI scales");}
    }
    HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Synthetic tray owner",WS_POPUP,100,100,20,20,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    const auto timer=SetTimer(nullptr,0,80,Tick);const UINT expected[]={0,1,5,0,9,0,0,0,0};
    for(bool dark:{false,true})for(mode=0;mode<9;++mode){SetForegroundWindow(owner);Expect(ui::TrackTrayMenu(owner,menu,{500,500},dark)==expected[mode],"native popup dispatches requested commands or safely dismisses");Expect(!FindWindowW(L"LumaShot.TrayMenu",nullptr)&&GetCapture()==nullptr,"dismissal destroys popup and releases capture");}
    Expect(seen>=18,"both themes exercised through live window messages");KillTimer(nullptr,timer);DestroyWindow(owner);DestroyMenu(menu);CoUninitialize();return failures?1:0;
}
