#include "app/hotkeys.h"
#include "app/settings_dialog.h"
#include "app/translation_settings.h"
#include "ui/acrylic.h"
#include "ui/brand_icon.h"
#include "ui/text_renderer.h"
#include "update/manifest.h"
#include "translate/engine.h"
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
#include <tuple>
#include <vector>
#include <cmath>

namespace lumashot {
using Microsoft::WRL::ComPtr;
bool ShortcutModifier(UINT k){return k==VK_CONTROL||k==VK_LCONTROL||k==VK_RCONTROL||k==VK_SHIFT||k==VK_LSHIFT||k==VK_RSHIFT||k==VK_MENU||k==VK_LMENU||k==VK_RMENU||k==VK_LWIN||k==VK_RWIN;}

namespace {
constexpr UINT Changed=WM_APP+71,Finished=WM_APP+72;
// Layout in DIP: sidebar navigation + one page at a time. The window only
// scales down when the work area is smaller than the panel itself.
constexpr float PanelWidth=780,PanelHeight=560,SideWidth=196,ContentLeft=220,ContentRight=756,CardTop=88,RowHeight=58,FooterTop=500;
constexpr int Controls[]={135,136,137,138,139,140,103,119,120,128,133,134,141,101,115,126,132,129,127,131,130,116,117,118,121,122,123,124,125,110,111,112,113,IDOK,IDCANCEL,114};
// Page that owns a control; -1 for the always-visible sidebar and footer.
constexpr int PageOf(int id){
    switch(id){
    case 103:case 119:case 120:case 128:case 133:return 0;
    case 101:case 115:case 116:case 117:case 118:return 1;
    case 126:case 132:case 129:return 2;
    case 134:case 141:return 3;
    case 110:case 111:case 112:case 121:case 122:case 123:case 124:case 125:return 4;
    case 127:case 130:case 131:return 5;
    default:return -1;
    }
}
std::wstring Widen(std::string_view value){
    if(value.empty())return {};
    const int size=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring out(static_cast<size_t>(std::max(size,0)),L'\0');
    if(size>0)MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),size);
    return out;
}
struct Settings {
    Preferences initial,draft;const std::function<bool(const Preferences&)>& accept;
    const std::function<void(bool)>& recording_changed;
    const std::function<void()>& check_updates;
    bool update_requested{};
    HWND window{};int hover{};HHOOK hook{};float scale{1};bool recording{},draining{},error{},acrylic{},material_dark{};
    int shortcut_id{103};
    int page{};std::wstring tr_name,tr_detail;bool tr_ready{};
    UINT& EditMods(){return shortcut_id==133?draft.translate_modifiers:shortcut_id==128?draft.clipboard_modifiers:shortcut_id==119?draft.gif_modifiers:shortcut_id==120?draft.video_modifiers:draft.modifiers;}
    UINT& EditKey(){return shortcut_id==133?draft.translate_key:shortcut_id==128?draft.clipboard_key:shortcut_id==119?draft.gif_key:shortcut_id==120?draft.video_key:draft.key;}
    UINT previous_mods{},previous_key{};std::array<bool,256> held{};std::wstring hint;
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1SolidColorBrush> brush;BrandIcon app_icon;TextRenderer text_renderer;
    std::map<std::tuple<float,bool,bool>,ComPtr<IDWriteTextFormat>> formats;
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
    void SetPage(int next){
        page=std::clamp(next,0,5);draft.settings_page=page;
        for(int id:Controls){
            const int owner=PageOf(id);if(owner<0)continue;
            const HWND child=GetDlgItem(window,id);const bool show=owner==page;
            if(!show&&GetFocus()==child)SetFocus(GetDlgItem(window,135+page));
            ShowWindow(child,show?SW_SHOWNA:SW_HIDE);
        }
        Refresh();
    }
    // Summary for the translation page; the engine window owns the details.
    void LoadTranslation(){
        const auto config=translate::LoadConfig(translate::DefaultConfigPath());
        const auto* preset=translate::FindPreset(config.provider);
        tr_ready=preset&&translate::Ready(config);
        if(!preset){tr_name=L"尚未设置翻译引擎";tr_detail=L"可选本地模型（Ollama、LM Studio）或在线翻译服务";return;}
        tr_name=std::wstring(preset->label);
        std::string model;if(const auto* settings=config.Settings(preset->id))model=settings->model;
        if(model.empty())model=std::string(preset->model);
        tr_detail=(model.empty()?std::wstring():Widen(model)+L" · ")+L"默认译为 "+std::wstring(translate::LanguageLabel(config.target));
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
    void Layout(){RECT client{};GetClientRect(window,&client);scale=std::min({GetDpiForWindow(window)/96.f,client.right/PanelWidth,client.bottom/PanelHeight});
        ResizeBorderlessWindow(window,scale);
        const auto place=[&](int id,float x,float y,float w,float h){SetWindowPos(GetDlgItem(window,id),nullptr,int(x*scale),int(y*scale),int(w*scale),int(h*scale),SWP_NOZORDER|SWP_NOACTIVATE);};
        const auto row=[](int index){return CardTop+index*RowHeight;};
        for(int i=0;i<6;++i)place(135+i,10,76+i*40.f,SideWidth-20,36);
        place(114,PanelWidth-42,10,30,30);
        // 快捷键
        {int i=0;for(int id:{103,119,120,128,133})place(id,500,row(i++)+10,ContentRight-16-500,38);}
        // 截图
        place(101,ContentRight-68,row(0)+14,52,30);place(115,ContentRight-68,row(1)+14,52,30);
        for(int i=0;i<3;++i)place(116+i,548+i*68.f,row(2)+14,66,30);
        // 剪贴板
        place(126,ContentRight-68,row(0)+14,52,30);place(132,ContentRight-68,row(1)+14,52,30);place(129,ContentRight-68,row(2)+14,52,30);
        // 截图翻译
        place(134,ContentRight-136,CardTop+24,120,36);place(141,ContentRight-68,184+RowHeight+14,52,30);
        // 外观
        for(int i=0;i<3;++i)place(110+i,236+i*140.f,130,128,96);
        for(int i=0;i<5;++i)place(121+i,236+i*100.f,310,90,94);
        // 通用
        place(127,ContentRight-68,row(0)+14,52,30);place(130,ContentRight-68,row(1)+14,52,30);place(131,ContentRight-126,row(2)+11,110,36);
        place(113,SideWidth+14,FooterTop+12,96,36);place(IDCANCEL,ContentRight-176,FooterTop+12,84,36);place(IDOK,ContentRight-84,FooterTop+12,84,36);
        Refresh();
    }
    IDWriteTextFormat* Format(float size,bool center,bool bold=false){
        auto& f=formats[{size,center,bold}];
        if(!f){
            CheckWin32(SUCCEEDED(dw->CreateTextFormat(L"Microsoft YaHei UI",nullptr,bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&f)),"Create settings text format");
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
        const bool dark=material_dark;
        const UINT32 bg=PanelBackground(dark),ink=dark?0xedf4ff:0x243142,muted=dark?0x9ba9bd:0x65758a,faint=dark?0x6f7c90:0x9aa6b5,border=dark?0x455164:0xdce2eb,accent=0x0784ff,
            accent_ink=dark?0x7dbbff:0x0660c9,selected_fill=dark?0x183e66:0xe2efff,error_ink=0xe45b66,ok_ink=dark?0x3ecf7f:0x1f9d57,warn_ink=dark?0xf0a53a:0xc77700;
        if(composite&&control){
            target->SetTransform(D2D1::Matrix3x2F::Translation(area.left/scale,area.top/scale));
            target->PushAxisAlignedClip({0,0,(area.right-area.left)/scale,(area.bottom-area.top)/scale},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }
        auto color=[&](UINT32 c,float alpha=1.f){brush->SetColor(D2D1::ColorF(c,alpha));};
        auto box=[&](D2D1_RECT_F r,UINT32 fill,UINT32 stroke){brush->SetColor(D2D1::ColorF(fill,fill==bg?.3f:1.f));target->FillRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());color(stroke);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());};
        auto text=[&](const std::wstring& value,D2D1_RECT_F r,float size,UINT32 c,bool center=false,bool bold=false){text_renderer.Draw(target.Get(),dw.Get(),value,Format(size,center,bold),r,D2D1::ColorF(c));};
        // Read-only key caps, right-aligned at `right` (used for summaries).
        auto keycaps=[&](const std::wstring& label,float right,float top,float bottom){
            std::vector<std::pair<std::wstring,float>> parts;float total=0;size_t begin=0;
            while(begin<label.size()){const auto end=label.find(L" + ",begin);auto part=label.substr(begin,end==std::wstring::npos?end:end-begin);
                const float width=std::min(105.f,std::max(34.f,18+9.f*static_cast<float>(part.size())));total+=width+(parts.empty()?0:23);parts.emplace_back(std::move(part),width);
                if(end==std::wstring::npos)break;begin=end+3;}
            float x=right-total;
            for(size_t k=0;k<parts.size();++k){if(k){text(L"+",{x,top,x+23,bottom},13,muted,true);x+=23;}
                box({x,top,x+parts[k].second,bottom},dark?0x303b4b:0xf3f6fa,border);text(parts[k].first,{x+4,top,x+parts[k].second-4,bottom},13,ink,true);x+=parts[k].second;}
        };
        auto card=[&](float top,float bottom){const auto r=D2D1::RoundedRect({ContentLeft,top,ContentRight,bottom},9,9);color(0xffffff,dark?.045f:.82f);target->FillRoundedRectangle(r,brush.Get());color(border);target->DrawRoundedRectangle(r,brush.Get());};
        // One settings row inside a card. Child rows are indented and tinted to
        // show they depend on the row above; `narrow` leaves room for wide controls.
        auto row=[&](float card_top,int index,const std::wstring& title,const std::wstring& detail,bool child=false,bool last=false,bool enabled=true,bool narrow=false,UINT32 detail_color=0){
            const float top=card_top+index*RowHeight,bottom=top+RowHeight;
            if(child){color(dark?0x000000:0xe9eef5,dark?.16f:.6f);
                if(last){target->FillRoundedRectangle(D2D1::RoundedRect({ContentLeft+1,top,ContentRight-1,bottom-1},8,8),brush.Get());target->FillRectangle({ContentLeft+1,top,ContentRight-1,bottom-10},brush.Get());}
                else target->FillRectangle({ContentLeft+1,top,ContentRight-1,bottom},brush.Get());}
            if(index){color(border);target->DrawLine({ContentLeft,top},{ContentRight,top},brush.Get());}
            const float x=ContentLeft+(child?34.f:16.f),right=narrow?ContentRight-236:ContentRight-84;
            text(title,{x,top+9,right,top+32},14,enabled?ink:faint);
            text(detail,{x,top+31,right,top+50},11.5f,detail_color?detail_color:muted);
        };
        auto nav_icon=[&](int index,float x,float y,UINT32 c){
            color(c);ID2D1Brush* b=brush.Get();constexpr float s=1.5f;
            switch(index){
            case 0:target->DrawRoundedRectangle(D2D1::RoundedRect({x+.5f,y+3,x+15.5f,y+13},2.5f,2.5f),b,s);for(float k:{4.f,8.f,12.f})target->DrawLine({x+k-.5f,y+6.5f},{x+k+.5f,y+6.5f},b,s);target->DrawLine({x+4.5f,y+9.8f},{x+11.5f,y+9.8f},b,s);break;
            case 1:target->DrawLine({x+1,y+5},{x+1,y+1},b,s);target->DrawLine({x+1,y+1},{x+5,y+1},b,s);target->DrawLine({x+11,y+1},{x+15,y+1},b,s);target->DrawLine({x+15,y+1},{x+15,y+5},b,s);
                target->DrawLine({x+15,y+11},{x+15,y+15},b,s);target->DrawLine({x+15,y+15},{x+11,y+15},b,s);target->DrawLine({x+5,y+15},{x+1,y+15},b,s);target->DrawLine({x+1,y+15},{x+1,y+11},b,s);
                target->DrawEllipse(D2D1::Ellipse({x+8,y+8},2.6f,2.6f),b,s);break;
            case 2:target->DrawRoundedRectangle(D2D1::RoundedRect({x+2,y+2.5f,x+14,y+15.5f},2,2),b,s);color(bg,1);target->FillRectangle({x+5,y+.8f,x+11,y+4.2f},b);color(c);target->DrawRoundedRectangle(D2D1::RoundedRect({x+5,y+.8f,x+11,y+4.2f},1,1),b,s);
                target->DrawLine({x+5,y+8.5f},{x+11,y+8.5f},b,s);target->DrawLine({x+5,y+11.8f},{x+9.5f,y+11.8f},b,s);break;
            case 3:text(L"文",{x-2,y-3,x+11,y+10},10.5f,c,true);text(L"A",{x+6,y+4,x+17,y+17},10.5f,c,true);break;
            case 4:target->DrawEllipse(D2D1::Ellipse({x+8,y+8},7,7),b,s);target->PushAxisAlignedClip({x+8,y,x+16,y+16},D2D1_ANTIALIAS_MODE_ALIASED);target->FillEllipse(D2D1::Ellipse({x+8,y+8},7,7),b);target->PopAxisAlignedClip();break;
            default:target->DrawEllipse(D2D1::Ellipse({x+8,y+8},5.2f,5.2f),b,s);target->DrawEllipse(D2D1::Ellipse({x+8,y+8},1.9f,1.9f),b,s);
                for(int k=0;k<8;++k){const float angle=k*3.14159265f/4,cs=std::cos(angle),sn=std::sin(angle);target->DrawLine({x+8+cs*5.4f,y+8+sn*5.4f},{x+8+cs*7.6f,y+8+sn*7.6f},b,2.f);}break;
            }
        };
        if(!control){
            color(bg,PanelOpacity(dark,acrylic));target->FillRoundedRectangle(D2D1::RoundedRect({0,0,PanelWidth,PanelHeight},12,12),brush.Get());
            target->PushAxisAlignedClip({0,0,SideWidth,PanelHeight},D2D1_ANTIALIAS_MODE_ALIASED);
            color(dark?0x000000:0x6f86a6,dark?.16f:.07f);target->FillRoundedRectangle(D2D1::RoundedRect({0,0,PanelWidth,PanelHeight},12,12),brush.Get());
            target->PopAxisAlignedClip();
            color(border);target->DrawLine({SideWidth,0},{SideWidth,PanelHeight},brush.Get());target->DrawLine({SideWidth,FooterTop},{PanelWidth,FooterTop},brush.Get());
            app_icon.Draw(target.Get(),{20,20,48,48},true);
            text(L"LumaShot 设置",{58,17,SideWidth-6,37},15,ink,false,true);text(L"截图 · 贴图 · 录屏",{58,37,SideWidth-6,53},11,muted);
            text(L"版本 "+update::VersionText(update::CurrentVersion()),{20,522,SideWidth-10,542},11,faint);
            static constexpr const wchar_t* titles[]{L"快捷键",L"截图",L"剪贴板",L"截图翻译",L"外观",L"通用"};
            static constexpr const wchar_t* subtitles[]{L"全局生效；点击后按下新组合，Esc 取消，退格清除",L"截取和粘贴时的行为",L"开启后记录复制过的文字和图片，仅保存在本机",L"只发送识别出的文字，不上传截图",L"界面主题与桌面贴图样式，保存后立即生效",L"启动与更新"};
            text(titles[page],{ContentLeft,18,ContentRight-48,48},20,ink,false,true);text(subtitles[page],{ContentLeft,50,ContentRight,70},12,muted);
            switch(page){
            case 0:{
                card(CardTop,CardTop+5*RowHeight);
                row(CardTop,0,L"截图",L"框选区域，编辑、贴图或复制",false,false,true,true);row(CardTop,1,L"GIF 动图",L"打开选区，点击开始后录制",false,false,true,true);
                row(CardTop,2,L"屏幕录制",L"打开选区，点击开始后录制",false,false,true,true);row(CardTop,3,L"剪贴板",L"唤出剪贴板历史",false,false,true,true);
                row(CardTop,4,L"截图翻译",L"框选后识别并翻译",false,false,true,true);
                const float y=CardTop+5*RowHeight+8;
                text(hint.empty()?(recording?L"Esc 取消录入":L"字母、数字键需配合 Ctrl / Alt / Shift / Win；F1–F11、PrtSc、Pause 可单独使用"):hint,{ContentLeft+2,y,ContentRight,y+22},12,error?error_ink:muted);
                break;}
            case 1:{
                card(CardTop,CardTop+3*RowHeight);
                row(CardTop,0,L"包含鼠标指针",L"保留唤起截图时的指针形态");row(CardTop,1,L"截图粘贴为文件",L"粘贴到文件夹或聊天窗口时生成图片文件");
                row(CardTop,2,L"文件格式",L"PNG 无损；JPEG 体积小；BMP 兼容老软件",true,true,draft.paste_as_file,true);
                const float top=CardTop+2*RowHeight;const auto group=D2D1::RoundedRect({544,top+11,754,top+47},8,8);
                color(dark?0x000000:0xffffff,dark?.22f:.9f);target->FillRoundedRectangle(group,brush.Get());color(border);target->DrawRoundedRectangle(group,brush.Get());
                break;}
            case 2:{
                card(CardTop,CardTop+3*RowHeight);
                const std::wstring open=draft.clipboard_key?L"用 "+ShortcutLabel(draft.clipboard_modifiers,draft.clipboard_key)+L" 或侧边条唤出":std::wstring(L"用侧边条唤出");
                row(CardTop,0,L"开启剪贴板",open);row(CardTop,1,L"显示剪贴板侧边条",L"把侧边条拖到屏幕底部的 × 可随时隐藏",true,false,draft.clipboard_enabled);
                row(CardTop,2,L"退出后保留历史",L"加密保存在本机；关闭后删除已保存的历史");
                break;}
            case 3:{
                card(CardTop,CardTop+84);
                color(selected_fill);target->FillRoundedRectangle(D2D1::RoundedRect({ContentLeft+16,CardTop+22,ContentLeft+56,CardTop+62},10,10),brush.Get());
                text(L"译",{ContentLeft+16,CardTop+22,ContentLeft+56,CardTop+62},19,accent_ink,true,true);
                text(tr_name,{ContentLeft+70,CardTop+18,ContentRight-150,CardTop+42},15,ink,false,true);
                text(tr_ready?L"● 已配置":L"● 未配置",{ContentLeft+70,CardTop+44,ContentLeft+132,CardTop+64},12,tr_ready?ok_ink:warn_ink);
                text(tr_detail,{ContentLeft+134,CardTop+44,ContentRight-150,CardTop+64},12,muted);
                const float top=184;card(top,top+2*RowHeight);
                row(top,0,L"快捷键",L"在「快捷键」页修改 · 贴图上按 T 也能翻译",false,false,true,true);
                if(draft.translate_key)keycaps(ShortcutLabel(draft.translate_modifiers,draft.translate_key),ContentRight-16,top+14,top+RowHeight-14);
                else text(L"未设置",{ContentRight-120,top,ContentRight-16,top+RowHeight},13,faint);
                row(top,1,L"完成后显示译文贴图",L"译文覆盖在原文位置；翻译面板里可随时切回原图");
                break;}
            case 4:{
                card(CardTop,236);text(L"界面主题",{ContentLeft+16,CardTop+10,ContentRight,CardTop+34},14,ink);
                card(248,414);text(L"贴图样式",{ContentLeft+16,258,ContentRight,280},14,ink);text(L"只装饰桌面贴图，不影响复制和导出",{ContentLeft+16,280,ContentRight,298},11.5f,muted);
                break;}
            default:{
                card(CardTop,CardTop+3*RowHeight);
                row(CardTop,0,L"开机自启动",L"登录后在后台启动，恢复未关闭的贴图");row(CardTop,1,L"自动检查更新",L"每天最多一次；下载和安装前会先询问");
                row(CardTop,2,L"当前版本 "+update::VersionText(update::CurrentVersion()),update_requested?L"正在检查，结果会单独提示":L"立即检查一次是否有新版本",false,false,true,true,update_requested?accent_ink:0);
                break;}
            }
        }else{
            const float w=(area.right-area.left)/scale,h=(area.bottom-area.top)/scale;const D2D1_RECT_F r{1,1,w-1,h-1};
            const bool disabled=(state&ODS_DISABLED)!=0,focus=(state&ODS_FOCUS)!=0,pressed=(state&ODS_SELECTED)!=0,hot=hover==control;
            if(control==101||control==115||control==126||control==127||control==129||control==130||control==132||control==141){const bool checked=control==141?draft.translate_auto_show:control==132?draft.clipboard_strip_visible:control==130?draft.update_auto_check:control==127?draft.start_with_windows:control==129?draft.clipboard_persist:control==101?draft.include_cursor:control==126?draft.clipboard_enabled:draft.paste_as_file;if(disabled)brush->SetOpacity(.4f);color(checked?accent:border);target->FillRoundedRectangle(D2D1::RoundedRect({3,4,w-3,h-4},11,11),brush.Get());color(0xffffff);target->FillEllipse(D2D1::Ellipse({checked?w-15:15,h/2},8,8),brush.Get());brush->SetOpacity(1);}
            else if(control==103||control==119||control==120||control==128||control==133){box(r,dark?0x1d2430:0xffffff,error?0xe45b66:recording||focus?accent:border);if(recording&&control==shortcut_id)text(Mods()?ShortcutLabel(Mods(),0)+L" + …":L"请按下快捷键（F1–F11 可单独使用）…",{15,0,w-14,h},16,ink);
                else {const auto current=AllShortcuts(draft)[control==133?4:control==128?3:control==119?1:control==120?2:0];const auto label=current.key?ShortcutLabel(current.modifiers,current.key):L"未设置";float x=13;size_t begin=0;
                    while(begin<label.size()){const auto end=label.find(L" + ",begin);const auto part=label.substr(begin,end==std::wstring::npos?end:end-begin);const float width=std::min(105.f,std::max(34.f,18+9.f*static_cast<float>(part.size())));
                        box({x,9,x+width,h-9},dark?0x303b4b:0xf3f6fa,border);text(part,{x+4,9,x+width-4,h-9},14,ink,true);x+=width;
                        if(end==std::wstring::npos)break;text(L"+",{x,9,x+23,h-9},13,muted,true);x+=23;begin=end+3;
                    }
                }}
            else if(control>=135&&control<=140){
                const bool selected=page==control-135;
                if(selected||hot||pressed){color(0xffffff,selected?(dark?.085f:.95f):(dark?.05f:.55f));target->FillRoundedRectangle(D2D1::RoundedRect({0,0,w,h},7,7),brush.Get());}
                if(selected){color(accent);target->FillRoundedRectangle(D2D1::RoundedRect({0,10,3,h-10},1.5f,1.5f),brush.Get());}
                if(focus){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());}
                nav_icon(control-135,13,h/2-8,selected?accent:muted);
                wchar_t label[32]{};GetDlgItemTextW(window,control,label,32);text(label,{40,0,w-8,h},14,ink,false,selected);
            }
            else if(control>=110&&control<=112){
                const int theme=control-110;const bool selected=draft.theme==theme;
                const D2D1_RECT_F preview{2,2,w-2,70};const auto rounded=D2D1::RoundedRect(preview,7,7);
                const UINT32 light_side=0xe9eef5,light_body=0xffffff,dark_side=0x171c25,dark_body=0x262d3a;
                const float side=preview.left+(preview.right-preview.left)*.3f,mid=(preview.left+preview.right)/2;
                color(theme==2?dark_body:light_body);target->FillRoundedRectangle(rounded,brush.Get());
                target->PushAxisAlignedClip({preview.left,preview.top,side,preview.bottom},D2D1_ANTIALIAS_MODE_ALIASED);color(theme==2?dark_side:light_side);target->FillRoundedRectangle(rounded,brush.Get());target->PopAxisAlignedClip();
                if(theme==0){target->PushAxisAlignedClip({mid,preview.top,preview.right,preview.bottom},D2D1_ANTIALIAS_MODE_ALIASED);color(dark_body);target->FillRoundedRectangle(rounded,brush.Get());target->PopAxisAlignedClip();}
                for(float y:{18.f,32.f,46.f}){const float x0=side+8,x1=preview.right-10-(y==32?22:0);
                    if(theme==0){color(0xd5dce6);target->FillRoundedRectangle(D2D1::RoundedRect({x0,y,mid-6,y+6},3,3),brush.Get());color(0x3a4455);target->FillRoundedRectangle(D2D1::RoundedRect({mid+6,y,x1,y+6},3,3),brush.Get());}
                    else{color(theme==2?0x3a4455:0xd5dce6);target->FillRoundedRectangle(D2D1::RoundedRect({x0,y,x1,y+6},3,3),brush.Get());}}
                color(selected?accent:hot?accent_ink:border,selected?1.f:hot?.6f:1.f);target->DrawRoundedRectangle(rounded,brush.Get(),selected?2.f:1.f);
                if(focus&&!selected){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect({1,1,w-1,h-1},8,8),brush.Get());}
                wchar_t label[32]{};GetDlgItemTextW(window,control,label,32);text(label,{0,72,w,h},13,selected?accent_ink:muted,true,selected);
            }
            else if(control>=121&&control<=125){
                const auto style=static_cast<PinStyle>(control-121);const bool selected=style==draft.pin_style;
                const auto frame=D2D1::RoundedRect({1,1,w-1,66},8,8);
                color(dark?0x000000:0xf3f6fa,dark?.18f:1.f);target->FillRoundedRectangle(frame,brush.Get());
                color(selected?accent:hot?accent_ink:border,selected?1.f:hot?.6f:1.f);target->DrawRoundedRectangle(frame,brush.Get(),selected?2.f:1.f);
                const float x=w/2-16,y=21,radius=style==PinStyle::Rounded?5.f:1.f;
                if(style!=PinStyle::None){brush->SetColor(D2D1::ColorF(0x182838,.18f));target->FillRoundedRectangle(D2D1::RoundedRect({x+2,y+3,x+34,y+27},radius,radius),brush.Get());}
                color(style==PinStyle::Polaroid?0xfff4da:0xffffff);target->FillRoundedRectangle(D2D1::RoundedRect({x,y,x+32,y+24},radius,radius),brush.Get());
                const float inset=style==PinStyle::None?0:style==PinStyle::Simple?1.f:4.f;
                color(0x8dc6e8);target->FillRectangle({x+inset,y+inset,x+32-inset,y+24-(style==PinStyle::Polaroid?8:inset)},brush.Get());
                if(style==PinStyle::Curl){color(0xd5c8b6);target->FillRectangle({x+26,y+18,x+32,y+24},brush.Get());color(0xfffcf4);target->DrawLine({x+25,y+24},{x+32,y+17},brush.Get(),2);}
                if(focus&&!selected){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect(r,8,8),brush.Get());}
                wchar_t label[32]{};GetDlgItemTextW(window,control,label,32);text(label,{0,68,w,h},12.5f,selected?accent_ink:muted,true,selected);
            }
            else if(control>=116&&control<=118){
                const bool selected=draft.paste_file_format==control-116;
                if(selected){color(dark?selected_fill:0xe2efff,disabled?.5f:1.f);target->FillRoundedRectangle(D2D1::RoundedRect(r,6,6),brush.Get());}
                else if(hot&&!disabled){color(dark?0xffffff:0x000000,.06f);target->FillRoundedRectangle(D2D1::RoundedRect(r,6,6),brush.Get());}
                if(focus){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect(r,6,6),brush.Get());}
                wchar_t label[16]{};GetDlgItemTextW(window,control,label,16);text(label,{0,0,w,h},13,disabled?faint:selected?accent_ink:muted,true,selected);
            }
            else if(control==IDOK){
                color(disabled?(dark?0x3a4658:0xc9d3df):(pressed?0x0667d6:hot?0x2b95ff:(dark?0x1a6fd6:0x0784ff)));target->FillRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());
                if(focus){color(0xffffff,.7f);target->DrawRoundedRectangle(D2D1::RoundedRect({3,3,w-3,h-3},5,5),brush.Get());}
                text(L"保存",{0,0,w,h},14,disabled?(dark?0x8f9bad:0xffffff):0xffffff,true,true);
            }
            else if(control==113||control==114){
                if(pressed||hot||focus){color(dark?0xffffff:0x000000,pressed?.1f:.06f);target->FillRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());}
                if(focus){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());}
                wchar_t label[32]{};GetDlgItemTextW(window,control,label,32);text(label,{0,0,w,h},control==114?16.f:13.5f,hot||pressed?ink:muted,true);
            }
            else {
                color(pressed?(dark?0x3a4658:0xdfe6ef):hot?(dark?0x343e4d:0xeef3f9):(dark?0x2c3441:0xffffff));target->FillRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());
                color(focus||hot?accent:border,focus?1.f:hot?.7f:1.f);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());
                wchar_t label[32]{};GetDlgItemTextW(window,control,label,32);text(label,{0,0,w,h},13.5f,disabled?muted:ink,true);
            }
            if(focus&&(control==101||control==115||control==126||control==127||control==129||control==130||control==132||control==141)){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());}
        }
        if(composite&&control)target->PopAxisAlignedClip();
        if(!composite)EndFrame();
    }
    void RenderSurface(){
        RECT client{};GetClientRect(window,&client);if(client.right<=0||client.bottom<=0)return;
        if(!surface||surface_width!=client.right||surface_height!=client.bottom){surface_width=client.right;surface_height=client.bottom;surface=std::make_unique<DibSurface>(surface_width,surface_height);}
        Paint(surface->Dc(),client,0,0,true);
        // Focus rings follow the system keyboard-cue state: shown after Tab/arrow
        // navigation, hidden while the user works with the mouse.
        const bool cues=!(SendMessageW(window,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);
        for(int id:Controls){
            HWND child=GetDlgItem(window,id);if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
            const int width=r.right-r.left,height=r.bottom-r.top;if(width<=0||height<=0)continue;
            UINT state=0;if(cues&&GetFocus()==child)state|=ODS_FOCUS;if(!IsWindowEnabled(child))state|=ODS_DISABLED;if(SendMessageW(child,BM_GETSTATE,0,0)&BST_PUSHED)state|=ODS_SELECTED;
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
            for(int id:Controls)SetWindowSubclass(GetDlgItem(w,id),Button,1,reinterpret_cast<DWORD_PTR>(p));

            POINT cursor{};GetCursorPos(&cursor);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);
            SetWindowPos(w,nullptr,monitor.rcWork.left+24,monitor.rcWork.top+24,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            p->scale=std::min({GetDpiForWindow(w)/96.f,(monitor.rcWork.right-monitor.rcWork.left)/PanelWidth,(monitor.rcWork.bottom-monitor.rcWork.top)/PanelHeight});const int width=int(PanelWidth*p->scale),height=int(PanelHeight*p->scale);
            SetWindowPos(w,nullptr,monitor.rcWork.left+std::max(0L,(monitor.rcWork.right-monitor.rcWork.left-width)/2),monitor.rcWork.top+std::max(0L,(monitor.rcWork.bottom-monitor.rcWork.top-height)/2),width,height,SWP_NOZORDER);p->LoadTranslation();p->SetPage(p->draft.settings_page);p->Layout();
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
        case WM_NCHITTEST:{POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&pt);if(pt.y<60*p->scale&&pt.x<(PanelWidth-48)*p->scale){SetWindowLongPtrW(w,DWLP_MSGRESULT,HTCAPTION);return TRUE;}break;}
        case WM_DPICHANGED:{const auto r=*reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);p->Layout();return TRUE;}
        case WM_SETTINGCHANGE:p->Refresh();return TRUE;
        case WM_UPDATEUISTATE:p->Refresh();break;
        case WM_ACTIVATE:if(LOWORD(wp)!=WA_INACTIVE){p->LoadTranslation();p->Refresh();}
            if(LOWORD(wp)==WA_INACTIVE&&(p->recording||p->draining)){p->EditMods()=p->previous_mods;p->EditKey()=p->previous_key;p->Stop();p->Validate();EnableWindow(GetDlgItem(w,IDOK),!p->error);p->Refresh();}break;
        case Changed:p->Refresh();return TRUE;
        case Finished:p->Stop();p->Validate();EnableWindow(GetDlgItem(w,IDOK),!p->error);p->Refresh();return TRUE;
        case WM_COMMAND:{const int id=LOWORD(wp);if(HIWORD(wp)!=BN_CLICKED)break;
            if(id==IDCANCEL||id==114){p->Stop();EndDialog(w,IDCANCEL);return TRUE;}
            if(p->recording||p->draining){p->EditMods()=p->previous_mods;p->EditKey()=p->previous_key;p->Stop();}
            if(id>=135&&id<=140){p->SetPage(id-135);return TRUE;}
            if(id==103||id==119||id==120||id==128||id==133){p->shortcut_id=id;p->Begin();}
            else if(id==134){if(!LaunchTranslationSettings(p->material_dark)){p->error=true;p->hint=L"无法打开翻译引擎设置";}}
            else if(id==101)p->draft.include_cursor=!p->draft.include_cursor;
            else if(id==127)p->draft.start_with_windows=!p->draft.start_with_windows;
            else if(id==126)p->draft.clipboard_enabled=!p->draft.clipboard_enabled;
            else if(id==129)p->draft.clipboard_persist=!p->draft.clipboard_persist;
            else if(id==132)p->draft.clipboard_strip_visible=!p->draft.clipboard_strip_visible;
            else if(id==141)p->draft.translate_auto_show=!p->draft.translate_auto_show;
            else if(id==130)p->draft.update_auto_check=!p->draft.update_auto_check;
            else if(id==131){p->update_requested=true;if(p->check_updates)p->check_updates();}
            else if(id==115)p->draft.paste_as_file=!p->draft.paste_as_file;
            else if(id>=116&&id<=118&&p->draft.paste_as_file)p->draft.paste_file_format=id-116;
            else if(id>=121&&id<=125)p->draft.pin_style=static_cast<PinStyle>(id-121);
            else if(id>=110&&id<=112)p->draft.theme=id-110;
            else if(id==113){p->draft.start_with_windows=true;p->draft.clipboard_enabled=false;p->draft.clipboard_persist=false;p->draft.clipboard_strip_visible=true;p->draft.pin_style=DefaultPinStyle;p->draft.gif_key='G';p->draft.video_key='R';p->draft.translate_key='Y';p->draft.translate_modifiers=MOD_CONTROL|MOD_ALT;p->draft.gif_modifiers=p->draft.video_modifiers=MOD_CONTROL|MOD_ALT;p->draft.key='A';p->draft.modifiers=MOD_CONTROL|MOD_ALT;p->draft.theme=0;p->draft.include_cursor=true;p->draft.paste_as_file=true;p->draft.paste_file_format=0;p->draft.update_auto_check=true;p->draft.translate_auto_show=true;}
            else if(id==IDOK){if(p->Validate()&&(!p->accept||p->accept(p->draft))){EndDialog(w,IDOK);return TRUE;}p->error=true;p->hint=L"该快捷键已被占用或由系统保留，请更换组合键";p->SetPage(0);}
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







