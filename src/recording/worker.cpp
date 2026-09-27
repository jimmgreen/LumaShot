#include "recording/temp_cleanup.h"
#include "recording/panel.h"
#include "recording/launch_options.h"
#include "recording/preview_player.h"
#include "capture/frame.h"
#include "ui/glass_surface.h"
#include "ui/dropdown_window.h"
#include "ui/themed_message.h"
#include "ui/text_renderer.h"
#include "recording/storage.h"
#include "recording/gif_export.h"
#include "recording/mp4_export.h"
#include "recording/core.h"
#include "recording/quality.h"
#include "recording/encoder.h"
#include <d2d1.h>
#include <dwrite.h>
#include <mfplay.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <fstream>
#include <mutex>
#include <vector>
#include <optional>
#include <utility>
namespace lumashot::recording {
namespace {
constexpr UINT StatusMessage=WM_APP+21,ExportMessage=WM_APP+22,PosterMessage=WM_APP+23,PlayerMessage=WM_APP+24;
using Button=PanelButton;
constexpr UINT_PTR TabTimer=5,FeedbackTimer=6;
int DisplayFrameRate(HMONITOR monitor){MONITORINFOEXW info{};info.cbSize=sizeof(info);DEVMODEW mode{};mode.dmSize=sizeof(mode);if(GetMonitorInfoW(monitor,&info)&&EnumDisplaySettingsW(info.szDevice,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>1)return std::min(60,int(mode.dmDisplayFrequency));return 30;}
struct Ui {
    HWND window{},overlay{},video{};bool dark{},gif{},drag{},picking{},selected{},system{true},microphone{},cursor{true},software{},exporting{},saved{};
    RECT monitor{},region{};POINT anchor{};HMONITOR screen{};HWND target{};
    float scale{1};int source{},fps{30},width{0},countdown{},focus{-1},hover{-1},trim_drag{};bool acrylic{},playing{},loop{true};long long trim_begin{},trim_end{},position{};
    ui::ToolbarMotion tab_motion;bool tab_timer{},tab_effects{true};int tab_pressed{-1};
    ui::InteractionMotion<96> feedback;bool feedback_timer{};int pressed{-1},feedback_context{-1};
    int quality{1},gif_preset{1},video_width{},video_fps{30},gif_width{960},gif_fps{15};
    SIZE recorded_size{};uintmax_t recorded_bytes{};
    void ApplyGifPreset(int preset){gif_preset=std::clamp(preset,0,2);const auto config=GifQualityPreset(gif_preset);width=config.width;fps=config.fps;}
    void SetMode(bool next){
        if(next==gif)return;
        if(gif){gif_width=width;gif_fps=fps;}else{video_width=width;video_fps=fps;}
        gif=next;width=gif?gif_width:video_width;fps=gif?gif_fps:video_fps;
    }
    std::wstring SizeEstimate()const{
        if(status.state==State::Preview&&!gif&&recorded_bytes){
            return quality==2?L"保存大小 "+EstimateMegabytes(double(recorded_bytes))+L" MB":
                L"导出不大于 "+EstimateMegabytes(double(recorded_bytes))+L" MB · 压缩后可能更小";
        }
        if(!selected&&status.state==State::Ready)return L"选择区域后显示体积预估";
        const auto input=status.state==State::Preview?recorded_size:SIZE{region.right-region.left,region.bottom-region.top};
        if(input.cx<=0||input.cy<=0)return L"正在读取录制尺寸…";
        const auto output=OutputSize(input.cx,input.cy,width);
        const double seconds=status.state==State::Preview?double(std::max(0LL,trim_end-trim_begin))/10000000.:10.;
        return (status.state==State::Ready?L"每 10 秒 · ":L"")+ExportSizeEstimate(gif,output.cx,output.cy,fps,seconds,quality,system||microphone);
    }
    Status status;std::mutex mutex;Status pending;std::wstring export_error,export_stage;int progress{};
    std::unique_ptr<Session> session;std::jthread exporter,preview_loader;std::unique_ptr<PreviewImages> images,pending_images;
    std::shared_ptr<PreviewPlayerMailbox> player_mailbox;
    WPARAM preview_generation{};bool player_ready{},play_requested{},preview_failed{},pending_preview_failed{},export_as_gif{};
    bool player_warming{},seek_drag{},seek_inflight{},seek_pending{};long long seek_target{};
    std::wstring notice;std::optional<Mp4ExportResult> export_result;
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> render,selection_render;TextRenderer selection_text;std::unique_ptr<DibSurface> surface,selection_surface;ComPtr<IDWriteFactory> write;ComPtr<IMFPMediaPlayer> player;
    std::filesystem::path client_directory;int client_exit{2};
    std::filesystem::path folder,temporary,export_file;std::vector<Button> buttons;
    ~Ui(){StopTabs();StopFeedback();StopPreview();exporter.request_stop();if(exporter.joinable())exporter.join();session.reset();if(player)player->Shutdown();if(video)DestroyWindow(video);if(overlay)DestroyWindow(overlay);std::error_code e;std::filesystem::remove(temporary,e);std::filesystem::remove(folder/L"export.gif",e);std::filesystem::remove(folder,e);}
    bool Busy()const{return status.state==State::Starting||status.state==State::Recording||status.state==State::Paused||status.state==State::Finishing;}
    void StopTabs(){const bool release=tab_pressed!=-1&&window&&GetCapture()==window;tab_pressed=-1;if(release)ReleaseCapture();if(window)KillTimer(window,TabTimer);tab_timer=false;tab_motion.Reset();}
    void ReadTabEffects(){BOOL enabled=TRUE;if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0))tab_effects=enabled!=FALSE;}
    void SyncTabs(uint64_t now){
        if(!window||!IsWindowVisible(window)||status.state!=State::Ready||exporting){StopTabs();return;}
        const auto boxes=TabSlots();
        tab_motion.Update(boxes,gif?2:3,TabSlot(hover),TabSlot(tab_pressed),now,tab_effects);
        if(tab_motion.Active(now)){
            if(!tab_timer){tab_timer=SetTimer(window,TabTimer,16,nullptr)!=0;if(!tab_timer)tab_motion.Update(boxes,gif?2:3,TabSlot(hover),TabSlot(tab_pressed),now,false);}
        }else if(tab_timer){KillTimer(window,TabTimer);tab_timer=false;}
    }
    void StopFeedback(){
        const bool release=pressed!=-1&&window&&GetCapture()==window;pressed=-1;
        if(release)ReleaseCapture();if(window)KillTimer(window,FeedbackTimer);
        feedback_timer=false;feedback_context=-1;feedback.Reset();
    }
    void SyncFeedback(uint64_t now){
        if(!window||!IsWindowVisible(window)){StopFeedback();return;}
        const int context=int(status.state)*2+int(gif);
        if(context!=feedback_context){feedback.Reset();if(feedback_context!=-1)pressed=-1;feedback_context=context;}
        std::array<bool,96> selected_controls{};
        if(status.state==State::Ready&&source>=0&&source<=2)selected_controls[size_t(3+source)]=true;
        const bool ready_video=status.state==State::Ready&&!gif;
        selected_controls[7]=ready_video&&cursor;selected_controls[8]=ready_video&&system;selected_controls[9]=ready_video&&microphone;
        selected_controls[14]=status.state==State::Preview&&gif&&loop;
        feedback.Update(hover,pressed,selected_controls,now,tab_effects);
        if(feedback.Active(now)){
            if(!feedback_timer){feedback_timer=SetTimer(window,FeedbackTimer,16,nullptr)!=0;
                if(!feedback_timer)feedback.Update(hover,pressed,selected_controls,now,false);}
        }else if(feedback_timer){KillTimer(window,FeedbackTimer);feedback_timer=false;}
    }
    void Invalidate(){const auto now=GetTickCount64();SyncTabs(now);SyncFeedback(now);if(window)InvalidateRect(window,nullptr,FALSE);}
    PanelState Model(){PanelState value;value.media_size=recorded_size;value.preview_scale=scale;value.quality=quality;value.gif_preset=gif_preset;value.size_estimate=SizeEstimate();value.interaction=feedback.Sample(GetTickCount64());value.focus=focus>=0&&focus<int(buttons.size())?buttons[size_t(focus)].id:-1;value.tab_motion=tab_motion.Sample(GetTickCount64());value.state=status.state;value.gif=gif;value.dark=dark;value.cursor=cursor;value.system=system;value.microphone=microphone;value.loop=loop;value.software=software;value.exporting=exporting;value.selected=selected;value.playing=playing;value.acrylic=acrylic;value.fps=fps;value.width=width;value.source=source;value.countdown=countdown;{std::lock_guard lock(mutex);value.progress=progress;value.export_stage=export_stage;}value.hover=hover;value.time=status.time;value.trim_begin=trim_begin;value.trim_end=trim_end;value.position=position;value.detail=status.detail;if(status.state==State::Failed){if(status.detail.find(L"Hardware")!=std::wstring::npos)value.detail=L"当前设备无法使用硬件编码，请在录制设置中选择兼容编码后重试。";else if(status.detail.find(L"audio")!=std::wstring::npos||status.detail.find(L"Audio")!=std::wstring::npos)value.detail=L"无法使用所选声音设备，请检查设备连接或关闭声音后重试。";else if(status.detail.find(L"resized")!=std::wstring::npos)value.detail=L"目标窗口尺寸已变化，请重新选择窗口并录制。";else value.detail=L"无法完成采集，请重新选择窗口，或切换兼容编码后重试。";}value.images=images.get();value.notice=notice;value.preview_loading=!images&&!preview_failed;value.play_pending=play_requested&&!playing;return value;}
    void Size(int,int,bool keep_tabs=false){RECT previous{};if(keep_tabs)GetWindowRect(window,&previous);const auto dimensions=PanelSize(Model());MONITORINFO info{sizeof(info)};if(!GetMonitorInfoW(keep_tabs?MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST):screen,&info)){info.rcWork={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};}const auto work=info.rcWork;const float oldScale=scale;
        const int fitWidth=status.state==State::Ready?544:dimensions.cx,fitHeight=status.state==State::Ready?208+(notice.empty()?0:26):dimensions.cy;
        scale=std::min({GetDpiForWindow(window)/96.f,float(work.right-work.left)/fitWidth,float(work.bottom-work.top-24)/fitHeight});
        if(std::abs(oldScale-scale)>.01f){StopTabs();StopFeedback();}
        const int sw=int(dimensions.cx*scale),sh=int(dimensions.cy*scale);
        // Reserve the taller ready layout so toggling GIF/video keeps the tab row anchored.
        const int placementHeight=status.state==State::Ready?int((208+(notice.empty()?0:26))*scale):sh;
        int x=work.left+std::max(0L,(work.right-work.left-sw)/2),y=work.bottom-placementHeight-30;
        if(selected&&status.state!=State::Preview&&status.state!=State::Failed){x=std::clamp(int((region.left+region.right-sw)/2),int(work.left),std::max(int(work.left),int(work.right)-sw));y=region.bottom+12;if(y+placementHeight>work.bottom)y=std::max(int(work.top),int(region.top)-placementHeight-12);}
        if(keep_tabs&&previous.right>previous.left){
            x=std::clamp(int((previous.left+previous.right-sw)/2),int(work.left),std::max(int(work.left),int(work.right)-sw));
            y=std::clamp(int(previous.top),int(work.top),std::max(int(work.top),int(work.bottom)-placementHeight));
        }
        SetWindowPos(window,HWND_TOPMOST,x,y,sw,sh,SWP_SHOWWINDOW);LayoutVideo();
    }
    void LayoutVideo(){
        if(!video)return;
        // Modal dialogs disable their owner. Keep the separate topmost video
        // surface hidden throughout that nested message loop, including seeks.
        const bool show=status.state==State::Preview&&player_ready&&!exporting&&IsWindowEnabled(window);
        if(!show){ShowWindow(video,SW_HIDE);return;}
        RECT parent{};GetWindowRect(window,&parent);const auto size=PanelSize(Model());
        SIZE media=recorded_size;if(media.cx<=0&&images)media={images->poster.width,images->poster.height};
        if(media.cx<=0)media={size.cx-32,gif?258:318};
        const auto box=PreviewImageBounds({16,48,float(size.cx-16),gif?306.f:366.f},media,scale);
        const int left=int(std::round(box.left*scale)),top=int(std::round(box.top*scale));
        SetWindowPos(video,HWND_TOPMOST,parent.left+left,parent.top+top,int(std::round(box.right*scale))-left,int(std::round(box.bottom*scale))-top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if(player)player->UpdateVideo();
    }
    void ApplySetting(int id,int choice){
        if(id==15){if(gif)ApplyGifPreset(choice);else quality=std::clamp(choice,0,2);}
        else if(id==12){fps=choice;if(gif)gif_preset=-1;}
        else if(id==13){width=choice;if(gif)gif_preset=-1;}
        else if(choice==3)Select();else software=choice==2;
        Invalidate();
    }
    void Popup(int id){ui::Dropdown menu;menu.property=id;int current{};
        if(id==12){current=fps;for(int value:{10,15,20,25,30,60})if(!gif||value<=25)menu.items.emplace_back(std::to_wstring(value)+L" 帧/秒",value);}
        else if(id==15){current=gif?gif_preset:quality;menu.items={{L"小体积",0},{L"均衡",1},{gif?L"清晰优先":L"高画质",2}};}
        else if(id==13){current=width;menu.items.emplace_back(L"原始尺寸",0);for(int value:{640,960,1280,1920,2560,3840})menu.items.emplace_back(std::to_wstring(value)+L" px",value);}
        else{current=software?2:1;menu.items={{L"硬件编码（低占用）",1},{L"兼容编码（CPU）",2}};if(status.state==State::Ready)menu.items.emplace_back(L"重新选择区域…",3);}
        for(int i=0;i<int(menu.items.size());++i)if(menu.items[i].second==current)menu.selected=i;menu.hover=menu.selected;
        RECT r{};GetWindowRect(window,&r);Box menu_anchor{float(r.left),float(r.top),float(r.right),float(r.bottom)};for(const auto& b:buttons)if(b.id==id)menu_anchor={r.left+b.box.left*scale,r.top+b.box.top*scale,r.left+b.box.right*scale,r.top+b.box.bottom*scale};MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info);menu.Place(menu_anchor,info.rcWork,scale,id==10?220.f:156.f);
        const auto choice=ui::TrackDropdown(window,std::move(menu),dark);if(!choice)return;ApplySetting(id,*choice);
    }
    void Select(){
        if(Busy()||exporting)return;picking=true;selected=false;target=nullptr;
        POINT p{};GetCursorPos(&p);screen=MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST);MONITORINFO info{sizeof(info)};GetMonitorInfoW(screen,&info);monitor=info.rcMonitor;
        if(!overlay)overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"LumaShot.RecordingSelection",L"",WS_POPUP,monitor.left,monitor.top,monitor.right-monitor.left,monitor.bottom-monitor.top,nullptr,nullptr,GetModuleHandleW(nullptr),this);
        SetWindowRgn(overlay,nullptr,FALSE);selection_surface.reset();SetWindowLongPtrW(overlay,GWL_EXSTYLE,WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED);SetLayeredWindowAttributes(overlay,RGB(1,1,1),110,LWA_COLORKEY|LWA_ALPHA);SetWindowDisplayAffinity(overlay,WDA_EXCLUDEFROMCAPTURE);
        ShowWindow(window,SW_HIDE);SetWindowPos(overlay,HWND_TOPMOST,monitor.left,monitor.top,monitor.right-monitor.left,monitor.bottom-monitor.top,SWP_SHOWWINDOW);SetForegroundWindow(overlay);InvalidateRect(overlay,nullptr,TRUE);
    }
    void Chosen(bool keep_notice=false){
        if(region.right-region.left<8||region.bottom-region.top<8)return;if(!keep_notice)notice.clear();selected=true;picking=false;drag=false;ReleaseCapture();SetWindowLongPtrW(overlay,GWL_EXSTYLE,WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT);SetLayeredWindowAttributes(overlay,RGB(1,1,1),255,LWA_COLORKEY|LWA_ALPHA);InvalidateRect(overlay,nullptr,FALSE);Size(760,170);SetForegroundWindow(window);Invalidate();
    }
    bool InitialRegion(RECT requested){
        const auto chosen=MonitorFromRect(&requested,MONITOR_DEFAULTTONULL);MONITORINFO info{sizeof(info)};
        if(!chosen||!GetMonitorInfoW(chosen,&info))return false;
        const auto clipped=ClipRecordingRegion(requested,info.rcMonitor);if(!clipped)return false;
        screen=chosen;monitor=info.rcMonitor;region=*clipped;source=0;target=nullptr;
        overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"LumaShot.RecordingSelection",L"",WS_POPUP,monitor.left,monitor.top,monitor.right-monitor.left,monitor.bottom-monitor.top,nullptr,nullptr,GetModuleHandleW(nullptr),this);
        if(!overlay)return false;
        
        SetWindowDisplayAffinity(overlay,WDA_EXCLUDEFROMCAPTURE);ShowWindow(overlay,SW_SHOWNOACTIVATE);
        if(!EqualRect(&requested,&region))notice=L"跨屏选区已限制到当前显示器，可重新选择区域";
        Chosen(true);return true;
    }
    static BOOL CALLBACK FindTarget(HWND w,LPARAM value){auto& u=*reinterpret_cast<Ui*>(value);if(w==u.window||w==u.overlay||!IsWindowVisible(w)||GetWindow(w,GW_OWNER))return TRUE;RECT r{};GetWindowRect(w,&r);if(PtInRect(&r,u.anchor)){u.target=w;u.region=r;return FALSE;}return TRUE;}
    static LRESULT CALLBACK Selection(HWND w,UINT msg,WPARAM wp,LPARAM lp){auto* u=reinterpret_cast<Ui*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(msg==WM_NCCREATE){u=static_cast<Ui*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(u));}if(!u)return DefWindowProcW(w,msg,wp,lp);
        if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){ShowWindow(w,SW_HIDE);ShowWindow(u->window,SW_SHOW);SetForegroundWindow(u->window);return 0;}
        if(msg==WM_LBUTTONDOWN){u->anchor={u->monitor.left+GET_X_LPARAM(lp),u->monitor.top+GET_Y_LPARAM(lp)};if(u->source==1){EnumWindows(FindTarget,reinterpret_cast<LPARAM>(u));if(u->target)u->Chosen();return 0;}u->drag=true;u->region={u->anchor.x,u->anchor.y,u->anchor.x,u->anchor.y};SetCapture(w);return 0;}
        if(msg==WM_MOUSEMOVE&&u->drag){POINT end{std::clamp(u->monitor.left+GET_X_LPARAM(lp),u->monitor.left,u->monitor.right),std::clamp(u->monitor.top+GET_Y_LPARAM(lp),u->monitor.top,u->monitor.bottom)};u->region={std::min(end.x,u->anchor.x),std::min(end.y,u->anchor.y),std::max(end.x,u->anchor.x),std::max(end.y,u->anchor.y)};InvalidateRect(w,nullptr,FALSE);return 0;}
        if(msg==WM_LBUTTONUP&&u->drag){u->Chosen();return 0;}
        if(msg==WM_SIZE){u->selection_surface.reset();return 0;}if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_DPICHANGED){u->selection_surface.reset();InvalidateRect(w,nullptr,FALSE);return 0;}
        if(msg==WM_PAINT){
            PAINTSTRUCT ps{};HDC windowDc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);
            try{
                if(r.right>0&&r.bottom>0){
                    u->PaintSelection(r,GetDpiForWindow(w)/96.f);
                    BitBlt(windowDc,0,0,r.right,r.bottom,u->selection_surface->Dc(),0,0,SRCCOPY);
                }
            }catch(...){
                u->selection_render.Reset();
                ShowWindow(w,SW_HIDE);ShowWindow(u->window,SW_SHOW);
                u->notice=L"无法显示选区提示，请重新选择录制区域";u->Invalidate();
            }
            EndPaint(w,&ps);return 0;
        }
        return DefWindowProcW(w,msg,wp,lp);
    }
    void PaintSelection(RECT rect,float dpiScale){
        if(!factory)Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Selection factory");
        if(!write)Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())),"Selection fonts");
        if(!selection_render){
            const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));
            Check(factory->CreateDCRenderTarget(&properties,&selection_render),"Selection surface");
        }
        if(!selection_surface)selection_surface=std::make_unique<DibSurface>(rect.right,rect.bottom);
        Check(selection_render->BindDC(selection_surface->Dc(),&rect),"Bind selection");
        auto* targetRender=selection_render.Get();targetRender->SetDpi(96*dpiScale,96*dpiScale);
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(targetRender->CreateSolidColorBrush(D2D1::ColorF(0x010101),&brush),"Selection brush");
        ComPtr<IDWriteTextFormat> format;
        if(picking){
            Check(write->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14,L"zh-CN",&format),"Selection hint format");
            Check(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),"Selection hint wrapping");
        }
        targetRender->BeginDraw();
        try{
            targetRender->Clear(D2D1::ColorF(picking?0x121c2d:0x010101));
            if(drag||selected){
                // Capture/selection coordinates stay physical pixels. Only the
                // D2D presentation converts them to the overlay monitor's DIP.
                const auto selection=D2D1::RectF((region.left-monitor.left)/dpiScale,
                    (region.top-monitor.top)/dpiScale,(region.right-monitor.left)/dpiScale,
                    (region.bottom-monitor.top)/dpiScale);
                targetRender->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                targetRender->FillRectangle(selection,brush.Get());brush->SetColor(D2D1::ColorF(0x008cff));
                targetRender->DrawRectangle(selection,brush.Get(),3/dpiScale);
                targetRender->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            }
            if(picking){
                const wchar_t* hint=source==1?L"单击要录制的窗口 · Esc 返回":L"拖动选择录制区域 · Esc 返回";
                selection_text.Draw(targetRender,write.Get(),hint,format.Get(),
                    D2D1::RectF(32,28,rect.right/dpiScale-16,60),D2D1::ColorF(0xffffff));
            }
        }catch(...){targetRender->EndDraw();selection_render.Reset();throw;}
        const auto result=targetRender->EndDraw();
        if(result==D2DERR_RECREATE_TARGET)selection_render.Reset();
        Check(result,"Draw selection");
    }

    void Start(){
        if(!selected){Select();return;}fps=std::clamp(fps,1,gif?25:60);width=std::clamp(width,0,32768);
        if(gif){system=false;microphone=false;}countdown=3;status.state=State::Starting;SetWindowDisplayAffinity(window,WDA_EXCLUDEFROMCAPTURE);Size(0,0);SetTimer(window,1,1000,nullptr);Invalidate();
    }
    Options CaptureOptions() const {
        Options options;
        options.region=region;options.monitor=screen;options.target=target;
        options.fps=gif?25:fps;options.width=gif?0:width;options.cursor=cursor;options.quality=gif?2:quality;
        options.software=software;options.system_audio=!gif&&system;options.microphone=!gif&&microphone;
        // User-controlled duration for both modes; engine safety checks remain active.
        options.duration_limit=0;
        return options;
    }
    void BeginCapture(){
        KillTimer(window,1);const auto options=CaptureOptions();recorded_size=OutputSize(region.right-region.left,region.bottom-region.top,options.width);recorded_bytes=0;
        saved=false;session=std::make_unique<Session>();session->Start(options,temporary,[this](Status value){{std::lock_guard lock(mutex);pending=std::move(value);}PostMessageW(window,StatusMessage,0,0);});
    }
    void StopPreview(){
        ++preview_generation;
        preview_loader.request_stop();
        if(preview_loader.joinable())preview_loader.join();
        {std::lock_guard lock(mutex);pending_images.reset();pending_preview_failed=false;}
        if(player_mailbox){std::lock_guard lock(player_mailbox->mutex);player_mailbox->window=nullptr;player_mailbox->item.Reset();}
        if(player){player->Shutdown();player.Reset();}
        player_mailbox.reset();player_ready=false;player_warming=false;play_requested=false;playing=false;seek_drag=false;seek_inflight=false;seek_pending=false;
        if(window)KillTimer(window,3);
    }
    void PlaybackUnavailable(){
        player_ready=false;player_warming=false;play_requested=false;playing=false;seek_inflight=false;seek_pending=false;KillTimer(window,3);LayoutVideo();
        if(player_mailbox){std::lock_guard lock(player_mailbox->mutex);player_mailbox->window=nullptr;player_mailbox->item.Reset();}
        if(player){player->Shutdown();player.Reset();}
        status.detail=L"内置播放不可用，仍可保存文件";Invalidate();
    }
    void SeekPending(){
        if(!player||!player_ready||seek_inflight||!seek_pending)return;
        PROPVARIANT value{};value.vt=VT_I8;
        value.hVal.QuadPart=std::clamp(seek_target,0LL,std::max(0LL,status.time-1));
        seek_pending=false;seek_inflight=true;
        if(FAILED(player->SetPosition(MFP_POSITIONTYPE_100NS,&value)))PlaybackUnavailable();
    }
    void Seek(long long time){
        position=std::clamp(time,0LL,status.time);seek_target=position;seek_pending=true;
        if(player&&player_ready&&playing){player->Pause();playing=false;KillTimer(window,3);}
        SeekPending();Invalidate();
    }
    void ResumePreview(){
        if(!player||!player_ready||!play_requested||seek_drag||trim_drag||seek_pending||seek_inflight)return;
        if(FAILED(player->Play())){PlaybackUnavailable();return;}
        playing=true;SetTimer(window,3,33,nullptr);LayoutVideo();
    }
    void PausePreview(){
        play_requested=false;playing=false;KillTimer(window,3);
        if(player&&player_ready&&FAILED(player->Pause())){PlaybackUnavailable();return;}
        LayoutVideo();Invalidate();
    }
    void SeekAt(float x){Seek(static_cast<long long>(std::clamp((x-16)/(PanelSize(Model()).cx-32),0.f,1.f)*status.time));}
    void PlayerUpdate(){
        if(!player_mailbox||!player)return;
        ComPtr<IMFPMediaItem> item;HRESULT error{};bool ready{},position_set{},ended{},started{},paused{};
        {std::lock_guard lock(player_mailbox->mutex);item=std::move(player_mailbox->item);error=player_mailbox->error;
            ready=std::exchange(player_mailbox->ready,false);position_set=std::exchange(player_mailbox->position_set,false);ended=std::exchange(player_mailbox->ended,false);started=std::exchange(player_mailbox->started,false);paused=std::exchange(player_mailbox->paused,false);}
        if(SUCCEEDED(error)&&item)error=player->SetMediaItem(item.Get());
        if(FAILED(error)){PlaybackUnavailable();return;}
        if(ready){SIZE native{};if(SUCCEEDED(player->GetNativeVideoSize(&native,nullptr)))recorded_size=native;// MFPlay's stopped state does not render seek frames. Warm the renderer
            // silently, then enter paused state before publishing the live surface.
            player_warming=true;player->SetMute(TRUE);if(FAILED(player->Play())){PlaybackUnavailable();return;}}
        if(started&&player_warming&&FAILED(player->Pause())){PlaybackUnavailable();return;}
        if(paused&&player_warming){player_warming=false;player_ready=true;player->SetMute(FALSE);LayoutVideo();Seek(position);}
        if(position_set){seek_inflight=false;player->UpdateVideo();SeekPending();ResumePreview();}
        if(ended&&!player_warming&&!seek_inflight&&!seek_pending){playing=false;play_requested=false;position=gif?trim_end:status.time;KillTimer(window,3);}
        LayoutVideo();Invalidate();
    }
    void Preview(){
        std::error_code size_error;recorded_bytes=std::filesystem::file_size(temporary,size_error);if(size_error)recorded_bytes=0;
        StopPreview();preview_failed=false;status.detail.clear();notice.clear();KillTimer(window,4);
        SetWindowDisplayAffinity(window,WDA_NONE);ShowWindow(overlay,SW_HIDE);selection_surface.reset();session.reset();trim_begin=0;trim_end=status.time;position=0;images.reset();Size(0,0);LayoutVideo();
        const auto duration=status.time;const auto path=temporary;const auto generation=preview_generation;const bool thumbnails=gif;
        preview_loader=std::jthread([this,duration,path,generation,thumbnails](std::stop_token stop){
            try{
                PreviewOptions options;options.thumbnails=thumbnails;
                options.publish=[&](const PreviewImages& preview){
                    if(stop.stop_requested())return;
                    {std::lock_guard lock(mutex);pending_images=std::make_unique<PreviewImages>(preview);}
                    PostMessageW(window,PosterMessage,generation,0);
                };
                ReadPreview(path,duration,stop,options);
            }catch(...){
                if(stop.stop_requested())return;
                {std::lock_guard lock(mutex);pending_preview_failed=true;}
                PostMessageW(window,PosterMessage,generation,0);
            }
        });
        // Opening media from a URL synchronously blocks the window. Create an
        // empty player, then resolve/set its item asynchronously through MFPlay.
        player_mailbox=std::make_shared<PreviewPlayerMailbox>();player_mailbox->window=window;player_mailbox->message=PlayerMessage;player_mailbox->generation=generation;
        ComPtr<IMFPMediaPlayerCallback> callback;callback.Attach(new PreviewPlayerCallback(player_mailbox));
        auto hr=MFPCreateMediaPlayer(nullptr,FALSE,0,callback.Get(),video,&player);
        if(SUCCEEDED(hr))hr=player->CreateMediaItemFromURL(temporary.c_str(),FALSE,0,nullptr);
        if(FAILED(hr))PlaybackUnavailable();
        Invalidate();
    }
    void ShowSavedNotice(){
        notice=export_as_gif?L"GIF 已保存":L"视频已保存";
        if(!export_as_gif&&export_result){
            const auto& result=*export_result;
            if(result.encoding==Mp4Encoding::Original)notice+=L" · 未压缩，已保留原文件";
            else{
                const auto percent=result.original_bytes?int(100.*(1.-double(result.saved_bytes)/double(result.original_bytes))):0;
                notice+=(result.encoding==Mp4Encoding::Av1?L" · AV1 · 缩小 ":L" · H.264 · 缩小 ")+std::to_wstring(std::clamp(percent,0,100))+L"%";
            }
        }
        KillTimer(window,4);SetTimer(window,4,4500,nullptr);Invalidate();
    }
    void CompleteExport(){
        if(exporter.joinable())exporter.join();exporting=false;
        std::wstring error;{std::lock_guard lock(mutex);error=export_error;}
        if(error.empty()){saved=true;if(!client_directory.empty()){client_exit=0;DestroyWindow(window);return;}ShowSavedNotice();}
        else ui::ShowThemedMessage(window,dark,L"导出未完成",error.find(L"canceled")!=std::wstring::npos?L"导出已取消，原始录制仍可继续预览。":L"导出失败，请检查目标位置与剩余空间后重试。原始录制仍然保留。");
        Invalidate();
    }
    bool SavePath(bool as_gif,std::filesystem::path& path){if(!client_directory.empty()){path=client_directory/(as_gif?L"result.gif":L"result.mp4");return !std::filesystem::exists(path);}wchar_t file[32768]{};wcscpy_s(file,as_gif?L"LumaShot.gif":L"LumaShot.mp4");OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=window;dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrFilter=as_gif?L"GIF 动图\0*.gif\0\0":L"MP4 视频\0*.mp4\0\0";dialog.lpstrDefExt=as_gif?L"gif":L"mp4";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetSaveFileNameW(&dialog))return false;path=file;return true;}
    void Export(bool as_gif){
        if(exporting)return;
        GifOptions options;options.fps=std::min(25,fps);options.width=width;options.loop=loop;options.begin=gif?trim_begin:0;options.end=gif?trim_end:status.time;options.end=std::min(options.end,status.time);
        if(as_gif&&options.begin>=options.end){ui::ShowThemedMessage(window,dark,L"GIF 导出",L"结束时间必须大于开始时间。");return;}
        std::filesystem::path destination;if(!SavePath(as_gif,destination))return;if(player)player->Pause();
        notice.clear();KillTimer(window,4);export_as_gif=as_gif;PausePreview();exporting=true;LayoutVideo();progress=0;export_stage.clear();export_error.clear();export_result.reset();export_file=destination;Invalidate();
        exporter=std::jthread([this,as_gif,destination,options](std::stop_token stop){
            std::wstring error;std::optional<Mp4ExportResult> result;try{if(as_gif){ExportGifToFile(temporary,destination,options,stop,[this](int value){{std::lock_guard lock(mutex);progress=value;}PostMessageW(window,ExportMessage,0,0);});}else if(quality==2){SaveOutput(temporary,destination,stop);const auto bytes=std::filesystem::file_size(destination);result=Mp4ExportResult{Mp4Encoding::Original,bytes,bytes};}else{result=ExportMp4ToFile(temporary,destination,stop,[](int){},[this](const Mp4StageProgress& update){
                std::wstring label;
                switch(update.stage){
                case Mp4Stage::Inspect:label=L"检查媒体";break;
                case Mp4Stage::Encode:label=update.attempt<=1?L"压缩 MP4":L"重新压缩 · 第 "+std::to_wstring(update.attempt)+L" 次";break;
                case Mp4Stage::Verify:label=L"校验画质";break;
                case Mp4Stage::Save:label=L"写入文件";break;
                case Mp4Stage::Complete:label=L"导出完成";break;
                }
                {std::lock_guard lock(mutex);progress=update.percent;export_stage=std::move(label);}PostMessageW(window,ExportMessage,0,0);
            });}}
            catch(const std::exception& e){const std::string text=e.what();error.assign(text.begin(),text.end());}
            {std::lock_guard lock(mutex);export_error=error;export_result=result;progress=error.empty()?100:-1;}PostMessageW(window,ExportMessage,1,0);
        });
    }
    void Action(int id){
        if(id==90){Close();return;}if(exporting){if(id==31)exporter.request_stop();return;}
        if((status.state==State::Ready||status.state==State::Preview)&&(id==10||id==12||id==13||(id==15&&(gif||status.state==State::Ready)))){Popup(id);return;}if(id==14){loop=!loop;Invalidate();return;}
        if(status.state==State::Ready){
            if(id==40){if(!client_directory.empty())return;const auto host=FindWindowW(L"LumaShot.Host",L"LumaShot");if(host)PostMessageW(host,WM_APP+64,0,0);DestroyWindow(window);return;}
            if(id==1||id==2){SetMode(id==1);Size(0,0,true);}
            if(id>=3&&id<=5){source=id-3;if(source==2){region=monitor;target=nullptr;selected=true;if(!overlay)overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"LumaShot.RecordingSelection",L"",WS_POPUP,monitor.left,monitor.top,monitor.right-monitor.left,monitor.bottom-monitor.top,nullptr,nullptr,GetModuleHandleW(nullptr),this);SetWindowDisplayAffinity(overlay,WDA_EXCLUDEFROMCAPTURE);ShowWindow(overlay,SW_SHOWNOACTIVATE);Chosen();}else Select();}
            if(id==6)Select();if(id==7)cursor=!cursor;if(id==8)system=!system;if(id==9)microphone=!microphone;if(id==11)Start();
        }else if(status.state==State::Recording||status.state==State::Paused){if(id==20)session->Pause(status.state!=State::Paused);if(id==21){session->Stop();status.state=State::Finishing;}}
        else if(status.state==State::Preview){if(id==22&&player){if(play_requested||playing)PausePreview();else{play_requested=true;if(position>=status.time-10000||(gif&&(position<trim_begin||position>=trim_end)))Seek(gif?trim_begin:0);ResumePreview();}LayoutVideo();}if(id==23)Export(gif);if(id==24)Export(true);if(id==25){if(!saved&&!ui::ShowThemedMessage(window,dark,L"重新录制？",L"当前录制尚未保存。重新录制将丢弃这段内容。",L"放弃并重录",L"保留录制"))return;StopPreview();images.reset();notice.clear();KillTimer(window,4);status={};playing=false;Size(0,0);LayoutVideo();}}
        else if(status.state==State::Failed&&id==25){session.reset();status={};playing=false;Size(0,0);LayoutVideo();}Invalidate();
    }
    void Close(){if(exporting){exporter.request_stop();return;}if(Busy()||status.state==State::Preview&&!saved){const auto response=ui::ShowThemedMessage(window,dark,L"关闭录制？",L"当前录制尚未保存。关闭后将无法恢复这段内容。",L"放弃并关闭",L"保留录制");if(!response)return;}DestroyWindow(window);}
    void Paint(){SyncTabs(GetTickCount64());PAINTSTRUCT ps{};BeginPaint(window,&ps);RECT rect{};GetClientRect(window,&rect);try{
        if(!factory)Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Panel factory");if(!write)Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())),"Panel fonts");
        if(!render){const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));Check(factory->CreateDCRenderTarget(&properties,&render),"Panel surface");}
        if(!surface)surface=std::make_unique<DibSurface>(rect.right,rect.bottom);
        Check(render->BindDC(surface->Dc(),&rect),"Bind panel");render->SetDpi(96*scale,96*scale);render->BeginDraw();buttons=DrawPanel(render.Get(),write.Get(),Model());if(render->EndDraw()==D2DERR_RECREATE_TARGET)render.Reset();
        POINT origin{};SIZE size{rect.right,rect.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(window,nullptr,nullptr,&size,surface->Dc(),&origin,0,&blend,ULW_ALPHA);
    }catch(...){EndPaint(window,&ps);throw;}EndPaint(window,&ps);}    static LRESULT CALLBACK Video(HWND w,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(msg==WM_LBUTTONUP){auto* u=reinterpret_cast<Ui*>(GetWindowLongPtrW(GetParent(w),GWLP_USERDATA));if(u)u->Action(22);return 0;}if(msg==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(w,&ps);EndPaint(w,&ps);auto* u=reinterpret_cast<Ui*>(GetWindowLongPtrW(GetParent(w),GWLP_USERDATA));if(u&&u->player)u->player->UpdateVideo();return 0;}return DefWindowProcW(w,msg,wp,lp);}
    static LRESULT CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){auto* u=reinterpret_cast<Ui*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(msg==WM_NCCREATE){u=static_cast<Ui*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);u->window=w;u->ReadTabEffects();SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(u));}if(!u)return DefWindowProcW(w,msg,wp,lp);
        try{switch(msg){case WM_PAINT:u->Paint();return 0;case WM_ERASEBKGND:return 1;case WM_NCCALCSIZE:return 0;case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);for(const auto& b:u->buttons)if(p.x/u->scale>=b.box.left&&p.x/u->scale<=b.box.right&&p.y/u->scale>=b.box.top&&p.y/u->scale<=b.box.bottom)return HTCLIENT;if(p.y<44*u->scale&&p.x<PanelSize(u->Model()).cx*u->scale-40)return HTCAPTION;break;}
        case WM_LBUTTONDOWN:{if(u->status.state==State::Ready){
            const float x=GET_X_LPARAM(lp)/u->scale,y=GET_Y_LPARAM(lp)/u->scale;
            for(const auto& b:u->buttons)if(TabSlot(b.id)>0&&x>=b.box.left&&x<=b.box.right&&y>=b.box.top&&y<=b.box.bottom){u->tab_pressed=b.id;SetCapture(w);u->Invalidate();return 0;}
        }if(u->status.state==State::Preview&&u->gif&&GET_Y_LPARAM(lp)/u->scale>=315&&GET_Y_LPARAM(lp)/u->scale<=365&&!u->exporting){const float x=GET_X_LPARAM(lp)/u->scale;const double a=16+528.*u->trim_begin/std::max(1LL,u->status.time),b=16+528.*u->trim_end/std::max(1LL,u->status.time);u->trim_drag=std::abs(x-a)<std::abs(x-b)?1:2;SetCapture(w);return 0;}
            const float x=GET_X_LPARAM(lp)/u->scale,y=GET_Y_LPARAM(lp)/u->scale;
            for(const auto& b:u->buttons)if(x>=b.box.left&&x<=b.box.right&&y>=b.box.top&&y<=b.box.bottom){u->pressed=b.id;SetCapture(w);if(b.id==26){u->seek_drag=true;u->SeekAt(x);}u->Invalidate();return 0;}break;}
        case WM_MOUSEMOVE:{TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);const float x=GET_X_LPARAM(lp)/u->scale,y=GET_Y_LPARAM(lp)/u->scale;if(u->seek_drag){u->SeekAt(x);return 0;}if(u->trim_drag){const long long time=static_cast<long long>(std::clamp((x-16)/528.,0.,1.)*u->status.time);if(u->trim_drag==1)u->trim_begin=std::min(time,std::max(0LL,u->trim_end-1000000));else u->trim_end=std::max(time,std::min(u->status.time,u->trim_begin+1000000));u->Seek(u->trim_drag==1?u->trim_begin:u->trim_end);return 0;}int hover=-1;for(const auto& b:u->buttons)if(x>=b.box.left&&x<=b.box.right&&y>=b.box.top&&y<=b.box.bottom)hover=b.id;if(hover!=u->hover){u->hover=hover;u->Invalidate();}return 0;}
        case WM_LBUTTONUP:{if(u->seek_drag){u->SeekAt(GET_X_LPARAM(lp)/u->scale);u->seek_drag=false;u->pressed=-1;ReleaseCapture();u->ResumePreview();return 0;}if(u->pressed!=-1){u->pressed=-1;if(GetCapture()==w)ReleaseCapture();u->Invalidate();}if(u->tab_pressed!=-1){u->tab_pressed=-1;if(GetCapture()==w)ReleaseCapture();u->Invalidate();}if(u->trim_drag){u->trim_drag=0;ReleaseCapture();u->ResumePreview();return 0;}const float x=GET_X_LPARAM(lp)/u->scale,y=GET_Y_LPARAM(lp)/u->scale;for(const auto& b:u->buttons)if(x>=b.box.left&&x<=b.box.right&&y>=b.box.top&&y<=b.box.bottom){if(b.id==26){u->SeekAt(x);}else u->Action(b.id);break;}return 0;}
        case WM_MOUSELEAVE:if(u->seek_drag||u->trim_drag)return 0;u->hover=-1;if((u->tab_pressed!=-1||u->pressed!=-1)&&GetCapture()==w)ReleaseCapture();u->pressed=-1;u->tab_pressed=-1;u->Invalidate();return 0;
        case WM_CAPTURECHANGED:u->seek_drag=false;u->trim_drag=0;u->ResumePreview();u->tab_pressed=-1;u->pressed=-1;u->Invalidate();break;
        case WM_CANCELMODE:u->hover=-1;u->StopTabs();u->StopFeedback();u->Invalidate();break; // Preserve default cancellation for preview drags.
        case WM_KILLFOCUS:u->hover=-1;u->StopTabs();u->StopFeedback();u->Invalidate();break;
        case WM_SHOWWINDOW:if(!wp){u->hover=-1;u->StopTabs();u->StopFeedback();}break;
        case WM_SETTINGCHANGE:u->ReadTabEffects();u->Invalidate();break;
        case WM_KEYDOWN:if(wp==VK_ESCAPE){u->Close();return 0;}if(wp==VK_TAB){u->focus=(u->focus+1)%std::max(1,int(u->buttons.size()));u->Invalidate();return 0;}if((wp==VK_RETURN||wp==VK_SPACE)&&u->focus>=0&&u->focus<int(u->buttons.size())){u->Action(u->buttons[u->focus].id);return 0;}break;
        case WM_ENABLE:if(!wp&&u->player)u->PausePreview();else u->LayoutVideo();return 0;
        case WM_SIZE:ResizeBorderlessWindow(w,u->scale,ui::ToolPanelRadius);u->surface.reset();u->LayoutVideo();u->Invalidate();return 0;case WM_MOVE:u->LayoutVideo();return 0;
        case WM_DPICHANGED:u->StopTabs();u->StopFeedback();u->scale=HIWORD(wp)/96.f;{const auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);}u->LayoutVideo();u->Invalidate();return 0;
        case WM_TIMER:if(wp==FeedbackTimer){u->Invalidate();return 0;}if(wp==TabTimer){u->Invalidate();return 0;}if(wp==4){KillTimer(w,4);u->notice.clear();u->Invalidate();return 0;}if(wp==3&&u->player){if(!u->seek_drag&&!u->seek_inflight&&!u->seek_pending){PROPVARIANT value{};if(SUCCEEDED(u->player->GetPosition(MFP_POSITIONTYPE_100NS,&value))){u->position=value.hVal.QuadPart;PropVariantClear(&value);}if(u->gif&&u->position>=u->trim_end)u->PausePreview();}u->Invalidate();}if(wp==1){if(--u->countdown<=0)u->BeginCapture();u->Invalidate();}return 0;
        case StatusMessage:{Status next;{std::lock_guard lock(u->mutex);next=u->pending;}if(next.state==State::Preview&&u->status.state==State::Preview)return 0;u->status=std::move(next);if(u->status.state==State::Preview)u->Preview();if(u->status.state==State::Failed){SetWindowDisplayAffinity(w,WDA_NONE);ShowWindow(u->overlay,SW_HIDE);u->Size(760,310);u->LayoutVideo();}u->Invalidate();return 0;}
        case PosterMessage:{
            if(wp!=u->preview_generation||u->status.state!=State::Preview)return 0;
            {std::lock_guard lock(u->mutex);if(u->pending_images)u->images=std::move(u->pending_images);u->preview_failed=u->pending_preview_failed;}
            // No join here: GIF timeline decoding may still be in progress.
            u->Invalidate();return 0;}
        case PlayerMessage:if(wp==u->preview_generation&&u->status.state==State::Preview)u->PlayerUpdate();return 0;
        case ExportMessage:if(wp){u->CompleteExport();u->LayoutVideo();}u->Invalidate();return 0;
        case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE&&reinterpret_cast<HWND>(lp)!=u->video&&u->player){u->PausePreview();}break;case WM_CLOSE:u->Close();return 0;case WM_DESTROY:u->StopTabs();u->StopFeedback();KillTimer(w,1);KillTimer(w,3);KillTimer(w,4);PostQuitMessage(0);return 0;}
        }catch(const std::exception& e){const std::string text=e.what();u->status.state=State::Failed;u->status.detail.assign(text.begin(),text.end());u->Size(760,310);u->Invalidate();}return DefWindowProcW(w,msg,wp,lp);
    }
};
}
int RunUi(bool gif,bool dark,std::optional<RECT> initial_region={},std::filesystem::path client_directory={}){
    if(!client_directory.empty()&&(!client_directory.is_absolute()||!std::filesystem::is_directory(client_directory)||!std::filesystem::is_empty(client_directory)))throw std::invalid_argument("Recording result directory must be absolute, existing and empty");
    Ui ui;ui.client_directory=std::move(client_directory);ui.gif=gif;ui.dark=dark;ui.fps=gif?15:30;ui.width=gif?960:0;POINT cursor{};GetCursorPos(&cursor);ui.screen=initial_region?MonitorFromRect(&*initial_region,MONITOR_DEFAULTTONEAREST):MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(ui.screen,&monitor);ui.monitor=monitor.rcMonitor;ui.video_fps=DisplayFrameRate(ui.screen);ui.fps=gif?15:ui.video_fps;
    wchar_t temp[32768]{};GetTempPathW(32768,temp);GUID id{};CoCreateGuid(&id);wchar_t guid[40]{};StringFromGUID2(id,guid,40);ui.folder=ui.client_directory.empty()?std::filesystem::path(temp)/L"LumaShot-Recording"/guid:ui.client_directory/L"work";if(ui.client_directory.empty())CleanupOrphanRecordingFolders(ui.folder.parent_path());std::filesystem::create_directories(ui.folder);
    // Declared after ui so it is released before ~Ui removes the folder.
    RecordingLease lease(ui.client_directory.empty()?ui.folder:std::filesystem::path{});ui.temporary=ui.folder/L"capture.mp4";
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"LumaShot.Recording";RegisterClassW(&wc);wc.lpfnWndProc=Ui::Selection;wc.lpszClassName=L"LumaShot.RecordingSelection";wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);RegisterClassW(&wc);wc.lpfnWndProc=Ui::Video;wc.lpszClassName=L"LumaShot.RecordingVideo";wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));RegisterClassW(&wc);
    ui.window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"LumaShot.Recording",L"LumaShot 录制",WS_POPUP|WS_CLIPCHILDREN,monitor.rcWork.left+30,monitor.rcWork.top+30,760,170,nullptr,nullptr,GetModuleHandleW(nullptr),&ui);if(!ui.window)return 1;
    ConfigureBorderlessWindow(ui.window);ui.scale=GetDpiForWindow(ui.window)/96.f;ui.acrylic=SetSettingsAcrylic(ui.window,dark);SetWindowDisplayAffinity(ui.window,WDA_NONE);
    ui.video=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"LumaShot.RecordingVideo",L"",WS_POPUP,0,0,1,1,ui.window,nullptr,GetModuleHandleW(nullptr),nullptr);ui.Size(760,170);ui.LayoutVideo();SetForegroundWindow(ui.window);
    if(initial_region&&!ui.InitialRegion(*initial_region)){ui.notice=L"截图选区不可用，请重新选择录制区域";ui.Size(0,0);ui.Invalidate();}
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return ui.client_directory.empty()?0:ui.client_exit;
}
}
#ifndef LUMASHOT_RECORDING_UI_TEST
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int code=0;bool dark=false,client_mode=false;
    try{
        int count{};LPWSTR* raw=CommandLineToArgvW(GetCommandLineW(),&count);if(!raw)throw std::runtime_error("Read recording arguments");
        std::vector<std::wstring> args;for(int i=1;i<count;++i)args.emplace_back(raw[i]);LocalFree(raw);
        bool gif=false;std::optional<RECT> region;std::filesystem::path client_directory;
        for(size_t i=0;i<args.size();++i){const auto& arg=args[i];if(arg==L"--result-dir"){client_mode=true;if(++i>=args.size())throw std::invalid_argument("Missing recording result directory");client_directory=args[i];if(client_directory.empty())throw std::invalid_argument("Empty recording result directory");}else if(arg==L"--gif")gif=true;else if(arg==L"--video")gif=false;else if(arg==L"--dark")dark=true;
            else if(arg.starts_with(L"--region=")){region=lumashot::recording::ParseRecordingRegion(std::wstring_view(arg).substr(9));if(!region)throw std::invalid_argument("Invalid recording region argument");}}
        code=lumashot::recording::RunUi(gif,dark,region,client_directory);
    }catch(const std::exception& e){(void)e;if(!client_mode)lumashot::ui::ShowThemedMessage(nullptr,dark,L"无法打开录制",L"录制窗口启动失败，请检查安装文件后重试。");code=1;}CoUninitialize();return code;
}

#endif





