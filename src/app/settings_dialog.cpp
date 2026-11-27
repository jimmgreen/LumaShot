#include "app/hotkeys.h"
#include "app/settings_dialog.h"
#include "app/translation_settings.h"
#include "ui/acrylic.h"
#include "clipboard/composition.h"
#include <chrono>
#include <dwmapi.h>
#include "ui/brand_icon.h"
#include "ui/text_renderer.h"
#include "update/manifest.h"
#include "translate/engine.h"
#include "capture/frame.h"
#include "ocr/config.h"
#include <memory>
#include <commctrl.h>
#include <shobjidl.h>
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
#include <cstdlib>
#include <shellapi.h>

namespace lumashot {
using Microsoft::WRL::ComPtr;
bool ShortcutModifier(UINT k){return k==VK_CONTROL||k==VK_LCONTROL||k==VK_RCONTROL||k==VK_SHIFT||k==VK_LSHIFT||k==VK_RSHIFT||k==VK_MENU||k==VK_LMENU||k==VK_RMENU||k==VK_LWIN||k==VK_RWIN;}

namespace {
constexpr UINT Changed=WM_APP+71,Finished=WM_APP+72;
// Layout in DIP: sidebar navigation + one page at a time. The window only
// scales down when the work area is smaller than the panel itself.
constexpr float PanelWidth=780,PanelHeight=560,SideWidth=196,ContentLeft=220,ContentRight=756,CardTop=88,RowHeight=58,FooterTop=500;
constexpr float ShortcutRowHeight=46;
constexpr int ShortcutControls[]={103,119,120,128,133,148,142,143};
constexpr bool IsShortcutControl(int id){for(int candidate:ShortcutControls)if(id==candidate)return true;return false;}
// Sidebar buttons in page order. 141/142 were taken, so the last two pages use 149/150.
constexpr int NavIds[]={135,136,137,138,139,140,149,150};
constexpr int PageCount=static_cast<int>(std::size(NavIds));
constexpr int NavIndex(int id){for(int i=0;i<PageCount;++i)if(NavIds[i]==id)return i;return -1;}
// 工具面板 page: one chip per customizable toolbar entry (CustomizableToolbarIds order),
// the 按钮文字 segment, the 属性栏样式 segment (窄条 / 卡片) and the 自动展开属性栏 switch.
// Chip control ids, parallel to CustomizableToolbarIds; 贴图 joined later and took 175.
constexpr std::array<int,std::size(CustomizableToolbarIds)> ChipIds{151,152,153,154,155,156,157,175,158,159,160,161};
constexpr int ChipFirst=151;
constexpr int LabelSegFirst=162,AutoPropertiesToggle=165,PropertyStyleSegFirst=176;
constexpr int ToolbarSurfaceSegFirst=178;
// 剪贴板 page: 最多保留 segment (ClipboardLimitChoices), 样式 segment (卡片堆叠 / 经典细条) and the
// 卡片显示内容预览 switch. 关于 page: 项目主页 and 第三方许可 buttons.
constexpr int LimitSegFirst=166,StyleSegFirst=170,CardPreviewToggle=172,HomepageButton=173,NoticesButton=174;
constexpr float ClipboardCards[]={102,272};
constexpr const wchar_t* ProjectUrl=L"https://github.com/jimmgreen/LumaShot";
constexpr const wchar_t* NoticesUrl=L"https://github.com/jimmgreen/LumaShot/blob/main/THIRD-PARTY-NOTICES.md";
constexpr bool IsChip(int id){for(int chip:ChipIds)if(chip==id)return true;return false;}
constexpr int ChipEntry(int id){for(size_t i=0;i<ChipIds.size();++i)if(ChipIds[i]==id)return CustomizableToolbarIds[i];return -1;}
constexpr int ChipControl(int entry){for(size_t i=0;i<std::size(CustomizableToolbarIds);++i)if(CustomizableToolbarIds[i]==entry)return ChipIds[i];return -1;}
constexpr float CaptureCards[]={102,272,396};
constexpr float ZoneTopPanel=102,ZoneTopMore=218,ZoneHeight=84,ChipHeight=32,ChipGap=4,ChipRow=36,ToolbarDisplayTop=334,ToolbarSurfaceTop=276;
constexpr float PinLineTop=266,AboutUpdateTop=184;
constexpr const wchar_t* ChipLabel(int entry){
    switch(entry){case 0:return L"选择";case 1:return L"矩形";case 2:return L"圆形";case 3:return L"箭头";case 4:return L"铅笔";case 5:return L"文字";case 6:return L"马赛克";
    case 14:return L"序号";case 13:return L"贴图";case 15:return L"识别文字";case 18:return L"截图翻译";case 17:return L"长截图";case 16:return L"录制";default:return L"";}
}
constexpr float ChipWidth(int entry){return 44+13.f*static_cast<float>(std::char_traits<wchar_t>::length(ChipLabel(entry)));}
constexpr int Controls[]={142,143,148,144,145,146,135,136,137,138,139,140,149,150,151,152,153,154,155,156,157,175,158,159,160,161,162,163,164,165,176,177,178,179,166,167,168,169,170,171,172,173,174,103,119,120,128,133,134,141,101,115,126,132,129,127,131,130,116,117,118,121,122,123,124,125,110,111,112,147,113,IDOK,IDCANCEL,114};
// Page that owns a control; -1 for the always-visible sidebar and footer.
constexpr int PageOf(int id){
    switch(id){
    case 127:case 130:return 0;
    case 103:case 119:case 120:case 128:case 133:case 142:case 143:case 148:return 1;
    case 101:case 115:case 116:case 117:case 118:case 144:case 145:case 146:case 134:case 141:return 2;
    case 126:case 132:case 129:case 166:case 167:case 168:case 169:case 170:case 171:case 172:return 4;
    case 121:case 122:case 123:case 124:case 125:case 147:return 5;
    case 110:case 111:case 112:return 6;
    case ToolbarSurfaceSegFirst:case ToolbarSurfaceSegFirst+1:return 6;
    case 131:case 173:case 174:return 7;
    case PropertyStyleSegFirst:case PropertyStyleSegFirst+1:return 3;
    default:return (id>=ChipFirst&&id<=AutoPropertiesToggle)||IsChip(id)?3:-1;
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
    int page{};std::wstring tr_name,tr_detail,ocr_hint;bool tr_ready{};
    // 恢复本页默认 asks once: the first click arms it, the second restores, anything else cancels.
    bool reset_armed{};
    // 工具面板 drag state: the chip being pressed, whether it moved far enough to be a
    // drag, and where it would land (zone + the entry it goes before; -1 = zone end).
    int drag_chip{},drop_before{-1};bool dragging{},drop_panel{},drop_valid{};POINT drag_origin{};
    ComPtr<ID2D1StrokeStyle> dashed;
    // Flow layout for one zone; the panel zone starts with the fixed 选择 chip (entry 0).
    std::vector<std::pair<int,D2D1_RECT_F>> ZoneChips(bool panel)const{
        std::vector<std::pair<int,D2D1_RECT_F>> out;const float top=panel?ZoneTopPanel:ZoneTopMore;float x=ContentLeft+8,y=top+8;
        const auto add=[&](int entry){const float width=ChipWidth(entry);if(x+width>ContentRight-8&&x>ContentLeft+8){x=ContentLeft+8;y+=ChipRow;}out.emplace_back(entry,D2D1_RECT_F{x,y,x+width,y+ChipHeight});x+=width+ChipGap;};
        for(int entry:draft.toolbar.order)if(draft.toolbar.OnPanel(entry)==panel)add(entry);
        return out;
    }
    bool DropTarget(D2D1_POINT_2F p,bool& panel,int& before)const{
        const int lifted=ChipEntry(drag_chip);
        for(bool zone:{true,false}){
            const float top=zone?ZoneTopPanel:ZoneTopMore;
            if(p.x<ContentLeft-12||p.x>ContentRight+12||p.y<top-14||p.y>top+ZoneHeight+14)continue;
            panel=zone;before=-1;
            for(const auto& [entry,r]:ZoneChips(zone)){
                if(entry==0||entry==lifted)continue;
                if(p.y<r.top-ChipGap||(p.y<=r.bottom+ChipGap&&p.x<(r.left+r.right)/2)){before=entry;break;}
            }
            return true;
        }
        return false;
    }
    void MoveChip(int entry,bool panel,int before){
        auto& t=draft.toolbar;if(!IsCustomizableToolbarId(entry)||entry==before)return;
        std::erase(t.order,entry);
        const auto at=before>=0?std::find(t.order.begin(),t.order.end(),before):t.order.end();
        t.order.insert(at,entry);t.SetOnPanel(entry,panel);
    }
    // Ctrl+←/→ on a focused chip: swap with its neighbour inside the same zone.
    void ShiftChip(int entry,int step){
        auto& t=draft.toolbar;const bool panel=t.OnPanel(entry);std::vector<int> zone;
        for(int e:t.order)if(t.OnPanel(e)==panel)zone.push_back(e);
        const auto index=std::find(zone.begin(),zone.end(),entry)-zone.begin()+step;
        if(index<0||index>=static_cast<std::ptrdiff_t>(zone.size()))return;
        std::iter_swap(std::find(t.order.begin(),t.order.end(),entry),std::find(t.order.begin(),t.order.end(),zone[static_cast<size_t>(index)]));
    }
    ID2D1StrokeStyle* Dashed(){
        if(!dashed){const auto props=D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT,D2D1_CAP_STYLE_FLAT,D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND,10.f,D2D1_DASH_STYLE_DASH,0.f);
            CheckWin32(SUCCEEDED(factory->CreateStrokeStyle(props,nullptr,0,&dashed)),"Create settings dash style");}
        return dashed.Get();
    }
    UINT& EditMods(){return shortcut_id==148?draft.selection_modifiers:shortcut_id==142?draft.ocr_modifiers:shortcut_id==143?draft.pin_modifiers:shortcut_id==133?draft.translate_modifiers:shortcut_id==128?draft.clipboard_modifiers:shortcut_id==119?draft.gif_modifiers:shortcut_id==120?draft.video_modifiers:draft.modifiers;}
    UINT& EditKey(){return shortcut_id==148?draft.selection_key:shortcut_id==142?draft.ocr_key:shortcut_id==143?draft.pin_key:shortcut_id==133?draft.translate_key:shortcut_id==128?draft.clipboard_key:shortcut_id==119?draft.gif_key:shortcut_id==120?draft.video_key:draft.key;}
    UINT previous_mods{},previous_key{};std::array<bool,256> held{};std::wstring hint;
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1SolidColorBrush> brush;BrandIcon app_icon;TextRenderer text_renderer;
    std::map<std::tuple<float,bool,bool>,ComPtr<IDWriteTextFormat>> formats;
    bool prepared_before_show{};unsigned presents{};
    // Cumulative paint cost (tests and diagnostics read it; a few counters per text draw).
    struct PaintProfile{unsigned frames{},partials{},texts{};double frame_ms{},partial_ms{},background_ms{},text_ms{};uint64_t shaping_us{},raster_us{},composition_us{},upload_us{};} profile;
    std::unique_ptr<DibSurface> surface;int surface_width{},surface_height{};std::unique_ptr<ClipboardComposition> composition;
    // Retained frame. `base` is the page without its controls. Content changes (Refresh)
    // re-render everything; a control-state change (hover, focus, press, enable) repaints
    // only that control over its base pixels. Moves and exposure render nothing: the
    // DirectComposition content outlives them (dragging the window back from off-screen
    // used to re-render the whole dialog several times per mouse move).
    std::unique_ptr<DibSurface> base;ComPtr<ID2D1Bitmap> base_bitmap;bool frame_dirty{true};std::vector<int> dirty_controls;std::map<int,UINT> drawn_state;
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
        // Strip options need a visible strip; the content preview only applies to the card stack.
        const bool strip_on=draft.clipboard_enabled&&draft.clipboard_strip_visible,preview_on=strip_on&&!draft.clipboard_classic_strip;
        for(int id:{StyleSegFirst,StyleSegFirst+1})if((IsWindowEnabled(GetDlgItem(window,id))!=FALSE)!=strip_on)EnableWindow(GetDlgItem(window,id),strip_on);
        if((IsWindowEnabled(GetDlgItem(window,CardPreviewToggle))!=FALSE)!=preview_on)EnableWindow(GetDlgItem(window,CardPreviewToggle),preview_on);
        const bool dark=draft.Dark();if(dark!=material_dark){material_dark=dark;acrylic=ApplyPopupBackdrop(window,dark);if(acrylic)SendMessageW(window,WM_NCACTIVATE,TRUE,0);}
        frame_dirty=true;InvalidateRect(window,nullptr,FALSE);
    }
    // Only this control's look changed; the next frame repaints just its rectangle.
    void RefreshControl(int id){
        if(!id)return;
        if(!frame_dirty&&std::find(dirty_controls.begin(),dirty_controls.end(),id)==dirty_controls.end())dirty_controls.push_back(id);
        InvalidateRect(window,nullptr,FALSE);
    }
    bool Cues()const{return !(SendMessageW(window,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);}
    static UINT ControlState(HWND child,bool cues){
        UINT state=0;if(cues&&GetFocus()==child)state|=ODS_FOCUS;if(!IsWindowEnabled(child))state|=ODS_DISABLED;if(SendMessageW(child,BM_GETSTATE,0,0)&BST_PUSHED)state|=ODS_SELECTED;return state;
    }
    UINT DrawnKey(int id,UINT state)const{return state|(hover==id?0x10000u:0u);}
    // WM_DRAWITEM also arrives for plain exposure; repaint only when the look differs from the last frame.
    void SyncControl(int id){
        const HWND child=GetDlgItem(window,id);if(!child)return;
        const auto drawn=drawn_state.find(id);
        if(drawn==drawn_state.end()||drawn->second!=DrawnKey(id,ControlState(child,Cues())))RefreshControl(id);
    }
    void SetPage(int next){
        page=std::clamp(next,0,PageCount-1);draft.settings_page=page;reset_armed=false;
        for(int id:Controls){
            const int owner=PageOf(id);if(owner<0)continue;
            const HWND child=GetDlgItem(window,id);const bool show=owner==page;
            if(!show&&GetFocus()==child)SetFocus(GetDlgItem(window,NavIds[page]));
            ShowWindow(child,show?SW_SHOWNA:SW_HIDE);
        }
        // 关于 has nothing to restore.
        {const HWND reset=GetDlgItem(window,113);if(page==PageCount-1&&GetFocus()==reset)SetFocus(GetDlgItem(window,NavIds[page]));ShowWindow(reset,page==PageCount-1?SW_HIDE:SW_SHOWNA);}
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
    void ChooseOcrModel(){
        ComPtr<IFileOpenDialog> dialog;
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){ocr_hint=L"无法打开文件夹选择窗口";return;}
        DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"选择兼容的本地 ONNX 模型文件夹");
        if(FAILED(dialog->Show(window)))return;
        ComPtr<IShellItem> item;PWSTR raw{};
        if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&raw)))return;
        struct FreePath{PWSTR value;~FreePath(){CoTaskMemFree(value);}} path{raw};
        const std::filesystem::path directory(path.value);
        SetOcrModel(directory);
    }
    bool SetOcrModel(const std::filesystem::path& directory){
        ocr_hint=ocr::ModelDirectoryError(directory);if(!ocr_hint.empty())return false;
        draft.ocr_model_directory=directory;ocr_hint.clear();return true;
    }
    void Begin(){if(recording||draining)return;previous_mods=EditMods();previous_key=EditKey();hint.clear();error=false;
        for(UINT k:{VK_LCONTROL,VK_RCONTROL,VK_LMENU,VK_RMENU,VK_LSHIFT,VK_RSHIFT,VK_LWIN,VK_RWIN})held[k]=(GetAsyncKeyState(static_cast<int>(k))&0x8000)!=0;
        if(recording_changed)recording_changed(true);
        recorder=this;hook=SetWindowsHookExW(WH_KEYBOARD_LL,Keyboard,GetModuleHandleW(nullptr),0);recording=hook!=nullptr;
        if(!hook){if(recording_changed)recording_changed(false);recorder=nullptr;hint=L"无法开始录入，请重试";error=true;}EnableWindow(GetDlgItem(window,IDOK),FALSE);Refresh();
    }
    bool Validate(){error=false;hint.clear();
        if(!UniqueShortcuts(draft)){error=true;hint=L"快捷键重复，请为各项功能设置不同组合";return false;}
        const auto next=AllShortcuts(draft),old=AllShortcuts(initial);
        for(const auto k:next){if((k.modifiers==MOD_WIN&&k.key=='V')||!k.key||std::find(old.begin(),old.end(),k)!=old.end())continue;
            if(RegisterHotKey(window,77,k.modifiers|MOD_NOREPEAT,k.key)){UnregisterHotKey(window,77);continue;}
            error=true;hint=L"快捷键已被占用或由系统保留，请更换组合键";return false;
        }return true;
    }
    static LRESULT CALLBACK Button(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& s=*reinterpret_cast<Settings*>(data);
        if(msg==WM_MOUSEMOVE&&s.hover!=GetDlgCtrlID(w)){const int old=s.hover;s.hover=GetDlgCtrlID(w);s.RefreshControl(old);s.RefreshControl(s.hover);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);}
        if(msg==WM_MOUSELEAVE){s.hover=0;s.RefreshControl(GetDlgCtrlID(w));}
        if(msg==WM_GETDLGCODE&&lp){const auto& event=*reinterpret_cast<MSG*>(lp);if(event.message==WM_KEYDOWN&&event.wParam==VK_RETURN)return DLGC_WANTMESSAGE;}
        if(msg==WM_KEYDOWN&&wp==VK_RETURN){SendMessageW(s.window,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(w),BN_CLICKED),reinterpret_cast<LPARAM>(w));return 0;}
        if(const int id=GetDlgCtrlID(w);IsChip(id)){
            const bool ctrl=GetKeyState(VK_CONTROL)<0;
            if(msg==WM_GETDLGCODE&&lp&&ctrl){const auto& event=*reinterpret_cast<MSG*>(lp);if(event.message==WM_KEYDOWN&&(event.wParam==VK_LEFT||event.wParam==VK_RIGHT))return DLGC_WANTMESSAGE;}
            if(msg==WM_KEYDOWN&&ctrl&&(wp==VK_LEFT||wp==VK_RIGHT)){s.ShiftChip(ChipEntry(id),wp==VK_LEFT?-1:1);s.Layout();return 0;}
            if(msg==WM_LBUTTONDOWN){s.drag_chip=id;s.dragging=false;s.drop_valid=false;GetCursorPos(&s.drag_origin);}
            else if(msg==WM_MOUSEMOVE&&s.drag_chip==id&&(wp&MK_LBUTTON)){
                POINT cursor{};GetCursorPos(&cursor);
                const bool started=!s.dragging&&std::abs(cursor.x-s.drag_origin.x)+std::abs(cursor.y-s.drag_origin.y)>GetSystemMetrics(SM_CXDRAG);if(started)s.dragging=true;
                if(s.dragging){ScreenToClient(s.window,&cursor);const bool was_valid=s.drop_valid,was_panel=s.drop_panel;const int was_before=s.drop_before;
                    s.drop_valid=s.DropTarget({cursor.x/s.scale,cursor.y/s.scale},s.drop_panel,s.drop_before);
                    SetCursor(LoadCursorW(nullptr,s.drop_valid?IDC_SIZEALL:IDC_NO));
                    // A full frame only when the insertion mark moves, not on every mouse move.
                    if(started||was_valid!=s.drop_valid||(s.drop_valid&&(was_panel!=s.drop_panel||was_before!=s.drop_before)))s.Refresh();return 0;}
            }
            else if(msg==WM_LBUTTONUP&&s.drag_chip==id){
                const bool dropped=s.dragging;s.drag_chip=0;s.dragging=false;
                if(dropped){ReleaseCapture();SendMessageW(w,BM_SETSTATE,FALSE,0);if(s.drop_valid)s.MoveChip(ChipEntry(id),s.drop_panel,s.drop_before);s.drop_valid=false;s.Layout();return 0;}
            }
            else if(msg==WM_CAPTURECHANGED&&s.dragging){s.dragging=false;s.drag_chip=0;s.drop_valid=false;s.Refresh();}
        }
        if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_ENABLE){s.RefreshControl(GetDlgCtrlID(w));}
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(w,Button,1);
        return DefSubclassProc(w,msg,wp,lp);
    }
    void OpenLink(const wchar_t* url)const{ShellExecuteW(window,L"open",url,nullptr,nullptr,SW_SHOWNORMAL);}
    // Installed builds ship THIRD-PARTY-NOTICES.md next to the exe (package.ps1). Notepad
    // opens it even without a .md association; development builds fall back to the web copy.
    void OpenNotices()const{
        wchar_t module[MAX_PATH]{};const DWORD length=GetModuleFileNameW(nullptr,module,MAX_PATH);
        if(length&&length<MAX_PATH){
            const auto notices=std::filesystem::path(module).parent_path()/L"THIRD-PARTY-NOTICES.md";std::error_code ec;
            if(std::filesystem::is_regular_file(notices,ec)){const std::wstring args=L"\""+notices.native()+L"\"";
                if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",L"notepad.exe",args.c_str(),nullptr,SW_SHOWNORMAL))>32)return;}
        }
        OpenLink(NoticesUrl);
    }
    void Layout(){RECT client{};GetClientRect(window,&client);scale=std::min({GetDpiForWindow(window)/96.f,client.right/PanelWidth,client.bottom/PanelHeight});
        const auto place=[&](int id,float x,float y,float w,float h){SetWindowPos(GetDlgItem(window,id),nullptr,int(x*scale),int(y*scale),int(w*scale),int(h*scale),SWP_NOZORDER|SWP_NOACTIVATE);};
        const auto row=[](int index){return CardTop+index*RowHeight;};
        for(int i=0;i<PageCount;++i)place(NavIds[i],10,76+i*40.f,SideWidth-20,36);
        place(114,PanelWidth-42,10,30,30);
        // 常规
        place(127,ContentRight-68,row(0)+14,52,30);place(130,ContentRight-68,row(1)+14,52,30);
        // 快捷键
        {int i=0;for(int id:ShortcutControls)place(id,500,CardTop+i++*ShortcutRowHeight+7,ContentRight-16-500,36);}
        // 截图: three compact cards (截图与粘贴 / 文字识别 / 翻译)
        {const auto compact=[](float card,int index){return card+index*ShortcutRowHeight;};
         place(101,ContentRight-68,compact(CaptureCards[0],0)+8,52,30);place(115,ContentRight-68,compact(CaptureCards[0],1)+8,52,30);
         for(int i=0;i<3;++i)place(116+i,548+i*68.f,compact(CaptureCards[0],2)+8,66,30);
         place(144,ContentRight-68,compact(CaptureCards[1],0)+8,52,30);
         place(145,ContentRight-244,compact(CaptureCards[1],1)+6,120,34);place(146,ContentRight-116,compact(CaptureCards[1],1)+6,100,34);
         place(134,ContentRight-136,compact(CaptureCards[2],0)+6,120,34);place(141,ContentRight-68,compact(CaptureCards[2],1)+8,52,30);}
        // 工具面板
        for(bool panel:{true,false})for(const auto& [entry,r]:ZoneChips(panel))if(entry)place(ChipControl(entry),r.left,r.top,r.right-r.left,r.bottom-r.top);
        {constexpr float widths[]{58,98,70};float x=ContentRight-16-233;for(int i=0;i<3;++i){place(LabelSegFirst+i,x,ToolbarDisplayTop+8,widths[i],30);x+=widths[i]+2;}}
        // 显示 uses compact rows: 按钮文字 / 属性栏样式 / 自动展开属性栏.
        for(int i=0;i<2;++i)place(PropertyStyleSegFirst+i,ContentRight-12-float(2-i)*88,ToolbarDisplayTop+ShortcutRowHeight+8,86,30);
        place(AutoPropertiesToggle,ContentRight-68,ToolbarDisplayTop+2*ShortcutRowHeight+8,52,30);
        // 剪贴板
        {const auto compact=[](float card,int index){return card+index*ShortcutRowHeight;};
         place(126,ContentRight-68,compact(ClipboardCards[0],0)+8,52,30);place(129,ContentRight-68,compact(ClipboardCards[0],1)+8,52,30);
         for(int i=0;i<4;++i)place(LimitSegFirst+i,ContentRight-12-float(4-i)*54,compact(ClipboardCards[0],2)+8,52,30);
         place(132,ContentRight-68,compact(ClipboardCards[1],0)+8,52,30);
         for(int i=0;i<2;++i)place(StyleSegFirst+i,ContentRight-12-float(2-i)*88,compact(ClipboardCards[1],1)+8,86,30);
         place(CardPreviewToggle,ContentRight-68,compact(ClipboardCards[1],2)+8,52,30);}
        // 贴图
        for(int i=0;i<5;++i)place(121+i,236+i*100.f,CardTop+62,90,94);
        place(147,ContentRight-68,PinLineTop+14,52,30);
        // 外观
        for(int i=0;i<3;++i)place(110+i,236+i*140.f,130,128,96);
        for(int i=0;i<2;++i)place(ToolbarSurfaceSegFirst+i,ContentRight-12-float(2-i)*88,ToolbarSurfaceTop+8,86,30);
        // 关于
        place(131,ContentRight-126,AboutUpdateTop+11,110,36);place(HomepageButton,ContentRight-126,AboutUpdateTop+RowHeight+11,110,36);place(NoticesButton,ContentRight-126,AboutUpdateTop+2*RowHeight+11,110,36);
        place(113,SideWidth+14,FooterTop+12,116,36);place(IDCANCEL,ContentRight-176,FooterTop+12,84,36);place(IDOK,ContentRight-84,FooterTop+12,84,36);
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
        if(result==D2DERR_RECREATE_TARGET){app_icon.Reset();brush.Reset();base_bitmap.Reset();target.Reset();Refresh();}
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
        auto text=[&](const std::wstring& value,D2D1_RECT_F r,float size,UINT32 c,bool center=false,bool bold=false){const auto start=std::chrono::steady_clock::now();const auto st=text_renderer.Draw(target.Get(),dw.Get(),value,Format(size,center,bold),r,D2D1::ColorF(c));
            ++profile.texts;profile.text_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();profile.shaping_us+=st.shaping_time_us;profile.raster_us+=st.raster_time_us;profile.composition_us+=st.composition_time_us;profile.upload_us+=st.upload_time_us;};
        auto card=[&](float top,float bottom){const auto r=D2D1::RoundedRect({ContentLeft,top,ContentRight,bottom},9,9);color(0xffffff,dark?.045f:.82f);target->FillRoundedRectangle(r,brush.Get());color(border);target->DrawRoundedRectangle(r,brush.Get());};
        // One settings row inside a card. Child rows are indented and tinted to
        // show they depend on the row above; `narrow` leaves room for wide controls.
        auto row=[&](float card_top,int index,const std::wstring& title,const std::wstring& detail,bool child=false,bool last=false,bool enabled=true,bool narrow=false,UINT32 detail_color=0){
            const bool compact=page==1||page==2||page==3||page==4;const float height=compact?ShortcutRowHeight:RowHeight;
            const float top=card_top+index*height,bottom=top+height;
            if(child){color(dark?0x000000:0xe9eef5,dark?.16f:.6f);
                if(last){target->FillRoundedRectangle(D2D1::RoundedRect({ContentLeft+1,top,ContentRight-1,bottom-1},8,8),brush.Get());target->FillRectangle({ContentLeft+1,top,ContentRight-1,bottom-10},brush.Get());}
                else target->FillRectangle({ContentLeft+1,top,ContentRight-1,bottom},brush.Get());}
            if(index){color(border);target->DrawLine({ContentLeft,top},{ContentRight,top},brush.Get());}
            const float x=ContentLeft+(child?34.f:16.f),right=narrow?ContentRight-236:ContentRight-84;
            text(title,{x,top+(compact?4.f:9.f),right,top+(compact?27.f:32.f)},14,enabled?ink:faint);
            text(detail,{x,top+(compact?26.f:31.f),right,top+(compact?45.f:50.f)},11.5f,detail_color?detail_color:muted);
        };
        // Small 16 px glyphs for the 工具面板 chips; entry ids follow the capture toolbar.
        auto chip_icon=[&](int entry,float x,float y,UINT32 c){
            color(c);ID2D1Brush* b=brush.Get();constexpr float s=1.5f;
            switch(entry){
            case 0:target->DrawLine({x+4,y+2},{x+4,y+14},b,s);target->DrawLine({x+4,y+14},{x+7.5f,y+10.5f},b,s);target->DrawLine({x+7.5f,y+10.5f},{x+12.5f,y+10.5f},b,s);target->DrawLine({x+12.5f,y+10.5f},{x+4,y+2},b,s);break;
            case 1:target->DrawRoundedRectangle(D2D1::RoundedRect({x+2,y+3.5f,x+14,y+12.5f},2,2),b,s);break;
            case 2:target->DrawEllipse(D2D1::Ellipse({x+8,y+8},6.2f,5.4f),b,s);break;
            case 3:target->DrawLine({x+3,y+13},{x+13,y+3},b,s);target->DrawLine({x+13,y+3},{x+7.5f,y+3},b,s);target->DrawLine({x+13,y+3},{x+13,y+8.5f},b,s);break;
            case 4:target->DrawLine({x+4,y+12},{x+12.5f,y+3.5f},b,2.4f);target->DrawLine({x+2.5f,y+13.5f},{x+4,y+12},b,s);break;
            case 5:text(L"T",{x,y-1,x+16,y+16},14,c,true,true);break;
            case 6:for(int i=0;i<3;++i)for(int j=0;j<3;++j)if((i+j)%2==0)target->FillRectangle({x+2+i*4.f,y+2+j*4.f,x+6+i*4.f,y+6+j*4.f},b);target->DrawRectangle({x+2,y+2,x+14,y+14},b,1);break;
            case 13:{const D2D1_POINT_2F pin_outline[]{{x+5.5f,y+2},{x+10.5f,y+2},{x+10,y+6},{x+12,y+8},{x+12,y+9.5f},{x+4,y+9.5f},{x+4,y+8},{x+6,y+6}};
                for(size_t k=0;k<std::size(pin_outline);++k)target->DrawLine(pin_outline[k],pin_outline[(k+1)%std::size(pin_outline)],b,s);
                target->DrawLine({x+8,y+9.5f},{x+8,y+14.5f},b,s);break;}
            case 14:target->DrawEllipse(D2D1::Ellipse({x+8,y+8},6.5f,6.5f),b,s);text(L"1",{x,y,x+16,y+16},9.5f,c,true,true);break;
            case 15:for(float side:{-1.f,1.f}){target->DrawLine({x+8+side*6,y+5},{x+8+side*6,y+2},b,s);target->DrawLine({x+8+side*6,y+2},{x+8+side*3,y+2},b,s);target->DrawLine({x+8+side*6,y+11},{x+8+side*6,y+14},b,s);target->DrawLine({x+8+side*6,y+14},{x+8+side*3,y+14},b,s);}
                target->DrawLine({x+5.5f,y+6.5f},{x+10.5f,y+6.5f},b,s);target->DrawLine({x+5.5f,y+9.5f},{x+9,y+9.5f},b,s);break;
            case 18:text(L"译",{x-1,y-1,x+17,y+17},11.5f,c,true);break;
            case 17:target->DrawRoundedRectangle(D2D1::RoundedRect({x+4,y+1.5f,x+12,y+14.5f},1.5f,1.5f),b,s);target->DrawLine({x+6.5f,y+6},{x+9.5f,y+6},b,s);target->DrawLine({x+6.5f,y+10},{x+9.5f,y+10},b,s);break;
            case 16:target->DrawEllipse(D2D1::Ellipse({x+8,y+8},6.5f,6.5f),b,s);target->FillEllipse(D2D1::Ellipse({x+8,y+8},3,3),b);break;
            default:break;
            }
        };
        auto nav_icon=[&](int index,float x,float y,UINT32 c){
            color(c);ID2D1Brush* b=brush.Get();constexpr float s=1.5f;
            // Page order: 常规 keys capture toolbar clipboard pin look about.
            static constexpr int kinds[]{5,0,1,6,2,7,4,8};
            switch(kinds[std::clamp(index,0,PageCount-1)]){
            case 6:target->DrawRoundedRectangle(D2D1::RoundedRect({x+.5f,y+4,x+15.5f,y+12},3,3),b,s);for(float k:{4.5f,8.f,11.5f})target->FillEllipse(D2D1::Ellipse({x+k,y+8},1.1f,1.1f),b);break;
            case 7:target->DrawLine({x+8,y+11},{x+8,y+15.5f},b,s);target->DrawLine({x+5,y+1},{x+11,y+1},b,s);target->DrawLine({x+6,y+1},{x+5.2f,y+6},b,s);target->DrawLine({x+10,y+1},{x+10.8f,y+6},b,s);target->DrawLine({x+5.2f,y+6},{x+2.5f,y+9},b,s);target->DrawLine({x+10.8f,y+6},{x+13.5f,y+9},b,s);target->DrawLine({x+2.5f,y+9},{x+2.5f,y+11},b,s);target->DrawLine({x+13.5f,y+9},{x+13.5f,y+11},b,s);target->DrawLine({x+2.5f,y+11},{x+13.5f,y+11},b,s);break;
            case 8:target->DrawEllipse(D2D1::Ellipse({x+8,y+8},7,7),b,s);target->DrawLine({x+8,y+7},{x+8,y+11.5f},b,s);target->FillEllipse(D2D1::Ellipse({x+8,y+4.6f},1,1),b);break;
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
            // One Luma Glass tint (60/72%) over DWM transient acrylic, as the clipboard panel and menus; opaque without
            // transparency effects. Full rect: DWM rounds the corners and draws the outline.
            color(bg,acrylic?glass::Opacity(dark):1.f);target->FillRectangle({0,0,PanelWidth,PanelHeight},brush.Get());
            target->PushAxisAlignedClip({0,0,SideWidth,PanelHeight},D2D1_ANTIALIAS_MODE_ALIASED);
            color(dark?0x000000:0x6f86a6,dark?.16f:.07f);target->FillRectangle({0,0,SideWidth,PanelHeight},brush.Get());
            target->PopAxisAlignedClip();
            color(border);target->DrawLine({SideWidth,0},{SideWidth,PanelHeight},brush.Get());target->DrawLine({SideWidth,FooterTop},{PanelWidth,FooterTop},brush.Get());
            app_icon.Draw(target.Get(),{20,20,48,48},true);
            text(L"LumaShot 设置",{58,17,SideWidth-6,37},15,ink,false,true);text(L"截图 · 贴图 · 录屏",{58,37,SideWidth-6,53},11,muted);
            text(L"版本 "+update::VersionText(update::CurrentVersion()),{20,522,SideWidth-10,542},11,faint);
            static constexpr const wchar_t* titles[]{L"常规",L"快捷键",L"截图",L"工具面板",L"剪贴板",L"贴图",L"外观",L"关于"};
            static constexpr const wchar_t* subtitles[]{L"启动方式和后台行为",L"全局生效；点击后按下新组合，Esc 取消，退格清除",L"截图内容、复制格式、文字识别和翻译",L"点击工具在“面板上”和“更多”菜单之间移动，拖动调整顺序",L"开启后记录复制过的文字和图片，仅保存在本机",L"钉在桌面上的截图外观，保存后立即生效",L"界面主题与截图工具栏背景；其余面板跟随主题",L"版本、更新与开源许可"};
            text(titles[page],{ContentLeft,18,ContentRight-48,48},20,ink,false,true);text(subtitles[page],{ContentLeft,50,ContentRight,70},12,muted);
            const auto group=[&](const std::wstring& label,float top){text(label,{ContentLeft+2,top,ContentRight,top+20},12.5f,muted,false,true);};
            if(reset_armed)text(L"再点一次，恢复“"+std::wstring(titles[page])+L"”页的默认值",{SideWidth+138,FooterTop+12,ContentRight-184,FooterTop+48},12,warn_ink);
            switch(page){
            case 0:{
                card(CardTop,CardTop+2*RowHeight);
                row(CardTop,0,L"开机自启动",L"登录后在后台启动，恢复未关闭的贴图");row(CardTop,1,L"自动检查更新",L"每天最多一次；也可在“关于”中手动检查");
                break;}
            case 1:{
                card(CardTop,CardTop+static_cast<float>(std::size(ShortcutControls))*ShortcutRowHeight);
                row(CardTop,0,L"截图",L"框选区域，编辑、贴图或复制",false,false,true,true);row(CardTop,1,L"GIF 动图",L"打开选区，点击开始后录制",false,false,true,true);
                row(CardTop,2,L"屏幕录制",L"打开选区，点击开始后录制",false,false,true,true);row(CardTop,3,L"剪贴板",L"唤出剪贴板历史",false,false,true,true);
                row(CardTop,4,L"截图翻译",L"框选后识别并翻译",false,false,true,true);
                row(CardTop,5,L"划词翻译",L"翻译任意窗口或贴图中选中的文字",false,false,true,true);
                row(CardTop,6,L"OCR 快捷复制",L"框选后识别文字并复制",false,false,true,true);
                row(CardTop,7,L"置顶贴图",L"框选后直接贴到桌面",false,false,true,true);
                const float y=CardTop+static_cast<float>(std::size(ShortcutControls))*ShortcutRowHeight+8;
                text(hint.empty()?(recording?L"Esc 取消录入":L"字母、数字键需配合 Ctrl / Alt / Shift / Win；F1–F11、PrtSc、Pause 可单独使用"):hint,{ContentLeft+2,y,ContentRight,y+22},12,error?error_ink:muted);
                break;}
            case 2:{
                const float card_a=CaptureCards[0],card_b=CaptureCards[1],card_t=CaptureCards[2];
                group(L"截图与粘贴",card_a-22);card(card_a,card_a+3*ShortcutRowHeight);
                row(card_a,0,L"包含鼠标指针",L"保留唤起截图时的指针形态");row(card_a,1,L"截图粘贴为文件",L"粘贴到文件夹或聊天窗口时生成图片文件");
                row(card_a,2,L"文件格式",L"PNG 无损；JPEG 体积小；BMP 兼容老软件",true,true,draft.paste_as_file,true);
                {const float top=card_a+2*ShortcutRowHeight;const auto segment=D2D1::RoundedRect({544,top+5,754,top+41},8,8);
                 color(dark?0x000000:0xffffff,dark?.22f:.9f);target->FillRoundedRectangle(segment,brush.Get());color(border);target->DrawRoundedRectangle(segment,brush.Get());}
                group(L"文字识别",card_b-22);card(card_b,card_b+2*ShortcutRowHeight);
                row(card_b,0,L"OCR 去除空白",L"OCR 快捷复制时移除空格、换行和制表符");
                row(card_b,1,draft.ocr_model_directory.empty()?L"识别模型：内置":L"识别模型：自定义",ocr_hint.empty()?(draft.ocr_model_directory.empty()?std::wstring(L"可改用本地 ONNX 模型（det、rec、字典）"):draft.ocr_model_directory.native()):ocr_hint,false,false,true,true,ocr_hint.empty()?0:error_ink);
                group(L"翻译",card_t-22);card(card_t,card_t+2*ShortcutRowHeight);
                row(card_t,0,L"翻译引擎："+tr_name,tr_detail,false,false,true,true);
                text(tr_ready?L"● 已配置":L"● 未配置",{ContentRight-214,card_t,ContentRight-142,card_t+ShortcutRowHeight},12,tr_ready?ok_ink:warn_ink);
                row(card_t,1,L"完成后显示译文贴图",L"译文覆盖在原文位置；翻译面板里可随时切回原图");
                break;}
            case 3:{
                group(L"面板上",ZoneTopPanel-22);text(L"选择、撤销、保存、完成等始终在面板上",{ContentLeft+64,ZoneTopPanel-22,ContentRight,ZoneTopPanel-2},11.5f,faint);
                const auto panel=D2D1::RoundedRect({ContentLeft,ZoneTopPanel,ContentRight,ZoneTopPanel+ZoneHeight},12,12);
                color(dark?0x000000:0xffffff,dark?.24f:.92f);target->FillRoundedRectangle(panel,brush.Get());color(border);target->DrawRoundedRectangle(panel,brush.Get());
                group(L"“更多”菜单",ZoneTopMore-22);text(L"截图时点工具栏末尾的“···”打开",{ContentLeft+86,ZoneTopMore-22,ContentRight,ZoneTopMore-2},11.5f,faint);
                const auto more=D2D1::RoundedRect({ContentLeft+.5f,ZoneTopMore+.5f,ContentRight-.5f,ZoneTopMore+ZoneHeight-.5f},12,12);
                color(0xffffff,dark?.03f:.5f);target->FillRoundedRectangle(more,brush.Get());color(dragging&&drop_valid&&!drop_panel?accent:border);target->DrawRoundedRectangle(more,brush.Get(),1.3f,Dashed());
                if(draft.toolbar.overflow.empty())text(L"没有收起的工具。点击上方的工具，或把它拖到这里",{ContentLeft+16,ZoneTopMore,ContentRight-16,ZoneTopMore+ZoneHeight},12.5f,faint,true);
                if(dragging&&drop_valid){
                    const auto chips=ZoneChips(drop_panel);const int lifted=ChipEntry(drag_chip);float mark_x=ContentLeft+10,mark_top=(drop_panel?ZoneTopPanel:ZoneTopMore)+8;
                    for(const auto& [entry,r]:chips){if(entry==lifted)continue;if(entry==drop_before){mark_x=r.left-3;mark_top=r.top;break;}if(entry){mark_x=r.right+1;mark_top=r.top;}}
                    color(accent);target->FillRoundedRectangle(D2D1::RoundedRect({mark_x-1.25f,mark_top+4,mark_x+1.25f,mark_top+ChipHeight-4},1.25f,1.25f),brush.Get());
                }
                group(L"显示",ToolbarDisplayTop-22);card(ToolbarDisplayTop,ToolbarDisplayTop+3*ShortcutRowHeight);
                {const auto segment_box=[&](float top,float left,float right){const auto segment=D2D1::RoundedRect({left,top+5,right,top+41},8,8);
                    color(dark?0x000000:0xffffff,dark?.22f:.9f);target->FillRoundedRectangle(segment,brush.Get());color(border);target->DrawRoundedRectangle(segment,brush.Get());};
                 row(ToolbarDisplayTop,0,L"按钮文字",L"“自动”保持默认外观；放不下时只显示图标",false,false,true,true);segment_box(ToolbarDisplayTop,ContentRight-16-236,ContentRight-16);
                 const bool card_style=draft.toolbar.property_style==PropertyBarStyle::Card;
                 row(ToolbarDisplayTop,1,L"属性栏样式",card_style?L"标签在控件上方，分组更醒目":L"单行排列，标签在控件左侧，更省空间",false,false,true,true);segment_box(ToolbarDisplayTop+ShortcutRowHeight,ContentRight-16-2*88.f,ContentRight-8);}
                row(ToolbarDisplayTop,2,L"自动展开属性栏",L"关闭后，点面板上的“属性”按钮再展开");
                break;}
            case 4:{
                const float card_h=ClipboardCards[0],card_s=ClipboardCards[1];
                const auto segment_box=[&](float top,float left){const auto segment=D2D1::RoundedRect({left,top+5,ContentRight-8,top+41},8,8);
                    color(dark?0x000000:0xffffff,dark?.22f:.9f);target->FillRoundedRectangle(segment,brush.Get());color(border);target->DrawRoundedRectangle(segment,brush.Get());};
                group(L"历史记录",card_h-22);card(card_h,card_h+3*ShortcutRowHeight);
                const std::wstring open=draft.clipboard_key?L"用 "+ShortcutLabel(draft.clipboard_modifiers,draft.clipboard_key)+L" 或侧边条唤出":std::wstring(L"用侧边条唤出");
                row(card_h,0,L"开启剪贴板",open);row(card_h,1,L"退出后保留历史",L"加密保存在本机；关闭后删除已保存的历史");
                row(card_h,2,L"最多保留",L"超出后自动删除最早的未收藏条目",false,false,true,true);segment_box(card_h+2*ShortcutRowHeight,ContentRight-16-4*54.f);
                const bool strip_on=draft.clipboard_enabled&&draft.clipboard_strip_visible;
                group(L"侧边条",card_s-22);card(card_s,card_s+3*ShortcutRowHeight);
                row(card_s,0,L"显示剪贴板侧边条",L"把侧边条拖到屏幕底部的 × 可随时隐藏",false,false,draft.clipboard_enabled);
                row(card_s,1,L"样式",draft.clipboard_classic_strip?L"贴在屏幕边缘的细条，点击展开":L"露出最近 3 张卡片，悬停后展开",true,false,strip_on,true);segment_box(card_s+ShortcutRowHeight,ContentRight-16-2*88.f);
                row(card_s,2,L"卡片显示内容预览",L"关闭后卡片只显示类型图标，适合演示或共享屏幕",true,true,strip_on&&!draft.clipboard_classic_strip);
                break;}
            case 5:{
                card(CardTop,PinLineTop-12);text(L"截图贴纸",{ContentLeft+16,CardTop+10,ContentRight,CardTop+32},14,ink);text(L"只装饰桌面贴图，不影响复制和导出",{ContentLeft+16,CardTop+32,ContentRight,CardTop+50},11.5f,muted);
                card(PinLineTop,PinLineTop+RowHeight);row(PinLineTop,0,L"顶部晾衣绳",L"把贴图拖到屏幕顶部或按 H 晾起；关闭后已晾起的贴图放回原处");
                break;}
            case 6:{
                card(CardTop,236);text(L"界面主题",{ContentLeft+16,CardTop+10,ContentRight,CardTop+34},14,ink);
                group(L"截图工具栏",ToolbarSurfaceTop-22);card(ToolbarSurfaceTop,ToolbarSurfaceTop+RowHeight);
                {const bool solid=draft.toolbar.surface_style==ToolbarSurfaceStyle::Solid;
                 row(ToolbarSurfaceTop,0,L"面板材质",solid?L"纯白底框配深色图标，黑底更易辨认":L"亚克力模糊桌面背景",false,true,true,true);
                 const float left=ContentRight-16-2*88.f,right=ContentRight-8;
                 const auto segment=D2D1::RoundedRect({left,ToolbarSurfaceTop+5,right,ToolbarSurfaceTop+41},8,8);
                 color(dark?0x000000:0xffffff,dark?.22f:.9f);target->FillRoundedRectangle(segment,brush.Get());color(border);target->DrawRoundedRectangle(segment,brush.Get());}
                break;}
            default:{
                card(CardTop,CardTop+84);app_icon.Draw(target.Get(),{ContentLeft+18,CardTop+20,ContentLeft+62,CardTop+64},true);
                text(L"LumaShot",{ContentLeft+76,CardTop+18,ContentRight-16,CardTop+42},16,ink,false,true);
                text(L"版本 "+update::VersionText(update::CurrentVersion())+L"  ·  截图 · 贴图 · 录屏",{ContentLeft+76,CardTop+44,ContentRight-16,CardTop+64},12,muted);
                card(AboutUpdateTop,AboutUpdateTop+3*RowHeight);
                row(AboutUpdateTop,0,L"检查更新",update_requested?L"正在检查，结果会单独提示":L"立即检查一次是否有新版本",false,false,true,true,update_requested?accent_ink:0);
                row(AboutUpdateTop,1,L"项目主页",L"github.com/jimmgreen/LumaShot",false,false,true,true);
                row(AboutUpdateTop,2,L"第三方许可",L"LumaShot 使用的开源组件及其许可证",false,false,true,true);
                break;}
            }
        }else{
            const float w=(area.right-area.left)/scale,h=(area.bottom-area.top)/scale;const D2D1_RECT_F r{1,1,w-1,h-1};
            const bool disabled=(state&ODS_DISABLED)!=0,focus=(state&ODS_FOCUS)!=0,pressed=(state&ODS_SELECTED)!=0,hot=hover==control;
            if(control==101||control==115||control==126||control==127||control==129||control==130||control==132||control==141||control==144||control==147||control==AutoPropertiesToggle||control==CardPreviewToggle){const bool checked=control==CardPreviewToggle?draft.clipboard_card_preview:control==AutoPropertiesToggle?draft.toolbar.auto_properties:control==147?draft.clothesline_enabled:control==144?draft.ocr_remove_whitespace:control==141?draft.translate_auto_show:control==132?draft.clipboard_strip_visible:control==130?draft.update_auto_check:control==127?draft.start_with_windows:control==129?draft.clipboard_persist:control==101?draft.include_cursor:control==126?draft.clipboard_enabled:draft.paste_as_file;if(disabled)brush->SetOpacity(.4f);color(checked?accent:border);target->FillRoundedRectangle(D2D1::RoundedRect({3,4,w-3,h-4},11,11),brush.Get());color(0xffffff);target->FillEllipse(D2D1::Ellipse({checked?w-15:15,h/2},8,8),brush.Get());brush->SetOpacity(1);}
            else if(IsShortcutControl(control)){box(r,dark?0x1d2430:0xffffff,error?0xe45b66:recording||focus?accent:border);if(recording&&control==shortcut_id)text(Mods()?ShortcutLabel(Mods(),0)+L" + …":L"请按下快捷键（F1–F11 可单独使用）…",{15,0,w-14,h},16,ink);
                else {const auto current=AllShortcuts(draft)[control==148?7:control==143?6:control==142?5:control==133?4:control==128?3:control==119?1:control==120?2:0];const auto label=current.key?ShortcutLabel(current.modifiers,current.key):L"未设置";float x=13;size_t begin=0;
                    while(begin<label.size()){const auto end=label.find(L" + ",begin);const auto part=label.substr(begin,end==std::wstring::npos?end:end-begin);const float width=std::min(105.f,std::max(34.f,18+9.f*static_cast<float>(part.size())));
                        box({x,9,x+width,h-9},dark?0x303b4b:0xf3f6fa,border);text(part,{x+4,9,x+width-4,h-9},14,ink,true);x+=width;
                        if(end==std::wstring::npos)break;text(L"+",{x,9,x+23,h-9},13,muted,true);x+=23;begin=end+3;
                    }
                }}
            else if(IsChip(control)){
                const int entry=ChipEntry(control);const bool on_panel=draft.toolbar.OnPanel(entry),lifted=dragging&&drag_chip==control;
                const auto chip=D2D1::RoundedRect(r,8,8);
                if(lifted)brush->SetOpacity(.35f);
                if(on_panel){if(hot||pressed||focus){color(dark?0xffffff:0x000000,pressed?.12f:.06f);target->FillRoundedRectangle(chip,brush.Get());}}
                else{color(dark?0x2c3441:0xffffff);target->FillRoundedRectangle(chip,brush.Get());color(hot&&!dragging?accent:border);target->DrawRoundedRectangle(chip,brush.Get());}
                if(focus){color(accent);target->DrawRoundedRectangle(chip,brush.Get());}
                chip_icon(entry,10,h/2-8,lifted?faint:on_panel?ink:muted);
                text(ChipLabel(entry),{29,0,w-12,h},13,lifted?faint:on_panel?ink:muted);
                // Hover hint for a click: chevron down = 收进“更多”, chevron up = 放回面板.
                if(hot&&!dragging){const float cx=w-11,cy=h/2,d=on_panel?1.f:-1.f;color(on_panel?muted:accent);
                    target->DrawLine({cx-3,cy-1.5f*d},{cx,cy+1.5f*d},brush.Get(),1.4f);target->DrawLine({cx,cy+1.5f*d},{cx+3,cy-1.5f*d},brush.Get(),1.4f);}
                brush->SetOpacity(1);
            }
            else if(NavIndex(control)>=0){
                const bool selected=page==NavIndex(control);
                if(selected||hot||pressed){color(0xffffff,selected?(dark?.085f:.95f):(dark?.05f:.55f));target->FillRoundedRectangle(D2D1::RoundedRect({0,0,w,h},7,7),brush.Get());}
                if(selected){color(accent);target->FillRoundedRectangle(D2D1::RoundedRect({0,10,3,h-10},1.5f,1.5f),brush.Get());}
                if(focus){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());}
                nav_icon(NavIndex(control),13,h/2-8,selected?accent:muted);
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
            else if((control>=116&&control<=118)||(control>=LabelSegFirst&&control<LabelSegFirst+3)||(control>=LimitSegFirst&&control<LimitSegFirst+4)||control==StyleSegFirst||control==StyleSegFirst+1||control==PropertyStyleSegFirst||control==PropertyStyleSegFirst+1||(control>=ToolbarSurfaceSegFirst&&control<ToolbarSurfaceSegFirst+2)){
                const bool selected=control>=ToolbarSurfaceSegFirst?static_cast<int>(draft.toolbar.surface_style)==control-ToolbarSurfaceSegFirst:
                    control<=118?draft.paste_file_format==control-116:control<LabelSegFirst+3?static_cast<int>(draft.toolbar.labels)==control-LabelSegFirst:
                    control>=PropertyStyleSegFirst?static_cast<int>(draft.toolbar.property_style)==control-PropertyStyleSegFirst:
                    control<LimitSegFirst+4?draft.clipboard_max_entries==ClipboardLimitChoices[static_cast<size_t>(control-LimitSegFirst)]:draft.clipboard_classic_strip==(control==StyleSegFirst+1);
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
            else if(control==113&&reset_armed){
                color(dark?0x3a2a12:0xfff3e0);target->FillRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());color(warn_ink);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());
                text(L"确认恢复",{0,0,w,h},13.5f,warn_ink,true,true);
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
            if(focus&&(control==101||control==115||control==126||control==127||control==129||control==130||control==132||control==141||control==144||control==147||control==AutoPropertiesToggle||control==CardPreviewToggle)){color(accent);target->DrawRoundedRectangle(D2D1::RoundedRect(r,7,7),brush.Get());}
        }
        if(composite&&control)target->PopAxisAlignedClip();
        if(!composite)EndFrame();
    }
    // Full frame: the page into `base`, then every visible control over it.
    void RenderSurface(){
        RECT client{};GetClientRect(window,&client);if(client.right<=0||client.bottom<=0)return;
        if(!surface||!base||surface_width!=client.right||surface_height!=client.bottom){
            surface_width=client.right;surface_height=client.bottom;
            surface=std::make_unique<DibSurface>(surface_width,surface_height);base=std::make_unique<DibSurface>(surface_width,surface_height);
        }
        frame_dirty=true;  // stays set if painting throws
        const auto frame_start=std::chrono::steady_clock::now();
        Paint(base->Dc(),client,0,0,true);EndFrame();GdiFlush();
        profile.background_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame_start).count();
        // Pixel units (96 DPI): drawn under a 1/scale transform it copies exactly.
        base_bitmap.Reset();
        CheckWin32(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(surface_width),static_cast<UINT32>(surface_height)),base->Pixels(),static_cast<UINT32>(surface_width)*4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&base_bitmap)),"Create settings base layer");
        dirty_controls.clear();drawn_state.clear();
        const std::vector<int> ids(std::begin(Controls),std::end(Controls));
        RenderControls(ids,true);
        frame_dirty=false;
        ++profile.frames;profile.frame_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame_start).count();
    }
    // Controls over the page layer. The DC target renders its whole bound rectangle and
    // writes all of it back at EndDraw, so every pass restores the page pixels of what it
    // binds: the full surface for a full frame, or just one control's rectangle.
    void RenderControls(const std::vector<int>& ids,bool full){
        const RECT client{0,0,surface_width,surface_height};
        struct Item{int id;HWND child;RECT r,area;};std::vector<Item> items;
        for(int id:ids){
            const HWND child=GetDlgItem(window,id);
            if(!child||!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)){drawn_state.erase(id);continue;}
            RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);RECT area{};
            if(r.right<=r.left||r.bottom<=r.top||!IntersectRect(&area,&r,&client))continue;
            items.push_back({id,child,r,area});
        }
        const auto begin=[&](const RECT& bound){
            CheckWin32(SUCCEEDED(target->BindDC(surface->Dc(),&bound)),"Bind settings surface");target->SetDpi(96*scale,96*scale);
            target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));target->SetTransform(D2D1::Matrix3x2F::Scale(1/scale,1/scale));
            target->DrawBitmap(base_bitmap.Get(),D2D1::RectF(0,0,static_cast<float>(bound.right-bound.left),static_cast<float>(bound.bottom-bound.top)),1.f,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                D2D1::RectF(static_cast<float>(bound.left),static_cast<float>(bound.top),static_cast<float>(bound.right),static_cast<float>(bound.bottom)));
        };
        // Focus rings follow the system keyboard-cue state: shown after Tab/arrow
        // navigation, hidden while the user works with the mouse.
        const bool cues=Cues();
        const auto paint=[&](const Item& item,const RECT& bound){
            const UINT state=ControlState(item.child,cues);
            Paint(surface->Dc(),RECT{item.r.left-bound.left,item.r.top-bound.top,item.r.right-bound.left,item.r.bottom-bound.top},item.id,state,true);
            drawn_state[item.id]=DrawnKey(item.id,state);
        };
        if(full){begin(client);for(const auto& item:items)paint(item,client);EndFrame();return;}
        for(const auto& item:items){begin(item.area);paint(item,item.area);EndFrame();}
    }
    bool Dirty()const{return frame_dirty||!dirty_controls.empty()||!surface||!composition;}
    void RenderDirty(){
        RECT client{};GetClientRect(window,&client);
        if(frame_dirty||!surface||!base||!base_bitmap||surface_width!=client.right||surface_height!=client.bottom){RenderSurface();return;}
        if(dirty_controls.empty())return;
        const auto start=std::chrono::steady_clock::now();
        const auto ids=std::move(dirty_controls);dirty_controls.clear();
        RenderControls(ids,false);
        ++profile.partials;profile.partial_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }

    bool Present(){
        RenderDirty();if(!surface)return false;GdiFlush();
        // WS_EX_NOREDIRECTIONBITMAP window (resources.rc): frames go through DirectComposition in both glass and opaque modes.
        bool ok=true;try{if(!composition)composition=std::make_unique<ClipboardComposition>();composition->Present(window,UINT(surface_width),UINT(surface_height),surface->Pixels());}catch(...){ok=false;}
        if(ok)++presents;return ok;
    }
    static INT_PTR CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
        auto* p=reinterpret_cast<Settings*>(GetWindowLongPtrW(w,DWLP_USER));
        if(msg==WM_INITDIALOG){p=reinterpret_cast<Settings*>(lp);p->window=w;SetWindowLongPtrW(w,DWLP_USER,lp);            SetDialogDpiChangeBehavior(w,DDC_DISABLE_ALL,DDC_DISABLE_ALL);
            {const DWORD round=2;DwmSetWindowAttribute(w,33,&round,sizeof(round));}SetWindowTextW(w,L"LumaShot 设置");
            p->material_dark=p->draft.Dark();p->acrylic=ApplyPopupBackdrop(w,p->material_dark);if(p->acrylic)SendMessageW(w,WM_NCACTIVATE,TRUE,0);  // active backdrop even before first activation
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
        // Exposure (moving back from off-screen, uncovering) only validates: the composition keeps the last frame.
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(w,&ps);try{if(p->Dirty())p->Present();}catch(...){p->surface.reset();}EndPaint(w,&ps);return TRUE;}
        case WM_DRAWITEM:p->SyncControl(static_cast<int>(reinterpret_cast<const DRAWITEMSTRUCT*>(lp)->CtlID));return TRUE;
        case WM_NCCALCSIZE:SetWindowLongPtrW(w,DWLP_MSGRESULT,0);return TRUE;
        // Windows 11 shows a flat fallback for inactive backdrops; keep the glass while another window has focus.
        case WM_NCACTIVATE:if(p->acrylic){SetWindowLongPtrW(w,DWLP_MSGRESULT,DefWindowProcW(w,msg,TRUE,lp));return TRUE;}break;
        case WM_SIZE:if(wp!=SIZE_MINIMIZED&&GetDlgItem(w,103))p->Layout();return TRUE;
        case WM_NCHITTEST:{POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&pt);if(pt.y<60*p->scale&&pt.x<(PanelWidth-48)*p->scale){SetWindowLongPtrW(w,DWLP_MSGRESULT,HTCAPTION);return TRUE;}SetWindowLongPtrW(w,DWLP_MSGRESULT,HTCLIENT);return TRUE;}
        case WM_DPICHANGED:{const auto r=*reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);p->Layout();return TRUE;}
        case WM_SETTINGCHANGE:p->Refresh();return TRUE;
        case WM_UPDATEUISTATE:p->Refresh();break;
        // The engine window may have changed the translation summary; repaint only if it did.
        case WM_ACTIVATE:if(LOWORD(wp)!=WA_INACTIVE){const auto before=std::tuple{p->tr_name,p->tr_detail,p->tr_ready};p->LoadTranslation();if(before!=std::tuple{p->tr_name,p->tr_detail,p->tr_ready})p->Refresh();}
            if(LOWORD(wp)==WA_INACTIVE&&(p->recording||p->draining)){p->EditMods()=p->previous_mods;p->EditKey()=p->previous_key;p->Stop();p->Validate();EnableWindow(GetDlgItem(w,IDOK),!p->error);p->Refresh();}break;
        case Changed:p->Refresh();return TRUE;
        case Finished:p->Stop();p->Validate();EnableWindow(GetDlgItem(w,IDOK),!p->error);p->Refresh();return TRUE;
        case WM_COMMAND:{const int id=LOWORD(wp);if(HIWORD(wp)!=BN_CLICKED)break;
            if(id!=113)p->reset_armed=false;
            if(id==IDCANCEL||id==114){p->Stop();EndDialog(w,IDCANCEL);return TRUE;}
            if(p->recording||p->draining){p->EditMods()=p->previous_mods;p->EditKey()=p->previous_key;p->Stop();}
            if(NavIndex(id)>=0){p->SetPage(NavIndex(id));return TRUE;}
            if(IsShortcutControl(id)){p->shortcut_id=id;p->Begin();}
            else if(id==134){if(!LaunchTranslationSettings(p->material_dark)){p->error=true;p->hint=L"无法打开翻译引擎设置";}}
            else if(id==101)p->draft.include_cursor=!p->draft.include_cursor;
            else if(id==127)p->draft.start_with_windows=!p->draft.start_with_windows;
            else if(id==126)p->draft.clipboard_enabled=!p->draft.clipboard_enabled;
            else if(id==129)p->draft.clipboard_persist=!p->draft.clipboard_persist;
            else if(id==132)p->draft.clipboard_strip_visible=!p->draft.clipboard_strip_visible;
            else if(id==144)p->draft.ocr_remove_whitespace=!p->draft.ocr_remove_whitespace;
            else if(id==145)p->ChooseOcrModel();
            else if(id==146){p->draft.ocr_model_directory.clear();p->ocr_hint.clear();}
            else if(id==141)p->draft.translate_auto_show=!p->draft.translate_auto_show;
            else if(id==147)p->draft.clothesline_enabled=!p->draft.clothesline_enabled;
            else if(id==130)p->draft.update_auto_check=!p->draft.update_auto_check;
            else if(id==131){p->update_requested=true;if(p->check_updates)p->check_updates();}
            else if(id==115)p->draft.paste_as_file=!p->draft.paste_as_file;
            else if(id>=116&&id<=118&&p->draft.paste_as_file)p->draft.paste_file_format=id-116;
            else if(id>=121&&id<=125)p->draft.pin_style=static_cast<PinStyle>(id-121);
            else if(id>=110&&id<=112)p->draft.theme=id-110;
            else if(IsChip(id)){const int entry=ChipEntry(id);p->draft.toolbar.SetOnPanel(entry,!p->draft.toolbar.OnPanel(entry));p->Layout();}
            else if(id>=LabelSegFirst&&id<LabelSegFirst+3)p->draft.toolbar.labels=static_cast<ToolbarLabels>(id-LabelSegFirst);
            else if(id==AutoPropertiesToggle)p->draft.toolbar.auto_properties=!p->draft.toolbar.auto_properties;
            else if(id==PropertyStyleSegFirst||id==PropertyStyleSegFirst+1)p->draft.toolbar.property_style=static_cast<PropertyBarStyle>(id-PropertyStyleSegFirst);
            else if(id==ToolbarSurfaceSegFirst||id==ToolbarSurfaceSegFirst+1)p->draft.toolbar.surface_style=static_cast<ToolbarSurfaceStyle>(id-ToolbarSurfaceSegFirst);
            else if(id==113){if(!p->reset_armed)p->reset_armed=true;else{p->reset_armed=false;ResetSettingsPage(p->draft,p->page);if(p->page==2)p->ocr_hint.clear();p->Layout();}}
            else if(id>=LimitSegFirst&&id<LimitSegFirst+4)p->draft.clipboard_max_entries=ClipboardLimitChoices[static_cast<size_t>(id-LimitSegFirst)];
            else if((id==StyleSegFirst||id==StyleSegFirst+1)&&p->draft.clipboard_enabled&&p->draft.clipboard_strip_visible)p->draft.clipboard_classic_strip=id==StyleSegFirst+1;
            else if(id==CardPreviewToggle&&p->draft.clipboard_enabled&&p->draft.clipboard_strip_visible&&!p->draft.clipboard_classic_strip)p->draft.clipboard_card_preview=!p->draft.clipboard_card_preview;
            else if(id==HomepageButton)p->OpenLink(ProjectUrl);
            else if(id==NoticesButton)p->OpenNotices();
            else if(id==IDOK){if(p->Validate()&&(!p->accept||p->accept(p->draft))){EndDialog(w,IDOK);return TRUE;}p->error=true;p->hint=L"该快捷键已被占用或由系统保留，请更换组合键";p->SetPage(0);}
            if(!IsShortcutControl(id)){if(id!=IDOK)p->Validate();EnableWindow(GetDlgItem(w,IDOK),!p->error);p->Refresh();}return TRUE;}
        case WM_CLOSE:p->Stop();EndDialog(w,IDCANCEL);return TRUE;
        case WM_DESTROY:p->Stop();return TRUE;
        }return FALSE;
    }
};
}
void ResetSettingsPage(Preferences& p,int page){
    const Preferences d;
    switch(page){
    case 0:p.start_with_windows=d.start_with_windows;p.update_auto_check=d.update_auto_check;break;
    case 1:p.modifiers=d.modifiers;p.key=d.key;p.gif_modifiers=d.gif_modifiers;p.gif_key=d.gif_key;p.video_modifiers=d.video_modifiers;p.video_key=d.video_key;
        p.clipboard_modifiers=d.clipboard_modifiers;p.clipboard_key=d.clipboard_key;p.translate_modifiers=d.translate_modifiers;p.translate_key=d.translate_key;
        p.selection_modifiers=d.selection_modifiers;p.selection_key=d.selection_key;p.ocr_modifiers=d.ocr_modifiers;p.ocr_key=d.ocr_key;p.pin_modifiers=d.pin_modifiers;p.pin_key=d.pin_key;break;
    case 2:p.include_cursor=d.include_cursor;p.paste_as_file=d.paste_as_file;p.paste_file_format=d.paste_file_format;p.ocr_remove_whitespace=d.ocr_remove_whitespace;
        p.ocr_model_directory=d.ocr_model_directory;p.translate_auto_show=d.translate_auto_show;break;
    case 3:p.toolbar=d.toolbar;break;
    case 4:p.clipboard_enabled=d.clipboard_enabled;p.clipboard_persist=d.clipboard_persist;p.clipboard_strip_visible=d.clipboard_strip_visible;
        p.clipboard_max_entries=d.clipboard_max_entries;p.clipboard_classic_strip=d.clipboard_classic_strip;p.clipboard_card_preview=d.clipboard_card_preview;break;
    case 5:p.pin_style=d.pin_style;p.clothesline_enabled=d.clothesline_enabled;break;
    case 6:p.theme=d.theme;p.toolbar.surface_style=d.toolbar.surface_style;break;
    default:break;
    }
}
bool ShowSettingsDialog(HWND owner,Preferences& value,const std::function<bool(const Preferences&)>& accept,const std::function<void(bool)>& recording_changed,const std::function<void()>& check_updates){
    Settings state{value,value,accept,recording_changed,check_updates};
    const auto result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(100),owner,Settings::Proc,reinterpret_cast<LPARAM>(&state));
    if(result!=IDOK)return false;value=state.draft;return true;
}
}
