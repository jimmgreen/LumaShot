#include "clipboard/panel.h"
#include "clipboard/win_v_shortcut.h"
#include "clipboard/quick_window.h"
#include "clipboard/quick_input.h"
#include "clipboard/liquid_surface.h"
#include "clipboard/input_focus.h"
#include "ui/shortcut_label.h"
#include "clipboard/composition.h"
#include "clipboard/native.h"
#include "clipboard/file_icons.h"
#include "clipboard/session_store.h"
#include "clipboard/layout.h"
#include "clipboard/preview_window.h"
#include "clipboard/preview_cache.h"
#include "ui/acrylic.h"
#include "ui/brand_icon.h"
#include "ui/text_renderer.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include "capture/frame.h"
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <imm.h>
#include <map>
#include <set>
#include <cmath>

namespace lumashot {
using Microsoft::WRL::ComPtr;
struct ClipboardPanel::Impl {
    HWND window{},native_window{},folded_window{},search{},owner{},previous{};HFONT font{};HBRUSH edit_brush{};
    std::function<void()> settings;
    std::function<bool()> hotkey_allowed;bool hotkeys_suspended{};clipboard::WinVShortcut win_v_shortcut;UINT shortcut_modifiers{MOD_WIN},shortcut_key{'V'};
    clipboard::History history;std::vector<uint64_t> visible;
    std::unique_ptr<clipboard::SessionStore> disk;bool persist_history{};
    uint64_t copy_token{},image_token{1},search_token{},preview_token{},last_read_id{};bool copy_pending{},copy_paste{},paste_inflight{};
    HWND copy_target{},paste_target{},previous_focus{},copy_focus{},paste_focus{};DWORD copy_process{},paste_process{},paste_sequence{};
    bool copy_plain{},copy_keyboard{},paste_keyboard{},paste_activated{},paste_returning{};ULONGLONG paste_return_at{};
    std::unique_ptr<clipboard::QuickWindow> quick;clipboard::QuickInput quick_input;
    std::vector<uint64_t> quick_ids;size_t quick_selection{};int quick_tab{};
    HWND quick_target{},quick_focus{};DWORD quick_process{};
    bool quick_active{},quick_continuous{},copy_quick{},paste_quick{};
    clipboard::InputFocusProbe input_focus_probe;clipboard::InputFocusSnapshot shortcut_focus;
    bool shortcut_pending{};ULONGLONG shortcut_deadline{};
    DWORD previous_process{};HWINEVENTHOOK foreground_hook{},ime_hook{};std::set<HWND> ime_windows;
    inline static std::map<HWINEVENTHOOK,Impl*> foreground_observers;
    static void CALLBACK ForegroundChanged(HWINEVENTHOOK hook,DWORD,HWND candidate,LONG,LONG,DWORD,DWORD){
        const auto it=foreground_observers.find(hook);
        if(it!=foreground_observers.end()&&candidate==GetForegroundWindow()){
            auto* self=it->second;if(self->quick_active&&candidate!=self->quick_target)self->CloseQuick();
            if(self->shortcut_pending&&candidate!=self->shortcut_focus.foreground)self->CancelShortcutProbe();
            self->RememberTarget(candidate);
        }
    }
    static void CALLBACK ImeChanged(HWINEVENTHOOK hook,DWORD event,HWND candidate,LONG,LONG,DWORD,DWORD){
        const auto it=foreground_observers.find(hook);if(it==foreground_observers.end())return;
        auto* self=it->second;
        if(event==EVENT_OBJECT_IME_HIDE)self->ime_windows.erase(candidate);
        else if(candidate){self->ime_windows.insert(candidate);self->CancelShortcutProbe();if(self->quick_active)self->CloseQuick();}
    }
    void TrackForeground(){
        if(!foreground_hook){foreground_hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,ForegroundChanged,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);if(foreground_hook)foreground_observers.emplace(foreground_hook,this);}
        if(!ime_hook){ime_hook=SetWinEventHook(EVENT_OBJECT_IME_SHOW,EVENT_OBJECT_IME_HIDE,nullptr,ImeChanged,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);if(ime_hook)foreground_observers.emplace(ime_hook,this);}
        RememberTarget(GetForegroundWindow());
    }
    void UpdateActivationPolicy(){
        const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
        SetWindowLongPtrW(window,GWL_EXSTYLE,(pinned||window==folded_window)?(style|WS_EX_NOACTIVATE):(style&~WS_EX_NOACTIVATE));
    }
    std::vector<uint64_t> search_matches;bool search_complete{};std::wstring completed_query;
    bool enabled{},expanded{},dark{},acrylic{},custom_outline{},caret_on{},pinned{},oldest{},hotkey{},listening{},test_mode{};
    bool ime_active{};std::wstring ime_original,ime_text;DWORD ime_first{},ime_last{},ime_cursor{};
    bool suppress_search_space{},scroll_drag{};float scroll_grab{};
    bool menu_open{},menu_confirm{};int menu_hover{};
    // Custom groups: in-panel popup (group list / group actions), inline naming via the search box, scrollable tab strip.
    struct PopupItem{std::wstring label,hint;UINT32 dot{};int command{};uint32_t arg{};bool enabled{true},checked{};};
    bool popup_open{};int popup_hover{-1};uint64_t popup_entry{};uint32_t popup_group{};D2D1_RECT_F popup_rect{};std::vector<PopupItem> popup_items;std::vector<D2D1_RECT_F> popup_rows;
    int naming{};uint32_t naming_group{};uint64_t naming_entry{};std::wstring naming_query;
    float tab_scroll{},tab_scroll_max{};bool reveal_tab{};
    int tab{},scroll{},retry{},paste_wait{};uint64_t selected{};DWORD sequence{};
    std::filesystem::path layout_path;
    int snap_x{},snap_y{};HMONITOR drag_monitor{};
    POINT anchor{},drag_start{};RECT drag_rect{};bool placed{},drag_pending{},dragged{};
    float scale{1};std::wstring query,status;int hover{-1};
    TextRenderer text_renderer;uint64_t text_glyphs{};
    ComPtr<ID2D1Factory> factory;ComPtr<IDWriteFactory> writer;ComPtr<ID2D1DCRenderTarget> target;ComPtr<ID2D1SolidColorBrush> brush;
    BrandIcon app_icon;
    std::map<uint64_t,ComPtr<ID2D1Bitmap>> images;
    std::set<uint64_t> thumbnail_pending,thumbnail_failed;
    std::unique_ptr<clipboard::FileIconCache> file_icons;
    std::map<std::pair<std::wstring,int>,ComPtr<ID2D1Bitmap>> file_bitmaps;
    std::map<std::pair<int,bool>,ComPtr<IDWriteTextFormat>> fonts;
    struct Hit {D2D1_RECT_F rect;int action;uint64_t id;};std::vector<Hit> hits;std::optional<Hit> paste_click;
    std::unique_ptr<DibSurface> surface;int width{},height{};uint64_t search_text_glyphs{};
    std::unique_ptr<ClipboardComposition> composition,parked_composition;
    bool switching_window{},reveal_window{};
    bool opening{};
    clipboard::LiquidTween opening_tween;
    RECT opening_work{},opening_viewport{};
    void StopOpening(bool reveal){
        if(!opening)return;
        opening=false;KillTimer(EventWindow(),10);
        ShowWindow(folded_window,SW_HIDE);
        if(liquid_surface)liquid_surface->ClearExpanded();
        if(reveal&&enabled&&expanded){ShowWindow(native_window,SW_SHOWNOACTIVATE);SetForegroundWindow(native_window);SetFocus(native_window);Invalidate();}
    }
    void OpeningTick(ULONGLONG now){
        if(!opening)return;
        if(opening_tween.Done(now)){StopOpening(true);return;}
        try{
            clipboard::LiquidFrame frame;frame.pose=opening_tween.Sample(now);
            frame.pose.bounds=clipboard::ConstrainLiquidBounds(frame.pose.bounds,opening_work);
            frame.viewport=opening_viewport;frame.work=opening_work;frame.scale=scale;frame.dark=dark;
            // Match DWMWCP_ROUND, not the 22-DIP software fallback. Otherwise
            // the last liquid frame visibly tightens when the native HWND appears.
            frame.expanded_radius_pixels=custom_outline?8.f*GetDpiForWindow(native_window)/96.f:22.f*scale;
            frame.detail=1-clipboard::LiquidSmooth((frame.pose.open-.65f)/.35f);
            liquid_surface->Render(frame);
            if(!parked_composition)parked_composition=std::make_unique<ClipboardComposition>();
            parked_composition->Present(folded_window,static_cast<UINT>(liquid_surface->Width()),static_cast<UINT>(liquid_surface->Height()),liquid_surface->Pixels());
            const HRGN region=liquid_surface->CreateInputRegion();CheckWin32(region!=nullptr,"Opening input mask");
            if(!SetWindowRgn(folded_window,region,FALSE)){DeleteObject(region);CheckWin32(false,"Opening input mask");}
        }catch(...){StopOpening(true);}
    }
    bool StartOpening(RECT from,RECT to,ULONGLONG now){
        if(!liquid_surface||!clipboard::LiquidBudget(from,to,true))return false;
        opening_work=WorkArea(to);RECT bounds{};UnionRect(&bounds,&from,&to);
        opening_viewport=clipboard::LiquidViewport(clipboard::LiquidRect(bounds),opening_work,scale);
        try{Render(to.right-to.left,to.bottom-to.top,true);liquid_surface->Cache(true,width,height,surface->Pixels());}
        catch(...){return false;}
        opening_tween.Reset(from,0,now);opening_tween.Target(to,1,now,350);opening=true;
        SetWindowPos(folded_window,HWND_TOPMOST,opening_viewport.left,opening_viewport.top,
            opening_viewport.right-opening_viewport.left,opening_viewport.bottom-opening_viewport.top,SWP_NOACTIVATE);
        OpeningTick(now);
        if(!opening)return false;
        ShowWindow(folded_window,SW_SHOWNOACTIVATE);
        if(!SetTimer(EventWindow(),10,16,nullptr)){StopOpening(true);return false;}
        return true;
    }
    HWND EventWindow()const{return native_window?native_window:window;}
    clipboard::PreviewWindow preview;
    clipboard::PreviewCache preview_cache;
    clipboard::PreviewCache thumbnail_cache{384 * 1024};
    uint64_t preview_pending{};
    float W=400,H=744,ListBottom=682;bool resizing{};
    clipboard::LiquidTween liquid_drag_tween;clipboard::LiquidPull liquid_pull;
    clipboard::LiquidEdge liquid_drag_edge{clipboard::LiquidEdge::None};
    std::unique_ptr<clipboard::LiquidSurface> liquid_surface;
    bool liquid_backdrop{},liquid_failed{},liquid_drag_active{},liquid_drag_timer{},liquid_placing{},compact_positioned{};
    RECT compact_body{},compact_viewport{},liquid_drag_goal{},liquid_drag_work{};
    POINT liquid_pointer{};ULONGLONG liquid_pointer_time{};
    static bool MotionEnabled(){BOOL effects=TRUE;return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&effects,0)&&effects;}
    RECT VisualBodyBounds()const{
        RECT actual{};GetWindowRect(window,&actual);
        return compact_positioned&&EqualRect(&actual,&compact_viewport)?compact_body:actual;
    }
    static RECT WorkArea(RECT body){
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromRect(&body,MONITOR_DEFAULTTONEAREST),&monitor))return monitor.rcWork;
        return body;
    }
    void LiquidBackdrop(bool enabled_shape){
        if(native_window&&folded_window)return; // Fixed roles after both windows are configured.
        if(!window||liquid_backdrop==enabled_shape)return;
        liquid_backdrop=enabled_shape;
        const auto style=GetWindowLongPtrW(window,GWL_STYLE);
        SetWindowLongPtrW(window,GWL_STYLE,enabled_shape?(style&~WS_THICKFRAME):(style|WS_THICKFRAME));
        const DWMNCRENDERINGPOLICY nc=enabled_shape?DWMNCRP_DISABLED:DWMNCRP_USEWINDOWSTYLE;
        DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&nc,sizeof(nc));
        const MARGINS margins{enabled_shape?0:-1};DwmExtendFrameIntoClientArea(window,&margins);
        if(!enabled_shape)SetWindowRgn(window,nullptr,FALSE);
        SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        const DWORD corner=enabled_shape?1:2,backdrop=enabled_shape?1:3;
        DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
        const bool supported=SUCCEEDED(DwmSetWindowAttribute(window,38,&backdrop,sizeof(backdrop)));
        if(!enabled_shape)acrylic=supported;
    }
    void PaintLiquid(const clipboard::LiquidFrame& frame){
        liquid_surface->Render(frame);
        liquid_placing=true;RECT current{};GetWindowRect(window,&current);
        if(!EqualRect(&current,&frame.viewport))SetWindowPos(window,nullptr,frame.viewport.left,frame.viewport.top,frame.viewport.right-frame.viewport.left,
            frame.viewport.bottom-frame.viewport.top,SWP_NOZORDER|SWP_NOACTIVATE);
        liquid_placing=false;
        if(liquid_backdrop){const HRGN region=liquid_surface->CreateInputRegion();CheckWin32(region!=nullptr,"Liquid input mask");
            if(!SetWindowRgn(window,region,FALSE)){DeleteObject(region);CheckWin32(false,"Liquid input mask");}}
        if(!composition)composition=std::make_unique<ClipboardComposition>();
        composition->Transition(0,1);
        composition->Present(window,static_cast<UINT>(liquid_surface->Width()),static_cast<UINT>(liquid_surface->Height()),liquid_surface->Pixels());
        if(!expanded){compact_body=clipboard::LiquidPixels(frame.pose.bounds);compact_viewport=frame.viewport;compact_positioned=true;}
    }
    void PresentFolded(){
        const RECT body=VisualBodyBounds(),work=WorkArea(body);
        if(!liquid_surface)liquid_surface=std::make_unique<clipboard::LiquidSurface>();
        Render(static_cast<int>(CW*scale),static_cast<int>(CH*scale),true);
        liquid_surface->Cache(false,width,height,surface->Pixels());liquid_surface->ClearExpanded();
        clipboard::LiquidFrame frame;frame.pose={clipboard::ConstrainLiquidBounds(clipboard::LiquidRect(body),work),0};
        frame.viewport=clipboard::LiquidViewport(frame.pose.bounds,work,scale);frame.work=work;frame.scale=scale;frame.dark=dark;
        LiquidBackdrop(true);PaintLiquid(frame);
    }
    void StopLiquidDrag(bool finish){
        if(window)KillTimer(EventWindow(),9);liquid_drag_timer=false;
        liquid_drag_edge=clipboard::LiquidEdge::None;
        if(!liquid_drag_active)return;
        const auto pose=liquid_drag_tween.Sample(GetTickCount64());
        compact_body=finish?liquid_drag_goal:clipboard::LiquidPixels(clipboard::ConstrainLiquidBounds(pose.bounds,liquid_drag_work));
        GetWindowRect(window,&compact_viewport);compact_positioned=true;liquid_drag_active=false;
    }
    // The two HWNDs keep their material for their entire lifetime. Only the
    // folded HWND owns a liquid region; the native HWND always owns Acrylic.
    void SelectWindow(){
        const HWND next=expanded?native_window:folded_window;
        if(!next||window==next)return;
        switching_window=true;
        StopLiquidDrag(true);
        if(GetCapture()==window)ReleaseCapture();
        drag_pending=scroll_drag=false;
        RECT bounds{};GetWindowRect(window,&bounds);
        reveal_window=IsWindowVisible(window)!=FALSE;
        ShowWindow(window,SW_HIDE);
        std::swap(composition,parked_composition);
        window=next;liquid_backdrop=window==folded_window;
        compact_positioned=false;
        SetWindowPos(window,HWND_TOPMOST,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOACTIVATE);
        UpdateActivationPolicy();
        switching_window=false;
    }
    bool PresentAt(RECT destination,bool paint=true){
        SelectWindow();StopLiquidDrag(true);compact_positioned=false;
        SetWindowPos(window,HWND_TOPMOST,destination.left,destination.top,destination.right-destination.left,destination.bottom-destination.top,SWP_NOACTIVATE);
        LayoutSearch();ShowWindow(search,expanded?SW_SHOW:SW_HIDE);if(paint)Present();Invalidate();
        return true;
    }
    void LiquidDragTick(ULONGLONG now){
        if(!liquid_drag_active||expanded||!enabled){StopLiquidDrag(false);return;}
        clipboard::LiquidFrame frame;frame.pose=liquid_drag_tween.Sample(now);
        frame.pose.bounds=clipboard::ConstrainLiquidBounds(frame.pose.bounds,liquid_drag_work);
        const auto edge=clipboard::FindLiquidDock(frame.pose.bounds,liquid_drag_work,scale).edge;
        if(liquid_drag_edge!=clipboard::LiquidEdge::None&&edge==clipboard::LiquidEdge::None){
            const float vx=liquid_drag_edge==clipboard::LiquidEdge::Right?180.f:liquid_drag_edge==clipboard::LiquidEdge::Left?-180.f:0.f;
            const float vy=liquid_drag_edge==clipboard::LiquidEdge::Bottom?180.f:liquid_drag_edge==clipboard::LiquidEdge::Top?-180.f:0.f;
            liquid_pull.Recoil(vx,vy,now);
        }
        liquid_drag_edge=edge;
        frame.work=liquid_drag_work;frame.scale=scale;frame.dark=dark;frame.pull=liquid_pull.Sample(now);
        frame.viewport=clipboard::LiquidViewport(frame.pose.bounds,frame.work,scale);
        LiquidBackdrop(true);PaintLiquid(frame);
        if(liquid_drag_tween.Done(now)&&liquid_pull.Settled()){
            KillTimer(EventWindow(),9);liquid_drag_timer=false;
            if(!drag_pending){StopLiquidDrag(true);Present();}
        }
    }
    void MoveLiquidDrag(RECT goal,POINT pointer,RECT work,ULONGLONG now){
        if(!liquid_drag_active){
            const RECT body=VisualBodyBounds();PresentFolded();
            liquid_drag_tween.Reset(body,0,now);liquid_pull.Reset(now);liquid_pointer=drag_start;liquid_pointer_time=now>16?now-16:0;
            liquid_drag_edge=clipboard::FindLiquidDock(clipboard::LiquidRect(body),work,scale).edge;
            liquid_drag_work=work;
            liquid_drag_active=true;
        }
        if(!EqualRect(&work,&liquid_drag_work))liquid_drag_edge=clipboard::LiquidEdge::None;
        liquid_drag_goal=goal;liquid_drag_work=work;liquid_drag_tween.Target(goal,0,now,280,true);
        const float dt=std::max(.008f,float(now-liquid_pointer_time)/1000.f);
        liquid_pull.Impulse((pointer.x-liquid_pointer.x)/(dt*scale),(pointer.y-liquid_pointer.y)/(dt*scale),now);
        liquid_pointer=pointer;liquid_pointer_time=now;
        if(!liquid_drag_timer){liquid_drag_timer=SetTimer(EventWindow(),9,15,nullptr)!=0;if(!liquid_drag_timer){StopLiquidDrag(true);Present();}}
    }
    void ReleaseLiquidDrag(){
        if(!liquid_drag_active)return;
        liquid_pull.Release(GetTickCount64());liquid_drag_timer=SetTimer(EventWindow(),9,15,nullptr)!=0;
        if(!liquid_drag_timer){StopLiquidDrag(true);Present();}
    }
    void ReflowLiquid(bool dpi_change=false){
        const bool resume=dpi_change&&drag_pending&&!expanded&&GetCapture()==window;
        POINT pointer{};GetCursorPos(&pointer);CloseQuick();StopLiquidDrag(true);
        if(!resume){drag_pending=false;if(GetCapture()==window)ReleaseCapture();}
        if(enabled)Place();
        if(resume){drag_start=pointer;drag_rect=VisualBodyBounds();dragged=true;snap_x=snap_y=0;drag_monitor=nullptr;}
    }
    static constexpr float CW=28,CH=96,HeaderBottom=76,ListTop=202;
    int PageRows()const{return std::max(1,static_cast<int>((ListBottom-ListTop)/96));}
    int ResizeHit(POINT point)const{
        if(!expanded){
            RECT actual{};GetWindowRect(window,&actual);
            if(liquid_surface&&(compact_positioned&&EqualRect(&actual,&compact_viewport))){
                POINT local=point;ScreenToClient(window,&local);if(!liquid_surface->Contains(local))return HTTRANSPARENT;
            }
            return HTCLIENT;
        }
        RECT bounds{};GetWindowRect(window,&bounds);const int edge=std::max(4,static_cast<int>(5*scale));
        const bool left=point.x<bounds.left+edge,right=point.x>=bounds.right-edge,top=point.y<bounds.top+edge,bottom=point.y>=bounds.bottom-edge;
        if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;
        if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;
        return left?HTLEFT:right?HTRIGHT:HTCLIENT;
    }
    bool HeaderDragHit(float x,float y)const{
        if(x<0||x>=W||y<0||y>=HeaderBottom)return false;
        // Keep the full click targets of header controls out of the drag area.
        return std::none_of(hits.begin(),hits.end(),[x,y](const Hit& hit){
            const auto r=hit.rect;
            return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;
        });
    }
    void ResizeClient(int pixels_w,int pixels_h){
        if(!expanded||!resizing||pixels_w<=0||pixels_h<=0)return;
        W=pixels_w/scale;H=pixels_h/scale;ListBottom=H-62;
        scroll=std::clamp(scroll,0,std::max(0,static_cast<int>(visible.size())-PageRows()));
        LayoutSearch();Invalidate();
    }
    explicit Impl(HWND host,std::function<void()> callback,std::function<bool()> allowed = {}):owner(host),settings(std::move(callback)),hotkey_allowed(std::move(allowed)){}
    ~Impl(){ReleaseDisabledResources();}
    void Invalidate(){if(window)InvalidateRect(window,nullptr,FALSE);}
    void ClearFileIcons(){if(file_icons)file_icons->Clear();file_bitmaps.clear();}
    void ReleaseDisabledResources(){
        StopOpening(false);
        CloseQuick();quick.reset();quick_ids.clear();
        StopLiquidDrag(false);liquid_surface.reset();compact_positioned=false;liquid_backdrop=liquid_failed=false;
        win_v_shortcut.Reset();
        if(foreground_hook){foreground_observers.erase(foreground_hook);UnhookWinEvent(foreground_hook);foreground_hook=nullptr;}
        if(ime_hook){foreground_observers.erase(ime_hook);UnhookWinEvent(ime_hook);ime_hook=nullptr;}ime_windows.clear();
        previous_process=0;
        // Disable callbacks first, join producers before destroying their target HWND.
        if(disk&&!test_mode){disk->WaitIdle();DrainStore();disk->Commit(history.entries,history.groups);disk->WaitIdle();}
        enabled=expanded=false;menu_open=menu_confirm=false;drag_pending=dragged=scroll_drag=false;
        ++copy_token;++image_token;++search_token;copy_pending=false;
        if(window){RemoveClipboardFormatListener(EventWindow());UnregisterShortcut();KillTimer(EventWindow(),1);KillTimer(EventWindow(),2);KillTimer(EventWindow(),3);KillTimer(EventWindow(),4);KillTimer(EventWindow(),6);if(GetCapture()==window)ReleaseCapture();}
        ClosePreview();listening=hotkey=paste_inflight=false;disk.reset();file_icons.reset();
        composition.reset();parked_composition.reset();
        switching_window=true;
        if(folded_window)DestroyWindow(folded_window);
        if(native_window)DestroyWindow(native_window);
        else if(window)DestroyWindow(window);
        window=native_window=folded_window=nullptr;search=nullptr;switching_window=reveal_window=false;
        // Release bitmap references before render target, and target before bound DC.
        images.clear();thumbnail_pending.clear();thumbnail_failed.clear();thumbnail_cache.Clear();file_bitmaps.clear();app_icon.Reset();brush.Reset();target.Reset();surface.reset();search_text_glyphs=0;ime_active=false;ime_original.clear();ime_text.clear();
        fonts.clear();writer.Reset();factory.Reset();
        if(font){DeleteObject(font);font=nullptr;}if(edit_brush){DeleteObject(edit_brush);edit_brush=nullptr;}
        history=clipboard::History{};std::vector<uint64_t>().swap(visible);std::vector<uint64_t>().swap(search_matches);std::vector<Hit>().swap(hits);
        std::wstring().swap(completed_query);std::wstring().swap(query);std::wstring().swap(status);selected=last_read_id=0;copy_target=previous=paste_target=previous_focus=copy_focus=paste_focus=nullptr;copy_process=paste_process=paste_sequence=0;
        copy_plain=copy_keyboard=paste_keyboard=paste_activated=paste_returning=copy_quick=paste_quick=false;paste_return_at=0;
        suppress_search_space=false;search_complete=false;tab=scroll=retry=paste_wait=0;hover=-1;width=height=0;
        // Keep only small user placement/policy fields; recreate resources on demand.
    }
    void SelectVisible(){scroll=std::clamp(scroll,0,std::max(0,static_cast<int>(visible.size())-PageRows()));if(std::find(visible.begin(),visible.end(),selected)==visible.end())selected=visible.empty()?0:visible.front();if(preview.IsOpen())ShowPreview();else PrefetchPreview();Invalidate();}
    void Filter(bool cancel_copy=true){
        if(tab<0||tab>=TabCount())tab=0;
        if(cancel_copy){++copy_token;copy_pending=false;}
        const auto summaries=history.Filter(tab,query,oldest);
        if(search_complete&&!query.empty()&&completed_query==query){
            visible=history.Filter(tab,L"",oldest);
            std::erase_if(visible,[&](uint64_t id){return std::find(search_matches.begin(),search_matches.end(),id)==search_matches.end()&&std::find(summaries.begin(),summaries.end(),id)==summaries.end();});
        }else{search_complete=false;visible=summaries;}
        SelectVisible();
        if(disk&&!query.empty())disk->Search(history.entries,query,++search_token);else ++search_token;
    }
    void ClosePreview(){++preview_token;preview_pending=0;if(disk)disk->CancelPreview();preview.Close();preview_cache.Clear();}
    void CancelReads(){ClosePreview();++copy_token;++image_token;++search_token;copy_pending=false;if(disk)disk->CancelReads();images.clear();thumbnail_pending.clear();thumbnail_failed.clear();thumbnail_cache.Clear();}
    bool ImageNeeded(uint64_t id)const{
        if(!expanded)return false;
        for(int i=scroll;i<std::min(scroll+PageRows(),static_cast<int>(visible.size()));++i)if(visible[static_cast<size_t>(i)]==id)return true;return false;
    }
    void DrainStore(){
        if(!disk)return;bool changed=false;
        for(auto& result:disk->Take()){
            switch(result.kind){
            case clipboard::StoreResultKind::Restored:
                history.SetGroups(std::move(result.groups));for(auto it=result.entries.rbegin();it!=result.entries.rend();++it)history.Add(std::move(*it));if(!result.message.empty())status=result.message;changed=true;break;
            case clipboard::StoreResultKind::Saved:
                if(!history.Add(std::move(result.entry)))status=L"历史容量已满，未记录该条";changed=true;break;
            case clipboard::StoreResultKind::Loaded:
                if(copy_pending&&result.token==copy_token){copy_pending=false;FinishCopy(result.entry,copy_paste,copy_target,copy_process);}break;
            case clipboard::StoreResultKind::Thumbnail:
                if(result.token==image_token&&history.Find(result.entry.id)){
                    thumbnail_pending.erase(result.entry.id);
                    if(result.image){thumbnail_cache.Put(result.entry.id,result.image);thumbnail_failed.erase(result.entry.id);images.erase(result.entry.id);}
                    else thumbnail_failed.insert(result.entry.id);
                }break;
            case clipboard::StoreResultKind::Preview:
                if(expanded&&result.token==preview_token&&history.Find(result.entry.id)) {
                    preview_pending=0;
                    preview_cache.Put(result.entry.id,result.preview.image);
                    const bool ready=result.preview.image!=nullptr;
                    if(preview.IsOpen()&&result.entry.id==selected)preview.SetContent(selected,std::move(result.preview));
                    if(ready)PrefetchPreview();
                }
                break;
            case clipboard::StoreResultKind::Search:
                if(result.token==search_token&&!query.empty()){
                    search_matches=std::move(result.matches);search_complete=true;completed_query=query;if(!result.message.empty())status=result.message;visible=history.Filter(tab,L"",oldest);
                    std::erase_if(visible,[&](uint64_t id){return std::find(search_matches.begin(),search_matches.end(),id)==search_matches.end();});SelectVisible();
                }break;
            case clipboard::StoreResultKind::Error:
                if(result.thumbnail_read&&result.token==image_token){thumbnail_pending.erase(result.entry.id);thumbnail_failed.insert(result.entry.id);}
                if(result.preview_read&&result.token==preview_token)preview_pending=0;
                if(result.preview_read&&expanded&&preview.IsOpen()&&result.token==preview_token&&result.entry.id==selected){
                    clipboard::PreviewData data;data.error=result.message;preview.SetContent(selected,std::move(data));
                }
                if(result.token==0||(result.foreground_read&&copy_pending&&result.token==copy_token)){status=result.message;if(result.token)copy_pending=false;}break;
            }
        }
        if(changed){disk->Commit(history.entries,history.groups);Filter(false);if(quick_active)RefreshQuick();}else if(quick_active)UpdateQuick();Invalidate();
    }
    bool Create(){
        if(window)return true;WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=Proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"LumaShot.Clipboard";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"LumaShot 剪贴板",WS_POPUP|WS_THICKFRAME|WS_CLIPCHILDREN,0,0,36,132,owner,nullptr,wc.hInstance,this);if(!window)return false;
        const DWORD corner=2,border_color=0xfffffffe;
        custom_outline=SUCCEEDED(DwmSetWindowAttribute(window,33,&corner,sizeof(corner)));
        DwmSetWindowAttribute(window,34,&border_color,sizeof(border_color));
        const MARGINS margins{-1};DwmExtendFrameIntoClientArea(window,&margins);
        acrylic=ApplyBackdrop();
        const BOOL yes=TRUE;DwmSetWindowAttribute(window,DWMWA_TRANSITIONS_FORCEDISABLED,&yes,sizeof(yes));
        // Clipboard UI is intentionally capturable, including the folded tab. Rely on
        // the WDA_NONE default: calling SetWindowDisplayAffinity on a never-shown
        // Acrylic HWND makes DWM composite its backdrop at the hidden position.
        search=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(201),wc.hInstance,nullptr);
        SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"搜索剪贴内容"));SendMessageW(search,EM_SETLIMITTEXT,512,0);
        SetWindowSubclass(search,SearchProc,1,reinterpret_cast<DWORD_PTR>(this));
        native_window=window;
        const HWND folded=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"LumaShot 剪贴板侧边条",WS_POPUP,0,0,28,96,owner,nullptr,wc.hInstance,this);
        if(!folded)return false;
        window=folded;LiquidBackdrop(true);folded_window=folded;
        DwmSetWindowAttribute(folded,DWMWA_TRANSITIONS_FORCEDISABLED,&yes,sizeof(yes));
        window=native_window;liquid_backdrop=false;
        UpdateActivationPolicy();TrackForeground();return search!=nullptr;
    }
    void LoadLayout(){
        if(layout_path.empty())return;
        if(const auto dimensions=clipboard::LoadPanelSize(layout_path)){W=static_cast<float>(dimensions->cx);H=static_cast<float>(dimensions->cy);ListBottom=H-62;}
        if(!placed){if(const auto saved=clipboard::LoadPosition(layout_path)){anchor=*saved;placed=true;}}
    }
    bool SaveLayout()const{
        // Tests only persist when given an explicit isolated path; never resolve personal settings.
        if(test_mode&&layout_path.empty())return true;
        return clipboard::SavePosition(layout_path,anchor,SIZE{static_cast<LONG>(std::lround(W)),static_cast<LONG>(std::lround(H))});
    }
    bool Enable(bool value,bool night){
        if(quick_active)CloseQuick();
        dark=night;if(!value){ReleaseDisabledResources();return true;}if(edit_brush)DeleteObject(edit_brush);edit_brush=CreateSolidBrush(dark?RGB(31,40,55):RGB(255,255,255));
        if(!file_icons)file_icons=std::make_unique<clipboard::FileIconCache>();
        if(!test_mode&&layout_path.empty()){
            PWSTR folder{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder))){layout_path=std::filesystem::path(folder)/L"LumaShot"/L"clipboard-layout.ini";CoTaskMemFree(folder);}
        }
        if(!window)LoadLayout();
        if(!Create()){ReleaseDisabledResources();return false;}acrylic=ApplyBackdrop();
        if(!test_mode&&!disk){if(layout_path.empty()){ReleaseDisabledResources();return false;}disk=std::make_unique<clipboard::SessionStore>(layout_path.parent_path()/L"clipboard-history",EventWindow(),persist_history);}
        if(!enabled){sequence=GetClipboardSequenceNumber();listening=test_mode||AddClipboardFormatListener(EventWindow())!=FALSE;if(!listening){ReleaseDisabledResources();return false;}enabled=true;expanded=false;
            hotkey=shortcut_key&&!test_mode&&!hotkeys_suspended&&RegisterShortcut(shortcut_modifiers,shortcut_key);
            status=(!shortcut_key||hotkeys_suspended)?L"快捷键已禁用，可点击侧边栏打开":hotkey?L"":L"快捷键被占用，可点击侧边栏打开";
        }
        Place();Invalidate();return true;
    }
    void ResolveLayoutPath(){
        if(test_mode||!layout_path.empty())return;
        PWSTR folder{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder))){layout_path=std::filesystem::path(folder)/L"LumaShot"/L"clipboard-layout.ini";CoTaskMemFree(folder);}
    }
    bool SetPersistent(bool value){
        const bool changed=persist_history!=value;persist_history=value;
        if(test_mode)return true;
        // A live store only exists while enabled; recreate it under the new policy.
        if(disk){if(!changed)return true;const bool night=dark;ReleaseDisabledResources();return Enable(true,night);}
        if(!value){ResolveLayoutPath();if(!layout_path.empty())return clipboard::SessionStore::PurgePersistent(layout_path.parent_path()/L"clipboard-history");}
        return true;
    }
    void UnregisterShortcut(){win_v_shortcut.Reset();if(window)UnregisterHotKey(EventWindow(),1);}
    bool RegisterShortcut(UINT modifiers,UINT key){
        if(modifiers==MOD_WIN&&key=='V')return win_v_shortcut.Install(EventWindow());// Hook runs on its own thread; WM_HOTKEY re-checks the live guard on the UI thread.
        return RegisterHotKey(EventWindow(),1,modifiers|MOD_NOREPEAT,key)!=FALSE;
    }
    bool SetShortcut(UINT modifiers,UINT key){
        if(modifiers==shortcut_modifiers&&key==shortcut_key)return true;
        const bool active=window&&enabled&&!hotkeys_suspended&&!test_mode;
        const bool was_registered=hotkey;
        if(active&&hotkey)UnregisterShortcut();
        if(active&&key&&!RegisterShortcut(modifiers,key)){
            hotkey=was_registered&&RegisterShortcut(shortcut_modifiers,shortcut_key);
            return false;
        }
        shortcut_modifiers=modifiers;shortcut_key=key;hotkey=active&&key;
        status=(!key||hotkeys_suspended)?L"快捷键已禁用，可点击侧边栏打开":L"";
        if(window)Invalidate();return true;
    }
    void SetHotkeysSuspended(bool suspended){
        if(hotkeys_suspended==suspended)return;
        hotkeys_suspended=suspended;
        if(suspended)CloseQuick();
        if(!window||!enabled)return;
        if(hotkey)UnregisterShortcut();
        hotkey=shortcut_key&&!suspended&&!test_mode&&RegisterShortcut(shortcut_modifiers,shortcut_key);
        status=(!shortcut_key||suspended)?L"快捷键已禁用，可点击侧边栏打开":hotkey?L"":L"快捷键被占用，可点击侧边栏打开";
        Invalidate();
    }
    void Place(bool animate=false){
        StopOpening(false);
        const RECT from=VisualBodyBounds();
        animate=animate&&expanded&&window==folded_window&&IsWindowVisible(window)&&MotionEnabled()&&!liquid_failed;
        StopLiquidDrag(true);SelectWindow();
        if(animate)reveal_window=false;
        MONITORINFO monitor{sizeof(monitor)};
        if(!placed){POINT cursor{};GetCursorPos(&cursor);GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);anchor={monitor.rcWork.right-6,monitor.rcWork.top+70};placed=true;}
        GetMonitorInfoW(MonitorFromPoint({anchor.x-1,anchor.y},MONITOR_DEFAULTTONEAREST),&monitor);
        const auto r=monitor.rcWork;scale=GetDpiForWindow(window)/96.f;
        if(expanded)scale=std::min({scale,(r.right-r.left-16)/W,(r.bottom-r.top-16)/H});scale=std::max(.25f,scale);
        const int w=static_cast<int>((expanded?W:CW)*scale),h=static_cast<int>((expanded?H:CH)*scale);
        const LONG margin=expanded?4:0;
        const LONG x=std::clamp(anchor.x-w,r.left+margin,std::max(r.left+margin,r.right-w-margin)),y=std::clamp(anchor.y,r.top+margin,std::max(r.top+margin,r.bottom-h-margin));
        PresentAt({x,y,x+w,y+h},enabled&&(!test_mode||reveal_window));
        if(animate&&StartOpening(from,{x,y,x+w,y+h},GetTickCount64()))return;
        if(enabled&&!test_mode)ShowWindow(window,SW_SHOWNOACTIVATE);
    }
    void LayoutSearch(){
        SelectWindow();
        const HFONT next=CreateFontW(-static_cast<int>(13*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        if(!next)return;SendMessageW(search,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);if(font)DeleteObject(font);font=next;
        HDC dc=GetDC(search);if(!dc)return;const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(search,dc);
        const int x=static_cast<int>(59*scale),w=static_cast<int>((W-159)*scale),h=metrics.tmHeight+4;
        SendMessageW(search,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
        SetWindowPos(search,nullptr,x,0,w,h,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
        RECT format{};SendMessageW(search,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        // A borderless single-line EDIT paints at its formatting-rect top, not
        // vertically centered in an arbitrarily tall child window.
        const int y=static_cast<int>(std::lround(98.5f*scale-format.top-metrics.tmHeight/2.f));
        SetWindowPos(search,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);InvalidateRect(search,nullptr,TRUE);
    }
    float ScrollThumb()const{return (ListBottom-ListTop)*PageRows()/static_cast<float>(std::max(static_cast<size_t>(PageRows()),visible.size()));}
    void ScrollAt(float y){
        const float travel=ListBottom-ListTop-ScrollThumb();if(travel<=0)return;
        const auto max_scroll=static_cast<int>(visible.size())-PageRows();
        scroll=std::clamp(static_cast<int>(std::lround((y-ListTop-scroll_grab)*max_scroll/travel)),0,max_scroll);Invalidate();
    }
    void BeginScroll(float y){
        const float thumb=ScrollThumb(),top=ListTop+(ListBottom-ListTop-thumb)*scroll/static_cast<float>(visible.size()-static_cast<size_t>(PageRows()));
        scroll_grab=y>=top&&y<=top+thumb?y-top:thumb/2;scroll_drag=true;SetCapture(window);ScrollAt(y);
    }
    void BeginDrag(POINT point){StopLiquidDrag(false);snap_x=snap_y=0;drag_monitor=nullptr;drag_start=point;drag_rect=VisualBodyBounds();drag_pending=true;dragged=false;SetCapture(window);}
    void Drag(POINT point){
        if(!drag_pending)return;const LONG dx=point.x-drag_start.x,dy=point.y-drag_start.y;
        if(!dragged&&std::abs(dx)<GetSystemMetrics(SM_CXDRAG)&&std::abs(dy)<GetSystemMetrics(SM_CYDRAG))return;
        dragged=true;MONITORINFO monitor{sizeof(monitor)};const auto current_monitor=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);GetMonitorInfoW(current_monitor,&monitor);if(current_monitor!=drag_monitor){snap_x=snap_y=0;drag_monitor=current_monitor;}
        const LONG w=drag_rect.right-drag_rect.left,h=drag_rect.bottom-drag_rect.top;const auto r=monitor.rcWork;
        LONG x=std::clamp(drag_rect.left+dx,r.left,std::max(r.left,r.right-w)),y=std::clamp(drag_rect.top+dy,r.top,std::max(r.top,r.bottom-h));
        if(!expanded){const float dpi=GetDpiForWindow(window)/96.f;
            x=clipboard::SnapAxis(x,r.left,std::max(r.left,r.right-w),dpi,snap_x);
            y=clipboard::SnapAxis(y,r.top,std::max(r.top,r.bottom-h),dpi,snap_y);
        }
        anchor={x+w,y};placed=true;
        if(!expanded&&!test_mode&&!liquid_failed&&MotionEnabled()){MoveLiquidDrag({x,y,x+w,y+h},point,r,GetTickCount64());return;}
        compact_positioned=false;SetWindowPos(window,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);Invalidate();
    }
    void EndDrag(){const bool click=drag_pending&&!dragged&&!expanded;const bool save=drag_pending&&dragged;drag_pending=false;ReleaseCapture();ReleaseLiquidDrag();if(save&&!SaveLayout()){status=L"位置保存失败，下次启动可能无法恢复";Invalidate();}if(click)Show();}
    void RememberTarget(HWND candidate){
        if(!IsWindow(candidate)||candidate==window||candidate==native_window||candidate==folded_window||candidate==owner||(quick&&candidate==quick->Window())||IsChild(native_window?native_window:window,candidate)||preview.OwnsWindow(candidate)||candidate==GetDesktopWindow()||candidate==GetShellWindow())return;
        if(owner&&GetAncestor(candidate,GA_ROOTOWNER)==owner)return;
        const HWND root=GetAncestor(candidate,GA_ROOT);DWORD process{};
        if(root&&GetWindowThreadProcessId(root,&process)){
            GUITHREADINFO info{sizeof(info)};const DWORD thread=GetWindowThreadProcessId(root,nullptr);
            const HWND focused=GetGUIThreadInfo(thread,&info)?info.hwndFocus:nullptr;
            if(root!=previous||process!=previous_process)previous_focus=nullptr;
            if(focused&&(focused==root||IsChild(root,focused)))previous_focus=focused;
            previous=root;previous_process=process;
        }
    }
    bool QuickDestinationValid()const{
        if(!quick_active||!IsWindow(quick_target)||GetForegroundWindow()!=quick_target)return false;
        DWORD process{};const DWORD thread=GetWindowThreadProcessId(quick_target,&process);
        if(!thread||process!=quick_process)return false;
        GUITHREADINFO info{sizeof(info)};
        return GetGUIThreadInfo(thread,&info)&&(!quick_focus||info.hwndFocus==quick_focus);
    }
    void CloseQuick(){
        CancelShortcutProbe();quick_active=false;quick_input.Stop();if(window)KillTimer(EventWindow(),7);
        if(quick)quick->Hide();
        if(copy_quick){++copy_token;copy_pending=false;copy_quick=false;}
        if(paste_quick){KillTimer(EventWindow(),2);paste_inflight=paste_returning=paste_quick=false;}
        quick_target=quick_focus=nullptr;quick_process=0;
    }
    void UpdateQuick(){
        if(!quick_active||!quick)return;
        std::vector<clipboard::QuickRow> rows;rows.reserve(quick_ids.size());
        for(auto id:quick_ids)if(const auto* entry=history.Find(id))rows.push_back({id,entry->kind,entry->text,entry->favorite});
        quick->Update(std::move(rows),quick_selection,quick_tab,quick_continuous,copy_pending||paste_inflight,status);
        quick_input.SetBounds(quick->Bounds());
    }
    void RefreshQuick(bool first=false){
        const auto id=quick_selection<quick_ids.size()?quick_ids[quick_selection]:0;
        constexpr int categories[]={0,4,1,2,3};quick_ids=history.Filter(categories[quick_tab],L"");
        const auto found=std::find(quick_ids.begin(),quick_ids.end(),id);
        quick_selection=!first&&found!=quick_ids.end()?static_cast<size_t>(found-quick_ids.begin()):0;
        UpdateQuick();
    }
    void QuickSelect(size_t index){
        if(!quick_active||copy_pending||paste_inflight||index>=quick_ids.size())return;
        quick_selection=index;status.clear();UpdateQuick();
    }
    void QuickCategory(int category){
        if(!quick_active||copy_pending||paste_inflight)return;
        quick_tab=(category+5)%5;status.clear();RefreshQuick(true);
    }
    void QuickPaste(bool plain){
        if(!QuickDestinationValid()){CloseQuick();return;}
        if(copy_pending||paste_inflight||quick_selection>=quick_ids.size())return;
        const auto* entry=history.Find(quick_ids[quick_selection]);if(!entry)return;
        if(plain&&entry->kind!=clipboard::Kind::Text){status=L"纯文本仅支持文本记录";UpdateQuick();return;}
        copy_quick=true;copy_plain=plain;copy_keyboard=false;copy_paste=true;
        copy_target=quick_target;copy_focus=quick_focus;copy_process=quick_process;
        if(entry->payload&&disk){
            copy_pending=true;const auto token=++copy_token;
            if(!disk->Load(*entry,token)){copy_pending=false;status=L"读取队列已满，请重试";}
            else status=L"正在准备粘贴…  Esc 取消";
        }else FinishCopy(*entry,true,copy_target,copy_process);
        UpdateQuick();
    }
    void QuickKey(WPARAM key,bool shift=false){
        if(!QuickDestinationValid()){CloseQuick();return;}
        if(key==VK_ESCAPE){CloseQuick();return;}
        if(copy_pending||paste_inflight)return;
        if(key==VK_F2){quick_continuous=!quick_continuous;status.clear();UpdateQuick();return;}
        if(key==VK_RETURN){QuickPaste(shift);return;}
        if(key==VK_LEFT||key==VK_RIGHT){QuickCategory(quick_tab+(key==VK_RIGHT?1:-1));return;}
        if(quick_ids.empty())return;
        const int last=static_cast<int>(quick_ids.size())-1;
        const int index=key==VK_HOME?0:key==VK_END?last:static_cast<int>(quick_selection)+(key==VK_UP?-1:key==VK_DOWN?1:key==VK_PRIOR?-6:key==VK_NEXT?6:0);
        QuickSelect(static_cast<size_t>(std::clamp(index,0,last)));
    }
    void CancelShortcutProbe(){shortcut_pending=false;if(window)KillTimer(EventWindow(),8);}
    void ApplyShortcutFocus(bool editable){
        CancelShortcutProbe();
        if(!shortcut_focus.Current(GetForegroundWindow()))return;
        if(editable)ShowQuick();else Show();
    }
    void ShortcutFocusTick(){
        if(!shortcut_pending)return;
        if(!enabled||!shortcut_key||hotkeys_suspended||(hotkey_allowed&&!hotkey_allowed())||
            !shortcut_focus.Current(GetForegroundWindow())||!ime_windows.empty()){CancelShortcutProbe();return;}
        const auto editable=input_focus_probe.Result();
        if(editable.has_value())ApplyShortcutFocus(*editable);
        else if(GetTickCount64()>=shortcut_deadline)ApplyShortcutFocus(false);
    }
    void ShowFromShortcut(){
        CancelShortcutProbe();
        if(!enabled||hotkeys_suspended)return;
        if(quick_active&&!QuickDestinationValid())CloseQuick();
        if((copy_pending||paste_inflight)&&!quick_active)return;
        std::erase_if(ime_windows,[](HWND h){return !IsWindow(h)||!IsWindowVisible(h);});
        if(!ime_windows.empty())return;
        const HWND foreground=GetForegroundWindow();RememberTarget(foreground);
        if(!foreground||previous!=foreground){Show();return;}
        // Snapshot current focus rather than the remembered paste destination.
        shortcut_focus=clipboard::InputFocusSnapshot::Capture(foreground);
        if(const auto editable=clipboard::NativeEditableInput(shortcut_focus);editable.has_value()){
            ApplyShortcutFocus(*editable);return;
        }
        // Browser/custom controls expose editability through UI Automation. Never
        // query third-party providers in the keyboard hook or block the UI thread.
        if(!input_focus_probe.Begin(shortcut_focus)){ApplyShortcutFocus(false);return;}
        shortcut_pending=true;shortcut_deadline=GetTickCount64()+300;
        if(!SetTimer(EventWindow(),8,15,nullptr))ApplyShortcutFocus(false);
    }
    void ShowQuick(){
        if(quick_active){CloseQuick();return;}
        MSG stale{};while(PeekMessageW(&stale,EventWindow(),clipboard::QuickKeyMessage,clipboard::QuickWheelMessage,PM_REMOVE)){}
        if(!enabled||hotkeys_suspended||copy_pending||paste_inflight)return;
        std::erase_if(ime_windows,[](HWND h){return !IsWindow(h)||!IsWindowVisible(h);});
        if(!ime_windows.empty())return; // Candidate keys belong to the active IME.
        const HWND foreground=GetForegroundWindow();RememberTarget(foreground);
        if(!IsWindow(previous)||previous!=foreground)return;
        if(expanded)Fold();else ClosePreview();
        quick_target=previous;quick_focus=previous_focus;quick_process=previous_process;quick_active=true;
        if(!QuickDestinationValid()){CloseQuick();return;}
        if(!quick){clipboard::QuickCallbacks callbacks;
            callbacks.select=[this](size_t index){QuickSelect(index);};
            callbacks.paste=[this](size_t index){QuickSelect(index);QuickPaste(false);};
            callbacks.category=[this](int category){QuickCategory(category);};
            callbacks.move=[this](int delta){if(QuickDestinationValid()&&!quick_ids.empty())QuickSelect(static_cast<size_t>(std::clamp(static_cast<int>(quick_selection)+delta,0,static_cast<int>(quick_ids.size())-1)));};
            callbacks.close=[this]{CloseQuick();};
            quick=std::make_unique<clipboard::QuickWindow>(std::move(callbacks));
        }
        POINT point{};GetCursorPos(&point);GUITHREADINFO info{sizeof(info)};
        if(GetGUIThreadInfo(GetWindowThreadProcessId(quick_target,nullptr),&info)&&info.hwndCaret&&info.rcCaret.bottom>info.rcCaret.top){point={info.rcCaret.left,info.rcCaret.top};ClientToScreen(info.hwndCaret,&point);}
        status.clear();RefreshQuick(true);
        const float initial_scale=GetDpiForWindow(quick_target)/96.f;
        quick->Show(window,point,initial_scale,dark);
        // A DPI-unaware target can report 96 even on a scaled monitor.
        // Once placed, our own per-monitor window supplies the actual DPI.
        const float popup_scale=GetDpiForWindow(quick->Window())/96.f;
        if(popup_scale>0&&popup_scale!=initial_scale)quick->Show(window,point,popup_scale,dark);
        if(!quick_input.Start(EventWindow(),quick_target,quick->Bounds(),[this]{return quick_active&&!hotkeys_suspended&&QuickDestinationValid();})){
            CloseQuick();status=L"快捷取用暂不可用，可点击侧边栏";Invalidate();return;
        }
        SetTimer(EventWindow(),7,100,nullptr);
    }
    void Show(){CloseQuick();if(!enabled)return;KillTimer(EventWindow(),4);ClosePreview();const HWND foreground=GetForegroundWindow();RememberTarget(foreground);expanded=true;SetWindowTextW(search,L"");query.clear();scroll=0;Filter();selected=visible.empty()?0:visible.front();Place(true);if(opening){SetForegroundWindow(folded_window);SetFocus(folded_window);}else{SetForegroundWindow(window);SetFocus(window);}}
    void Fold(){StopOpening(false);paste_click.reset();paste_inflight=false;scroll_drag=false;if(GetCapture()==window)ReleaseCapture();CloseMore();ClosePopup();CancelNaming();CancelReads();ClearFileIcons();app_icon.Reset();brush.Reset();target.Reset();surface.reset();search_text_glyphs=0;ime_active=false;ime_original.clear();ime_text.clear();expanded=false;KillTimer(EventWindow(),2);KillTimer(EventWindow(),3);Place();SetTimer(EventWindow(),4,10000,nullptr);}
    // ---- Custom groups ----------------------------------------------------
    int TabCount()const{return clipboard::History::GroupTabBase+static_cast<int>(history.groups.size());}
    uint32_t TabGroup()const{return tab>=clipboard::History::GroupTabBase&&tab<TabCount()?history.groups[static_cast<size_t>(tab-clipboard::History::GroupTabBase)].id:0;}
    std::wstring GroupName(uint32_t id)const{const auto* g=history.FindGroup(id);return g?g->name:std::wstring{};}
    void CommitHistory(){if(disk)disk->Commit(history.entries,history.groups);}
    void SelectTab(int value){tab=std::clamp(value,0,std::max(0,TabCount()-1));scroll=0;reveal_tab=true;Filter();RevealSelection();Invalidate();}
    float Measure(const std::wstring& s,int size){
        ComPtr<IDWriteTextLayout> layout;DWRITE_TEXT_METRICS metrics{};
        if(!writer||FAILED(writer->CreateTextLayout(s.data(),static_cast<UINT32>(s.size()),Format(size),4096,64,&layout))||FAILED(layout->GetMetrics(&metrics)))return static_cast<float>(s.size()*static_cast<size_t>(size));
        return metrics.widthIncludingTrailingWhitespace;
    }
    // Moves an entry into a group; choosing its current group again removes it (0 always removes).
    void AssignGroup(uint64_t entry,uint32_t group){
        auto* e=history.Find(entry);if(!e){status=L"请先选择一条记录";Invalidate();return;}
        if(group&&e->group==group)group=0;
        const uint32_t before=e->group;
        if(!group&&!before){status=L"这条记录不在分组中";Invalidate();return;}
        if(!history.SetGroup(entry,group)){status=L"收藏和分组最多 200 条，请先移出一些再试";Invalidate();return;}
        selected=entry;status=group?L"已移入“"+GroupName(group)+L"” · Ctrl+0 移出":L"已移出“"+GroupName(before)+L"”";
        CommitHistory();Filter();RevealSelection();Invalidate();
    }
    void DeleteGroup(uint32_t group){
        const int index=history.GroupIndex(group);if(index<0)return;const auto name=GroupName(group);
        const int group_tab=clipboard::History::GroupTabBase+index;
        history.RemoveGroup(group);if(tab==group_tab)tab=0;else if(tab>group_tab)--tab;
        CommitHistory();scroll=0;reveal_tab=true;Filter();status=L"已删除分组“"+name+L"”，记录仍保留在“全部”";Invalidate();
    }
    bool PopupSelectable(int i)const{return i>=0&&i<static_cast<int>(popup_items.size())&&popup_items[static_cast<size_t>(i)].command&&popup_items[static_cast<size_t>(i)].enabled;}
    int NextPopupRow(int from,int step)const{
        const int n=static_cast<int>(popup_items.size());int i=from;
        for(int k=0;k<n;++k){i=from<0&&k==0?(step>0?0:n-1):((i+step)%n+n)%n;if(PopupSelectable(i))return i;}
        return -1;
    }
    // Lays out and opens the popup at (x,y); when it does not fit below, it opens above `above`.
    void OpenPopup(std::vector<PopupItem> items,float x,float y,bool keyboard,float above=-1){
        CloseMore();if(!expanded||items.empty())return;
        popup_items=std::move(items);popup_rows.clear();
        int rows=0;float fixed=12;for(const auto& item:popup_items){if(item.label.empty())fixed+=9;else if(item.command)++rows;else fixed+=24;}
        const float top_limit=128,bottom_limit=H-12.f;
        const float row_height=rows?std::clamp((bottom_limit-top_limit-fixed)/static_cast<float>(rows),22.f,32.f):32.f;
        const float popup_width=std::min(W-24.f,236.f),popup_height=fixed+row_height*static_cast<float>(rows);
        const float left=std::clamp(x,12.f,std::max(12.f,W-12.f-popup_width));
        float top=y;if(top+popup_height>bottom_limit)top=(above>=0?above:y)-popup_height;
        top=std::clamp(top,top_limit,std::max(top_limit,bottom_limit-popup_height));
        popup_rect={left,top,left+popup_width,top+popup_height};
        float cursor=top+6;for(const auto& item:popup_items){const float h=item.label.empty()?9.f:item.command?row_height:24.f;popup_rows.push_back({left+6,cursor,left+popup_width-6,cursor+h});cursor+=h;}
        popup_hover=keyboard?NextPopupRow(-1,1):-1;popup_open=true;
        SetFocus(window);SetCapture(window);Invalidate();
    }
    void ClosePopup(){
        if(!popup_open)return;popup_open=false;popup_hover=-1;popup_items.clear();popup_rows.clear();
        if(GetCapture()==window)ReleaseCapture();Invalidate();
    }
    bool InsidePopup(float x,float y)const{return popup_open&&x>=popup_rect.left&&x<popup_rect.right&&y>=popup_rect.top&&y<popup_rect.bottom;}
    int PopupHit(float x,float y)const{
        for(size_t i=0;i<popup_rows.size();++i){const auto& r=popup_rows[i];if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom)return PopupSelectable(static_cast<int>(i))?static_cast<int>(i):-1;}
        return -1;
    }
    void ChoosePopup(int i){
        if(!PopupSelectable(i))return;const auto item=popup_items[static_cast<size_t>(i)];const auto entry=popup_entry;const auto group=popup_group;
        if(item.command==4){history.CycleGroupColor(group);if(const auto* g=history.FindGroup(group))popup_items[static_cast<size_t>(i)].dot=g->color;CommitHistory();Invalidate();return;}
        ClosePopup();
        switch(item.command){
        case 1:AssignGroup(entry,item.arg);break;
        case 2:BeginNaming(1,0,entry);break;
        case 3:BeginNaming(2,group,0);break;
        case 5:DeleteGroup(group);break;
        case 6:BeginNaming(1,0,0);break;
        default:break;
        }
    }
    // Returns true when the key was consumed; other keys close the popup and fall through.
    bool PopupKey(WPARAM key){
        if(key==VK_ESCAPE){ClosePopup();return true;}
        if(key==VK_UP||key==VK_DOWN||key==VK_TAB){popup_hover=NextPopupRow(popup_hover,key==VK_UP?-1:1);Invalidate();return true;}
        if(key==VK_RETURN||key==VK_SPACE){ChoosePopup(popup_hover);return true;}
        ClosePopup();return false;
    }
    void OpenEntryPopup(uint64_t entry,float x,float y,bool keyboard,float above=-1){
        const auto* e=history.Find(entry);if(!e)return;selected=entry;
        std::vector<PopupItem> items;items.push_back({L"移到分组"});
        for(size_t i=0;i<history.groups.size();++i){const auto& g=history.groups[i];items.push_back({g.name,i<9?L"Ctrl+"+std::to_wstring(i+1):std::wstring{},g.color,1,g.id,true,e->group==g.id});}
        if(history.groups.empty())items.push_back({L"还没有分组",L"",0,1,0,false});
        items.push_back({L"移出分组",L"Ctrl+0",0,1,0,e->group!=0});
        items.push_back({});
        items.push_back({L"新建分组并移入…",L"Ctrl+Shift+N",0,2,0,history.groups.size()<clipboard::History::MaxGroups});
        popup_entry=entry;popup_group=0;OpenPopup(std::move(items),x,y,keyboard,above);
    }
    // Opens the group list under the card's folder button (mouse or Ctrl+G).
    void OpenEntryPopupAtButton(uint64_t entry,bool keyboard){
        for(const auto& hit:hits)if(hit.action==35&&hit.id==entry){OpenEntryPopup(entry,hit.rect.right-236,hit.rect.bottom+2,keyboard,hit.rect.top-2);return;}
        OpenEntryPopup(entry,W/2.f-118,ListTop+24,keyboard);
    }
    void OpenGroupPopup(uint32_t group,float x,float y){
        const auto* g=history.FindGroup(group);if(!g)return;
        std::vector<PopupItem> items;items.push_back({L"分组 · "+g->name});
        items.push_back({L"重命名…",L"",0,3});
        items.push_back({L"更换颜色",L"",g->color,4});
        items.push_back({L"删除分组",L"记录保留",0,5});
        items.push_back({});
        items.push_back({L"新建分组…",L"",0,6,0,history.groups.size()<clipboard::History::MaxGroups});
        popup_entry=0;popup_group=group;OpenPopup(std::move(items),x,y,false);
    }
    void DrawPopup(){
        const UINT32 ink=dark?0xe7effb:0x35445b,muted=dark?0x91a4bf:0x95a3b7;const auto r=popup_rect;
        for(int i=5;i>=1;--i){const float inset=static_cast<float>(i);brush->SetColor(D2D1::ColorF(0x274568,dark?.025f:.012f));target->FillRoundedRectangle(D2D1::RoundedRect({r.left-inset,r.top-inset/2,r.right+inset,r.bottom+inset},12+inset,12+inset),brush.Get());}
        Box(r,dark?0x222f42:0xfcfdff,dark?0x3a4c66:0xe1e8f2,12);
        for(size_t i=0;i<popup_items.size()&&i<popup_rows.size();++i){
            const auto& item=popup_items[i];const auto row=popup_rows[i];
            if(item.label.empty()){Color(dark?0x3a4a60:0xe9eef5);target->DrawLine({row.left+10,row.top+4.5f},{row.right-10,row.top+4.5f},brush.Get());continue;}
            if(!item.command){TextLine(item.label,{row.left+10,row.top+2,row.right-8,row.bottom},10,muted);continue;}
            const bool danger=item.command==5,hot=static_cast<int>(i)==popup_hover&&item.enabled;
            if(hot)Box(row,danger?(dark?0x4a303d:0xffeeee):(dark?0x2d4262:0xeaf2ff),danger?(dark?0x4a303d:0xffeeee):(dark?0x2d4262:0xeaf2ff),7);
            const float mid=(row.top+row.bottom)/2;float label_left=row.left+12;
            if(item.dot){Color(item.dot);target->FillEllipse(D2D1::Ellipse({row.left+16,mid},4.5f,4.5f),brush.Get());label_left=row.left+28;}
            else if(item.command!=1||!item.arg){const wchar_t glyph=item.command==1?L'\xE894':item.command==2||item.command==6?L'\xE710':item.command==3?L'\xE8AC':L'\xE74D';if(item.enabled)Center(std::wstring(1,glyph),{row.left+6,row.top,row.left+26,row.bottom},11,danger?0xd46a72:0x8092ac,true);label_left=row.left+28;}
            float right=row.right-8;
            if(!item.hint.empty()){const float w=Measure(item.hint,10);TextLine(item.hint,{right-w-2,row.top,right,row.bottom},10,muted);right-=w+8;}
            if(item.checked){Center(L"\xE73E",{right-18,row.top,right,row.bottom},11,0x3975ef,true);right-=20;}
            TextLine(item.label,{label_left,row.top,right,row.bottom},12,!item.enabled?muted:danger?0xd15c66:ink);
        }
    }
    // Inline naming reuses the self-painted search edit (IME + LumaText painting); the query is restored afterwards.
    void BeginNaming(int mode,uint32_t group,uint64_t entry){
        if(!expanded||!search)return;
        if(mode==1&&history.groups.size()>=clipboard::History::MaxGroups){status=L"最多 20 个分组";Invalidate();return;}
        if(mode==2&&!history.FindGroup(group))return;
        ClosePopup();CloseMore();
        if(!naming)naming_query=query;
        naming=mode;naming_group=group;naming_entry=entry;
        SendMessageW(search,EM_SETLIMITTEXT,clipboard::History::MaxGroupName,0);
        SetWindowTextW(search,mode==2?GroupName(group).c_str():L"");
        ShowWindow(search,SW_SHOW);SetFocus(search);SendMessageW(search,EM_SETSEL,0,-1);
        status=mode==2?L"重命名分组 · Enter 确定 · Esc 取消":entry?L"新建分组并移入所选记录 · Enter 确定 · Esc 取消":L"输入新分组名称 · Enter 确定 · Esc 取消";Invalidate();
    }
    void EndNaming(){
        if(!naming)return;
        SendMessageW(search,EM_SETLIMITTEXT,512,0);SetWindowTextW(search,naming_query.c_str());  // EN_CHANGE is ignored while naming
        query=naming_query;naming=0;naming_group=0;naming_entry=0;naming_query.clear();
        if(GetFocus()==search)SetFocus(window);
        Filter();Invalidate();
    }
    void CancelNaming(){if(!naming)return;EndNaming();status.clear();Invalidate();}
    void CommitNaming(){
        if(!naming)return;wchar_t value[64]{};GetWindowTextW(search,value,64);
        const auto name=clipboard::History::CleanGroupName(value);
        if(name.empty()){status=L"分组名称不能为空 · Esc 取消";Invalidate();return;}
        if(history.GroupNameTaken(name,naming==2?naming_group:0)){status=L"已有同名分组，请换一个名称";Invalidate();return;}
        const int mode=naming;const auto group=naming_group;const auto entry=naming_entry;EndNaming();
        if(mode==2){history.RenameGroup(group,name);CommitHistory();reveal_tab=true;status=L"已重命名为“"+name+L"”";Invalidate();return;}
        const auto id=history.AddGroup(name);if(!id){status=L"最多 20 个分组";Invalidate();return;}
        CommitHistory();reveal_tab=true;
        if(entry&&history.Find(entry)){AssignGroup(entry,id);if(const auto* e=history.Find(entry);e&&e->group==id)status=L"已新建“"+name+L"”并移入所选记录";Invalidate();return;}
        const int index=history.GroupIndex(id);SelectTab(clipboard::History::GroupTabBase+index);
        status=index<9?L"已新建“"+name+L"” · 在其他标签选中记录按 Ctrl+"+std::to_wstring(index+1)+L" 移入":L"已新建“"+name+L"” · 右键记录可移入";Invalidate();
    }
    int MenuCount()const{return menu_confirm?2:3;}
    bool MenuEnabled(int row)const{if(row==0||menu_confirm)return true;if(row==1)return std::any_of(history.entries.begin(),history.entries.end(),[](const clipboard::Entry& e){return !clipboard::History::Protected(e);});return !history.entries.empty();}
    int MenuHit(float x,float y)const{
        if(x<W-256||x>W-24)return -1;
        const float first=menu_confirm?112.f:76.f;
        for(int i=0;i<MenuCount();++i)if(y>=first+i*38&&y<first+i*38+34)return i;
        return -1;
    }
    void OpenMore(){
        ClosePopup();menu_open=true;menu_confirm=false;menu_hover=0;ShowWindow(search,SW_HIDE);SetFocus(window);SetCapture(window);Invalidate();
    }
    void CloseMore(){
        if(!menu_open)return;menu_open=false;menu_confirm=false;
        if(GetCapture()==window)ReleaseCapture();if(expanded)ShowWindow(search,SW_SHOW);Invalidate();
    }
    void ChooseMore(int row){
        if(row<0||row>=MenuCount()||!MenuEnabled(row))return;
        if(menu_confirm){if(row==1){CancelReads();if(disk)disk->CancelSaves();history.Clear(false);if(disk)disk->Commit(history.entries,history.groups);images.clear();ClearFileIcons();Filter();}CloseMore();return;}
        if(row==0){CloseMore();Fold();return;}
        if(row==1){CancelReads();if(disk)disk->CancelSaves();history.Clear(true);if(disk)disk->Commit(history.entries,history.groups);images.clear();ClearFileIcons();Filter();CloseMore();return;}
        menu_confirm=true;menu_hover=0;Invalidate();
    }
    void MenuKey(WPARAM key){
        if(key==VK_ESCAPE){CloseMore();return;}
        if(key==VK_RETURN||key==VK_SPACE){ChooseMore(menu_hover);return;}
        if(key==VK_UP||key==VK_DOWN||key==VK_TAB){const int n=MenuCount(),direction=key==VK_UP?-1:1;for(int i=0;i<n;++i){menu_hover=(menu_hover+direction+n)%n;if(MenuEnabled(menu_hover))break;}Invalidate();}
    }
    void Read(){
        if(!enabled||test_mode)return;const DWORD current=GetClipboardSequenceNumber();if(current==sequence)return;
        bool busy=false;auto entry=clipboard::Read(window,busy);
        if(busy&&retry++<5){SetTimer(EventWindow(),1,50,nullptr);return;}
        KillTimer(EventWindow(),1);retry=0;sequence=current;
        if(entry){if(!disk||!disk->Save(std::move(*entry)))status=L"保存队列或单条容量已满，未记录该条";Invalidate();}
    }
    IDWriteTextFormat* Format(int size,bool icon=false){auto& f=fonts[{size,icon}];if(!f){CheckWin32(SUCCEEDED(writer->CreateTextFormat(icon?L"Segoe MDL2 Assets":L"Microsoft YaHei UI",nullptr,(size==22||size==24)?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,static_cast<float>(size),L"zh-CN",&f)),"Clipboard font");f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);}return f.Get();}
    void Color(UINT32 c){brush->SetColor(D2D1::ColorF(c));}
    void Text(const std::wstring& s,D2D1_RECT_F r,int size,UINT32 c,bool icon=false){text_glyphs+=text_renderer.Draw(target.Get(),writer.Get(),s,Format(size,icon),r,D2D1::ColorF(c)).freetype_glyphs;}
    void Center(const std::wstring& s,D2D1_RECT_F r,int size,UINT32 c,bool icon=false){auto* f=Format(size,icon);f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);Text(s,r,size,c,icon);f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);}
    void TextLine(const std::wstring& text,D2D1_RECT_F rect,int size,UINT32 color){
        if(rect.right<=rect.left||rect.bottom<=rect.top)return;
        ComPtr<IDWriteTextLayout> layout;ComPtr<IDWriteInlineObject> ellipsis;
        if(FAILED(writer->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),Format(size),rect.right-rect.left,rect.bottom-rect.top,&layout)))return;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if(SUCCEEDED(writer->CreateEllipsisTrimmingSign(Format(size),&ellipsis))){const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};layout->SetTrimming(&trimming,ellipsis.Get());}
        text_glyphs+=text_renderer.DrawLayout(target.Get(),writer.Get(),layout.Get(),{rect.left,rect.top},rect,D2D1::ColorF(color)).freetype_glyphs;
    }
    void TextBlock(const std::wstring& text,D2D1_RECT_F rect,int size,UINT32 color){
        const float line=std::ceil(size*1.35f),text_height=std::floor((rect.bottom-rect.top)/line)*line;
        if(text_height<=0||rect.right<=rect.left)return;
        ComPtr<IDWriteTextLayout> layout;ComPtr<IDWriteInlineObject> ellipsis;
        if(FAILED(writer->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),Format(size),rect.right-rect.left,text_height,&layout)))return;
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,line,std::ceil(size*1.05f));
        if(SUCCEEDED(writer->CreateEllipsisTrimmingSign(Format(size),&ellipsis))){const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};layout->SetTrimming(&trimming,ellipsis.Get());}
        UINT32 count{};layout->GetLineMetrics(nullptr,0,&count);const auto visible_lines=static_cast<UINT32>(text_height/line);
        if(count>visible_lines){
            std::vector<DWRITE_LINE_METRICS> lines(count);if(SUCCEEDED(layout->GetLineMetrics(lines.data(),count,&count))){
                std::wstring shown;size_t offset=0;for(UINT32 i=0;i<visible_lines;++i){if(i)shown+=L'\n';shown+=text.substr(offset,lines[i].length-lines[i].newlineLength);offset+=lines[i].length;}shown+=L"…";
                layout.Reset();if(FAILED(writer->CreateTextLayout(shown.data(),static_cast<UINT32>(shown.size()),Format(size),rect.right-rect.left,text_height,&layout)))return;
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,line,std::ceil(size*1.05f));
                if(ellipsis){const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};layout->SetTrimming(&trimming,ellipsis.Get());}
            }
        }
        text_glyphs+=text_renderer.DrawLayout(target.Get(),writer.Get(),layout.Get(),{rect.left,rect.top},rect,D2D1::ColorF(color)).freetype_glyphs;
    }
    void Card(D2D1_RECT_F r,UINT32 fill,UINT32 border,bool highlighted=false){
        brush->SetColor(D2D1::ColorF(fill,highlighted?.88f:.57f));target->FillRoundedRectangle(D2D1::RoundedRect(r,13,13),brush.Get());
        brush->SetColor(D2D1::ColorF(border,highlighted?.65f:.79f));target->DrawRoundedRectangle(D2D1::RoundedRect(r,13,13),brush.Get());
        if(highlighted){Color(0x689af0);target->FillRoundedRectangle(D2D1::RoundedRect({r.left,r.top+23,r.left+3,r.bottom-23},1.5f,1.5f),brush.Get());}
    }
    void Box(D2D1_RECT_F r,UINT32 fill,UINT32 border,float radius=10){Color(fill);target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush.Get());Color(border);target->DrawRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush.Get());}
    void ClipboardIcon(float x,float y,float size){app_icon.Draw(target.Get(),{x,y,x+size,y+size});}
    void Icon(wchar_t glyph,float x,float y,int action,uint64_t id=0,bool active=false,bool on_accent=false){
        D2D1_RECT_F r{x,y,x+32,y+32};if(action)hits.push_back({r,action,id});
        if(hover>=0&&hover<static_cast<int>(hits.size())&&action&&hits[static_cast<size_t>(hover)].action==action&&hits[static_cast<size_t>(hover)].id==id)Box(r,on_accent?0x1769d9:dark?0x34435a:0xe6efff,on_accent?0x1769d9:dark?0x34435a:0xe6efff,6);
        D2D1_MATRIX_3X2_F previous_transform{};target->GetTransform(&previous_transform);
        const float icon_scale=action==14?.65f:(action==31||action==34)?.72f:.80f;
        target->SetTransform(D2D1::Matrix3x2F::Scale(icon_scale,icon_scale,{x+16,y+16})*previous_transform);
        if(glyph==L'\xE8C8'||glyph==L'\xE77F'||glyph==L'\xE74D'){
            Color(on_accent?0xffffff:dark?0xaabbd1:0x748198);const float left=x+9,top=y+8;
            if(glyph==L'\xE8C8'){
                target->DrawRoundedRectangle(D2D1::RoundedRect({left+3,top,left+15,top+15},2,2),brush.Get(),1.4f);
                target->DrawLine({left,top+4},{left,top+18},brush.Get(),1.4f);target->DrawLine({left,top+18},{left+11,top+18},brush.Get(),1.4f);
            }else if(glyph==L'\xE77F'){
                target->DrawRoundedRectangle(D2D1::RoundedRect({left,top+3,left+14,top+18},2,2),brush.Get(),1.4f);
                target->DrawRoundedRectangle(D2D1::RoundedRect({left+3,top,left+11,top+5},1,1),brush.Get(),1.4f);
            }else{
                target->DrawRoundedRectangle(D2D1::RoundedRect({left+1,top+4,left+13,top+18},1.5f,1.5f),brush.Get(),1.4f);
                target->DrawLine({left-1,top+3},{left+15,top+3},brush.Get(),1.4f);target->DrawLine({left+4,top},{left+10,top},brush.Get(),1.4f);
                for(float xx:{left+5,left+9})target->DrawLine({xx,top+7},{xx,top+14},brush.Get(),1.3f);
            }
        }else if(glyph==L'\xE734'){
            Color(active?(dark?0xffce60:0xc98a0a):(dark?0xaabbd1:0x748198));
            ComPtr<ID2D1PathGeometry> star;ComPtr<ID2D1GeometrySink> sink;
            CheckWin32(SUCCEEDED(factory->CreatePathGeometry(&star)),"Favorite star");
            CheckWin32(SUCCEEDED(star->Open(&sink)),"Favorite star path");
            for(int i=0;i<10;++i){const float angle=-1.57079633f+i*.62831853f,radius=i%2?4.5f:10.f;const D2D1_POINT_2F point{x+16+radius*std::cos(angle),y+16+radius*std::sin(angle)};if(i==0)sink->BeginFigure(point,D2D1_FIGURE_BEGIN_FILLED);else sink->AddLine(point);}
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);CheckWin32(SUCCEEDED(sink->Close()),"Favorite star close");
            if(active)target->FillGeometry(star.Get(),brush.Get());target->DrawGeometry(star.Get(),brush.Get(),1.4f);
        }else if(glyph==L'\xE718'){
            Color(on_accent?0xffffff:active?0x2580ff:dark?0xaabbd1:0x748198);const float weight=active?1.9f:1.4f;
            target->DrawLine({x+9,y+25},{x+15,y+18},brush.Get(),weight);target->DrawLine({x+11,y+13},{x+21,y+23},brush.Get(),weight);
            target->DrawLine({x+11,y+13},{x+17,y+11},brush.Get(),weight);target->DrawLine({x+21,y+23},{x+23,y+17},brush.Get(),weight);
            target->DrawLine({x+17,y+11},{x+20,y+7},brush.Get(),weight);target->DrawLine({x+23,y+17},{x+27,y+14},brush.Get(),weight);target->DrawLine({x+20,y+7},{x+27,y+14},brush.Get(),weight);
        }else Center(std::wstring(1,glyph),{x,y,x+32,y+32},16,on_accent?0xffffff:active?0x2580ff:dark?0xaabbd1:0x748198,true);
        target->SetTransform(previous_transform);
    }
    void Image(const clipboard::Entry& e,D2D1_RECT_F r){
        auto found=images.find(e.id);
        if(found==images.end()){
            if(auto frame=thumbnail_cache.Find(e.id)){
                ComPtr<ID2D1Bitmap> cached;const auto props=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
                if(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(frame->Width(),frame->Height()),frame->pixels.data(),frame->Width()*4,props,&cached)))images[e.id]=std::move(cached);
            }else if(!thumbnail_pending.contains(e.id)&&!thumbnail_failed.contains(e.id)){
                if(e.payload&&disk){if(disk->Load(e,image_token,true))thumbnail_pending.insert(e.id);}
                else if(test_mode){auto source=clipboard::Thumbnail(e);ComPtr<ID2D1Bitmap> local;if(source&&SUCCEEDED(target->CreateBitmapFromWicBitmap(source.Get(),nullptr,&local)))images[e.id]=std::move(local);else thumbnail_failed.insert(e.id);}
            }
            found=images.find(e.id);
        }
        ComPtr<ID2D1Bitmap> bitmap;if(found!=images.end())bitmap=found->second;
        if(!bitmap){Center(thumbnail_failed.contains(e.id)?L"点击重试":L"加载中…",r,10,0x6489b2);return;}
        const auto s=bitmap->GetSize();const float factor=std::min((r.right-r.left)/s.width,(r.bottom-r.top)/s.height),w=s.width*factor,h=s.height*factor;
        r.left+=(r.right-r.left-w)/2;r.top+=(r.bottom-r.top-h)/2;r.right=r.left+w;r.bottom=r.top+h;target->DrawBitmap(bitmap.Get(),r);
    }
    static std::vector<std::wstring> FileNames(const clipboard::Entry& entry){
        std::vector<std::wstring> names;for(const auto& name:entry.file_names)if(!name.empty())names.push_back(name);
        if(!names.empty())return names;
        size_t start=0;while(start<entry.text.size()&&names.size()<3){const auto end=entry.text.find(L'\n',start);const auto path=entry.text.substr(start,end==std::wstring::npos?end:end-start);const auto slash=path.find_last_of(L"\\/");names.push_back(path.substr(slash==std::wstring::npos?0:slash+1));if(end==std::wstring::npos)break;start=end+1;}return names;
    }
    static uint32_t FileCount(const clipboard::Entry& entry){return entry.file_count?entry.file_count:entry.text.empty()?0:1+static_cast<uint32_t>(std::count(entry.text.begin(),entry.text.end(),L'\n'));}
    void FileImage(const std::wstring& path,D2D1_RECT_F rect,UINT32 muted){
        const auto key=std::pair{path,clipboard::FileIconResolution(int(std::ceil(32*scale)))};
        auto found=file_bitmaps.find(key);
        if(found==file_bitmaps.end()){
            const auto source=file_icons->Get(key.first,key.second,window);
            if(source){ComPtr<ID2D1Bitmap> bitmap;const auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
                if(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(source->Width(),source->Height()),source->pixels.data(),source->Width()*4,properties,&bitmap))){if(file_bitmaps.size()>=clipboard::FileIconCache::MaxEntries)file_bitmaps.clear();found=file_bitmaps.emplace(key,std::move(bitmap)).first;}
            }
        }
        if(found==file_bitmaps.end()){Center(L"\xE8A5",rect,22,muted,true);return;}
        const auto size=found->second->GetSize();const float factor=std::min((rect.right-rect.left)/size.width,(rect.bottom-rect.top)/size.height),w=size.width*factor,h=size.height*factor;
        rect.left+=(rect.right-rect.left-w)/2;rect.top+=(rect.bottom-rect.top-h)/2;rect.right=rect.left+w;rect.bottom=rect.top+h;target->DrawBitmap(found->second.Get(),rect,1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    static bool Code(const std::wstring& text){return text.find(L"=>")!=std::wstring::npos||text.starts_with(L"const ")||text.starts_with(L"function ")||text.starts_with(L"def ");}
    void DrawMore(){
        const float left=W-264;const UINT32 ink=dark?0xe7effb:0x35445b,muted=dark?0x91a4bf:0x95a3b7;
        for(int i=5;i>=1;--i){const float inset=static_cast<float>(i);brush->SetColor(D2D1::ColorF(0x274568,dark?.025f:.012f));target->FillRoundedRectangle(D2D1::RoundedRect({left-inset,64-inset/2,W-16+inset,252+inset},12+inset,12+inset),brush.Get());}
        Box({left,64,W-16,252},dark?0x222f42:0xfcfdff,dark?0x3a4c66:0xe1e8f2,12);
        const wchar_t* normal[]={L"折叠到屏幕边缘",L"清空历史（保留收藏和分组）",L"清空全部记录…"};
        const wchar_t* confirm[]={L"取消",L"确认清空全部记录"};
        if(menu_confirm)Text(L"清空所有记录及收藏？",{left+18,73,W-32,103},13,ink);
        const float first=menu_confirm?112.f:76.f;
        for(int i=0;i<MenuCount();++i){const float y=first+i*38;const bool available=MenuEnabled(i),danger=menu_confirm?i==1:i==2;
            if(i==menu_hover&&available)Box({left+8,y,W-24,y+34},danger?(dark?0x4a303d:0xffeeee):(dark?0x2d4262:0xeaf2ff),danger?(dark?0x4a303d:0xffeeee):(dark?0x2d4262:0xeaf2ff),7);
            const wchar_t glyph=menu_confirm?(i==0?L'\xE711':L'\xE74D'):(i==0?L'\xE76C':i==1?L'\xE894':L'\xE74D');
            Center(std::wstring(1,glyph),{left+17,y,left+37,y+34},13,!available?muted:danger?0xd46a72:0x8092ac,true);
            Text(menu_confirm?confirm[i]:normal[i],{left+48,y,W-32,y+34},13,!available?muted:danger?0xd15c66:ink);
        }
        Color(dark?0x3a4a60:0xe9eef5);target->DrawLine({left+16,204},{W-32,204},brush.Get());
        Text(menu_confirm?L"系统剪贴板不受影响":L"普通 100 · 收藏/分组 200 · 128 MB",{left+17,210,W-28,241},11,muted);
    }
    void PaintSearch(){
        if(!expanded||menu_open||!IsWindowVisible(search)||!surface)return;
        RECT r{};GetWindowRect(search,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
        const D2D1_RECT_F bounds{r.left/scale,r.top/scale,r.right/scale,r.bottom/scale};
        wchar_t buffer[513]{};GetWindowTextW(search,buffer,513);std::wstring value=buffer;if(ime_active&&!ime_text.empty())value=ime_original.substr(0,ime_first)+ime_text+ime_original.substr(ime_last);
        if(value.empty()){Text(naming?L"输入分组名称（最多 12 字）":L"搜索剪贴内容",bounds,13,dark?0x95a7bf:0x7e899a);if(GetFocus()==search&&caret_on){Color(dark?0xe7effb:0x223248);target->FillRectangle({bounds.left,bounds.top+2,bounds.left+1,bounds.bottom-2},brush.Get());}return;}
        ComPtr<IDWriteTextLayout> layout;if(FAILED(writer->CreateTextLayout(value.data(),static_cast<UINT32>(value.size()),Format(13),100000,bounds.bottom-bounds.top,&layout)))return;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        const auto first_visible=std::min(static_cast<UINT32>(LOWORD(SendMessageW(search,EM_CHARFROMPOS,0,0))),static_cast<UINT32>(value.size()));
        FLOAT offset{},vertical{};DWRITE_HIT_TEST_METRICS hit{};layout->HitTestTextPosition(first_visible,FALSE,&offset,&vertical,&hit);
        const auto native_position=SendMessageW(search,EM_POSFROMCHAR,first_visible,0);
        const D2D1_POINT_2F origin{bounds.left-offset+(native_position==-1?0.f:GET_X_LPARAM(native_position)/scale),bounds.top};DWORD first{},last{};SendMessageW(search,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        if(ime_active&&!ime_text.empty())first=last=ime_first+std::min(ime_cursor,static_cast<DWORD>(ime_text.size()));
        target->PushAxisAlignedClip(bounds,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        std::vector<DWRITE_HIT_TEST_METRICS> selection;
        if(first!=last&&GetFocus()==search){UINT32 count{};layout->HitTestTextRange(first,last-first,origin.x,origin.y,nullptr,0,&count);selection.resize(count);if(count)layout->HitTestTextRange(first,last-first,origin.x,origin.y,selection.data(),count,&count);
            Color(0x267aff);for(const auto& part:selection)target->FillRectangle({part.left,part.top,part.left+part.width,part.top+part.height},brush.Get());}
        search_text_glyphs+=text_renderer.DrawLayout(target.Get(),writer.Get(),layout.Get(),origin,bounds,D2D1::ColorF(dark?0xe7effb:0x223248)).freetype_glyphs;
        for(const auto& part:selection)text_renderer.DrawLayout(target.Get(),writer.Get(),layout.Get(),origin,{part.left,part.top,part.left+part.width,part.top+part.height},D2D1::ColorF(0xffffff));
        if(first==last&&GetFocus()==search&&caret_on){FLOAT x{},y{};layout->HitTestTextPosition(last,FALSE,&x,&y,&hit);Color(dark?0xe7effb:0x223248);target->FillRectangle({origin.x+x,origin.y+y,origin.x+x+1,origin.y+y+hit.height},brush.Get());}
        if(ime_active&&!ime_text.empty()){UINT32 count{};layout->HitTestTextRange(ime_first,static_cast<UINT32>(ime_text.size()),origin.x,origin.y,nullptr,0,&count);std::vector<DWRITE_HIT_TEST_METRICS> parts(count);if(count)layout->HitTestTextRange(ime_first,static_cast<UINT32>(ime_text.size()),origin.x,origin.y,parts.data(),count,&count);Color(0x3975ef);for(const auto& part:parts)target->DrawLine({part.left,part.top+part.height-1},{part.left+part.width,part.top+part.height-1},brush.Get());}
        target->PopAxisAlignedClip();
    }
    bool ApplyBackdrop(){
        const HWND native=EventWindow();const BOOL night=dark;
        DwmSetWindowAttribute(native,20,&night,sizeof(night));
        const DWORD backdrop=3;return SUCCEEDED(DwmSetWindowAttribute(native,38,&backdrop,sizeof(backdrop)));
    }
    void Present(){
        if(opening||liquid_drag_active||!enabled)return;
        SelectWindow();
        struct Reveal {Impl& p;int exceptions=std::uncaught_exceptions();~Reveal(){if(p.reveal_window&&std::uncaught_exceptions()==exceptions){p.reveal_window=false;ShowWindow(p.window,SW_SHOWNOACTIVATE);}}} reveal{*this};
        if(!expanded&&!liquid_failed){const RECT body=VisualBodyBounds();try{PresentFolded();return;}catch(...){
            liquid_surface.reset();liquid_failed=true;compact_positioned=false;LiquidBackdrop(false);
            SetWindowPos(window,nullptr,body.left,body.top,body.right-body.left,body.bottom-body.top,SWP_NOZORDER|SWP_NOACTIVATE);
        }}
        LiquidBackdrop(false);Render();if(!surface)return;
        if(!composition)composition=std::make_unique<ClipboardComposition>();
        composition->Present(window,static_cast<UINT>(width),static_cast<UINT>(height),surface->Pixels());
    }
    void Render(int requested_width=0,int requested_height=0,bool content_only=false){
        if(!enabled||!window)return;
        if(!content_only)SelectWindow();
        std::erase_if(images,[&](const auto& item){return !ImageNeeded(item.first);});
        RECT client{};GetClientRect(window,&client);if(requested_width&&requested_height)client={0,0,requested_width,requested_height};if(!client.right||!client.bottom)return;
        if(!surface||width!=client.right||height!=client.bottom){width=client.right;height=client.bottom;surface=std::make_unique<DibSurface>(width,height);}
        if(!factory)CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf())),"Clipboard renderer");
        if(!writer)CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(writer.GetAddressOf()))),"Clipboard text");
        if(!target){const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));CheckWin32(SUCCEEDED(factory->CreateDCRenderTarget(&props,&target)),"Clipboard surface");CheckWin32(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0),&brush)),"Clipboard brush");}
        CheckWin32(SUCCEEDED(target->BindDC(surface->Dc(),&client)),"Clipboard bind");target->SetDpi(96*scale,96*scale);target->BeginDraw();target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        const UINT32 bg=dark?0x192330:0xf5f8fc,card=dark?0x222e40:0xffffff,ink=dark?0xe7effb:0x223248,muted=dark?0x95a7bf:0x7e899a,border=dark?0x334358:0xe9edf3,accent=0x267aff;
        if(content_only)target->Clear(D2D1::ColorF(0,0.f));
        else if(custom_outline)target->Clear(D2D1::ColorF(bg,PanelOpacity(dark,expanded&&acrylic)));
        else {target->Clear(D2D1::ColorF(0,0.f));const float radius=expanded?22.f:8.f;Color(bg);target->FillRoundedRectangle(D2D1::RoundedRect({0,0,width/scale,height/scale},radius,radius),brush.Get());}hits.clear();
        if(!expanded){

            ClipboardIcon((CW-26)/2,6,26);
            Box({4,37,CW-4,57},dark?0x2a4870:0xe1edff,dark?0x2a4870:0xe1edff,10);
            Center(std::to_wstring(history.entries.size()),{2,37,CW-2,57},history.entries.size()>=100?9:10,accent);
            Color(dark?0x9aabc3:0x7c91ae);target->DrawLine({12,73},{16,77},brush.Get(),1.4f);target->DrawLine({16,77},{12,81},brush.Get(),1.4f);

        }else{
            ClipboardIcon(17,19,43);Text(L"剪贴板",{64,20,148,46},19,ink);Text(L"L U M A S H O T",{65,44,156,59},9,muted);
            if(W>=368)TextLine(hotkey?ShortcutLabel(shortcut_modifiers,shortcut_key):L"侧边栏打开",{175,28,W-133,52},10,muted);
            Icon(L'\xE718',W-118,24,10,0,pinned);Icon(L'\xE713',W-84,24,11);Icon(L'\xE712',W-50,24,12,0,menu_open);
            Card({22,HeaderBottom,W-22,121},card,naming?0x6595f1:border);Text(naming?L"\xE8F4":L"\xE721",{34,81,54,115},16,naming?0x3975ef:muted,true);if(!IsWindowVisible(search)||menu_open)Text(query.empty()?L"搜索剪贴内容":query,{59,81,W-104,115},13,query.empty()?muted:ink);Text(naming?L"Esc 取消":L"Ctrl F",{W-69,81,W-28,115},10,muted);
            const wchar_t* tabs[]={L"全部",L"文本",L"图片",L"文件",L"收藏"};
            const float tab_width=std::min(history.groups.empty()?46.f:40.f,(W-100.f)/5.f),tab_gap=history.groups.empty()?5.f:3.f;
            // Fixed categories, a separator, custom groups and "+", scrolled horizontally when they overflow.
            const float strip_left=18,strip_right=W-92;std::vector<std::pair<float,float>> spans;float strip_x=22;
            for(int i=0;i<5;++i){spans.push_back({strip_x,strip_x+tab_width});strip_x+=tab_width+tab_gap;}
            const float separator_x=strip_x+2;strip_x+=9;
            for(const auto& g:history.groups){const float w=std::clamp(Measure(g.name,12)+30.f,44.f,132.f);spans.push_back({strip_x,strip_x+w});strip_x+=w+5;}
            tab_scroll_max=std::max(0.f,strip_x-1-strip_right);
            if(reveal_tab&&tab>=0&&tab<static_cast<int>(spans.size())){const auto span=spans[static_cast<size_t>(tab)];if(span.first-tab_scroll<strip_left+4)tab_scroll=span.first-22;if(span.second-tab_scroll>strip_right-4)tab_scroll=span.second-strip_right+6;}
            reveal_tab=false;tab_scroll=std::clamp(tab_scroll,0.f,tab_scroll_max);
            auto strip_hit=[&](D2D1_RECT_F r,int action,uint64_t id){r.left=std::max(r.left,strip_left);r.right=std::min(r.right,strip_right);if(r.right>r.left)hits.push_back({r,action,id});};
            target->PushAxisAlignedClip({strip_left,130,strip_right,168},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for(int i=0;i<static_cast<int>(spans.size());++i){
                const float x=spans[static_cast<size_t>(i)].first-tab_scroll,w=spans[static_cast<size_t>(i)].second-spans[static_cast<size_t>(i)].first;const D2D1_RECT_F r{x,134,x+w,165};
                if(r.right<strip_left||r.left>strip_right)continue;
                if(tab==i){Box(r,dark?0x344257:0xffffff,dark?0x344257:0xffffff,8);Color(0x6595f1);target->FillRoundedRectangle(D2D1::RoundedRect({x+w/2-7,162,x+w/2+7,164},1,1),brush.Get());}
                if(i<5){Center(tabs[i],r,12,tab==i?0x3975ef:muted);strip_hit(r,20+i,0);}
                else{const auto& g=history.groups[static_cast<size_t>(i-5)];Color(g.color);target->FillEllipse(D2D1::Ellipse({x+13,149.5f},3.5f,3.5f),brush.Get());TextLine(g.name,{x+21,134,x+w-7,165},12,tab==i?0x3975ef:muted);strip_hit(r,40,g.id);}
            }
            if(separator_x-tab_scroll>strip_left&&separator_x-tab_scroll<strip_right){Color(dark?0x3a4a60:0xdfe6f0);target->DrawLine({separator_x-tab_scroll,140},{separator_x-tab_scroll,159},brush.Get());}
            // Soft fades mark edges with more tabs; wheel, Alt+←/→ and auto-reveal reach them.
            for(int i=0;i<18;++i){const float alpha=static_cast<float>(i+1)/18.f*.95f;brush->SetColor(D2D1::ColorF(bg,alpha));
                if(tab_scroll<tab_scroll_max-.5f)target->FillRectangle({strip_right-18+static_cast<float>(i),130,strip_right-17+static_cast<float>(i),168},brush.Get());
                if(tab_scroll>.5f)target->FillRectangle({strip_left+17-static_cast<float>(i),130,strip_left+18-static_cast<float>(i),168},brush.Get());}
            target->PopAxisAlignedClip();
            // "+" stays pinned beside the sort button so group creation is always reachable.
            {const D2D1_RECT_F r{W-88,137,W-62,162};Box(r,dark?0x243246:0xf6f8fb,dark?0x3a4a60:0xd5deea,7);Center(L"\xE710",r,10,history.groups.size()<clipboard::History::MaxGroups?muted:(dark?0x4c5b70:0xc7d0dc),true);hits.push_back({r,41,0});}
            Icon(L'\xE8CB',W-52,134,13,0,oldest);
            {std::wstring heading=query.empty()?L"最近复制":L"搜索结果";if(query.empty()&&TabGroup())heading=L"分组 · "+GroupName(TabGroup());TextLine(heading,{24,172,W-90,193},10,muted);}Text(std::to_wstring(visible.size())+L" 条内容",{W-77,172,W-20,193},10,muted);
            target->PushAxisAlignedClip({12,ListTop,W-12,ListBottom},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            float y=ListTop;
            for(int i=scroll;i<std::min(scroll+PageRows(),static_cast<int>(visible.size()));++i){
                const auto* e=history.Find(visible[static_cast<size_t>(i)]);if(!e)continue;
                const bool code=Code(e->text),image=e->kind==clipboard::Kind::Image,link=e->text.starts_with(L"http"),file=e->kind==clipboard::Kind::Files,active=e->id==selected;
                // Allocate the available five-card column by content, not uniform blocks.
                float desired=code?116.f:92.f,total=0;
                for(int j=scroll;j<std::min(scroll+PageRows(),static_cast<int>(visible.size()));++j){const auto* row=history.Find(visible[static_cast<size_t>(j)]);if(row)total+=Code(row->text)?126.f:102.f;}
                const float ratio=std::min(1.f,(ListBottom-ListTop)/std::max(1.f,total));const float h=(desired+10)*ratio-10;
                const D2D1_RECT_F r{17,y,W-17,y+h};Card(r,active?(dark?0x304058:0xf4f8ff):card,active?0xa8c4f4:border,active);hits.push_back({r,30,e->id});
                const float actions=y+h-36,content_bottom=actions-4;const bool compact=W<380;
                const float badge_size=compact?std::min(48.f,std::max(0.f,content_bottom-y-14)):48.f,badge_scale=badge_size/48.f;
                const float tx=image?126.f:(file||link)?40.f+badge_size:33.f;const UINT32 row_muted=muted;
                if(image){const float bottom=compact?std::min(y+h-13,content_bottom):y+h-13;Box({24,y+13,116,bottom},dark?0x334054:0xe7eff7,dark?0x334054:0xe7eff7,7);Image(*e,{25,y+14,115,bottom-1});}
                else if(file||link){const D2D1_RECT_F badge{30,y+14,30+badge_size,y+14+badge_size};Box(badge,dark?0x334054:0xedf1f6,dark?0x334054:0xedf1f6,7);
                    if(file){
                        const auto names=FileNames(*e);const size_t n=std::min(size_t(3),names.size());
                        for(size_t k=0;k<n;++k){const float offset=static_cast<float>(k)*7;FileImage(n==1?clipboard::FirstFilePath(e->text):names[k],n==1?D2D1::RectF(30+8*badge_scale,y+14+8*badge_scale,30+40*badge_scale,y+14+40*badge_scale):D2D1::RectF(30+(2+offset)*badge_scale,y+14+(3+offset)*badge_scale,30+(27+offset)*badge_scale,y+14+(28+offset)*badge_scale),muted);}
                    }
                    else if(code)Center(L"</>",badge,std::max(10,static_cast<int>(19*badge_scale)),muted);
                    else if(!file&&!link)Center(L"T",badge,std::max(10,static_cast<int>(25*badge_scale)),muted);
                    else Center(file?L"\xE8A5":L"\xE71B",badge,std::max(10,static_cast<int>(21*badge_scale)),muted,true);
                }
                std::wstring label=e->text;const auto names=file?FileNames(*e):std::vector<std::wstring>{};const auto count=file?FileCount(*e):0;
                if(file)label=count>1?std::to_wstring(count)+L" 个文件":names.empty()?L"文件":names.front();
                const auto* chip_group=tab<clipboard::History::GroupTabBase&&e->group?history.FindGroup(e->group):nullptr;
                const float chip_width=chip_group?std::min(104.f,Measure(chip_group->name,10)+26.f):0.f,text_right=chip_group&&(image||file||link)?W-36-chip_width:W-32;
                // Content and thumbnails stay above the full-size action hit row.
                if(!image&&!file&&!link)Text(code?L"</>  代码文本":L"T  文本",{tx,y+5,W-32,y+24},10,muted);
                TextBlock(label,{tx,y+((image||file||link)?9:25),text_right,(image||count>1)?std::min(y+35,content_bottom):content_bottom},13,link?accent:ink);
                if(chip_group){const D2D1_RECT_F chip{W-30-chip_width,y+7,W-30,y+25};Box(chip,dark?0x2b3a50:0xeef3fa,dark?0x3a4c66:0xdde6f2,9);Color(chip_group->color);target->FillEllipse(D2D1::Ellipse({chip.left+10,y+16},3.f,3.f),brush.Get());TextLine(chip_group->name,{chip.left+17,chip.top,chip.right-7,chip.bottom},10,muted);}
                if(count>1){std::wstring detail;for(size_t k=0;k<std::min(size_t(2),names.size());++k){if(k)detail+=L"、";detail+=names[k];}if(count>2)detail+=L" 等";Text(detail,{tx,y+33,W-24,content_bottom},11,row_muted);}
                if(image){const auto bytes=e->Bytes();Text(L"图片 · "+std::to_wstring((bytes+1023)/1024)+L" KB",{tx,y+35,W-24,std::min(y+55,content_bottom)},11,row_muted);}
                SYSTEMTIME now{};GetLocalTime(&now);wchar_t time[64]{};
                if(e->time.wYear==now.wYear&&e->time.wMonth==now.wMonth&&e->time.wDay==now.wDay)swprintf_s(time,L"今天 %02u:%02u",e->time.wHour,e->time.wMinute);
                else swprintf_s(time,L"%02u/%02u %02u:%02u",e->time.wMonth,e->time.wDay,e->time.wHour,e->time.wMinute);
                TextLine(time,{compact?33.f:tx,actions+9,W-221,actions+32},11,row_muted);
                Box({W-95,actions+3,W-65,actions+29},dark?0x30435c:0xe7effb,dark?0x30435c:0xe7effb,6);
                Icon(L'\xE8F4',W-216,actions,35,e->id,e->group!=0);Icon(L'\xE734',W-176,actions,31,e->id,e->favorite);Icon(L'\xE8C8',W-136,actions,32,e->id);Icon(L'\xE77F',W-96,actions,33,e->id);Icon(L'\xE74D',W-56,actions,34,e->id);
                y+=h+10;
            }
            if(visible.empty()){Center(query.empty()?(TabGroup()?L"这个分组还是空的":tab==4?L"还没有收藏":L"还没有剪贴记录"):L"没有匹配的内容",{24,330+(H-744)/2,W-24,366+(H-744)/2},19,ink);Center(TabGroup()?(tab-4<=9?L"在其他标签选中记录按 Ctrl+"+std::to_wstring(tab-4)+L" 移入":std::wstring(L"右键记录，选择“移到分组”")):std::wstring(tab==4?L"点击记录上的星标，或按 Ctrl+D 收藏":L"复制文本、图片或文件后将在这里显示"),{24,373+(H-744)/2,W-24,417+(H-744)/2},14,muted);}
            target->PopAxisAlignedClip();
            if(visible.size()>static_cast<size_t>(PageRows())){const float track=ListBottom-ListTop;const float thumb=track*PageRows()/static_cast<float>(visible.size());const float top=ListTop+(track-thumb)*scroll/static_cast<float>(visible.size()-static_cast<size_t>(PageRows()));Box({W-8,top,W-5,top+thumb},0xb7c8df,0xb7c8df,2);}
            Color(dark?0x3a4a60:0xe2e9f2);target->DrawLine({22,H-44},{W-22,H-44},brush.Get());Color(0x73b69b);target->FillEllipse(D2D1::Ellipse({27,H-24},2.5f,2.5f),brush.Get());TextLine(status.empty()?(history.groups.empty()?L"↑↓ 选择 · Enter 粘贴 · Shift+Enter 纯文本":L"↑↓ 选择 · Enter 粘贴 · Ctrl+1–9 移入分组 · Alt+←→ 切换"):status,{39,H-41,W-51,H-9},10,muted);Icon(L'\xE76C',W-48,H-42,14);
        }
        if(menu_open&&expanded)DrawMore();if(popup_open&&expanded)DrawPopup();
        PaintSearch();
        const auto hr=target->EndDraw();if(hr==D2DERR_RECREATE_TARGET){CancelReads();images.clear();file_bitmaps.clear();app_icon.Reset();brush.Reset();target.Reset();Invalidate();}else {CheckWin32(SUCCEEDED(hr),"Render clipboard");}
    }
    void FinishCopy(const clipboard::Entry& entry,bool paste,HWND destination,DWORD process){
        if(test_mode){last_read_id=entry.id;status=L"测试模式：未改动系统剪贴板";return;}
        if(copy_quick&&!QuickDestinationValid()){CloseQuick();return;}
        const HWND foreground=GetForegroundWindow();
        if(foreground!=window&&!IsChild(window,foreground)&&!((pinned||copy_quick)&&foreground==destination)){status=L"已取消：输入焦点已改变";Invalidate();return;}
        DWORD current_process{};if(paste&&(!IsWindow(destination)||destination==window||!GetWindowThreadProcessId(destination,&current_process)||current_process!=process)){status=L"原输入窗口已失效，请重新选择目标窗口";Invalidate();return;}
        if(!clipboard::Write(window,entry,copy_plain)){status=L"剪贴板正忙或内容不可用，请重试";Invalidate();return;}
        sequence=GetClipboardSequenceNumber();status=L"已复制";Invalidate();
        if(paste){ClosePreview();previous=destination;paste_target=destination;paste_focus=copy_focus;paste_process=process;paste_sequence=sequence;paste_wait=0;paste_keyboard=copy_keyboard;paste_quick=copy_quick;paste_activated=paste_returning=false;paste_inflight=true;SetTimer(EventWindow(),2,40,nullptr);}
    }
    void StopPaste(const wchar_t* message){KillTimer(EventWindow(),2);paste_inflight=paste_returning=paste_quick=false;status=message;UpdateQuick();Invalidate();}
    static bool PasteKeysHeld(){
        // Keep the triggering Enter (including its key-up/repeat) in our window.
        for(int key:{VK_RETURN,VK_LBUTTON,VK_RBUTTON,VK_CONTROL,VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN})if(GetAsyncKeyState(key)&0x8000)return true;
        return false;
    }
    bool RestoreInputFocus(){
        if(!paste_focus)return true; // Custom controls can expose only a top-level window.
        if(!IsWindow(paste_focus)||(paste_focus!=paste_target&&!IsChild(paste_target,paste_focus)))return false;
        const DWORD thread=GetWindowThreadProcessId(paste_target,nullptr);GUITHREADINFO info{sizeof(info)};
        if(GetGUIThreadInfo(thread,&info)&&info.hwndFocus==paste_focus)return true;
        const DWORD current=GetCurrentThreadId();const bool attach=thread!=current;
        if(attach&&!AttachThreadInput(current,thread,TRUE))return false;
        SetFocus(paste_focus);
        const bool restored=GetGUIThreadInfo(thread,&info)&&info.hwndFocus==paste_focus;
        if(attach)AttachThreadInput(current,thread,FALSE);
        return restored;
    }
    void PasteTick(){
        if(!paste_inflight)return;
        DWORD process{};
        if(++paste_wait>125||!IsWindow(paste_target)||
            !GetWindowThreadProcessId(paste_target,&process)||process!=paste_process||GetClipboardSequenceNumber()!=paste_sequence){
            StopPaste(L"自动粘贴已取消：目标窗口或剪贴板已变化");return;
        }
        const HWND foreground=GetForegroundWindow();
        if(paste_quick){
            if(!QuickDestinationValid()){CloseQuick();return;}
            // The quick picker never activates or restores focus: the original
            // application and its input control must still own it throughout.
            paste_activated=true;
        }
        if(paste_returning){
            // SendInput queues input; leave time for the target to consume it.
            // Never resend or claim that the receiving application accepted it.
            if(foreground!=paste_target){StopPaste(L"已发送粘贴请求，输入窗口已切换");return;}
            if(GetTickCount64()<paste_return_at||PasteKeysHeld())return;
            StopPaste(L"已发送粘贴 · ↑↓ 继续选择 · Enter 插入");
            if(expanded&&pinned){
                if(SetForegroundWindow(window))SetFocus(window);
                else {status=L"已发送粘贴 · 按剪贴板快捷键继续选择";Invalidate();}
            }
            return;
        }
        if(!paste_activated){
            if(foreground!=paste_target&&foreground!=window&&!IsChild(window,foreground)){StopPaste(L"已取消：输入窗口已切换");return;}
            if(PasteKeysHeld())return;
            if(!pinned){Fold();paste_inflight=true;SetTimer(EventWindow(),2,40,nullptr);}
            if(foreground!=paste_target&&!SetForegroundWindow(paste_target)){StopPaste(L"无法返回输入窗口，内容已复制，请手动粘贴");return;}
            paste_activated=true;
            if(GetForegroundWindow()!=paste_target)return;
        }
        const HWND current_foreground=GetForegroundWindow();
        if(current_foreground!=paste_target){
            if(current_foreground==window||IsChild(window,current_foreground))return;
            StopPaste(L"已取消：输入窗口已切换");return;
        }
        if(PasteKeysHeld())return;
        if(!paste_quick&&!RestoreInputFocus()){StopPaste(L"无法恢复原输入位置，内容已复制，请手动粘贴");return;}
        INPUT input[4]{};for(auto& i:input)i.type=INPUT_KEYBOARD;
        input[0].ki.wVk=VK_CONTROL;input[1].ki.wVk='V';input[2].ki.wVk='V';input[2].ki.dwFlags=KEYEVENTF_KEYUP;input[3].ki.wVk=VK_CONTROL;input[3].ki.dwFlags=KEYEVENTF_KEYUP;
        if(SendInput(4,input,sizeof(INPUT))!=4){StopPaste(L"目标不允许自动粘贴，请手动 Ctrl+V");return;}
        if(paste_quick){StopPaste(L"");copy_quick=false;if(!quick_continuous)CloseQuick();}
        else if(pinned&&paste_keyboard){paste_returning=true;paste_return_at=GetTickCount64()+200;status=L"已发送粘贴请求…";Invalidate();}
        else StopPaste(pinned?L"已发送粘贴，可继续点击下一条":L"已发送粘贴请求");
    }
    void Copy(uint64_t id,bool paste,bool plain=false,bool keyboard=false){
        const auto* e=history.Find(id);if(!e||copy_pending||paste_inflight)return;
        if(plain&&e->kind!=clipboard::Kind::Text){status=L"纯文本粘贴仅支持文本记录";Invalidate();return;}
        if(pinned)RememberTarget(GetForegroundWindow());
        copy_target=previous;copy_process=previous_process;copy_focus=previous_focus;copy_plain=plain;copy_keyboard=keyboard;copy_quick=false;
        if(e->payload&&disk){copy_paste=paste;copy_pending=true;const auto token=++copy_token;
            if(!disk->Load(*e,token)){copy_pending=false;status=L"读取队列已满，请重试";}else status=paste?L"正在准备粘贴…（Esc 取消）":L"正在准备复制…（Esc 取消）";Invalidate();return;}
        FinishCopy(*e,paste,copy_target,copy_process);
    }
    void Action(int action,uint64_t id,bool keep_focus=false){
        if(action>=20&&action<25){SelectTab(action-20);return;}
        if(action==40){const int index=history.GroupIndex(static_cast<uint32_t>(id));if(index>=0)SelectTab(clipboard::History::GroupTabBase+index);return;}
        if(action==41){BeginNaming(1,0,0);return;}
        if(action==35){OpenEntryPopupAtButton(id,false);return;}
        if(action==10){RememberTarget(GetForegroundWindow());pinned=!pinned;UpdateActivationPolicy();
            DWORD process{};if(pinned&&!keep_focus&&IsWindow(previous)&&GetWindowThreadProcessId(previous,&process)&&process==previous_process)SetForegroundWindow(previous);
            status=pinned?L"已固定：↑↓ 选择，Enter 连续粘贴":L"已取消固定：失焦时折叠";
            // Unpinning resumes normal focus handling; fold on the next deactivation.
            if(!pinned&&expanded){SetForegroundWindow(window);if(GetFocus()!=search)SetFocus(window);}
            Invalidate();}
        if(action==11){Fold();if(settings)settings();}
        if(action==12){if(menu_open)CloseMore();else OpenMore();}
        if(action==13){oldest=!oldest;Filter();RevealSelection();}if(action==14)Fold();
        if(action==30&&pinned&&!preview.IsOpen()){if(copy_pending||paste_inflight){status=L"上一条正在插入，请稍候";Invalidate();return;}selected=id;Copy(id,true);Invalidate();return;}
        if(action==30){if(thumbnail_failed.erase(id))images.erase(id);++copy_token;copy_pending=false;selected=id;if(preview.IsOpen())ShowPreview();else PrefetchPreview();Invalidate();}if(action==31){auto* e=history.Find(id);if(e&&!e->favorite&&!history.CanProtect(*e)){selected=id;status=L"收藏和分组最多 200 条，请先整理";Invalidate();return;}if(e){e->favorite=!e->favorite;if(disk)disk->Commit(history.entries,history.groups);selected=id;status=e->favorite?L"已收藏，清空历史时保留":L"已取消收藏";}Filter();RevealSelection();}
        if(action==32||action==33)Copy(id,action==33);
        if(action==34){
            const uint64_t viewport_top=scroll>=0&&scroll<static_cast<int>(visible.size())?visible[static_cast<size_t>(scroll)]:0;
            const int previous_scroll=scroll;
            const auto it=std::find(visible.begin(),visible.end(),id);
            if(id==selected&&it!=visible.end())selected=std::next(it)!=visible.end()?*std::next(it):it!=visible.begin()?*std::prev(it):0;
            CancelReads();history.Erase(id);if(disk)disk->Commit(history.entries,history.groups);images.erase(id);ClearFileIcons();Filter();
            // Deletion preserves the browsing viewport, even when the selected
            // row was scrolled offscreen. Only keyboard navigation reveals it.
            const auto top=std::find(visible.begin(),visible.end(),viewport_top);
            scroll=std::clamp(top!=visible.end()?static_cast<int>(top-visible.begin()):previous_scroll,0,std::max(0,static_cast<int>(visible.size())-PageRows()));
            Invalidate();
        }
    }
    void RevealSelection(){
        const auto it=std::find(visible.begin(),visible.end(),selected);if(it==visible.end())return;
        const int index=static_cast<int>(it-visible.begin());if(index<scroll)scroll=index;if(index>=scroll+PageRows())scroll=index-PageRows()+1;Invalidate();
    }
    void TogglePreview(){
        if(preview.IsOpen()){preview.Hide();return;}
        ShowPreview();
    }
    bool RequestPreview(const clipboard::Entry& entry){
        if(!disk||!entry.payload)return false;
        if(preview_pending==entry.id)return true;
        ++preview_token;preview_pending=0;
        if(!disk->LoadPreview(entry,preview_token))return false;
        preview_pending=entry.id;return true;
    }
    void PrefetchPreview(){
        if(!expanded||!disk)return;
        const auto* entry=history.Find(selected);
        if(!entry)return;
        // Selection takes priority over speculative neighbors. Replacing the
        // request drops queued stale work; only bounded pixels return to UI.
        if(entry->kind==clipboard::Kind::Image&&!preview_cache.Find(selected)){
            RequestPreview(*entry);return;
        }
        if(preview_pending)return;
        const auto it=std::find(visible.begin(),visible.end(),selected);
        if(it==visible.end())return;
        const auto index=it-visible.begin();
        for(const auto offset:{1,-1}){
            const auto next=index+offset;
            if(next<0||next>=static_cast<decltype(index)>(visible.size()))continue;
            const auto* neighbor=history.Find(visible[static_cast<size_t>(next)]);
            if(neighbor&&neighbor->kind==clipboard::Kind::Image&&!preview_cache.Find(neighbor->id)){
                RequestPreview(*neighbor);return;
            }
        }
    }
    void ShowPreview(){
        auto* entry=history.Find(selected);
        if(!expanded||!entry){ClosePreview();return;}
        if(preview.IsOpen()&&preview.CurrentId()==selected)return;
        RECT rect{};GetWindowRect(window,&rect);
        clipboard::PreviewData data;data.image=preview_cache.Find(selected);
        const bool cached=data.image!=nullptr;
        if(!cached)data.image=thumbnail_cache.Find(selected);
        preview.SetCopyAction([this,id=entry->id]{Copy(id,false);});
        preview.Show(window,*entry,rect,dark,scale,std::move(data));
        if(cached){PrefetchPreview();return;}
        if(entry->payload&&disk){
            if(!RequestPreview(*entry)){
                clipboard::PreviewData error;error.error=L"预览读取队列已满，请关闭后重试";
                preview.SetContent(selected,std::move(error));
            }
        }else{
            preview.SetContent(selected,clipboard::PreparePreview(*entry));
        }
    }
    void Key(WPARAM key){
        if(key==VK_F2){
            const HWND foreground=GetForegroundWindow();
            if(!expanded||(foreground!=window&&!IsChild(window,foreground))||
                ((GetKeyState(VK_CONTROL)|GetKeyState(VK_SHIFT)|GetKeyState(VK_MENU)|GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000))return;
            CloseMore();Action(10,0,true);status=pinned?L"已固定 · F2 取消固定":L"已取消固定 · F2 固定面板";
            SetTimer(EventWindow(),6,2000,nullptr);Invalidate();return;
        }
        if(menu_open){MenuKey(key);return;}
        if(popup_open&&PopupKey(key))return;
        if(naming){if(key==VK_ESCAPE)CancelNaming();else if(key==VK_RETURN)CommitNaming();return;}
        if(key==VK_ESCAPE){if(preview.IsOpen()){preview.Hide();return;}const HWND destination=previous;Fold();if(!test_mode&&IsWindow(destination))SetForegroundWindow(destination);return;}
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        if(key==VK_TAB){SetFocus(search);return;}
        if(ctrl&&key=='F'){SetFocus(search);SendMessageW(search,EM_SETSEL,0,-1);return;}
        if(ctrl&&key=='D'){Action(31,selected);return;}
        if(ctrl&&(GetKeyState(VK_SHIFT)&0x8000)&&key=='N'){BeginNaming(1,0,history.Find(selected)?selected:0);return;}
        if(ctrl&&key>='0'&&key<='9'){
            if(!history.Find(selected)){status=L"请先选择一条记录";Invalidate();return;}
            if(key=='0'){AssignGroup(selected,0);return;}
            const size_t index=static_cast<size_t>(key-'1');
            if(index>=history.groups.size()){status=history.groups.empty()?L"还没有分组 · Ctrl+Shift+N 新建并移入":L"没有第 "+std::to_wstring(index+1)+L" 个分组";Invalidate();return;}
            AssignGroup(selected,history.groups[index].id);return;
        }
        if(ctrl&&key=='G'){if(history.Find(selected))OpenEntryPopupAtButton(selected,true);else{status=L"请先选择一条记录";Invalidate();}return;}
        if(ctrl&&key=='C'){Copy(selected,false);return;}if(key==VK_RETURN){Copy(selected,true,(GetKeyState(VK_SHIFT)&0x8000)!=0,true);return;}
        if(key==VK_DELETE){Action(34,selected);return;}
        if(key==VK_SPACE){SetFocus(window);TogglePreview();return;}
        if(key==VK_LEFT||key==VK_RIGHT){const int n=TabCount();SelectTab((tab+(key==VK_RIGHT?1:n-1))%n);return;}
        if(key==VK_HOME||key==VK_END||key==VK_PRIOR||key==VK_NEXT){
            if(visible.empty())return;
            const auto it=std::find(visible.begin(),visible.end(),selected);
            const int index=it==visible.end()?0:static_cast<int>(it-visible.begin());
            const int next=key==VK_HOME?0:key==VK_END?static_cast<int>(visible.size())-1:std::clamp(index+(key==VK_NEXT?PageRows():-PageRows()),0,static_cast<int>(visible.size())-1);
            ++copy_token;copy_pending=false;selected=visible[static_cast<size_t>(next)];RevealSelection();if(preview.IsOpen())ShowPreview();else PrefetchPreview();Invalidate();return;
        }
        if(key==VK_UP||key==VK_DOWN){SetFocus(window);++copy_token;copy_pending=false;if(visible.empty())return;const auto it=std::find(visible.begin(),visible.end(),selected);int index=static_cast<int>(it-visible.begin());index=std::clamp(index+(key==VK_DOWN?1:-1),0,static_cast<int>(visible.size())-1);selected=visible[static_cast<size_t>(index)];if(index<scroll)scroll=index;if(index>=scroll+PageRows())scroll=index-PageRows()+1;if(preview.IsOpen())ShowPreview();else PrefetchPreview();Invalidate();}
    }
    static LRESULT CALLBACK SearchProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto* p=reinterpret_cast<Impl*>(data);
        if(m==WM_LBUTTONDOWN){p->RememberTarget(GetForegroundWindow());SetForegroundWindow(p->window);SetFocus(w);}
        if(m==WM_IME_STARTCOMPOSITION){wchar_t value[513]{};GetWindowTextW(w,value,513);p->ime_original=value;p->ime_text.clear();p->ime_active=true;SendMessageW(w,EM_GETSEL,reinterpret_cast<WPARAM>(&p->ime_first),reinterpret_cast<LPARAM>(&p->ime_last));p->Invalidate();}
        if(m==WM_IME_COMPOSITION){HIMC context=ImmGetContext(w);if(context){if(lp&GCS_COMPSTR){const LONG bytes=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0);if(bytes>=0&&bytes<=1024){p->ime_text.resize(static_cast<size_t>(bytes)/sizeof(wchar_t));if(bytes)ImmGetCompositionStringW(context,GCS_COMPSTR,p->ime_text.data(),static_cast<DWORD>(bytes));}const LONG cursor=ImmGetCompositionStringW(context,GCS_CURSORPOS,nullptr,0);p->ime_cursor=cursor<0?0:static_cast<DWORD>(cursor);}if(lp&GCS_RESULTSTR)p->ime_text.clear();ImmReleaseContext(w,context);}p->Invalidate();}
        if(m==WM_IME_ENDCOMPOSITION){p->ime_active=false;p->ime_text.clear();p->ime_original.clear();p->Invalidate();}
        if(p->ime_active&&(m==WM_KEYDOWN||m==WM_CHAR))return DefSubclassProc(w,m,wp,lp);

        if(m==WM_SETFOCUS){p->suppress_search_space=false;p->caret_on=true;const UINT blink=GetCaretBlinkTime();if(blink&&blink!=INFINITE)SetTimer(p->EventWindow(),3,blink,nullptr);p->Invalidate();}
        if(m==WM_KILLFOCUS){KillTimer(p->EventWindow(),3);p->caret_on=false;p->CancelNaming();p->Invalidate();}
        if(m==WM_SYSKEYDOWN&&(wp==VK_LEFT||wp==VK_RIGHT)&&!p->naming&&!p->ime_active){p->Key(wp);return 0;}
        if(m==WM_PAINT){PAINTSTRUCT paint{};BeginPaint(w,&paint);EndPaint(w,&paint);p->Invalidate();return 0;}
        if(m==WM_KEYDOWN||m==WM_LBUTTONUP){p->caret_on=true;p->Invalidate();}
        if(m==WM_KEYDOWN){
            if(p->ime_active)return DefSubclassProc(w,m,wp,lp);
            if(p->naming){
                if(wp==VK_RETURN){if(!(lp&(1LL<<30)))p->CommitNaming();return 0;}
                if(wp==VK_ESCAPE){p->CancelNaming();return 0;}
                if(wp==VK_TAB||wp==VK_UP||wp==VK_DOWN||wp==VK_PRIOR||wp==VK_NEXT||wp==VK_F2)return 0;
                return DefSubclassProc(w,m,wp,lp);
            }
            if(wp==VK_F2){if(!(lp&(1LL<<30)))p->Key(wp);return 0;}
            if((GetKeyState(VK_CONTROL)&0x8000)&&((wp>='0'&&wp<='9')||wp=='G'||(wp=='N'&&(GetKeyState(VK_SHIFT)&0x8000)))){if(!(lp&(1LL<<30)))p->Key(wp);return 0;}
            if(wp=='D'&&(GetKeyState(VK_CONTROL)&0x8000)){if(!(lp&(1LL<<30)))p->Key(wp);return 0;}
            if(wp==VK_TAB){SetFocus(p->window);return 0;}
            if(wp==VK_SPACE&&p->query.empty()){
                p->suppress_search_space=true;
                if(!(lp&(1LL<<30)))p->Key(wp);
                return 0;
            }
            if(wp==VK_ESCAPE||wp==VK_RETURN||wp==VK_DOWN||wp==VK_UP||wp==VK_PRIOR||wp==VK_NEXT){if(wp!=VK_RETURN||!(lp&(1LL<<30)))p->Key(wp);return 0;}
            if(wp!=VK_SPACE)p->suppress_search_space=false;
        }
        // TranslateMessage emits WM_CHAR even when WM_KEYDOWN was handled.
        if(m==WM_CHAR&&(wp==VK_RETURN||wp==VK_ESCAPE||wp==VK_TAB||wp==4||wp==7||wp==14))return 0;
        if(m==WM_CHAR&&wp==L' '&&p->suppress_search_space){p->suppress_search_space=false;return 0;}
        if(m==WM_KEYUP&&wp==VK_SPACE)p->suppress_search_space=false;
        if(m==WM_NCDESTROY)RemoveWindowSubclass(w,SearchProc,1);return DefSubclassProc(w,m,wp,lp);
    }
    static LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
        auto* p=reinterpret_cast<Impl*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){p=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);if(!p->window)p->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}if(!p)return DefWindowProcW(w,m,wp,lp);
        if(p->opening&&w==p->folded_window){
            if(m==WM_ACTIVATE&&LOWORD(wp)==WA_INACTIVE&&reinterpret_cast<HWND>(lp)!=p->native_window&&!p->pinned){p->Fold();return 0;}
            if(m==WM_KEYDOWN||m==WM_CHAR){p->StopOpening(true);return SendMessageW(p->native_window,m,wp,lp);}
        }
        // Parked native HWND remains the stable async/controller endpoint.
        if(p->switching_window||(w!=p->window&&m!=WM_NCCREATE)){
            const bool service=m==clipboard::SessionStoreReady||m==clipboard::FileIconReady||m==WM_CLIPBOARDUPDATE||m==WM_HOTKEY||m==WM_TIMER||m==clipboard::QuickKeyMessage||m==clipboard::QuickWheelMessage||m==clipboard::QuickDismissMessage||m==WM_COMMAND||m==WM_CTLCOLOREDIT||(m==WM_SETTINGCHANGE&&w==p->EventWindow());
            if(!service){
                if(m==WM_NCCALCSIZE&&wp)return 0;
                if(m==WM_ERASEBKGND)return 1;
                if(m==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(w,&ps);EndPaint(w,&ps);return 0;}
                return DefWindowProcW(w,m,wp,lp);
            }
        }
        try {switch(m){
        case clipboard::SessionStoreReady:p->DrainStore();return 0;
        case clipboard::FileIconReady:p->Invalidate();return 0;
        case WM_SETTINGCHANGE:if(!MotionEnabled()){p->StopOpening(true);p->StopLiquidDrag(true);}p->acrylic=p->ApplyBackdrop();p->ClearFileIcons();p->Invalidate();return 0;
        case WM_NCCALCSIZE:if(wp)return 0;break;
        case WM_NCHITTEST:return p->ResizeHit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
        case WM_GETMINMAXINFO:{auto* limits=reinterpret_cast<MINMAXINFO*>(lp);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&monitor);
            const LONG max_w=monitor.rcWork.right-monitor.rcWork.left-8,max_h=monitor.rcWork.bottom-monitor.rcWork.top-8;
            limits->ptMinTrackSize={std::min(max_w,static_cast<LONG>((p->expanded?static_cast<float>(clipboard::PanelMinWidthDip):p->CW)*p->scale)),std::min(max_h,static_cast<LONG>((p->expanded?static_cast<float>(clipboard::PanelMinHeightDip):p->CH)*p->scale))};
            limits->ptMaxTrackSize={max_w,max_h};return 0;}
        case WM_ENTERSIZEMOVE:p->resizing=p->expanded;p->CloseMore();return 0;
        case WM_SIZE:p->ResizeClient(LOWORD(lp),HIWORD(lp));return 0;
        case WM_EXITSIZEMOVE:if(p->resizing){p->resizing=false;RECT bounds{};GetWindowRect(w,&bounds);p->anchor={bounds.right,bounds.top};p->placed=true;
            if(!p->SaveLayout()){p->status=L"窗口尺寸保存失败";p->Invalidate();}}return 0;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(w,&ps);try{p->Present();}catch(...){EndPaint(w,&ps);throw;}EndPaint(w,&ps);return 0;}
        case WM_CLIPBOARDUPDATE:p->Read();return 0;
        case WM_MOUSEACTIVATE:p->RememberTarget(GetForegroundWindow());if(p->pinned||!p->expanded)return MA_NOACTIVATE;break;
        case WM_HOTKEY:if(p->shortcut_key&&(!p->hotkey_allowed||p->hotkey_allowed())&&!p->hotkeys_suspended)p->ShowFromShortcut();return 0;
        case clipboard::QuickKeyMessage:if(p->quick_active)p->QuickKey(wp,(lp&1)!=0);return 0;
        case clipboard::QuickWheelMessage:if(p->quick_active&&p->quick&&p->QuickDestinationValid())SendMessageW(p->quick->Window(),WM_MOUSEWHEEL,wp,lp);return 0;
        case clipboard::QuickDismissMessage:p->CloseQuick();return 0;
        case WM_TIMER:if(wp==10){p->OpeningTick(GetTickCount64());return 0;}if(wp==9){p->LiquidDragTick(GetTickCount64());return 0;}if(wp==8){p->ShortcutFocusTick();return 0;}if(wp==7){if(p->quick_active&&(!p->QuickDestinationValid()||p->hotkeys_suspended||(p->hotkey_allowed&&!p->hotkey_allowed())))p->CloseQuick();return 0;}if(wp==6){KillTimer(w,6);if(p->status==L"已固定 · F2 取消固定"||p->status==L"已取消固定 · F2 固定面板"){p->status.clear();p->Invalidate();}return 0;}if(wp==5){KillTimer(w,5);return 0;}if(wp==4){KillTimer(w,4);if(!p->expanded){if(p->composition)p->composition->TrimIdle();p->parked_composition.reset();}return 0;}if(wp==3){if(GetFocus()==p->search){p->caret_on=!p->caret_on;p->Invalidate();}else KillTimer(w,3);return 0;}if(wp==1)p->Read();if(wp==2)p->PasteTick();return 0;
        case WM_LBUTTONDOWN:p->RememberTarget(GetForegroundWindow());p->paste_click.reset();if(p->popup_open){const float mx=GET_X_LPARAM(lp)/p->scale,my=GET_Y_LPARAM(lp)/p->scale;if(p->InsidePopup(mx,my)){const int row=p->PopupHit(mx,my);if(row>=0)p->ChoosePopup(row);}else p->ClosePopup();return 0;}if(p->menu_open){const float mx=GET_X_LPARAM(lp)/p->scale,my=GET_Y_LPARAM(lp)/p->scale;const int row=p->MenuHit(mx,my);if(row>=0)p->ChooseMore(row);else if(mx<p->W-264||mx>p->W-16||my<64||my>252)p->CloseMore();return 0;}{const float x=GET_X_LPARAM(lp)/p->scale,y=GET_Y_LPARAM(lp)/p->scale;if(p->expanded&&p->visible.size()>static_cast<size_t>(p->PageRows())&&x>=p->W-12&&y>=p->ListTop&&y<=p->ListBottom){p->BeginScroll(y);return 0;}if(!p->expanded||p->HeaderDragHit(x,y)){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(w,&point);p->BeginDrag(point);return 0;}}if(!p->pinned||GetForegroundWindow()==w)SetFocus(w);for(auto it=p->hits.rbegin();it!=p->hits.rend();++it){const float x=GET_X_LPARAM(lp)/p->scale,y=GET_Y_LPARAM(lp)/p->scale;const auto r=it->rect;if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom){const auto h=*it;if(p->pinned&&(h.action==30||h.action==33)&&!p->preview.IsOpen())p->paste_click=h;else p->Action(h.action,h.id);break;}}return 0;
        case WM_LBUTTONUP:if(p->paste_click){const auto hit=*p->paste_click;p->paste_click.reset();const float x=GET_X_LPARAM(lp)/p->scale,y=GET_Y_LPARAM(lp)/p->scale;if(x>=hit.rect.left&&x<hit.rect.right&&y>=hit.rect.top&&y<hit.rect.bottom)p->Action(hit.action,hit.id);return 0;}if(p->scroll_drag){p->ScrollAt(GET_Y_LPARAM(lp)/p->scale);p->scroll_drag=false;ReleaseCapture();return 0;}if(p->drag_pending){p->EndDrag();return 0;}break;
        case WM_CAPTURECHANGED:case WM_CANCELMODE:p->paste_click.reset();p->drag_pending=p->scroll_drag=false;p->ReleaseLiquidDrag();p->CloseMore();p->ClosePopup();return 0;
        case WM_MOUSEMOVE:{if(p->scroll_drag){p->ScrollAt(GET_Y_LPARAM(lp)/p->scale);return 0;}if(p->menu_open){const int row=p->MenuHit(GET_X_LPARAM(lp)/p->scale,GET_Y_LPARAM(lp)/p->scale);if(row!=p->menu_hover){p->menu_hover=row;p->Invalidate();}return 0;}if(p->popup_open){const int row=p->PopupHit(GET_X_LPARAM(lp)/p->scale,GET_Y_LPARAM(lp)/p->scale);if(row!=p->popup_hover){p->popup_hover=row;p->Invalidate();}return 0;}if(p->drag_pending){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(w,&point);p->Drag(point);return 0;}int hit=-1;const float x=GET_X_LPARAM(lp)/p->scale,y=GET_Y_LPARAM(lp)/p->scale;for(size_t i=0;i<p->hits.size();++i){const auto r=p->hits[i].rect;if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom)hit=static_cast<int>(i);}if(hit!=p->hover){p->hover=hit;const wchar_t* hint=L"";if(hit>=0){switch(p->hits[static_cast<size_t>(hit)].action){case 31:hint=L"收藏 / 取消收藏 · Ctrl+D";break;case 32:hint=L"复制内容 · Ctrl+C";break;case 33:hint=L"Enter 粘贴 · Shift+Enter 纯文本";break;case 34:hint=L"删除记录 · Delete";break;case 35:hint=L"移到分组 · Ctrl+G · Ctrl+1–9";break;case 40:hint=L"右键可重命名、更换颜色或删除分组";break;case 41:hint=L"新建分组 · Ctrl+Shift+N";break;case 10:hint=L"固定 / 取消固定面板 · F2";break;case 14:hint=L"折叠到屏幕边缘 · Esc";break;default:break;}}p->status=hint;p->Invalidate();}return 0;}
        case WM_MOUSEWHEEL:if(p->preview.ForwardWheel(wp,lp))return 0;if(p->menu_open||p->popup_open)return 0;{POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&point);const float x=point.x/p->scale,y=point.y/p->scale;if(p->expanded&&y>=130&&y<168&&x<p->W-92){p->tab_scroll=std::clamp(p->tab_scroll-GET_WHEEL_DELTA_WPARAM(wp)/static_cast<float>(WHEEL_DELTA)*48.f,0.f,p->tab_scroll_max);p->Invalidate();return 0;}}p->scroll=std::clamp(p->scroll-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA,0,std::max(0,static_cast<int>(p->visible.size())-p->PageRows()));p->Invalidate();return 0;
        case WM_KEYDOWN:{const bool ctrl_key=(GetKeyState(VK_CONTROL)&0x8000)&&(wp=='D'||wp=='G'||wp=='N'||(wp>='0'&&wp<='9'));if((wp!=VK_SPACE&&wp!=VK_F2&&wp!=VK_RETURN&&!ctrl_key)||!(lp&(1LL<<30)))p->Key(wp);return 0;}
        case WM_SYSKEYDOWN:if((wp==VK_LEFT||wp==VK_RIGHT)&&p->expanded&&!p->naming){p->Key(wp);return 0;}break;
        case WM_RBUTTONUP:{const float x=GET_X_LPARAM(lp)/p->scale,y=GET_Y_LPARAM(lp)/p->scale;if(p->popup_open){p->ClosePopup();return 0;}if(p->menu_open){p->CloseMore();return 0;}if(!p->expanded)return 0;
            for(auto it=p->hits.rbegin();it!=p->hits.rend();++it){const auto r=it->rect;if(x<r.left||x>=r.right||y<r.top||y>=r.bottom)continue;
                if(it->action==40){p->OpenGroupPopup(static_cast<uint32_t>(it->id),x-12,r.bottom+4);return 0;}
                if(it->action==41){p->BeginNaming(1,0,0);return 0;}
                if(it->action>=30&&it->action<=35&&it->id){p->RememberTarget(GetForegroundWindow());p->OpenEntryPopup(it->id,x-12,y+6,false,y-6);return 0;}
                break;}
            return 0;}
        case WM_CHAR:if(p->menu_open||p->popup_open||wp==L' ')return 0;if(wp>32&&!(GetKeyState(VK_CONTROL)&0x8000)){SetFocus(p->search);SendMessageW(p->search,m,wp,lp);}return 0;
        case WM_COMMAND:if(LOWORD(wp)==201&&HIWORD(wp)==EN_CHANGE&&p->naming){p->Invalidate();return 0;}if(LOWORD(wp)==201&&HIWORD(wp)==EN_CHANGE){wchar_t text[513]{};GetWindowTextW(p->search,text,513);p->query=text;p->scroll=0;p->Filter();}return 0;
        case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(wp),p->dark?RGB(231,239,251):RGB(40,51,70));SetBkColor(reinterpret_cast<HDC>(wp),p->dark?RGB(31,40,55):RGB(255,255,255));return reinterpret_cast<LRESULT>(p->edit_brush);
        case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE){p->CloseMore();p->ClosePopup();}p->RememberTarget(GetForegroundWindow());if(LOWORD(wp)==WA_INACTIVE&&p->expanded&&!p->pinned&&!p->drag_pending&&!p->resizing&&!p->preview.OwnsWindow(reinterpret_cast<HWND>(lp)))p->Fold();return 0;
        case WM_DPICHANGED:case WM_DISPLAYCHANGE:if(p->liquid_placing){PostMessageW(w,WM_APP+184,m==WM_DPICHANGED,0);return 0;}p->ReflowLiquid(m==WM_DPICHANGED);return 0;
        case WM_APP+184:p->ReflowLiquid(wp!=0);return 0;
        case WM_CLOSE:p->Fold();return 0;
        case WM_DESTROY:p->liquid_drag_active=p->liquid_drag_timer=false;KillTimer(w,9);KillTimer(w,5);RemoveClipboardFormatListener(w);UnregisterHotKey(w,1);KillTimer(w,1);KillTimer(w,2);KillTimer(w,3);KillTimer(w,4);return 0;
        case WM_NCDESTROY:if(w==p->window)p->window=nullptr;break;
        }}catch(...){
            if(p->liquid_drag_active){KillTimer(w,5);p->StopLiquidDrag(true);p->liquid_failed=true;p->liquid_surface.reset();p->LiquidBackdrop(false);p->compact_positioned=false;try{p->Place();}catch(...){p->Invalidate();}}
            p->status=L"操作失败，请重试";
        }
        return DefWindowProcW(w,m,wp,lp);
    }
};
ClipboardPanel::ClipboardPanel(HWND owner,std::function<void()> settings,std::function<bool()> allowed):impl_(std::make_unique<Impl>(owner,std::move(settings),std::move(allowed))){}
ClipboardPanel::~ClipboardPanel()=default;
bool ClipboardPanel::SetShortcut(UINT modifiers,UINT key){return impl_->SetShortcut(modifiers,key);}
bool ClipboardPanel::Enable(bool enabled,bool dark){return impl_->Enable(enabled,dark);}
bool ClipboardPanel::SetPersistent(bool persistent){return impl_->SetPersistent(persistent);}
void ClipboardPanel::Show(){impl_->Show();}
void ClipboardPanel::SetHotkeysSuspended(bool suspended){impl_->SetHotkeysSuspended(suspended);}
}
