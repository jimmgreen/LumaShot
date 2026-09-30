#include "app/hotkeys.h"
#include "app/settings_dialog.h"
#include "app/translation_settings.h"
#include "ui/acrylic.h"
#include "ui/brand_icon.h"
#include "ui/text_renderer.h"
#include "update/manifest.h"
#include "capture/frame.h"
#include <memory>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <array>
#include <algorithm>
#include <map>

namespace lumashot {
using Microsoft::WRL::ComPtr;
bool ShortcutModifier(UINT k){return k==VK_CONTROL||k==VK_LCONTROL||k==VK_RCONTROL||k==VK_SHIFT||k==VK_LSHIFT||k==VK_RSHIFT||k==VK_MENU||k==VK_LMENU||k==VK_RMENU||k==VK_LWIN||k==VK_RWIN;}

namespace {
constexpr UINT Changed=WM_APP+71,Finished=WM_APP+72;
// Layout height in DIP; the settings page scales down to fit short work areas.
constexpr float PanelHeight=1224;
struct Settings {
    Preferences initial,draft;const std::function<bool(const Preferences&)>& accept;
    const std::function<void(bool)>& recording_changed;
    const std::function<void()>& check_updates;
    bool update_requested{};
    HWND window{};int hover{};HHOOK hook{};float scale{1};bool recording{},draining{},error{},acrylic{},material_dark{};
    int shortcut_id{103};
    UINT& EditMods(){return shortcut_id==133?draft.translate_modifiers:shortcut_id==128?draft.clipboard_modifiers:shortcut_id==119?draft.gif_modifiers:shortcut_id==120?draft.video_modifiers:draft.modifiers;}
    UINT& EditKey(){return shortcut_id==133?draft.translate_key:shortcut_id==128?draft.clipboard_key:shortcut_id==119?draft.gif_key:shortcut_id==120?draft.video_key:draft.key;}
    UINT previous_mods{},previous_key{};std::array<bool,256> held{};std::wstring hint;
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1SolidColorBrush> brush;BrandIcon app_icon;TextRenderer text_renderer;
    std::map<std::pair<float,bool>,ComPtr<IDWriteTextFormat>> formats;
    bool prepared_before_show{};unsigned presents{};
    std::unique_ptr<DibSurface> surface;int surface_width{},surface_height{};
    inline static thread_local Settings* recorder{};
    ~Settings(){Stop();}
    void Stop(){if(hook){UnhookWindowsHookEx(hook);hook=nullptr;}if(recorder==this)recorder=nullptr;recording=draining=false;held.fill(false);if(recording_changed)recording_changed(false);}
    UINT Mods()const{return ((held[VK_CONTROL]||held[VK_LCONTROL]||held[VK_RCONTROL])?MOD_CONTROL:0)|((held[VK_MENU]||held[VK_LMENU]||held[VK_RMENU])?MOD_ALT:0)|((held[VK_SHIFT]||held[VK_LSHIFT]||held[VK_RSHIFT])?MOD_SHIFT:0)|((held[VK_LWIN]||held[VK_RWIN])?MOD_WIN:0);}
    static LRESULT CALLBACK Keyboard(int code,WPARAM wp,LPARAM lp){
        auto* p=recorder;if(code<0||!p||GetForegroundWindow()!=p->window)return CallNextHookEx(nullptr,code,wp,lp);
        const auto& event=*reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);if(event.vkCode>=256)return CallNextHookEx(nullptr,code,wp,lp);
        p->Input(event.vkCode,wp==WM_KEYDOWN||wp==WM_SYSKEYDOWN);return 1;
    }
    void Input(UINT key,bool down){
        auto* p=this;p->held[key]=down;
        if(p->recording&&down&&!ShortcutModifier(key)){
            if(key==VK_ESCAPE){p->EditMods()=p->previous_mods;p->EditKey()=p->previous_key;p->hint.clear();p->recording=false;p->draining=true;}
            else if(key==VK_BACK&&!p->Mods()){p->EditKey()=0;p->recording=false;p->draining=true;}
            else if((p->Mods()||StandaloneShortcutKey(key))&&key!=VK_F12){p->EditMods()=p->Mods();p->EditKey()=key;p->recording=false;p->draining=true;}
            else p->hint=L"字母、数字等键请包含 Ctrl、Alt、Shift 或 Win；F1–F11、PrtSc、Pause 可单独使用；F12 为系统保留键";
        }
        PostMessageW(p->window,Changed,0,0);
        if(p->draining&&std::none_of(p->held.begin(),p->held.end(),[](bool v){return v;}))PostMessageW(p->window,Finished,0,0);
    }
    void Refresh(){
        for(int id:{116,117,118})if((IsWindowEnabled(GetDlgItem(window,id))!=FALSE)!=draft.paste_as_file)EnableWindow(GetDlgItem(window,id),draft.paste_as_file);
        // The strip only exists while the clipboard is on.
        if((IsWindowEnabled(GetDlgItem(window,132))!=FALSE)!=draft.clipboard_enabled)EnableWindow(GetDlgItem(window,132),draft.clipboard_enabled);
        const bool dark=draft.Dark();if(dark!=material_dark){material_dark=dark;acrylic=SetSettingsAcrylic(window,dark);}
        InvalidateRect(window,nullptr,FALSE);
    }
    void Begin(){if(recording||draining)return;previous_mods=EditMods();previous_key=EditKey();hint.clear();error=false;
        for(UINT k:{VK_LCONTROL,VK_RCONTROL,VK_LMENU,VK_RMENU,VK_LSHIFT,VK_RSHIFT,VK_LWIN,VK_RWIN})held[k]=(GetAsyncKeyState(static_cast<int>(k))&0x8000)!=0;
        if(recording_changed)recording_changed(true);
        recorder=this;hook=SetWindowsHookExW(WH_KEYBOARD_LL,Keyboard,GetModuleHandleW(nullptr),0);recording=hook!=nullptr;
        if(!hook){if(recording_changed)recording_changed(false);recorder=nullptr;hint=L"无法开始录入，请重试";error=true;}EnableWindow(GetDlgItem(window,IDOK),FALSE);Refresh();
    }
    bool Validate(){error=false;hint.clear();
        if(!UniqueShortcuts(draft)){error=true;hint=L"快捷键重复，请为截图、GIF、录屏、翻译和剪贴板设置不同组合";return false;}
        const auto next=AllShortcuts(draft),old=AllShortcuts(initial);
        for(const auto k:next){if((k.modifiers==MOD_WIN&&k.key=='V')||!k.key||std::find(old.begin(),old.end(),k)!=old.end())continue;
            if(RegisterHotKey(window,77,k.modifiers|MOD_NOREPEAT,k.key)){UnregisterHotKey(window,77);continue;}
            error=true;hint=L"快捷键已被占用或由系统保留，请更换组合键";return false;
        }return true;
    }
    static LRESULT CALLBACK Button(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& s=*reinterpret_cast<Settings*>(data);
        if(msg==WM_MOUSEMOVE&&s.hover!=GetDlgCtrlID(w)){const int old=s.hover;s.hover=GetDlgCtrlID(w);if(old)InvalidateRect(GetDlgItem(s.window,old),nullptr,FALSE);InvalidateRect(w,nullptr,FALSE);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);}
        if(msg==WM_MOUSELEAVE){s.hover=0;InvalidateRect(w,nullptr,FALSE);}
        if(msg==WM_GETDLGCODE&&lp){const auto& event=*reinterpret_cast<MSG*>(lp);if(event.message==WM_KEYDOWN&&event.wParam==VK_RETURN)return DLGC_WANTMESSAGE;}
        if(msg==WM_KEYDOWN&&wp==VK_RETURN){SendMessageW(s.window,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(w),BN_CLICKED),reinterpret_cast<LPARAM>(w));return 0;}
        if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_ENABLE){s.Refresh();}
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(w,Button,1);
        return DefSubclassProc(w,msg,wp,lp);
    }
    void Layout(){RECT client{};GetClientRect(window,&client);scale=std::min({GetDpiForWindow(window)/96.f,client.right/500.f,client.bottom/PanelHeight});
        ResizeBorderlessWindow(window,scale);
        const auto place=[&](int id,int x,int y,int w,int h){SetWindowPos(GetDlgItem(window,id),nullptr,int(x*scale),int(y*scale),int(w*scale),int(h*scale),SWP_NOZORDER|SWP_NOACTIVATE);};
        place(128,152,248,324,42);place(133,152,300,218,42);place(134,378,300,98,42);place(114,454,16,30,30);place(103,152,92,324,42);place(119,152,144,324,42);place(120,152,196,324,42);place(101,424,427,52,30);
        place(115,424,489,52,30);place(126,424,607,52,30);place(132,424,669,52,30);place(129,424,731,52,30);place(127,424,793,52,30);place(131,316,852,96,36);place(130,424,855,52,30);
        place(116,24,552,150,38);place(117,174,552,151,38);place(118,325,552,151,38);
        for(int id=121;id<=125;++id)place(id,24+(id-121)*92,976,84,58);
        place(110,24,1078,150,38);place(111,174,1078,151,38);place(112,325,1078,151,38);
        place(113,24,1164,100,36);place(IDCANCEL,292,1164,84,36);place(IDOK,388,1164,88,36);
        Refresh();
    }
    IDWriteTextFormat* Format(float size,bool center){
        auto& f=formats[{size,center}];
        if(!f){
            CheckWin32(SUCCEEDED(dw->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&f)),"Create settings text format");
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            if(center)f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }return f.Get();
    }
    void EndFrame(){
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        const HRESULT result=target->EndDraw();
        if(result==D2DERR_RECREATE_TARGET){app_icon.Reset();brush.Reset();target.Reset();Refresh();}
        CheckWin32(SUCCEEDED(result),"Render complete settings frame");
    }
    void Paint(HDC dc,RECT area,int control=0,UINT state=0,bool composite=false){
        if(!control||!composite){
            if(!factory)CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf())),"Create settings renderer");
            if(!dw)CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dw.GetAddressOf()))),"Create settings text renderer");
            if(!target){auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));CheckWin32(SUCCEEDED(factory->CreateDCRenderTarget(&props,&target)),"Create settings surface renderer");}
            CheckWin32(SUCCEEDED(target->BindDC(dc,&area)),"Bind settings surface");target->SetDpi(96*scale,96*scale);
            if(!brush)CheckWin32(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0),&brush)),"Create settings brush");
            target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());target->Clear(D2D1::ColorF(0,0.f));
        }
        const bool dark=material_dark;const UINT32 bg=PanelBackground(dark),ink=dark?0xedf4ff:0x243142,muted=dark?0x9ba9bd:0x65758a,border=dark?0x455164:0xdce2eb,accent=0x0784ff;
        if(composite&&control){
            target->SetTransform(D2D1::Matrix3x2F::Translation(area.left/scale,area.top/scale));
            target->PushAxisAlignedClip({0,0,(area.right-area.left)/scale,(area.bottom-area.top)/scale},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }
        auto color=[&](UINT32 c){brush->SetColor(D2D1::ColorF(c));};
        auto box=[&](D2D1_RECT_F r,UINT32 fill,UINT32 stroke){brush->SetColor(D2D1::ColorF(fill,fill==bg?.3f:1.f));target->FillRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());color(stroke);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());};
        auto text=[&](const std::wstring& value,D2D1_RECT_F r,float size,UINT32 c,bool center=false){text_renderer.Draw(target.Get(),dw.Get(),value,Format(size,center),r,D2D1::ColorF(c));};
        if(!control){
            brush->SetColor(D2D1::ColorF(bg,PanelOpacity(dark,acrylic)));target->FillRoundedRectangle(D2D1::RoundedRect({0,0,500,PanelHeight},12,12),brush.Get());
            app_icon.Draw(target.Get(),{26,24,48,47},true);text(L"LumaShot 设置",{57,17,430,49},18,ink);
            text(L"全局快捷键",{24,59,470,87},14,ink);
            text(L"剪贴板",{24,248,145,290},14,ink);text(L"截图翻译",{24,300,145,342},14,ink);text(L"截图",{24,92,145,134},14,ink);text(L"GIF 动图",{24,144,145,186},14,ink);text(L"屏幕录制",{24,196,145,238},14,ink);
            text(L"GIF / 录屏快捷键打开选区，点击开始后录制",{24,383,476,406},12,accent);
            text(hint.empty()?(recording?L"Esc 取消录入":L"点击录入，支持 Win 键；退格清除"):hint,{24,349,479,377},12,error?0xe45b66:muted);
            color(border);target->DrawLine({24,414},{476,414},brush.Get());target->DrawLine({24,1143},{476,1143},brush.Get());
            text(L"包含鼠标指针",{24,423,405,449},14,ink);text(L"保留唤起截图时的指针形态",{24,450,405,473},12,muted);
            text(L"截图粘贴为文件",{24,485,405,511},14,ink);
            text(L"粘贴到文件夹或聊天时使用所选格式",{24,512,405,535},12,muted);
            text(L"开启剪贴板",{24,601,405,628},14,ink);text(L"仅开启后记录 · 可用快捷键或侧边栏唤出",{24,629,413,654},11,muted);
            text(L"显示剪贴板侧边条",{24,663,405,690},14,draft.clipboard_enabled?ink:muted);text(L"拖动侧边条到屏幕底部的 × 可随时隐藏",{24,691,413,716},11,muted);
            text(L"退出后保留剪贴板历史",{24,725,405,752},14,ink);text(L"加密保存在本机；关闭后删除已保存的历史",{24,753,413,778},11,muted);
            text(L"开机自启动",{24,787,405,814},14,ink);text(L"登录后后台启动，恢复未关闭的贴图",{24,815,413,840},11,muted);
            text(L"自动检查更新",{24,849,300,876},14,ink);
            text(update_requested?std::wstring(L"正在检查，结果会单独提示"):L"当前版本 "+update::VersionText(update::CurrentVersion())+L" · 每天最多一次",{24,877,300,902},11,update_requested?accent:muted);
            text(L"截图贴纸",{24,921,476,946},14,ink);
            text(L"仅装饰桌面贴图，保存后立即生效；不影响复制和导出",{24,947,476,967},12,muted);
            text(L"界面主题",{24,1046,476,1071},14,ink);
        }else{
            const float w=(area.right-area.left)/scale,h=(area.bottom-area.top)/scale;const D2D1_RECT_F r{1,1,w-1,h-1};
            const bool disabled=(state&ODS_DISABLED)!=0,focus=(state&ODS_FOCUS)!=0,pressed=(state&ODS_SELECTED)!=0;
            if(control==101||control==115||control==126||control==127||control==129||control==130||control==132){const bool checked=control==132?draft.clipboard_strip_visible:control==130?draft.update_auto_check:control==127?draft.start_with_windows:control==129?draft.clipboard_persist:control==101?draft.include_cursor:control==126?draft.clipboard_enabled:draft.paste_as_file;if(disabled)brush->SetOpacity(.4f);color(checked?accent:border);target->FillRoundedRectangle(D2D1::RoundedRect({3,4,w-3,h-4},11,11),brush.Get());color(0xffffff);target->FillEllipse(D2D1::Ellipse({checked?w-15:15,h/2},8,8),brush.Get());brush->SetOpacity(1);}
            else if(control==103||control==119||control==120||control==128||control==133){box(r,dark?0x1d2430:0xffffff,error?0xe45b66:recording||focus?accent:border);if(recording&&control==shortcut_id)text(Mods()?ShortcutLabel(Mods(),0)+L" + …":L"请按下快捷键（F1–F11 可单独使用）…",{15,0,w-14,h},16,ink);
                else {const auto current=AllShortcuts(draft)[control==133?4:control==128?3:control==119?1:control==120?2:0];const auto label=current.key?ShortcutLabel(current.modifiers,current.key):L"未设置";float x=13;size_t begin=0;
                    while(begin<label.size()){const auto end=label.find(L" + ",begin);const auto part=label.substr(begin,end==std::wstring::npos?end:end-begin);const float width=std::min(105.f,std::max(34.f,18+9.f*static_cast<float>(part.size())));
                        box({x,9,x+width,h-9},dark?0x303b4b:0xf3f6fa,border);text(part,{x+4,9,x+width-4,h-9},14,ink,true);x+=width;
                        if(end==std::wstring::npos)break;text(L"+",{x,9,x+23,h-9},13,muted,true);x+=23;begin=end+3;
                    }
                }}
            else if(control>=121&&control<=125){
                const auto style=static_cast<PinStyle>(control-121);const bool selected=style==draft.pin_style;
                box(r,selected?(dark?0x183e66:0xe2efff):hover==control?(dark?0x354257:0xe6ebf3):bg,focus||selected?accent:border);
                const float x=w/2-16,y=9,radius=style==PinStyle::Rounded?5.f:1.f;
                if(style!=PinStyle::None){brush->SetColor(D2D1::ColorF(0x182838,.18f));target->FillRoundedRectangle(D2D1::RoundedRect({x+2,y+3,x+34,y+27},radius,radius),brush.Get());}
                color(style==PinStyle::Polaroid?0xfff4da:0xffffff);target->FillRoundedRectangle(D2D1::RoundedRect({x,y,x+32,y+24},radius,radius),brush.Get());
                const float inset=style==PinStyle::None?0:style==PinStyle::Simple?1.f:4.f;
                color(0x8dc6e8);target->FillRectangle({x+inset,y+inset,x+32-inset,y+24-(style==PinStyle::Polaroid?8:inset)},brush.Get());
                if(style==PinStyle::Curl){color(0xd5c8b6);target->FillRectangle({x+26,y+18,x+32,y+24},brush.Get());color(0xfffcf4);target->DrawLine({x+25,y+24},{x+32,y+17},brush.Get(),2);}
                wchar_t label[32]{};GetDlgItemTextW(window,control,label,32);text(label,{0,35,w,h-2},12,selected?accent:ink,true);
            }
            else {const bool selected=(control>=110&&control<=112&&draft.theme==control-110)||(control>=116&&control<=118&&draft.paste_file_format==control-116);const bool primary=control==IDOK;
                if((control!=114&&control!=113)||pressed||hover==control||focus)box(r,primary?(disabled?border:accent):selected?(dark?0x183e66:0xe2efff):pressed||hover==control?(dark?0x354257:0xe6ebf3):bg,focus?accent:control==114||control==113?bg:border);
                wchar_t label[64]{};GetDlgItemTextW(window,control,label,64);text(label,{0,0,w,h},14,primary?0xffffff:disabled?muted:selected?accent:ink,true);
            }
            if(focus&&(control==101||control==115||control==126||control==127||control==129||control==130||control==132)){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());}
        }
        if(composite&&control)target->PopAxisAlignedClip();
        if(!composite)EndFrame();
    }
    void RenderSurface(){
        RECT client{};GetClientRect(window,&client);if(client.right<=0||client.bottom<=0)return;
        if(!surface||surface_width!=client.right||surface_height!=client.bottom){surface_width=client.right;surface_height=client.bottom;surface=std::make_unique<DibSurface>(surface_width,surface_height);}
        Paint(surface->Dc(),client,0,0,true);
        for(int id:{103,119,120,128,133,134,101,115,126,132,129,127,131,130,116,117,118,121,122,123,124,125,110,111,112,113,IDOK,IDCANCEL,114}){
            HWND child=GetDlgItem(window,id);RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
            const int width=r.right-r.left,height=r.bottom-r.top;if(width<=0||height<=0)continue;
            UINT state=0;if(GetFocus()==child)state|=ODS_FOCUS;if(!IsWindowEnabled(child))state|=ODS_DISABLED;if(SendMessageW(child,BM_GETSTATE,0,0)&BST_PUSHED)state|=ODS_SELECTED;
            Paint(surface->Dc(),r,id,state,true);
        }
        EndFrame();
    }

    bool Present(){
        RenderSurface();if(!surface)return false;POINT origin{};SIZE size{surface_width,surface_height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        const bool ok=UpdateLayeredWindow(window,nullptr,nullptr,&size,surface->Dc(),&origin,0,&blend,ULW_ALPHA)!=FALSE;
        if(ok)++presents;return ok;
    }
    static INT_PTR CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
        auto* p=reinterpret_cast<Settings*>(GetWindowLongPtrW(w,DWLP_USER));
        if(msg==WM_INITDIALOG){p=reinterpret_cast<Settings*>(lp);p->window=w;SetWindowLongPtrW(w,DWLP_USER,lp);            SetDialogDpiChangeBehavior(w,DDC_DISABLE_ALL,DDC_DISABLE_ALL);
            ConfigureBorderlessWindow(w);SetWindowTextW(w,L"LumaShot 设置");
            p->material_dark=p->draft.Dark();p->acrylic=SetSettingsAcrylic(w,p->material_dark);
            for(int id:{103,119,120,128,133,134,101,115,126,132,129,127,131,130,116,117,118,121,122,123,124,125,110,111,112,113,IDOK,IDCANCEL,114})SetWindowSubclass(GetDlgItem(w,id),Button,1,reinterpret_cast<DWORD_PTR>(p));

            POINT cursor{};GetCursorPos(&cursor);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);
            SetWindowPos(w,nullptr,monitor.rcWork.left+24,monitor.rcWork.top+24,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            p->scale=std::min({GetDpiForWindow(w)/96.f,(monitor.rcWork.right-monitor.rcWork.left)/500.f,(monitor.rcWork.bottom-monitor.rcWork.top)/PanelHeight});const int width=int(500*p->scale),height=int(PanelHeight*p->scale);
            SetWindowPos(w,nullptr,monitor.rcWork.left+std::max(0L,(monitor.rcWork.right-monitor.rcWork.left-width)/2),monitor.rcWork.top+std::max(0L,(monitor.rcWork.bottom-monitor.rcWork.top-height)/2),width,height,SWP_NOZORDER);p->Layout();
            // Upload one complete alpha frame while the dialog is still hidden.
            // DialogBox shows it only after WM_INITDIALOG returns.
            try{p->prepared_before_show=p->Present();}catch(...){p->prepared_before_show=false;}
            if(!p->prepared_before_show){EndDialog(w,IDCANCEL);return FALSE;}
            return TRUE;}
        if(!p)return FALSE;
        switch(msg){
        case WM_ERASEBKGND:return TRUE;
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(w,&ps);try{p->Present();}catch(...){p->surface.reset();}EndPaint(w,&ps);return TRUE;}
        case WM_DRAWITEM:p->Refresh();return TRUE;
        case WM_NCCALCSIZE:SetWindowLongPtrW(w,DWLP_MSGRESULT,0);return TRUE;
        case WM_SIZE:if(wp!=SIZE_MINIMIZED&&GetDlgItem(w,103))p->Layout();return TRUE;
        case WM_NCHITTEST:{POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&pt);if(pt.y<60*p->scale&&pt.x<445*p->scale){SetWindowLongPtrW(w,DWLP_MSGRESULT,HTCAPTION);return TRUE;}break;}
        case WM_DPICHANGED:{const auto r=*reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);p->Layout();return TRUE;}
        case WM_SETTINGCHANGE:p->Refresh();return TRUE;
        case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE&&(p->recording||p->draining)){p->EditMods()=p->previous_mods;p->EditKey()=p->previous_key;p->Stop();p->Validate();EnableWindow(GetDlgItem(w,IDOK),!p->error);p->Refresh();}break;
        case Changed:p->Refresh();return TRUE;
        case Finished:p->Stop();p->Validate();EnableWindow(GetDlgItem(w,IDOK),!p->error);p->Refresh();return TRUE;
        case WM_COMMAND:{const int id=LOWORD(wp);if(HIWORD(wp)!=BN_CLICKED)break;
            if(id==IDCANCEL||id==114){p->Stop();EndDialog(w,IDCANCEL);return TRUE;}
            if(p->recording||p->draining){p->EditMods()=p->previous_mods;p->EditKey()=p->previous_key;p->Stop();}
            if(id==103||id==119||id==120||id==128||id==133){p->shortcut_id=id;p->Begin();}
            else if(id==134){if(!LaunchTranslationSettings(p->material_dark)){p->error=true;p->hint=L"无法打开翻译引擎设置";}}
            else if(id==101)p->draft.include_cursor=!p->draft.include_cursor;
            else if(id==127)p->draft.start_with_windows=!p->draft.start_with_windows;
            else if(id==126)p->draft.clipboard_enabled=!p->draft.clipboard_enabled;
            else if(id==129)p->draft.clipboard_persist=!p->draft.clipboard_persist;
            else if(id==132)p->draft.clipboard_strip_visible=!p->draft.clipboard_strip_visible;
            else if(id==130)p->draft.update_auto_check=!p->draft.update_auto_check;
            else if(id==131){p->update_requested=true;if(p->check_updates)p->check_updates();}
            else if(id==115)p->draft.paste_as_file=!p->draft.paste_as_file;
            else if(id>=116&&id<=118&&p->draft.paste_as_file)p->draft.paste_file_format=id-116;
            else if(id>=121&&id<=125)p->draft.pin_style=static_cast<PinStyle>(id-121);
            else if(id>=110&&id<=112)p->draft.theme=id-110;
            else if(id==113){p->draft.start_with_windows=true;p->draft.clipboard_enabled=false;p->draft.clipboard_persist=false;p->draft.clipboard_strip_visible=true;p->draft.pin_style=DefaultPinStyle;p->draft.gif_key='G';p->draft.video_key='R';p->draft.translate_key='Y';p->draft.translate_modifiers=MOD_CONTROL|MOD_ALT;p->draft.gif_modifiers=p->draft.video_modifiers=MOD_CONTROL|MOD_ALT;p->draft.key='A';p->draft.modifiers=MOD_CONTROL|MOD_ALT;p->draft.theme=0;p->draft.include_cursor=true;p->draft.paste_as_file=true;p->draft.paste_file_format=0;p->draft.update_auto_check=true;}
            else if(id==IDOK){if(p->Validate()&&(!p->accept||p->accept(p->draft))){EndDialog(w,IDOK);return TRUE;}p->error=true;p->hint=L"该快捷键已被占用或由系统保留，请更换组合键";}
            if(id!=103&&id!=119&&id!=120&&id!=128&&id!=133){if(id!=IDOK)p->Validate();EnableWindow(GetDlgItem(w,IDOK),!p->error);p->Refresh();}return TRUE;}
        case WM_CLOSE:p->Stop();EndDialog(w,IDCANCEL);return TRUE;
        case WM_DESTROY:p->Stop();return TRUE;
        }return FALSE;
    }
};
}
bool ShowSettingsDialog(HWND owner,Preferences& value,const std::function<bool(const Preferences&)>& accept,const std::function<void(bool)>& recording_changed,const std::function<void()>& check_updates){
    Settings state{value,value,accept,recording_changed,check_updates};
    const auto result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(100),owner,Settings::Proc,reinterpret_cast<LPARAM>(&state));
    if(result!=IDOK)return false;value=state.draft;return true;
}
}







