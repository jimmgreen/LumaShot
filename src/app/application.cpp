#include "app/hotkeys.h"
#include "app/settings_dialog.h"
#include "app/startup.h"
#include "ui/tray_menu.h"
#include "ui/selection_cursor.h"
#include "ui/themed_message.h"
#include "app/settings_process.h"
#include "pin/annotation.h"
#include "model/mark_properties.h"
#include "ocr/availability.h"
#include "app/application.h"
#include "app/translation_settings.h"
#include "model/number_label.h"
#include "app/diagnostics.h"
#include "resource.h"
#include "capture/desktop.h"
#include "export/png.h"
#include "export/clipboard.h"
#include "recording/temp_cleanup.h"
#include "pin/image.h"
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cwctype>
#include <utility>

namespace lumashot {
constexpr UINT kTray=WM_APP+1,kResult=WM_APP+2,kCancel=WM_APP+3,kElementsReady=WM_APP+4,kClientStart=WM_APP+5;
// Updater: worker notifications (WPARAM = update::Updater::Notification, or
// kUpdateCheckRequest from the settings worker), the daily-check timer and the
// timer that retries a deferred prompt once no capture/recording is active.
constexpr UINT kUpdate=WM_APP+120;constexpr WPARAM kUpdateCheckRequest=100;
constexpr UINT_PTR kUpdateTimer=23,kUpdatePromptTimer=24;
constexpr UINT kUpdateStartupDelay=90'000,kUpdateInterval=6*60*60*1000;
constexpr long long kUpdateCheckPeriod=24*60*60;
constexpr UINT kTrayCheckUpdate=40,kTrayCancelUpdate=41,kTrayInstallUpdate=42,kTrayDownloadUpdate=43;
// Folded clipboard strip: tray toggle, tray "enable clipboard", and the panel's
// notification that the strip was dragged onto its close target.
constexpr UINT kTrayClipboardStrip=11,kTrayEnableClipboard=12,kStripDismissed=WM_APP+121;
constexpr UINT kTrayTranslate=13,kTrayTranslationSettings=14;
constexpr wchar_t kTrayTip[]=L"LumaShot · 截图与标注";
static Point GlobalPoint(RECT r,LPARAM lp) {return {float(r.left+GET_X_LPARAM(lp)),float(r.top+GET_Y_LPARAM(lp))};}
static void PlaceNumberBadge(Mark& mark,Point p){const auto b=Normalize(mark.a,mark.b);const float dx=p.x-(b.left+b.right)/2,dy=p.y-(b.top+b.bottom)/2;mark.a.x+=dx;mark.b.x+=dx;mark.a.y+=dy;mark.b.y+=dy;}
static POINT NativePoint(Point p) {return {static_cast<LONG>(p.x),static_cast<LONG>(p.y)};}
static Box BoxOf(RECT r) {return {float(r.left),float(r.top),float(r.right),float(r.bottom)};}
static bool HasArea(Box b) {return b.right-b.left>=1 && b.bottom-b.top>=1;}
// A foreground application that is inside a menu modal loop (context menus in
// CAD tools, Explorer, browsers) or that simply owns the foreground makes plain
// SetForegroundWindow fail for this tray process. The overlay then never has
// keyboard focus, so Esc, Enter and shortcuts go to the covered program. Share
// that thread's input queue for the single activation call so the overlay can
// take focus without closing the menu it is about to capture.
static void ActivateOverlay(HWND window) {
    const double started=Diagnostics::Now();
    if(SetForegroundWindow(window)&&GetForegroundWindow()==window){SetFocus(window);Diagnostics::Get().Add("activate_direct",started,1);return;}
    Diagnostics::Get().Add("activate_direct",started,0);
    const HWND foreground=GetForegroundWindow();const DWORD current=GetCurrentThreadId();
    const DWORD owner=foreground?GetWindowThreadProcessId(foreground,nullptr):0;
    const bool attached=owner&&owner!=current&&AttachThreadInput(current,owner,TRUE);
    SetForegroundWindow(window);SetFocus(window);
    if(attached)AttachThreadInput(current,owner,FALSE);
    if(GetForegroundWindow()==window)return;
    // Last resort: bring the overlay above the foreground window even if the
    // shell refused activation, so the escape hatch stays visible and clickable.
    SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
static std::wstring ErrorText(const std::exception& error) {
    const std::string text=error.what();
    const int count=MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,nullptr,0);
    std::wstring wide(static_cast<size_t>(std::max(1,count)),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,wide.data(),count);
    if(!wide.empty())wide.pop_back();return wide;
}


Application::~Application() {
    if(update_version_dirty_)settings_writer_.Request(preferences_); // flushed by the writer's destructor
    if(ipc_client_){ipc_client_->Complete(1);ipc_client_.reset();}
    StopToolbarMotion();
    clipboard_panel_.reset();
    FlushProperties();
    element_scanner_.Cancel();
    magnifier_.Close();
    pins_.reset();
    worker_.request_stop();if(worker_.joinable())worker_.join();
    updater_.reset(); // cancels an in-flight check/download and joins promptly
    if(main_) {NOTIFYICONDATAW tray{sizeof(tray)};tray.hWnd=main_;tray.uID=1;Shell_NotifyIconW(NIM_DELETE,&tray);}
    if(tray_icon_)DestroyIcon(tray_icon_);
    CommitText(true);
    StopToolbarMotion(); // Text-editor teardown can invalidate once more.
    for(auto& view:views_)if(IsWindow(view->window))DestroyWindow(view->window);
}
HWND Application::Owner() const {
    if(active_&&!views_.empty())return views_.front()->window;
    return main_;
}
void Application::Notice(const std::wstring& text) {ui::ShowThemedMessage(Owner(),preferences_.Dark(),L"LumaShot",text);}
int Application::RunForClient(const std::filesystem::path& output,bool demo) {
    if(!output.is_absolute()||output.extension()!=L".png"||!std::filesystem::is_directory(output.parent_path())||std::filesystem::exists(output))
        throw std::runtime_error("Invalid or existing capture output path");
    client_output_=output;
    return Run(true,demo,true);
}
int Application::Run(bool capture_now,bool demo,bool diagnostic_session) {
    demo_=demo;ocr_available_=client_output_.empty()&&ocr::Available();
    diagnostic_session_=diagnostic_session;
    preferences_=demo||(diagnostic_session&&client_output_.empty())?Preferences{}:Preferences::Load();
    if(Diagnostics::Get().Enabled()) {
        auto& trace=Diagnostics::Get();
        trace.Add("mode_demo",Diagnostics::Now(),demo);trace.Add("mode_isolated",Diagnostics::Now(),diagnostic_session);
        trace.Add("include_cursor",Diagnostics::Now(),preferences_.include_cursor);trace.Add("theme",Diagnostics::Now(),preferences_.theme);
        trace.Add("process_priority",Diagnostics::Now(),GetPriorityClass(GetCurrentProcess()));
        DWORD_PTR process_mask{},system_mask{};GetProcessAffinityMask(GetCurrentProcess(),&process_mask,&system_mask);
        trace.Add("process_affinity",Diagnostics::Now(),static_cast<long long>(process_mask));
        DEVMODEW display{};display.dmSize=sizeof(display);if(EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&display))trace.Add("refresh_hz",Diagnostics::Now(),display.dmDisplayFrequency);
    }
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    if(FAILED(LoadIconMetric(instance,MAKEINTRESOURCEW(IDI_LUMASHOT),LIM_SMALL,&tray_icon_)))
        throw std::runtime_error("Unable to load tray icon");
    WNDCLASSW main_class{};main_class.lpfnWndProc=MainProc;main_class.hInstance=instance;main_class.lpszClassName=ipc_demo_?L"LumaShot.CaptureIPC.Test":client_output_.empty()?L"LumaShot.Host":L"LumaShot.CaptureClient";
    main_class.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_LUMASHOT));
    CheckWin32(RegisterClassW(&main_class)!=0,"Register host window");
    WNDCLASSW overlay_class{};overlay_class.style=CS_DBLCLKS;overlay_class.lpfnWndProc=OverlayProc;
    overlay_class.hInstance=instance;overlay_class.lpszClassName=L"LumaShot.Overlay";overlay_class.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    overlay_class.hIcon=main_class.hIcon;
    CheckWin32(RegisterClassW(&overlay_class)!=0,"Register capture window");
    main_=CreateWindowExW(WS_EX_TOOLWINDOW,main_class.lpszClassName,L"LumaShot",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,this);
    CheckWin32(main_!=nullptr,"Create host window");
    clipboard_panel_=std::make_unique<ClipboardPanel>(main_,[this]{PostMessageW(main_,WM_APP+85,0,0);},[this]{if(SettingsShortcutRecording())return false;RefreshHotkeys();return !hotkeys_suspended_;});
    clipboard_panel_->SetStripDismissedHandler([this]{PostMessageW(main_,kStripDismissed,0,0);});
    pins_=std::make_unique<PinManager>(main_);
    pins_->sticker_style=[this]{return preferences_.pin_style;};
    pins_->dark_theme=[this]{return preferences_.Dark();};
    pins_->clipboard_format=[this]{return preferences_.paste_as_file?preferences_.paste_file_format:-1;};
    pins_->open_translation_settings=[this]{if(!LaunchTranslationSettings(preferences_.Dark()))Notice(L"无法打开翻译设置。");};
    pins_->annotate=[this](uint64_t id,std::shared_ptr<const Frame> image,Document document,RECT bounds){return AnnotatePin(id,std::move(image),std::move(document),bounds);};
    {
        longshot::Host host;
        host.dark=[this]{return preferences_.Dark();};
        host.clipboard_format=[this]{return preferences_.paste_as_file?preferences_.paste_file_format:-1;};
        host.save_directory=[this]{return preferences_.save_directory;};
        host.remember_directory=[this](const std::filesystem::path& folder){
            preferences_.save_directory=folder;
            if(!demo_&&!diagnostic_session_){settings_writer_.Request(preferences_);settings_writer_.Flush();}
        };
        host.pin=[this](Frame image,Frame ocr,POINT position,bool recognize){
            // An empty OCR image means "same pixels"; the pin shares them.
            pins_->Create(std::move(image),std::move(ocr),position,std::nullopt,{},recognize&&ocr_available_);
        };
        host.annotate=[this](std::shared_ptr<const Frame> image,Document marks,RECT bounds,longshot::Host::AnnotateDone done){
            try{return AnnotateImage(std::move(image),std::move(marks),bounds,std::move(done));}catch(const std::exception&){return false;}
        };
        host.ocr_available=[this]{return ocr_available_;};
        host.recapture=[this]{PostMessageW(main_,LaunchCommandMessage,1,0);};
        longshot_=std::make_unique<LongCaptureManager>(std::move(host));
    }
    taskbar_created_=RegisterWindowMessageW(L"TaskbarCreated");
    NOTIFYICONDATAW tray{sizeof(tray)};tray.hWnd=main_;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
    tray.uCallbackMessage=kTray;tray.hIcon=tray_icon_;wcscpy_s(tray.szTip,kTrayTip);
    if(!demo_&&!diagnostic_session_)CheckWin32(Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE,"Add tray icon");
    clipboard_panel_->SetShortcut(preferences_.clipboard_modifiers,preferences_.clipboard_key);
    clipboard_panel_->SetStripVisible(preferences_.clipboard_strip_visible);
    if(!demo_&&!diagnostic_session_)ConfigureHotkeyPolicy();
    if(!demo_&&!diagnostic_session_)clipboard_panel_->SetPersistent(preferences_.clipboard_persist);
    if(!demo_&&!diagnostic_session_&&!clipboard_panel_->Enable(preferences_.clipboard_enabled,preferences_.Dark()))Notice(L"剪贴板监听无法启动，请重新开启。");
    if(!demo_&&!diagnostic_session_){CleanupClipboardFiles(std::chrono::hours(24),CurrentClipboardFile(main_));recording::CleanupOrphanRecordingFolders(recording::RecordingTempRoot());}
    if(!demo_&&!diagnostic_session_){if(!ConfigureLoginStartup(preferences_.start_with_windows))Notice(L"无法更新开机自启动设置，请检查系统权限。");}
    if(!demo_&&!diagnostic_session_&&!pins_->EnableSession(PinSessionStore::DefaultDirectory()))Notice(L"部分贴图未能恢复。原始会话缓存保留在本机，可检查磁盘空间和权限。");
    if(!demo_&&!diagnostic_session_)StartUpdater();
    if(capture_now||(demo_&&!ipc_demo_))PostMessageW(main_,LaunchCommandMessage,1,0);
    MSG msg{};
    for(;;) {
        // Drain queued input without entering a second blocking message wait.
        // QS_ALLINPUT can wake for sent/internal messages with nothing to retrieve.
        // PeekMessage also dispatches sent messages before we collect dirty views.
        if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT)break;
            TranslateMessage(&msg);DispatchMessageW(&msg);continue;
        }
        WaitForWork();
    }
    KillTimer(main_,20);for(int id=1;id<=3;++id)UnregisterHotKey(main_,id);return client_output_.empty()?0:client_exit_;
}

// Both the main loop and the settings child wait must service swap-chain frame
// readiness. Dispatching only Windows messages can stall selection/annotation.
DWORD Application::WaitForWork(std::span<const HANDLE> extra){
    std::vector<HANDLE> events(extra.begin(),extra.end());std::vector<View*> waiting;
    const size_t caller_index=events.size();if(ipc_client_)events.push_back(ipc_client_->caller);
    const size_t frame_begin=events.size();
    for(auto& view:views_)if(view->needs_paint&&view->renderer->FrameEvent()){
        if(events.size()>=MAXIMUM_WAIT_OBJECTS-1)break;
        events.push_back(view->renderer->FrameEvent());waiting.push_back(view.get());
    }
    const DWORD ready=MsgWaitForMultipleObjectsEx(static_cast<DWORD>(events.size()),events.empty()?nullptr:events.data(),INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    if(ready==WAIT_FAILED)throw std::runtime_error("Unable to wait for capture display");
    if(ipc_client_&&ready==WAIT_OBJECT_0+caller_index){Cancel(false);return WAIT_OBJECT_0+static_cast<DWORD>(extra.size());}
    if(ready>=WAIT_OBJECT_0+frame_begin&&ready<WAIT_OBJECT_0+events.size()){
        auto* view=waiting[ready-WAIT_OBJECT_0-frame_begin];view->renderer->FrameReady();view->needs_paint=false;
        InvalidateRect(view->window,nullptr,FALSE);
    }
    return ready<WAIT_OBJECT_0+extra.size()?ready:WAIT_OBJECT_0+static_cast<DWORD>(extra.size());
}

LRESULT CALLBACK Application::MainProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,wp,lp);
    const bool client_message=static_cast<bool>(app->ipc_client_);
    try {
        if(message==CaptureVersionMessage())return 1;
        if(message==WM_COPYDATA){
            const auto* data=reinterpret_cast<const COPYDATASTRUCT*>(lp);
            if(!data)return 0;
            const auto text=CapturePayload(data);
            if(data->dwData==CaptureCancelTag){if(app->ipc_client_&&app->ipc_client_->token==text){app->Cancel(false);return 1;}return 0;}
            if(data->dwData!=CaptureRequestTag)return 0;
            if(app->active_||app->pending_||app->ipc_client_||!app->client_output_.empty()||app->settings_open_||app->recording_process_.Active())return 2;
            auto client=CaptureIpcSession::Open(text);if(!client)return 0;
            app->client_output_=client->output;app->client_exit_=2;app->ipc_client_=std::move(client);
            if(!PostMessageW(window,kClientStart,0,0)){app->client_exit_=1;app->Cancel(false);}
            return 1;
        }
        if(message==kClientStart){if(app->ipc_client_){app->client_exit_=1;app->Start();if(app->ipc_client_)app->client_exit_=2;}return 0;}
        if(message==kPinSessionError){NOTIFYICONDATAW info{sizeof(info)};info.hWnd=window;info.uID=1;info.uFlags=NIF_INFO;info.dwInfoFlags=NIIF_WARNING;wcscpy_s(info.szInfoTitle,L"贴图会话未能保存");wcscpy_s(info.szInfo,L"请检查磁盘空间和权限。当前贴图仍可使用，但重启后可能无法恢复最新状态。");Shell_NotifyIconW(NIM_MODIFY,&info);return 0;}
        if(message==WM_APP+85){if(!app->ipc_client_)app->Settings();return 0;}
        if(message==WM_APP+64){app->recording_finish_wait_=15;SetTimer(window,19,200,nullptr);return 0;}
        if(message==WM_TIMER&&wp==19){if(!app->recording_process_.Active()){KillTimer(window,19);app->Start();}else if(--app->recording_finish_wait_<=0)KillTimer(window,19);return 0;}
        if(message==WM_TIMER&&wp==20){app->RefreshHotkeys();return 0;}
        if(message==kUpdate){app->UpdateNotification(wp);return 0;}
        if(message==kStripDismissed){app->ClipboardStripDismissed();return 0;}
        if(message==WM_TIMER&&wp==kUpdateTimer){SetTimer(window,kUpdateTimer,kUpdateInterval,nullptr);app->UpdateTimer();return 0;}
        if(message==WM_TIMER&&wp==kUpdatePromptTimer){if(!app->UpdateBusy()){KillTimer(window,kUpdatePromptTimer);app->PromptUpdate();}return 0;}
        if(message==WM_HOTKEY||message==LaunchCommandMessage){
            if(app->ipc_client_)return 0;
            if(message==WM_HOTKEY&&app->hotkeys_initialized_)app->RefreshHotkeys();
            if(!AllowLaunch(message,app->hotkeys_suspended_||app->preferences_.hotkeys_disabled))return 0;
            if(SettingsShortcutRecording())return 0;
            if((wp==2||wp==3)&&app->recording_process_.Active()){app->recording_process_.Show();return 0;}
            if(wp==2||wp==3){if(!app->active_&&!app->pending_&&!app->recording_process_.Start(wp==2,app->preferences_.Dark()))app->Notice(L"无法启动录制，请检查录制组件是否完整。");return 0;}
            if(wp==4){if(!app->ocr_available_){app->Notice(L"截图翻译需要文字识别组件。");return 0;}if(!app->active_&&!app->pending_){app->translate_request_=true;app->Start();app->translate_request_=false;}return 0;}
            if(wp==1)app->Start();return 0;}
        if(message==kOcrReady){app->pins_->Ready();return 0;}
        if(message==kTranslateReady){app->pins_->TranslationReady(uint64_t(wp));return 0;}
        if(message==kTranslationConfigChanged){app->pins_->TranslationConfigChanged();return 0;}
        if(message==kPinSaveReady||(message==WM_TIMER&&wp==kPinSaveReady)){app->pins_->Saved();return 0;}
        if(message==kResult){app->ResultReady();return 0;}
        if(message==kElementsReady){app->ElementsReady();return 0;}
        if(message==WM_TIMER&&wp==10){app->DeferProperties();return 0;}
        if(message==WM_TIMER&&wp==9){app->AnimateToolbar();return 0;}
        if(message==WM_TIMER&&wp==22){app->TickToolbarMotion();return 0;}
        if(message==WM_SETTINGCHANGE){BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);app->toolbar_effects_=enabled!=FALSE;app->Invalidate();}
        if(message==kCancel){app->Cancel();return 0;}
        if(message==kTray) {
            if(LOWORD(lp)==WM_RBUTTONUP)app->TrayMenu();
            if(LOWORD(lp)==WM_LBUTTONUP)app->Start();return 0;
        }
        if(message==WM_QUERYENDSESSION){if(app->pins_)app->pins_->FlushSession();return TRUE;}
        if(message==WM_ENDSESSION){if(wp){if(app->pins_)app->pins_->PreserveSession();PostQuitMessage(0);}return 0;}
        if(message==WM_CLOSE){if(app->pins_)app->pins_->PreserveSession();app->Cancel();DestroyWindow(window);return 0;}
        if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
        if(app->taskbar_created_ && message==app->taskbar_created_) {
            NOTIFYICONDATAW tray{sizeof(tray)};tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
            tray.uCallbackMessage=kTray;tray.hIcon=app->tray_icon_;wcscpy_s(tray.szTip,kTrayTip);
            Shell_NotifyIconW(NIM_ADD,&tray);return 0;
        }
    } catch(const std::exception& e){if(app->ipc_client_){app->client_exit_=1;app->Cancel(false);}else if(!client_message)app->Notice(ErrorText(e));}
    return DefWindowProcW(window,message,wp,lp);
}

void Application::TrayMenu() {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"开始截图");
    AppendMenuW(menu,MF_STRING,2,L"3 秒后截图");
    if(ocr_available_)AppendMenuW(menu,MF_STRING,kTrayTranslate,(preferences_.translate_key?L"截图翻译（"+ShortcutLabel(preferences_.translate_modifiers,preferences_.translate_key)+L"）":std::wstring(L"截图翻译")).c_str());
    AppendMenuW(menu,MF_STRING,6,L"录制 GIF…");AppendMenuW(menu,MF_STRING,7,L"录制屏幕…");
    AppendMenuW(menu,MF_STRING|(preferences_.include_cursor?MF_CHECKED:0),3,L"包含鼠标指针");
    if(preferences_.clipboard_enabled){
        AppendMenuW(menu,MF_STRING,8,(preferences_.clipboard_key?L"剪贴板（"+ShortcutLabel(preferences_.clipboard_modifiers,preferences_.clipboard_key)+L"）":L"剪贴板").c_str());
        AppendMenuW(menu,MF_STRING|(preferences_.clipboard_strip_visible?MF_CHECKED:0),kTrayClipboardStrip,L"显示剪贴板侧边条");
    }else AppendMenuW(menu,MF_STRING,kTrayEnableClipboard,L"开启剪贴板");
    AppendHotkeyPolicyMenu(menu,preferences_);
    if(updater_){
        const auto phase=updater_->phase();
        if(phase==update::Updater::Phase::Downloading){
            const auto [received,total]=updater_->DownloadProgress();
            AppendMenuW(menu,MF_STRING,kTrayCancelUpdate,(L"取消下载更新（"+std::to_wstring(total?received*100/total:0)+L"%）").c_str());
        }
        else if(phase!=update::Updater::Phase::Idle)AppendMenuW(menu,MF_STRING|MF_GRAYED,kTrayCheckUpdate,L"正在检查更新…");
        else if(update_manifest_&&!update_ready_.empty())AppendMenuW(menu,MF_STRING,kTrayInstallUpdate,(L"安装更新 "+update::VersionText(update_manifest_->version)).c_str());
        else if(update_manifest_)AppendMenuW(menu,MF_STRING,kTrayDownloadUpdate,(L"下载更新 "+update::VersionText(update_manifest_->version)+L"…").c_str());
        else AppendMenuW(menu,MF_STRING,kTrayCheckUpdate,L"检查更新…");
    }
    if(ocr_available_)AppendMenuW(menu,MF_STRING,kTrayTranslationSettings,L"翻译引擎设置…");
    AppendMenuW(menu,MF_STRING,4,L"设置…");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,5,L"退出");
    POINT p{};GetCursorPos(&p);SetForegroundWindow(main_);
    const UINT choice=ui::TrackTrayMenu(main_,menu,p,preferences_.Dark());
    DestroyMenu(menu);PostMessageW(main_,WM_NULL,0,0);
    if(choice==1)PostMessageW(main_,LaunchCommandMessage,1,0);
    if(choice==6||choice==7)PostMessageW(main_,LaunchCommandMessage,choice==6?2:3,0);
    if(choice==2)SetTimer(main_,8,3000,[](HWND w,UINT,UINT_PTR id,DWORD){KillTimer(w,id);PostMessageW(w,LaunchCommandMessage,1,0);});
    if(choice==3){preferences_.include_cursor=!preferences_.include_cursor;settings_writer_.Request(preferences_);settings_writer_.Flush();}
    if(choice==8&&clipboard_panel_)clipboard_panel_->Show();
    if(choice==kTrayTranslate)PostMessageW(main_,LaunchCommandMessage,4,0);
    if(choice==kTrayTranslationSettings&&!LaunchTranslationSettings(preferences_.Dark()))Notice(L"无法打开翻译设置。");
    if(choice==kTrayClipboardStrip)SetClipboardStripVisible(!preferences_.clipboard_strip_visible);
    if(choice==kTrayEnableClipboard)EnableClipboardFromTray();
    if(choice==GameHotkeyMenu||choice==DisableHotkeyMenu)ToggleHotkeyPolicy(choice);
    if(choice==4)Settings();
    if(choice==kTrayCheckUpdate)CheckForUpdates(true);
    if(choice==kTrayCancelUpdate&&updater_)updater_->Cancel();
    if(choice==kTrayInstallUpdate)InstallUpdate();
    if(choice==kTrayDownloadUpdate)StartUpdateDownload();
    if(choice==5){
        // Exiting closes the worker job and ends the recording, so ask first.
        if(recording_process_.Active()&&!ui::ShowThemedMessage(Owner(),preferences_.Dark(),L"正在录制",L"录制窗口仍在运行。退出 LumaShot 会立即结束录制，未导出的内容将丢失。",L"仍然退出",L"返回录制")){recording_process_.Show();return;}
        PostMessageW(main_,WM_CLOSE,0,0);
    }
}
namespace {
std::wstring MegabytesText(std::uint64_t bytes){wchar_t text[32]{};swprintf_s(text,L"%.1f",double(bytes)/1048576.0);return text;}
std::wstring UpdateHighlights(const std::wstring& notes){
    // First non-empty line of the signed notes, shortened to fit the prompt.
    size_t begin=0;
    while(begin<notes.size()){
        const auto end=notes.find(L'\n',begin);
        auto line=notes.substr(begin,end==std::wstring::npos?std::wstring::npos:end-begin);
        while(!line.empty()&&iswspace(line.back()))line.pop_back();
        if(!line.empty())return line.size()>34?line.substr(0,33)+L"…":line;
        if(end==std::wstring::npos)break;
        begin=end+1;
    }
    return {};
}
}
void Application::StartUpdater(){
    updater_=std::make_unique<update::Updater>(main_,kUpdate);
    const auto current=update::VersionText(update::CurrentVersion());
    if(preferences_.last_run_version!=current){
        const auto previous=preferences_.last_run_version;
        // Persisted on the first update tick so cold start stays free of disk I/O.
        preferences_.last_run_version=current;update_version_dirty_=true;
        if(!previous.empty()){
            NOTIFYICONDATAW info{sizeof(info)};info.hWnd=main_;info.uID=1;info.uFlags=NIF_INFO;info.dwInfoFlags=NIIF_INFO;
            wcscpy_s(info.szInfoTitle,(L"LumaShot 已更新到 "+current).c_str());
            wcscpy_s(info.szInfo,L"新版本已在后台运行。可在托盘菜单中随时检查更新。");
            Shell_NotifyIconW(NIM_MODIFY,&info);
        }
    }
    SetTimer(main_,kUpdateTimer,kUpdateStartupDelay,nullptr);
}
update::Endpoints Application::UpdateEndpoints() const{
    std::vector<std::string> remembered;
    size_t begin=0;const auto& text=preferences_.update_mirrors;
    while(begin<text.size()){
        const auto end=std::min(text.find(L'|',begin),text.size());
        std::string mirror;bool ascii=true;
        for(size_t i=begin;i<end;++i){if(text[i]>0x7e){ascii=false;break;}mirror.push_back(static_cast<char>(text[i]));}
        if(ascii&&update::ValidMirror(mirror))remembered.push_back(std::move(mirror));
        begin=end+1;
    }
    return update::ReleaseEndpoints(remembered);
}
bool Application::UpdateBusy() const{
    return active_||pending_||settings_open_||ipc_client_||recording_process_.Active()||(longshot_&&longshot_->Active());
}
void Application::SetClipboardStripVisible(bool visible){
    preferences_.clipboard_strip_visible=visible;
    if(clipboard_panel_)clipboard_panel_->SetStripVisible(visible);
    if(!demo_&&!diagnostic_session_){settings_writer_.Request(preferences_);settings_writer_.Flush();}
}
void Application::ClipboardStripDismissed(){
    // The panel has already hidden the strip; persist that and explain the way back once.
    const bool first=!preferences_.clipboard_strip_hint_shown;
    preferences_.clipboard_strip_hint_shown=true;
    SetClipboardStripVisible(false);
    if(!first||demo_||diagnostic_session_)return;
    NOTIFYICONDATAW info{sizeof(info)};info.hWnd=main_;info.uID=1;info.uFlags=NIF_INFO;info.dwInfoFlags=NIIF_INFO|NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(info.szInfoTitle,L"剪贴板侧边条已隐藏");
    const std::wstring shortcut=preferences_.clipboard_key&&!preferences_.hotkeys_disabled?L"按 "+ShortcutLabel(preferences_.clipboard_modifiers,preferences_.clipboard_key)+L" 仍可打开剪贴板。":L"";
    wcsncpy_s(info.szInfo,(L"剪贴板仍在记录。"+shortcut+L"可在托盘菜单或设置中重新显示侧边条。").c_str(),_TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY,&info);
}
void Application::EnableClipboardFromTray(){
    if(!clipboard_panel_||preferences_.clipboard_enabled)return;
    preferences_.clipboard_strip_visible=true;clipboard_panel_->SetStripVisible(true);
    if(!clipboard_panel_->Enable(true,preferences_.Dark())){Notice(L"剪贴板监听无法启动，请重新开启。");return;}
    preferences_.clipboard_enabled=true;
    if(!demo_&&!diagnostic_session_){settings_writer_.Request(preferences_);settings_writer_.Flush();}
}
void Application::SetTrayTip(const std::wstring& text){
    NOTIFYICONDATAW tray{sizeof(tray)};tray.hWnd=main_;tray.uID=1;tray.uFlags=NIF_TIP;
    wcsncpy_s(tray.szTip,text.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void Application::UpdateTimer(){
    if(std::exchange(update_version_dirty_,false))settings_writer_.Request(preferences_);
    if(!updater_||!preferences_.update_auto_check||update_prompting_)return;
    const long long now=static_cast<long long>(std::time(nullptr));
    // A clock moved backwards also re-checks rather than waiting indefinitely.
    if(preferences_.update_last_check>0&&now>=preferences_.update_last_check&&now-preferences_.update_last_check<kUpdateCheckPeriod)return;
    CheckForUpdates(false);
}
void Application::CheckForUpdates(bool manual){
    if(!updater_)return;
    if(manual)update_manual_=true;
    const auto phase=updater_->phase();
    if(phase==update::Updater::Phase::Checking)return; // the running check reports to a manual request too
    if(phase!=update::Updater::Phase::Idle){
        if(manual&&phase==update::Updater::Phase::Downloading)Notice(L"正在下载更新，完成后会提示安装。");
        update_manual_=false;return;
    }
    if(manual&&update_manifest_&&!update_ready_.empty()){update_manual_=false;PromptInstall();return;}
    if(!updater_->Check(UpdateEndpoints()))update_manual_=false;
}
void Application::UpdateNotification(WPARAM code){
    if(code==kUpdateCheckRequest){CheckForUpdates(true);return;}
    if(!updater_)return;
    using Notification=update::Updater::Notification;
    switch(static_cast<Notification>(code)){
    case Notification::CheckDone:{
        auto result=updater_->TakeCheck();
        const bool manual=std::exchange(update_manual_,false);
        if(result.manifest){
            // Only a verified manifest counts as a completed daily check.
            preferences_.update_last_check=static_cast<long long>(std::time(nullptr));
            if(!result.manifest->mirrors.empty()){
                std::wstring joined;
                for(const auto& mirror:result.manifest->mirrors){if(!joined.empty())joined+=L'|';joined.append(mirror.begin(),mirror.end());}
                preferences_.update_mirrors=joined;
            }
            settings_writer_.Request(preferences_);
        }
        if(result.kind==update::CheckResult::Kind::Available){
            const bool same=update_manifest_&&update_manifest_->version==result.manifest->version;
            update_manifest_=result.manifest;update_sources_=std::move(result.sources);
            if(!same)update_ready_.clear();
            if(update_prompting_){} // an open prompt already covers it
            else if(manual||!UpdateBusy())PromptUpdate();
            else SetTimer(main_,kUpdatePromptTimer,30'000,nullptr);
        }
        else if(result.kind==update::CheckResult::Kind::UpToDate){
            update_manifest_.reset();update_ready_.clear();
            if(manual)Notice(L"LumaShot 已是最新版本（"+update::VersionText(update::CurrentVersion())+L"）。");
        }
        else if(result.kind==update::CheckResult::Kind::Failed&&manual){
            if(result.rejected)Notice(L"收到的更新信息未通过签名校验，已忽略。请稍后重试。");
            else Notice(L"无法连接更新服务器（已尝试 "+std::to_wstring(result.attempted)+L" 个下载源）。请检查网络或代理设置后重试。");
        }
        break;}
    case Notification::Progress:{
        if(updater_->phase()!=update::Updater::Phase::Downloading)break;
        const auto [received,total]=updater_->DownloadProgress();
        SetTrayTip(L"LumaShot · 正在下载更新 "+std::to_wstring(total?received*100/total:0)+L"%");
        break;}
    case Notification::DownloadDone:{
        auto result=updater_->TakeDownload();
        SetTrayTip(kTrayTip);
        if(result.kind==update::DownloadResult::Kind::Ok){update_ready_=result.file;PromptInstall();}
        else if(result.kind==update::DownloadResult::Kind::Disk)Notice(L"无法保存更新文件，请检查磁盘空间和权限后重试。");
        else if(result.kind==update::DownloadResult::Kind::Failed)
            Notice(result.corrupt?L"下载的安装包未通过完整性校验，已丢弃。请稍后在托盘菜单中重试。":L"更新下载失败：所有下载源都不可用。请稍后在托盘菜单中重试。");
        break;}
    case Notification::VerifyDone:{
        if(!updater_->TakeVerify()){update_ready_.clear();Notice(L"更新文件校验失败，已放弃安装。请重新检查更新。");break;}
        if(recording_process_.Active()){Notice(L"请先结束录制，再安装更新。");break;}
        // The installer closes LumaShot through WM_CLOSE (pins are preserved) and restarts it.
        if(pins_)pins_->FlushSession();
        if(!update::LaunchInstaller(update_ready_))Notice(L"无法启动更新安装程序。可以从 GitHub Releases 下载安装包手动更新。");
        break;}
    }
}
void Application::PromptUpdate(){
    if(!update_manifest_||!updater_||updater_->phase()!=update::Updater::Phase::Idle||update_prompting_)return;
    const auto manifest=*update_manifest_;
    if(!update_ready_.empty()){PromptInstall();return;}
    const auto highlights=UpdateHighlights(manifest.notes);
    const auto body=L"LumaShot "+update::VersionText(manifest.version)+L" 已发布（当前 "+update::VersionText(update::CurrentVersion())+L"）。\n"
        +(highlights.empty()?std::wstring{}:highlights+L"\n")+L"下载约 "+MegabytesText(manifest.size)+L" MB，校验通过后安装并自动重启 LumaShot。";
    update_prompting_=true;
    const bool accepted=ui::ShowThemedMessage(Owner(),preferences_.Dark(),L"发现新版本",body,L"下载并安装",L"稍后");
    update_prompting_=false;
    if(accepted)StartUpdateDownload();
}
void Application::StartUpdateDownload(){
    if(!updater_||!update_manifest_)return;
    if(updater_->Download(UpdateEndpoints(),*update_manifest_,update_sources_))SetTrayTip(L"LumaShot · 正在下载更新 0%");
}
void Application::PromptInstall(){
    if(!update_manifest_||update_ready_.empty()||update_prompting_)return;
    const auto body=L"LumaShot "+update::VersionText(update_manifest_->version)+L" 已下载，并通过签名与完整性校验。\n安装时会关闭 LumaShot，完成后自动重新启动；桌面贴图会保留。";
    update_prompting_=true;
    const bool accepted=ui::ShowThemedMessage(Owner(),preferences_.Dark(),L"更新已就绪",body,L"立即安装",L"稍后");
    update_prompting_=false;
    if(accepted)InstallUpdate();
}
void Application::InstallUpdate(){
    if(!updater_||!update_manifest_||update_ready_.empty())return;
    if(recording_process_.Active()){Notice(L"请先结束录制，再安装更新。");return;}
    // Re-verify off the UI thread right before launching the installer.
    updater_->Verify(update_ready_,*update_manifest_);
}
void Application::Settings() {
    if(settings_open_)return;
    settings_open_=true;
    struct Reset{bool& value;~Reset(){value=false;}} reset{settings_open_};
    auto edited=preferences_;
    if(!EditPreferencesIsolated(main_,edited,[&](const Preferences& candidate){
        // Captures can change tools and the save folder while this draft is open.
        auto next=candidate;next.tools=preferences_.tools;next.save_directory=preferences_.save_directory;
        // These tray-only policies may have changed while the settings worker was open.
        next.hotkeys_disabled=preferences_.hotkeys_disabled;next.disable_hotkeys_in_game=preferences_.disable_hotkeys_in_game;
        // Update bookkeeping is host-owned and may change while the dialog is open.
        next.update_last_check=preferences_.update_last_check;next.update_mirrors=preferences_.update_mirrors;next.last_run_version=preferences_.last_run_version;
        next.clipboard_strip_hint_shown=preferences_.clipboard_strip_hint_shown;
        RefreshHotkeys();
        const auto before=EffectiveHotkeys(preferences_,hotkeys_suspended_),after=EffectiveHotkeys(next,hotkeys_suspended_);
        if(!UniqueShortcuts(next)||!ApplyShortcuts(main_,before,after))return false;
        if(clipboard_panel_&&!clipboard_panel_->SetShortcut(next.clipboard_modifiers,next.clipboard_key)){ApplyShortcuts(main_,after,before);return false;}
        const auto restoreClipboard=[&]{if(clipboard_panel_)clipboard_panel_->SetShortcut(preferences_.clipboard_modifiers,preferences_.clipboard_key);};
        if(!demo_&&!diagnostic_session_&&!ConfigureLoginStartup(next.start_with_windows)){restoreClipboard();ApplyShortcuts(main_,after,before);return false;}
        settings_writer_.Request(next);
        if(!settings_writer_.Flush()){settings_writer_.DropPending();restoreClipboard();if(!demo_&&!diagnostic_session_)ConfigureLoginStartup(preferences_.start_with_windows);ApplyShortcuts(main_,after,before);return false;}
        preferences_=std::move(next);return true;
    },[this](std::span<const HANDLE> events){return WaitForWork(events);},[this]{PostMessageW(main_,kUpdate,kUpdateCheckRequest,0);}))return;
    // Re-evaluate soon in case automatic checks were just turned on.
    if(updater_&&preferences_.update_auto_check)SetTimer(main_,kUpdateTimer,kUpdateStartupDelay,nullptr);
    if(clipboard_panel_){
        // Disable first so a persistence change does not rebuild a store that is going away.
        if(!preferences_.clipboard_enabled)clipboard_panel_->Enable(false,preferences_.Dark());
        clipboard_panel_->SetPersistent(preferences_.clipboard_persist);
        clipboard_panel_->SetStripVisible(preferences_.clipboard_strip_visible);
    }
    if(clipboard_panel_&&!clipboard_panel_->Enable(preferences_.clipboard_enabled,preferences_.Dark()))Notice(L"剪贴板监听无法启动，请重新开启。");
    if(pins_)pins_->RefreshAppearance();
}
bool Application::AnnotatePin(uint64_t id,std::shared_ptr<const Frame> image,Document annotations,RECT bounds){
    if(active_||pending_)return false;
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&monitor);
    pin_edit_id_=id;pin_edit_bounds_=bounds;pin_edit_source_=std::move(image);pin_edit_copy_=false;
    try{
        frame_=std::make_shared<Frame>(PinAnnotationPreview(*pin_edit_source_,bounds,monitor.rcWork));
        acrylic_=std::make_shared<Frame>(BlurBackdrop(*frame_));cursor_patch_.reset();monitors_={monitor.rcWork};windows_.clear();document_=PinAnnotationDisplayDocument(annotations,*pin_edit_source_,bounds);draft_.reset();
        state_={};static_cast<ToolProperties&>(state_)=preferences_.tools;state_.dark=preferences_.Dark();state_.selection=BoxOf(bounds);state_.selected=true;
        state_.hint=L"贴图标注 · Enter 完成 · Esc 取消";ResetSessionTool();ShowViews();UpdatePinEditRegion();Invalidate();
    }catch(...){Cancel(false);throw;}
    return true;
}
bool Application::AnnotateImage(std::shared_ptr<const Frame> image,Document annotations,RECT bounds,longshot::Host::AnnotateDone done){
    // Long-image annotation reuses the pin editing session: fixed selection over
    // the viewer, full toolset, marks returned in source pixels on completion.
    if(!image||!AnnotatePin(kImageEditId,std::move(image),std::move(annotations),bounds))return false;
    image_edit_done_=std::move(done);image_edit_follow_=0;
    state_.hint=L"长图标注 · Enter 完成 · Esc 取消";Invalidate();
    return true;
}
void Application::UpdatePinEditRegion(){
    if(!pin_edit_id_)return;
    for(auto& view:views_){
        HRGN region=CreateRectRgn(pin_edit_bounds_.left-view->bounds.left,pin_edit_bounds_.top-view->bounds.top,pin_edit_bounds_.right-view->bounds.left,pin_edit_bounds_.bottom-view->bounds.top);
        auto add=[&](Box b,bool rounded){const int x=int(b.left)-view->bounds.left,y=int(b.top)-view->bounds.top,r=int(std::ceil(b.right))-view->bounds.left,bt=int(std::ceil(b.bottom))-view->bounds.top;
            HRGN part=rounded?CreateRoundRectRgn(x,y,r+1,bt+1,int(20*state_.toolbar.scale),int(20*state_.toolbar.scale)):CreateRectRgn(x,y,r,bt);CombineRgn(region,region,part,RGN_OR);DeleteObject(part);};
        if(document_.selected>=0&&size_t(document_.selected)<document_.marks.size()){
            auto box=Bounds(document_.marks[document_.selected]);for(auto handle:EditHandles(document_.marks[document_.selected],document_.selected_part,state_.toolbar.scale)){box.left=std::min(box.left,handle.point.x);box.right=std::max(box.right,handle.point.x);box.top=std::min(box.top,handle.point.y);box.bottom=std::max(box.bottom,handle.point.y);}
            const float padding=7*state_.toolbar.scale;add({box.left-padding,box.top-padding,box.right+padding,box.bottom+padding},false);
        }
        add(state_.toolbar.bounds,true);if(state_.picker.open)add(state_.picker.bounds,true);if(state_.dropdown.Open())add(state_.dropdown.bounds,true);
        if(!SetWindowRgn(view->window,region,TRUE))DeleteObject(region);
    }
}
void Application::Start() {

    if(active_||pending_)return;
    if(longshot_&&longshot_->Active())return; // one live capture at a time
    const bool translate=std::exchange(translate_request_,false);translate_next_=false;
    capture_started_=Diagnostics::Now();
    element_scanner_.Cancel();element_windows_.clear();element_regions_.clear();
    if(demo_) {
        Renderer renderer;frame_=std::make_shared<Frame>(renderer.Demo(false));
        acrylic_=std::make_shared<Frame>(BlurBackdrop(*frame_));monitors_={frame_->bounds};windows_={{180,90,1110,600}};
        if(Diagnostics::Get().Enabled()) {
            monitors_.clear();EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR,HDC,LPRECT r,LPARAM p)->BOOL{reinterpret_cast<Application*>(p)->monitors_.push_back(*r);return TRUE;},reinterpret_cast<LPARAM>(this));
            RECT desktop=monitors_.front();for(auto r:monitors_)UnionRect(&desktop,&desktop,&r);
            auto synthetic=MakeFrame(desktop);for(int y=0;y<synthetic.Height();++y)for(int x=0;x<synthetic.Width();++x)synthetic.pixels[static_cast<size_t>(y)*synthetic.Width()+x]=frame_->pixels[static_cast<size_t>(y%frame_->Height())*frame_->Width()+x%frame_->Width()];
            frame_=std::make_shared<Frame>(std::move(synthetic));acrylic_=std::make_shared<Frame>(BlurBackdrop(*frame_));
            for(auto r:monitors_){Diagnostics::Get().Add("monitor_width",Diagnostics::Now(),r.right-r.left);Diagnostics::Get().Add("monitor_height",Diagnostics::Now(),r.bottom-r.top);}
        }
        document_.Reset();state_={};state_.width=2;ResetSessionTool();state_.hint=L"交互验证 · 合成测试画面 · 拖动框选 / 单击窗口 · Esc 退出";
        ShowViews();return;
    }
    FrozenCursor cursor=preferences_.include_cursor?FrozenCursor::Snapshot():FrozenCursor{};
    monitors_.clear();windows_.clear();
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR,HDC,LPRECT r,LPARAM context)->BOOL {
        reinterpret_cast<Application*>(context)->monitors_.push_back(*r);return TRUE;
    },reinterpret_cast<LPARAM>(this));
    document_.Reset();draft_.reset();state_={};static_cast<ToolProperties&>(state_)=preferences_.tools;state_.dark=preferences_.Dark();ResetSessionTool();
    state_.hint=preferences_.include_cursor?L"已包含鼠标指针 · 拖动框选 / 单击窗口 · F 全屏 · A 全部屏幕 · Esc 取消":
        L"拖动框选 / 单击窗口 · F 全屏 · A 全部屏幕 · Esc 取消";
    translate_capture_=translate;
    if(translate)state_.hint=L"截图翻译 · 拖动框选要翻译的区域 / 单击窗口 · F 全屏 · Esc 取消";
    const uint64_t generation=++generation_;
    const bool include=preferences_.include_cursor;pending_=true;
    worker_=std::jthread([this,generation,include,cursor=std::move(cursor)](std::stop_token stop) {
        TraceScope worker_trace("capture_and_blur");
        auto result=std::make_unique<Result>();result->capture=true;result->generation=generation;
        try {
            {TraceScope capture("desktop_capture");result->frame=CaptureDesktop();}
            if(include){RECT patch{};const RECT cursor_bounds=cursor.Bounds();
                if(cursor.Visible()&&IntersectRect(&patch,&result->frame.bounds,&cursor_bounds))result->cursor_patch=Crop(result->frame,patch);
                cursor.Composite(result->frame);}
            if(!stop.stop_requested()){TraceScope blur("backdrop_blur");result->acrylic=BlurBackdrop(result->frame);}
        }catch(const std::exception& e){result->error=ErrorText(e);}
        if(stop.stop_requested()){result->frame={};result->acrylic={};result->cursor_patch={};}
        {std::lock_guard lock(mutex_);result_=std::move(result);}
        PostMessageW(main_,kResult,0,0);
    });
    // Freeze window geometry while the desktop copy is already running. None of
    // the slower cross-process accessibility calls run on this UI thread.
    EnumWindows([](HWND w,LPARAM context)->BOOL {
        auto* app=reinterpret_cast<Application*>(context);
        if(!IsWindowVisible(w)||IsIconic(w)||w==app->main_)return TRUE;
        DWORD cloaked{};DwmGetWindowAttribute(w,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        if(cloaked)return TRUE;
        RECT r{};
        if(FAILED(DwmGetWindowAttribute(w,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r))))GetWindowRect(w,&r);
        if(r.right>r.left&&r.bottom>r.top){app->windows_.push_back(r);app->element_windows_.push_back({w,r});}return TRUE;
    },reinterpret_cast<LPARAM>(this));
    POINT pointer{};GetCursorPos(&pointer);
    element_scanner_.Start(main_,kElementsReady,generation,element_windows_,pointer);
    // Hidden graphics setup overlaps the worker's desktop copy. There is still
    // no visible overlay until the frozen image and its first paint are ready.
    try {
        CreateViews();
        for(auto& view:views_)view->renderer->Prepare(view->window,view->bounds);
    }catch(...){Cancel(false);throw;}
}

void Application::ElementsReady() {
    auto result=element_scanner_.Take();
    if(!result||result->generation!=generation_||(!active_&&!pending_))return;
    element_regions_=std::move(result->regions);
    Diagnostics::Get().Add("element_regions",capture_started_,static_cast<long long>(element_regions_.size()));
    if(active_&&!state_.selected&&!state_.dragging&&!state_.busy){
        const Box before=state_.selection;
        state_.selection=WindowAt(NativePoint(state_.pointer));
        if(before.left!=state_.selection.left||before.top!=state_.selection.top||before.right!=state_.selection.right||before.bottom!=state_.selection.bottom)Invalidate();
    }
}

void Application::ResultReady() {
    std::unique_ptr<Result> result;
    {std::lock_guard lock(mutex_);result=std::move(result_);}
    if(!result)return;
    if(worker_.joinable())worker_.join();pending_=false;state_.busy=false;
    if(result->generation!=generation_)return;
    if(!result->error.empty()){
        if(!client_output_.empty()){client_exit_=1;Cancel();return;}
        if(result->capture)Cancel(false);
        Notice(result->error);Invalidate();return;
    }
    if(result->capture) {
        cursor_patch_=std::make_shared<Frame>(std::move(result->cursor_patch));
        frame_=std::make_shared<Frame>(std::move(result->frame));
        acrylic_=std::make_shared<Frame>(std::move(result->acrylic));ShowViews();
    }else if(result->editing==kImageEditId){
        auto done=std::move(image_edit_done_);image_edit_done_=nullptr;const int follow=std::exchange(image_edit_follow_,0);
        pin_edit_id_=0;pin_edit_source_.reset();Cancel(false);
        if(done)done(std::move(result->annotations),follow);
    }else if(result->editing){
        if(result->edit_copy)PublishClipboardImage(Owner(),*result->clipboard_image);
        pins_->CompleteAnnotation(result->editing,std::move(result->frame),std::move(result->annotations),std::move(result->annotation_base),result->recognize);pin_edit_id_=0;pin_edit_source_.reset();Cancel(false);
    }else if(result->pinned) {
        const POINT origin{result->frame.bounds.left,result->frame.bounds.top};
        const bool translate=std::exchange(translate_next_,false);
        pins_->Create(std::move(result->frame),std::move(result->ocr_frame),origin,std::move(result->annotation_base),std::move(result->annotations),result->recognize);Cancel(false);
        if(translate)pins_->TranslateLast();
    }else {
        try {
            if(!client_output_.empty()){if(!result->saved)throw std::runtime_error("Capture output was not saved");client_exit_=0;}
            else if(!result->saved)PublishClipboardImage(Owner(),*result->clipboard_image);
            Cancel();
        }catch(const std::exception& e){Notice(ErrorText(e));Invalidate();}
    }
}
void Application::CreateViews() {
    if(!views_.empty())return;
    for(const auto r:monitors_) {
        auto view=std::make_unique<View>();view->app=this;view->bounds=r;view->renderer=std::make_unique<Renderer>();
        view->window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"LumaShot.Overlay",L"LumaShot 截图",
            WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,main_,nullptr,GetModuleHandleW(nullptr),view.get());
        CheckWin32(view->window!=nullptr,"Create monitor overlay");
        views_.push_back(std::move(view));
    }
}
void Application::ShowViews() try {
    TraceScope trace("show_views");
    if(Diagnostics::Get().Enabled())for(auto r:monitors_){Diagnostics::Get().Add("monitor_width",Diagnostics::Now(),r.right-r.left);Diagnostics::Get().Add("monitor_height",Diagnostics::Now(),r.bottom-r.top);}
    POINT mouse{};GetCursorPos(&mouse);state_.pointer={float(mouse.x),float(mouse.y)};
    active_=true;state_.external_magnifier=true;
    CreateViews();
    UpdateToolbar(mouse);if(!pin_edit_id_)state_.selection=WindowAt(mouse);
    // Upload each monitor's image and perform the cold first draw before an overlay
    // becomes visible. Otherwise the user can start a gesture while this UI
    // thread is still initializing the renderer, queuing hundreds of ms of input.
    for(auto& view:views_) {
        TraceScope prepare("prepare_overlay");
        bool prepared=false;
        for(int attempt=0;attempt<2&&!prepared;++attempt)
            prepared=view->renderer->Paint(view->window,view->bounds,*frame_,*acrylic_,document_,draft_,state_);
        if(!prepared)throw std::runtime_error("Unable to prepare capture display");
    }
    // The cursor may have moved during capture/upload. The visible first frame
    // must use its current position, not the sample taken before initialization.
    GetCursorPos(&mouse);state_.pointer={float(mouse.x),float(mouse.y)};
    UpdateToolbar(mouse);if(!pin_edit_id_)state_.selection=WindowAt(mouse);
    UpdatePinEditRegion();
    // Same rule for the magnifier (closed after every capture): its window and first
    // layered frame are created hidden, so the first visible hover only moves it.
    if(!state_.selected&&!state_.dragging&&!state_.busy){const POINT pointer=NativePoint(state_.pointer);
        for(const auto& view:views_)if(PtInRect(&view->bounds,pointer)){magnifier_.Prepare(view->window,*frame_,view->bounds,pointer);break;}}
    HWND focus=views_.front()->window;
    for(auto& view:views_) {
        ShowWindow(view->window,SW_SHOWNOACTIVATE);UpdateWindow(view->window);
        if(PtInRect(&view->bounds,mouse))focus=view->window;
    }
    {TraceScope activate("activate_overlay");ActivateOverlay(focus);}UpdateMagnifier();
    if(capture_started_)Diagnostics::Get().Add("capture_ready",capture_started_);
} catch(...) {
    // A failed hidden preparation must not leave an invisible active session
    // that prevents every subsequent capture attempt.
    if(ipc_client_)client_exit_=1;Cancel(false);throw;
}
void Application::UpdateMagnifier() {
    if(!active_||!frame_||state_.selected||state_.dragging||state_.busy){magnifier_.Hide();return;}
    const POINT pointer=NativePoint(state_.pointer);
    for(const auto& view:views_)if(PtInRect(&view->bounds,pointer)){
        magnifier_.Show(view->window,*frame_,view->bounds,pointer);return;
    }
    magnifier_.Hide();
}
void Application::Invalidate() {SyncToolbarMotion();UpdatePinEditRegion();for(auto& view:views_)InvalidateRect(view->window,nullptr,FALSE);}
void Application::Cancel(bool close_demo) {
    line_vertices_.clear();state_.polyline_confirmed=0;state_.polyline_active=false;state_.line_snap.reset();
    if(active_){ClosePicker(true);RememberProperties();}FlushProperties();
    const auto edited=std::exchange(pin_edit_id_,0);if(edited)close_demo=false;
    translate_capture_=false;
    KillTimer(main_,9);toolbar_transition_.Reset();state_.closing_tool=Tool::Select;
    Diagnostics::Get().Flush();
    ++generation_;worker_.request_stop();element_scanner_.Cancel();element_regions_.clear();element_windows_.clear();CommitText(true);ReleaseCapture();magnifier_.Close();
    active_=false;state_.selected=false;state_.dragging=false;state_.property_drag=-1;
    StopToolbarMotion();
    for(auto& view:views_)if(IsWindow(view->window))DestroyWindow(view->window);
    views_.clear();frame_.reset();acrylic_.reset();cursor_patch_.reset();document_.Reset();draft_.reset();
    moving_=resizing_=mark_moving_=false;edit_original_.reset();mark_handle_=-1;
    if(edited==kImageEditId){auto done=std::move(image_edit_done_);image_edit_done_=nullptr;image_edit_follow_=0;if(done)done(std::nullopt,0);}
    else if(edited&&pins_)pins_->CompleteAnnotation(edited,std::nullopt);
    pin_edit_source_.reset();pin_edit_copy_=false;
    if(ipc_client_){
        // Finish the stopped export before releasing its output path or notifying
        // the caller. A late PNG must never survive a canceled request.
        if(worker_.joinable())worker_.join();pending_=false;state_.busy=false;
        bool committed=false;{std::lock_guard lock(mutex_);committed=result_&&result_->saved;result_.reset();}
        if(client_exit_!=0&&committed){std::error_code ec;std::filesystem::remove(client_output_,ec);}
        client_output_.clear();ipc_client_->Complete(client_exit_);ipc_client_.reset();
        return;
    }
    if((demo_||diagnostic_session_)&&close_demo&&!ipc_demo_)PostMessageW(main_,WM_CLOSE,0,0);
}
Box Application::WindowAt(POINT point) const {
    if(const auto region=HitTestElements(point,element_windows_,element_regions_)){
        RECT clipped{};if(IntersectRect(&clipped,&*region,&frame_->bounds))return BoxOf(clipped);
    }
    for(const auto rect:windows_)if(PtInRect(&rect,point)) {
        RECT clipped{};if(IntersectRect(&clipped,&rect,&frame_->bounds))return BoxOf(clipped);
    }
    for(const auto rect:monitors_)if(PtInRect(&rect,point))return BoxOf(rect);
    return {};
}
void Application::UpdateToolbar(POINT point) {
    RECT chosen=monitors_.front();float scale=1;
    for(const auto& view:views_)if(PtInRect(&view->bounds,point)) {
        chosen=view->bounds;scale=float(GetDpiForWindow(view->window))/96;break;
    }
    toolbar_monitor_=chosen;
    state_.toolbar=PlaceToolbar(state_.selection,chosen,scale,toolbar_transition_.Value(GetTickCount64()),state_.PropertyTool()==Tool::Select?state_.closing_tool:state_.PropertyTool(),state_.number_combo);state_.toolbar.ocr_available=ocr_available_;state_.toolbar.recording_available=pin_edit_id_==0;
}
void Application::AnimateToolbar() {
    const auto now=GetTickCount64();
    if(!state_.selected){KillTimer(main_,9);return;}
    state_.toolbar=PlaceToolbar(state_.selection,toolbar_monitor_,state_.toolbar.scale,toolbar_transition_.Value(now),state_.PropertyTool()==Tool::Select?state_.closing_tool:state_.PropertyTool(),state_.number_combo);state_.toolbar.ocr_available=ocr_available_;state_.toolbar.recording_available=pin_edit_id_==0;
    if(!toolbar_transition_.Active(now)){KillTimer(main_,9);state_.closing_tool=Tool::Select;}
    state_.hover=-1;Invalidate();
}

void Application::PointerDown(View& view,Point point) {
    if(state_.busy)return;
    magnifier_.Hide();
    CommitText();SetFocus(view.window);
    if(state_.dropdown.Open()){ApplyToolbarDropdown(state_.dropdown.Hit(point));AnimateToolbar();Invalidate();return;}
    if(state_.picker.open){
        if(!Contains(state_.picker.bounds,point)||state_.picker.Hit(point)==2){ClosePicker();return;}
        state_.picker.Down(point);state_.PickerColor()=state_.picker.color;ApplySelectedProperties(true);if(state_.picker.drag>=0)SetCapture(view.window);Invalidate();return;
    }
    if(state_.selected) {
        const int command=ui::HitTest(ToolbarControls(state_,document_),point);
        if(command==46||command==47||command==56||command==64||command==65){state_.property_drag=command;SetCapture(view.window);SetToolbarSlider(state_,command,point);ApplySelectedProperties(true);Invalidate();return;}
        if(command>=0){toolbar_pressed_=command<int(ui::ToolbarSlots)?command:-1;SyncToolbarMotion();Command(command);return;}
        if(Contains(state_.toolbar.bounds,point))return;
    }
    if(state_.selected&&state_.tool==Tool::Pen&&state_.pen_polyline){
        if(Contains(state_.selection,point))PolylineClick(point);
        return;
    }
    bool snapped_start=false;
    if(state_.selected&&state_.tool==Tool::Pen&&state_.pen_straight&&Contains(state_.selection,point)){
        point=PenSnapPoint(point);snapped_start=state_.line_snap.has_value();
    }
    if(!snapped_start&&HasPropertyTarget()){
        auto& mark=document_.marks[document_.selected];const int handle=HitEditHandle(mark,document_.selected_part,point,state_.toolbar.scale);
        if(handle>=0){
            // The newest mark exposes handles without ending continuous drawing.
            // Actually grabbing a handle enters the normal selection workflow.
            if(state_.tool!=Tool::Select){const int selected=document_.selected;Command(0);document_.selected=selected;SyncSelectedProperties();}
            SetCapture(view.window);start_=previous_=point;mark_checkpointed_=false;edit_original_=mark;mark_handle_=handle;mark_moving_=true;
            if(const auto cursor=ui::SelectionEditCursor(document_,state_,point,GetDpiForWindow(view.window),handle))SetCursor(cursor);return;}
    }
    SetFocus(view.window);SetCapture(view.window);start_=previous_=point;
    if(!state_.selected) {state_.dragging=true;state_.selection=Normalize(point,point);Invalidate();return;}
    const auto b=state_.selection;
    const std::array<Point,8> handles{Point{b.left,b.top},Point{(b.left+b.right)/2,b.top},Point{b.right,b.top},
        Point{b.right,(b.top+b.bottom)/2},Point{b.right,b.bottom},Point{(b.left+b.right)/2,b.bottom},
        Point{b.left,b.bottom},Point{b.left,(b.top+b.bottom)/2}};
    for(size_t i=0;i<handles.size();++i)if(!pin_edit_id_&&state_.tool==Tool::Select && std::hypot(point.x-handles[i].x,point.y-handles[i].y)<=7*state_.toolbar.scale) {
        resizing_=true;resize_handle_=static_cast<int>(i);original_selection_=b;return;
    }
    if(!Contains(b,point)){
        if(pin_edit_id_){ReleaseCapture();return;}
        // A fresh range replaces the previous edit session, including undo history.
        // Reuse the frozen desktop so the capture overlay can never enter the image.
        document_.Reset();draft_.reset();KillTimer(main_,9);toolbar_transition_.Reset();
        moving_=resizing_=mark_moving_=false;edit_original_.reset();mark_handle_=-1;resize_handle_=-1;original_selection_={};
        state_.selected=false;state_.dragging=true;state_.tool=Tool::Select;state_.closing_tool=Tool::Select;state_.selected_tool=Tool::Select;property_original_.reset();
        state_.property_drag=-1;state_.hover=-1;state_.hint.clear();state_.selection=Normalize(point,point);
        Invalidate();return;
    }
    // Existing annotations are directly editable from every drawing tool.
    // Toolbar, edit handles and capture-range gestures keep their precedence.
    const int hit=snapped_start?-1:document_.HitTest(point);
    if(hit>=0&&state_.tool!=Tool::Select)Command(static_cast<int>(Tool::Select));
    if(state_.tool!=Tool::Select){
        // Starting the next annotation detaches the previous property target,
        // including when this new gesture is empty or canceled.
        document_.selected=-1;document_.selected_part=EditPart::Whole;SyncSelectedProperties();
    }
    if(state_.tool==Tool::Select) {
        document_.selected=hit;SyncSelectedProperties();
        if(document_.selected>=0){document_.selected_part=(GetKeyState(VK_CONTROL)&0x8000)?EditPart::Whole:HitMarkPart(document_.marks[document_.selected],point);mark_checkpointed_=false;edit_original_=document_.marks[document_.selected];mark_handle_=-1;mark_moving_=true;}
        else if(!pin_edit_id_){moving_=true;original_selection_=b;}
    }else if(state_.tool==Tool::Number){
        Mark mark;mark.tool=Tool::Number;mark.color=state_.ActiveColor();mark.number_shape=state_.number_shape;mark.number_size=state_.number_size*state_.toolbar.scale;mark.number_combo=state_.number_combo;
        mark.number_label=NormalizeNumberLabel(state_.number_label);mark.number_text_color=state_.number_text_color;mark.number_text_preset=state_.number_text_preset;mark.number_text_size=state_.number_text_size*state_.toolbar.scale;
        for(const auto& existing:document_.marks)if(existing.tool==Tool::Number)mark.number=std::max(mark.number,existing.number+1);
        const float radius=mark.number_size/2;const float half_width=mark.number_shape==NumberShape::Capsule?radius*1.5f:radius;
        mark.a={point.x-half_width,point.y-radius};mark.b={point.x+half_width,point.y+radius};mark.number_target={point.x+mark.number_size*4,point.y+mark.number_size};
        if(mark.number_shape==NumberShape::Pin)mark.b.y+=radius*.6f;
        if(!mark.number_label.empty()){mark.a=mark.b=point;FitNumberBadge(mark);}
        if(mark.number_combo==NumberCombo::Leader){mark.number_target=point;PlaceNumberBadge(mark,point);}
        draft_=std::move(mark);Invalidate();return;
    }else if(state_.tool==Tool::Text) {
        ReleaseCapture();
        BeginText(view,point);
    }
    else {
        Mark mark;mark.tool=state_.tool;mark.a=mark.b=point;mark.color=state_.ActiveColor();
        mark.width=state_.ActiveWidth()*state_.toolbar.scale;
        mark.pen_straight=state_.pen_straight;mark.pen_polyline=state_.pen_polyline;mark.pen_mode=state_.pen_mode;mark.pen_opacity=state_.PenOpacity();mark.pen_smoothing=state_.pen_smoothing;
        mark.fill_color=state_.fill_color;mark.fill_opacity=state_.fill_opacity;mark.corner_radius=state_.corner_radius*state_.toolbar.scale;
        mark.line_style=state_.line_style;mark.arrow_style=state_.arrow_style;mark.arrow_size=state_.arrow_size*state_.toolbar.scale;
        mark.arrow_type=state_.arrow_type;mark.arrow_head=state_.arrow_head;
        mark.mosaic_cell=(4+state_.mosaic_strength*.4f)*state_.toolbar.scale;
        mark.mosaic_mode=state_.mosaic_mode;mark.mosaic_method=state_.mosaic_method;
        mark.mosaic_brush=state_.mosaic_brush*state_.toolbar.scale;
        if(mark.tool==Tool::Pen || (mark.tool==Tool::Arrow&&mark.arrow_type==ArrowType::HandDrawn) || (mark.tool==Tool::Mosaic&&mark.mosaic_method==MosaicMethod::Brush))mark.points.push_back(point);
        draft_=std::move(mark);
    }
    Invalidate();
}
void Application::PointerMove(View& source,Point point,WPARAM) {
    TraceScope trace("pointer_move",state_.dragging?1:(state_.selected?2:0));
    const Box old_selection=state_.selection;
    if(state_.busy)return;state_.pointer=point;
    if(mark_moving_&&mark_handle_>=0){if(const auto cursor=ui::SelectionEditCursor(document_,state_,point,GetDpiForWindow(source.window),mark_handle_))SetCursor(cursor);}
    if(state_.dropdown.Open()){const int hovered=state_.dropdown.Hit(point);if(hovered!=state_.dropdown.hover){state_.dropdown.hover=hovered;Invalidate();}return;}
    if(state_.property_drag>=0){SetToolbarSlider(state_,state_.property_drag,point);ApplySelectedProperties(true);Invalidate();for(auto& view:views_)UpdateWindow(view->window);return;}
    if(state_.picker.open){state_.picker.Move(point);state_.PickerColor()=state_.picker.color;ApplySelectedProperties(true);Invalidate();return;}
    if(!frame_)return;
    point.x=std::clamp(point.x,float(frame_->bounds.left),float(frame_->bounds.right));
    point.y=std::clamp(point.y,float(frame_->bounds.top),float(frame_->bounds.bottom));
    if(!line_vertices_.empty()){
        PolylinePreview(point);state_.hover=ui::HitTest(ToolbarControls(state_,document_),point);Invalidate();return;
    }
    if(state_.dragging)state_.selection=Normalize(start_,point);
    else if(resizing_) {
        Box b=original_selection_;
        if(resize_handle_==0||resize_handle_==6||resize_handle_==7)b.left=point.x;
        if(resize_handle_==2||resize_handle_==3||resize_handle_==4)b.right=point.x;
        if(resize_handle_==0||resize_handle_==1||resize_handle_==2)b.top=point.y;
        if(resize_handle_==4||resize_handle_==5||resize_handle_==6)b.bottom=point.y;
        if(b.right-b.left>=1&&b.bottom-b.top>=1)state_.selection=b;
    }else if(moving_) {
        const float dx=std::clamp(point.x-start_.x,float(frame_->bounds.left)-original_selection_.left,
            float(frame_->bounds.right)-original_selection_.right);
        const float dy=std::clamp(point.y-start_.y,float(frame_->bounds.top)-original_selection_.top,
            float(frame_->bounds.bottom)-original_selection_.bottom);
        state_.selection={original_selection_.left+dx,original_selection_.top+dy,original_selection_.right+dx,original_selection_.bottom+dy};
    }else if(mark_moving_ && document_.selected>=0) {
        if(edit_original_&&(point!=start_||mark_checkpointed_)){
            auto changed=EditMark(*edit_original_,document_.selected_part,mark_handle_,start_,point,(GetKeyState(VK_SHIFT)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0,state_.toolbar.scale);
            if(changed!=document_.marks[document_.selected]){
                // Merely selecting must not add an undo step or discard redo.
                if(!mark_checkpointed_){document_.Checkpoint();mark_checkpointed_=true;}
                document_.marks[document_.selected]=std::move(changed);
            }
        }
    }else if(draft_) {
        point.x=std::clamp(point.x,state_.selection.left,state_.selection.right);
        point.y=std::clamp(point.y,state_.selection.top,state_.selection.bottom);
        if(draft_->tool==Tool::Pen){
            if(draft_->pen_straight){
                point=PenSnapPoint(point,start_);
                draft_->b=point;
                draft_->points=point==start_?std::vector<Point>{start_}:std::vector<Point>{start_,point};
            }else{
                draft_->b=point;
                if(std::hypot(point.x-previous_.x,point.y-previous_.y)>=1)draft_->points.push_back(point);
            }
        }else if(draft_->tool==Tool::Number){if(draft_->number_combo==NumberCombo::Leader)PlaceNumberBadge(*draft_,point);else if(std::hypot(point.x-start_.x,point.y-start_.y)>3)draft_->number_target=point;}
        else draft_->b=point;
        if(((draft_->tool==Tool::Arrow&&draft_->arrow_type==ArrowType::HandDrawn)||(draft_->tool==Tool::Mosaic&&draft_->mosaic_method==MosaicMethod::Brush)) && std::hypot(point.x-previous_.x,point.y-previous_.y)>=1)draft_->points.push_back(point);
    }else if(!state_.selected)state_.selection=WindowAt(NativePoint(point));
    state_.hover=state_.selected?ui::HitTest(ToolbarControls(state_,document_),point):-1;
    if(!draft_&&!moving_&&!resizing_&&!mark_moving_){
        state_.line_snap.reset();
        if(state_.selected&&state_.tool==Tool::Pen&&state_.pen_straight&&Contains(state_.selection,point)&&!Contains(state_.toolbar.bounds,point))PenSnapPoint(point);
    }
    previous_=point;
    if(!state_.selected&&!state_.dragging) {
        UpdateMagnifier();
        // Within the same highlighted window, only the 122px lens changes.
        // Do not submit an identical full-screen frame for every mouse event.
        const Box b=state_.selection;
        if(b.left==old_selection.left&&b.top==old_selection.top&&b.right==old_selection.right&&b.bottom==old_selection.bottom)return;
    }
    Invalidate();
    // Mouse traffic can keep low-priority WM_PAINT pending. Present the magnifier
    // as well as the selection; the render target does not wait for vblank.
    // This remains event-driven and has no idle rendering loop.
    if(!state_.selected||state_.dragging||moving_||resizing_)for(auto& view:views_)UpdateWindow(view->window);
}
void Application::PointerDoubleClick(View& view,Point point) {
    if(state_.busy||pending_)return;
    if(state_.dropdown.Open()||state_.picker.open){PointerDown(view,point);return;}
    if(state_.tool==Tool::Pen&&state_.pen_polyline){
        if(!line_vertices_.empty()&&Contains(state_.selection,point)&&!Contains(state_.toolbar.bounds,point)){
            PolylineClick(point);EndPolyline(false);
        }
        return;
    }
    if(!state_.selected||!Contains(state_.selection,point)||Contains(state_.toolbar.bounds,point)||
        ui::HitTest(ToolbarControls(state_,document_),point)>=0)return;
    const int index=document_.HitTest(point);
    if(index>=0){const auto& mark=document_.marks[index];
        if(mark.tool==Tool::Number&&HitMarkPart(mark,point)==EditPart::Badge){BeginText(view,point,index,true);return;}
        if(mark.tool==Tool::Text||(mark.tool==Tool::Number&&mark.number_combo==NumberCombo::Text)){
            BeginText(view,mark.a,index);return;
        }
    }
    // Use the same completion path as Enter, including pin annotation sessions.
    Key(view,VK_RETURN);
}
void Application::PointerUp(View& view,Point point) {
    if(toolbar_pressed_>=0){toolbar_pressed_=-1;TickToolbarMotion();}
    if(state_.busy)return;
    if(state_.property_drag>=0){SetToolbarSlider(state_,state_.property_drag,point);ApplySelectedProperties();state_.property_drag=-1;ReleaseCapture();RememberProperties();Invalidate();return;}
    if(state_.picker.open){state_.picker.Move(point);state_.picker.drag=-1;state_.PickerColor()=state_.picker.color;ApplySelectedProperties(true);ReleaseCapture();Invalidate();return;}
    if(!line_vertices_.empty()){ReleaseCapture();return;}
    const bool editing_handle=mark_moving_&&mark_handle_>=0;
    if(draft_||state_.dragging||moving_||resizing_||mark_moving_)PointerMove(view,point,MK_LBUTTON);
    ReleaseCapture();
    if(state_.dragging) {
        state_.dragging=false;
        if(std::hypot(point.x-start_.x,point.y-start_.y)<3)state_.selection=WindowAt(NativePoint(start_));
        state_.selected=HasArea(state_.selection);
        // Translate hotkey: the selection itself is the command; pin, recognize and translate at once.
        if(state_.selected&&translate_capture_){translate_next_=true;Finish(false,true,true);if(!pending_)translate_next_=false;return;}
        if(state_.selected)state_.hint=preferences_.include_cursor?L"包含原鼠标指针 · 双击选区 / Enter / Ctrl+C 复制 · Ctrl+S 保存 · Esc 取消":L"双击选区 / Enter / Ctrl+C 复制 · Ctrl+S 保存 · Esc 取消";
    }
    if(draft_&&draft_->tool==Tool::Number&&draft_->number_combo==NumberCombo::Leader){
        point.x=std::clamp(point.x,state_.selection.left,state_.selection.right);point.y=std::clamp(point.y,state_.selection.top,state_.selection.bottom);
        if(std::hypot(point.x-start_.x,point.y-start_.y)<=3)draft_.reset();else PlaceNumberBadge(*draft_,point);
    }
    if(draft_) {
        const bool edit_label=draft_->tool==Tool::Number&&draft_->number_combo==NumberCombo::Text;
        const bool hand_curve=draft_->tool==Tool::Arrow&&draft_->arrow_type==ArrowType::HandDrawn&&
            std::any_of(draft_->points.begin(),draft_->points.end(),[&](Point p){return std::hypot(p.x-draft_->a.x,p.y-draft_->a.y)>1;});
        if(draft_->tool==Tool::Pen || draft_->tool==Tool::Mosaic || hand_curve || std::hypot(draft_->b.x-draft_->a.x,draft_->b.y-draft_->a.y)>1){
            document_.Add(std::move(*draft_));document_.selected=static_cast<int>(document_.marks.size())-1;
            document_.selected_part=EditPart::Whole;
        }
        draft_.reset();
        if(edit_label&&document_.selected>=0)BeginText(view,document_.marks.back().a,static_cast<int>(document_.marks.size())-1);
    }
    state_.line_snap.reset();
    moving_=resizing_=mark_moving_=mark_checkpointed_=false;edit_original_.reset();mark_handle_=-1;
    if(document_.selected>=0)SyncSelectedProperties();
    if(state_.selected)UpdateToolbar(NativePoint(point));UpdateMagnifier();Invalidate();
    if(editing_handle){const auto cursor=ui::SelectionEditCursor(document_,state_,point,GetDpiForWindow(view.window));SetCursor(cursor?cursor:LoadCursorW(nullptr,IDC_ARROW));}
}
void Application::Command(int id) {
    if(!client_output_.empty()&&(id==13||id==15||id==16||id==17||id==18)){Notice(L"聊天截图模式：请点击完成，将图片插入聊天。");return;}
    if(!state_.busy&&!line_vertices_.empty()){
        if(id==7){UndoPolylinePoint();return;}
        if(id==8)return;
        if((id>=0&&id<=6)||id==14||(id>=83&&id<=85)){
            EndPolyline(false);document_.selected=-1;SyncSelectedProperties();
        }
    }
    state_.line_snap.reset();
    if(state_.busy)return;
    // A workflow-mode change must not reshape the auto-selected completed chain.
    // Explicit selection (V) still allows intentional property conversion.
    if(id>=83&&id<=85&&state_.tool==Tool::Pen&&HasPropertyTarget()&&
        (id==85||document_.marks[document_.selected].pen_polyline)){
        document_.selected=-1;SyncSelectedProperties();
    }
    if(id==81){
        if(!active_||pending_||!state_.selected||state_.PropertyTool()!=Tool::Number)return;
        const auto b=state_.toolbar.Property(81,Tool::Number);const Point anchor{b.left,b.bottom};
        for(auto& view:views_)if(Contains(BoxOf(view->bounds),{(b.left+b.right)/2,(b.top+b.bottom)/2})){
            BeginText(*view,anchor,HasPropertyTarget()?document_.selected:-1,true);break;
        }
        return;
    }
    if(id==16){
        if(pin_edit_id_||pending_||!active_||!frame_||!state_.selected)return;
        const auto b=state_.selection;
        RECT requested{LONG(std::floor(b.left)),LONG(std::floor(b.top)),LONG(std::ceil(b.right)),LONG(std::ceil(b.bottom))},region{};
        if(!IntersectRect(&region,&requested,&frame_->bounds)||!recording::ValidRecordingRegion(region))return;
        CommitText();if(state_.picker.open)ClosePicker(true);state_.property_drag=-1;ReleaseCapture();state_.dropdown.Close();
        // Launch before releasing the screenshot. Existing workers are only raised.
        if(recording_process_.Start(false,preferences_.Dark(),region))Cancel(false);
        else {ui::ShowThemedMessage(Owner(),preferences_.Dark(),L"无法打开录制",L"无法打开录制窗口。截图仍保留，请重试或检查安装文件。");Invalidate();}
        return;
    }
    if(id==17){
        if(pin_edit_id_||pending_||!active_||!frame_||!state_.selected||!longshot_)return;
        const auto b=state_.selection;
        RECT requested{LONG(std::floor(b.left)),LONG(std::floor(b.top)),LONG(std::ceil(b.right)),LONG(std::ceil(b.bottom))},region{};
        if(!IntersectRect(&region,&requested,&frame_->bounds))return;
        // One monitor: one DPI and one window under the region to scroll.
        MONITORINFO monitor{sizeof(monitor)};const POINT center{(region.left+region.right)/2,(region.top+region.bottom)/2};
        GetMonitorInfoW(MonitorFromPoint(center,MONITOR_DEFAULTTONEAREST),&monitor);
        if(!IntersectRect(&region,&region,&monitor.rcMonitor)||region.right-region.left<32||region.bottom-region.top<32){Notice(L"选区太小，无法进行长截图。");Invalidate();return;}
        if(longshot_->Active()){Notice(L"已有一个长截图正在进行。");Invalidate();return;}
        CommitText();if(state_.picker.open)ClosePicker(true);state_.property_drag=-1;ReleaseCapture();state_.dropdown.Close();
        // The frozen overlay closes so the live page can scroll; the session
        // waits for DWM before its first capture.
        Cancel(false);
        try{longshot_->Start(region);}catch(const std::exception& e){Notice(ErrorText(e));}
        return;
    }
    if(id>=20&&(state_.PropertyTool()==Tool::Select||(state_.PropertyTool()==Tool::Mosaic&&(id<24||id==28))))return;
    if((id>=0&&id<7)||id==14){
        const Tool next=id==14?Tool::Number:static_cast<Tool>(id);
        if(next!=state_.tool){
            if(state_.picker.open)ClosePicker(true);
            if(state_.property_drag>=0){state_.property_drag=-1;ReleaseCapture();}
            if(next==Tool::Select)state_.closing_tool=state_.tool;
            const auto now=GetTickCount64();toolbar_transition_.Target(next==Tool::Select?0.f:1.f,now);
            BOOL animate=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);
            if(!animate)toolbar_transition_.Reset(next==Tool::Select?0.f:1.f);
            state_.tool=next;AnimateToolbar();
            if(toolbar_transition_.Active(now)&&!SetTimer(main_,9,16,nullptr)){toolbar_transition_.Reset(next==Tool::Select?0.f:1.f);AnimateToolbar();}
        }
        document_.selected=-1;SyncSelectedProperties();
    }
    if(id==13){Finish(false,true);return;}
    if(id==15){if(ocr_available_)Finish(false,true,true);return;}
    if(id==18){if(ocr_available_&&!pin_edit_id_){translate_next_=true;Finish(false,true,true);if(!pending_)translate_next_=false;}return;}
    if(id==7){document_.Undo();SyncSelectedProperties();}if(id==8){document_.Redo();SyncSelectedProperties();}
    if(id==9)Finish(true);if(id==10||id==12){pin_edit_copy_=pin_edit_id_&&id==10;Finish(false);}if(id==11){Cancel();return;}
    if(id<40)if(auto color=PenToolbarColor(id,state_))state_.ActiveColor()=*color;
    if(state_.PropertyTool()==Tool::Rectangle||state_.PropertyTool()==Tool::Ellipse){
        if(id==40)state_.fill_color.reset();
        if((id>=41&&id<=45)||id==82)state_.fill_color=ToolbarColor(id);
    }
    if(state_.PropertyTool()==Tool::Pen&&id>=83&&id<=85){state_.pen_straight=id!=83;state_.pen_polyline=id==85;}
    if(state_.PropertyTool()==Tool::Pen&&id==86)state_.pen_snap=!state_.pen_snap;
    if(state_.PropertyTool()==Tool::Pen&&(id==58||id==59))state_.pen_mode=id==59?PenMode::Highlighter:PenMode::Normal;
    if(state_.PropertyTool()==Tool::Number&&state_.number_combo==NumberCombo::Text&&id>=72&&id<=77){state_.number_text_preset=-1;state_.number_text_color=*ToolbarColor(id);}
    if(id==79&&state_.PropertyTool()==Tool::Number&&state_.number_combo==NumberCombo::Text)state_.number_text_preset=-2;
    if(id==28||(id==70&&state_.PropertyTool()==Tool::Number&&state_.number_combo==NumberCombo::Text)) {
        state_.picker_text_color=id==70;state_.picker_original_preset=state_.number_text_preset;
        if(state_.picker_text_color)state_.number_text_preset=-1;
        RECT monitor=monitors_.front();const Box anchor=state_.toolbar.Property(id);const POINT center{LONG((anchor.left+anchor.right)/2),LONG((anchor.top+anchor.bottom)/2)};
        for(auto r:monitors_)if(PtInRect(&r,center)){monitor=r;break;}
        if(!recent_colors_.empty())state_.picker.recent=recent_colors_;
        state_.picker.Open(state_.PickerColor(),anchor,monitor,state_.toolbar.scale);

    }
    if(state_.PropertyTool()==Tool::Text){
        if(id==51)state_.text_bold=!state_.text_bold;
        if(id>=53&&id<=55)state_.text_align=static_cast<TextAlign>(id-53);
    }
    if(state_.PropertyTool()==Tool::Mosaic){
        if(id==60||id==61)state_.mosaic_mode=static_cast<MosaicMode>(id-60);
        if(id==62||id==63)state_.mosaic_method=static_cast<MosaicMethod>(id-62);
    }
    if(id==24||id==27||id==48||id==49||id==50||id==52||id==57||id==66||id==67||id==68||id==69||id==71||id==80) {
        const auto anchor=state_.toolbar.Property(id,state_.PropertyTool());
        const POINT center{LONG((anchor.left+anchor.right)/2),LONG((anchor.top+anchor.bottom)/2)};
        RECT monitor=toolbar_monitor_;
        for(const auto candidate:monitors_)if(PtInRect(&candidate,center)){monitor=candidate;break;}
        OpenToolbarDropdown(state_,id,monitor);
    }
    if(state_.PropertyTool()==Tool::Mosaic)state_.hint=state_.mosaic_method==MosaicMethod::Brush?L"马赛克 · 按住左键涂抹，单击也可遮挡 · 调整笔刷与强度 · Ctrl+Z 撤销":L"马赛克 · 拖动框选区域 · 调整强度 · Ctrl+Z 撤销";
    else if(state_.PropertyTool()==Tool::Pen)PenHint();
    else if(state_.PropertyTool()==Tool::Text)state_.hint=L"文字 · 单击输入，双击编辑 · Enter 完成 · Shift+Enter 换行";
    else if(state_.PropertyTool()==Tool::Number)state_.hint=state_.number_combo==NumberCombo::Leader?L"引导线 · 按下确定目标点，拖动放置序号或标签 · 双击序号编辑标签 · Ctrl+Z 撤销":L"序号 · 拖动设置组合范围 · 双击序号编辑标签，双击说明编辑文字 · Ctrl+Z 撤销";
    else state_.hint=L"Enter / Ctrl+C 复制 · Ctrl+S 保存 · Ctrl+Z 撤销 · Esc 取消";
    if(id>=20)ApplySelectedProperties(state_.picker.open);
    if(!line_vertices_.empty()){PolylinePreview(state_.pointer);PenHint();}
    RememberProperties();Invalidate();
}
bool Application::HasPropertyTarget()const{
    return document_.selected>=0&&static_cast<size_t>(document_.selected)<document_.marks.size()&&
        (state_.tool==Tool::Select||state_.tool==document_.marks[document_.selected].tool);
}
void Application::SyncSelectedProperties(){
    property_original_.reset();
    const Tool previous=state_.PropertyTool();state_.selected_tool=Tool::Select;
    if(HasPropertyTarget()){
        const auto& mark=document_.marks[document_.selected];state_.selected_tool=mark.tool;
        property_scale_=std::max(.1f,state_.toolbar.scale);
        static_cast<ToolProperties&>(state_)=PropertiesOfMark(mark,property_scale_,state_);
    }
    if(state_.tool!=Tool::Select)return;
    if(state_.selected_tool==Tool::Select&&previous!=Tool::Select)state_.closing_tool=previous;
    const auto now=GetTickCount64();toolbar_transition_.Target(state_.selected_tool==Tool::Select?0.f:1.f,now);
    BOOL animate=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);
    if(!animate)toolbar_transition_.Reset(state_.selected_tool==Tool::Select?0.f:1.f);
    AnimateToolbar();
    if(toolbar_transition_.Active(now)&&main_)SetTimer(main_,9,16,nullptr);
}
void Application::ApplySelectedProperties(bool preview){
    if(!line_vertices_.empty()&&draft_){
        draft_=WithMarkProperties(*draft_,state_,state_.toolbar.scale);return;
    }
    if(!HasPropertyTarget())return;
    auto& mark=document_.marks[document_.selected];
    auto changed=WithMarkProperties(mark,state_,property_scale_);
    if(preview){if(!property_original_)property_original_=mark;mark=std::move(changed);return;}
    if(property_original_){mark=*property_original_;property_original_.reset();}
    if(changed!=mark){document_.Checkpoint();mark=std::move(changed);}
}
void Application::ApplyToolbarDropdown(int index) {
    const bool font=state_.dropdown.property==80&&index>=0&&index<static_cast<int>(state_.dropdown.items.size());
    ChooseToolbarDropdown(state_,index);ApplySelectedProperties();
    RememberProperties();
    if(font&&document_.selected>=0&&static_cast<size_t>(document_.selected)<document_.marks.size()){
        auto& mark=document_.marks[document_.selected];
        if(mark.tool==Tool::Text&&mark.font_family!=state_.font_family){document_.Checkpoint();mark.font_family=state_.font_family;FitTextBounds(mark);}
    }
}
void Application::ResetSessionTool(){
    StopToolbarMotion();BOOL effects=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&effects,0);toolbar_effects_=effects!=FALSE;
    line_vertices_.clear();state_.polyline_confirmed=0;state_.polyline_active=false;state_.line_snap.reset();
    // Each new capture/pin-edit starts in selection, regardless of prior tools.
    state_.tool=Tool::Select;
    toolbar_transition_.Reset(0);
}
void Application::RememberProperties(){
    // Picker changes are previews until accepted. Keep canceled colors out of defaults.
    if(demo_||diagnostic_session_||!active_||state_.picker.open)return;
    const auto& tools=static_cast<const ToolProperties&>(state_);
    if(tools==preferences_.tools)return;
    preferences_.tools=tools;properties_dirty_=true;
    if(main_&&!SetTimer(main_,10,400,nullptr))DeferProperties();
}
void Application::FlushProperties(){
    if(main_)KillTimer(main_,10);
    if(!properties_dirty_||demo_||diagnostic_session_)return;
    properties_dirty_=false;
    settings_writer_.Request(preferences_);
    if(!settings_writer_.Flush())properties_dirty_=true; // Retry at session end.
}
void Application::DeferProperties(){
    // Debounced settings writes run on the persistence worker. The UI thread
    // never waits for the INI flush while annotating or drawing.
    if(!properties_dirty_||demo_||diagnostic_session_)return;
    properties_dirty_=false;
    settings_writer_.Request(preferences_);
}
void Application::ClosePicker(bool cancel) {
    auto& p=state_.picker;if(!p.open)return;
    if(cancel)state_.PickerColor()=p.original;else{p.Commit();state_.PickerColor()=p.color;p.Remember();recent_colors_=p.recent;}
    if(cancel&&state_.picker_text_color)state_.number_text_preset=state_.picker_original_preset;
    p.open=false;p.drag=-1;ApplySelectedProperties();ReleaseCapture();RememberProperties();Invalidate();
}
void Application::Key(View& view,WPARAM key) {
    if(state_.dropdown.Open()){
        auto& popup=state_.dropdown;
        if(key==VK_ESCAPE||key==VK_TAB)popup.Close();
        else if(key==VK_RETURN||key==VK_SPACE){ApplyToolbarDropdown(popup.hover);AnimateToolbar();}
        else if(key==VK_HOME)popup.hover=0;
        else if(key==VK_END)popup.hover=static_cast<int>(popup.items.size())-1;
        else if(key==VK_UP||key==VK_DOWN)popup.hover=std::clamp(popup.hover+(key==VK_UP?-1:1),0,static_cast<int>(popup.items.size())-1);
        Invalidate();return;
    }
    if(state_.picker.open){auto& p=state_.picker;
        if(key==VK_ESCAPE){ClosePicker(true);return;}
        if(key==VK_RETURN){if(p.field>=3){if(p.Commit())p.field=-1;}else ClosePicker();}
        if(key==VK_TAB){if(p.Commit())p.Focus(p.field<3||p.field==6?3:p.field+1);}
        if((key==VK_UP||key==VK_DOWN)&&p.field>=3)p.Step(p.field,key==VK_UP?1:-1);
        if((GetKeyState(VK_CONTROL)&0x8000)&&key=='A')p.replace=true;
        if((GetKeyState(VK_CONTROL)&0x8000)&&key=='V'&&p.field>=3&&OpenClipboard(view.window)){
            if(HANDLE data=GetClipboardData(CF_UNICODETEXT))if(const auto* text=static_cast<const wchar_t*>(GlobalLock(data))){
                const size_t maximum=std::min(size_t(8),GlobalSize(data)/sizeof(wchar_t));p.input.assign(text,wcsnlen_s(text,maximum));GlobalUnlock(data);p.replace=false;p.Commit();}
            CloseClipboard();
        }
        state_.PickerColor()=p.color;if(p.open)ApplySelectedProperties(true);Invalidate();return;
    }
    if(!state_.busy&&!line_vertices_.empty()){
        if(key==VK_ESCAPE){EndPolyline(true);return;}
        if(key==VK_RETURN){EndPolyline(false);return;}
        if(key==VK_BACK||((GetKeyState(VK_CONTROL)&0x8000)&&key=='Z')){UndoPolylinePoint();return;}
        if((GetKeyState(VK_CONTROL)&0x8000)&&key=='Y')return;
        if(key==VK_DELETE)return;
    }
    if(key==VK_ESCAPE){Cancel();return;}
    if(state_.busy)return;
    const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
    if(control&&key=='Z'){document_.Undo();SyncSelectedProperties();Invalidate();return;}
    if(control&&key=='Y'){document_.Redo();SyncSelectedProperties();Invalidate();return;}
    if(control&&key=='S'){Finish(true);return;}
    if(key==VK_RETURN||(control&&key=='C')){pin_edit_copy_=pin_edit_id_&&control&&key=='C';Finish(false);return;}
    if(key==VK_DELETE){document_.DeleteSelected();SyncSelectedProperties();Invalidate();return;}
    if(!state_.selected&&(key=='F'||key=='A')) {
        state_.selection=BoxOf(key=='A'?frame_->bounds:view.bounds);state_.selected=true;
        if(translate_capture_){translate_next_=true;Finish(false,true,true);if(!pending_)translate_next_=false;return;}
        UpdateToolbar({view.bounds.left,view.bounds.top});UpdateMagnifier();Invalidate();return;
    }
    if(state_.selected) {
        if(key=='V')Command(0);if(key=='R')Command(1);if(key=='E')Command(2);if(key=='A')Command(3);
        if(key=='P')Command(4);if(key=='T')Command(5);if(key=='M')Command(6);
        if(key=='L')Command(17);
    }
}

void Application::Finish(bool save,bool pin,bool recognize) {
    if(!state_.selected||state_.busy||pending_)return;EndPolyline(false);state_.line_snap.reset();CommitText();
    std::filesystem::path path=client_output_;
    if(!client_output_.empty()){save=false;pin=false;recognize=false;}
    if(pin_edit_id_==kImageEditId){
        // Export buttons in a long-image edit apply to the whole long image: finish
        // the edit here and let the viewer run the action afterwards.
        image_edit_follow_=save?9:pin&&recognize?15:pin?13:pin_edit_copy_?10:0;
        save=pin=recognize=false;pin_edit_copy_=false;
    }
    if(save) {
        SYSTEMTIME time{};GetLocalTime(&time);wchar_t filename[32768]{};
        swprintf_s(filename,L"LumaShot-%04u%02u%02u-%02u%02u%02u.png",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
        OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=Owner();dialog.lpstrFilter=L"PNG 图片 (*.png)\0*.png\0\0";
        dialog.lpstrFile=filename;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"png";
        dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        const auto folder=preferences_.save_directory.native();if(!folder.empty())dialog.lpstrInitialDir=folder.c_str();
        if(!GetSaveFileNameW(&dialog))return;path=filename;
        preferences_.save_directory=path.parent_path();
        if(!demo_&&!diagnostic_session_){settings_writer_.Request(preferences_);settings_writer_.Flush();}
    }
    recognize=ocr_available_&&(recognize||pin||pin_edit_id_!=0);
    state_.busy=true;pending_=true;Invalidate();
    const std::shared_ptr<const Frame> frame=pin_edit_id_?pin_edit_source_:frame_;
    auto document=pin_edit_id_?PinAnnotationDocument(document_,pin_edit_bounds_,*pin_edit_source_):document_.Snapshot();
    const RECT selection=pin_edit_id_?frame->bounds:PixelRect(state_.selection);
    const auto cursor_patch=cursor_patch_;
    const bool paste_file=preferences_.paste_as_file;const int file_format=preferences_.paste_file_format;
    const uint64_t generation=generation_,editing=pin_edit_id_;const bool edit_copy=pin_edit_copy_&&!save;
    const bool client_export=!client_output_.empty();
    worker_=std::jthread([this,frame,client_export,document=std::move(document),selection,path,generation,pin,recognize,cursor_patch,editing,edit_copy,paste_file,file_format](std::stop_token stop) {
        auto result=std::make_unique<Result>();result->generation=generation;result->editing=editing;result->edit_copy=edit_copy;result->recognize=recognize;
        const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        std::filesystem::path temporary;
        try {
            if(FAILED(com))throw std::runtime_error("Cannot initialize image export");
            Renderer renderer;result->frame=renderer.Flatten(*frame,document,selection);
            if(editing||pin){result->annotation_base=Crop(*frame,selection);result->annotations=PinAnnotationDocument(document,selection,*result->annotation_base);}
            if(pin&&!editing&&!stop.stop_requested()) {
                result->ocr_frame=PinOcrImage(*frame,cursor_patch.get(),document,selection);result->pinned=true;
            }
            if(path.empty()&&((!pin&&!editing)||edit_copy)&&!stop.stop_requested()){
                auto file=paste_file?PrepareClipboardFile(result->frame,file_format):nullptr;
                if(!stop.stop_requested())result->clipboard_image=PrepareClipboardImage(result->frame,file);
            }
            if(!path.empty()&&!stop.stop_requested()) {
                temporary=path;temporary+=L".lumashot-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(generation)+L".tmp";
                HANDLE reserved=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                CheckWin32(reserved!=INVALID_HANDLE_VALUE,"Create temporary PNG");CloseHandle(reserved);
                SavePng(result->frame,temporary);
                if(!stop.stop_requested()) {
                    CheckWin32(MoveFileExW(temporary.c_str(),path.c_str(),(client_export?0:MOVEFILE_REPLACE_EXISTING)|MOVEFILE_WRITE_THROUGH)!=FALSE,"Save PNG");
                    result->saved=true;
                }
            }
        }catch(const std::exception& e){result->error=ErrorText(e);}
        if(!temporary.empty())DeleteFileW(temporary.c_str());
        if(SUCCEEDED(com))CoUninitialize();
        {std::lock_guard lock(mutex_);result_=std::move(result);}
        PostMessageW(main_,kResult,0,0);
    });
}

LRESULT CALLBACK Application::OverlayProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* view=reinterpret_cast<View*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){view=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(view));}
    if(!view)return DefWindowProcW(window,message,wp,lp);
    auto* app=view->app;
    try {
        switch(message) {
        case WM_ERASEBKGND:return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};BeginPaint(window,&paint);
            try {if(app->frame_) {
                // The native popup owns the live text while editing, including when
                // the original object is rotated. Do not paint a second copy below it.
                const int editing_text=app->edit_&&!app->edit_badge_label_?app->edit_index_:-1;
                view->needs_paint=!view->renderer->Paint(window,view->bounds,*app->frame_,*app->acrylic_,app->document_,app->draft_,app->state_,editing_text);
                if(view->needs_paint&&!view->renderer->FrameEvent())InvalidateRect(window,nullptr,FALSE);
            }}
            catch(...){EndPaint(window,&paint);throw;}
            EndPaint(window,&paint);return 0;
        }
        case WM_LBUTTONDOWN:app->PointerDown(*view,GlobalPoint(view->bounds,lp));return 0;
        case WM_MOUSEMOVE:
            {TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);}
            if(Diagnostics::Get().Enabled())Diagnostics::Get().Add("input_age_ms",Diagnostics::Now(),static_cast<DWORD>(GetTickCount()-static_cast<DWORD>(GetMessageTime())));
            app->PointerMove(*view,GlobalPoint(view->bounds,lp),wp);return 0;
        case WM_MOUSEWHEEL: {
            if(app->state_.dropdown.Open()){app->Key(*view,GET_WHEEL_DELTA_WPARAM(wp)>0?VK_UP:VK_DOWN);return 0;}
            const Point point{float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            if(app->state_.picker.open){const int field=app->state_.picker.Hit(point);if(field>=3&&field<=6){app->state_.picker.Step(field,GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA);app->state_.PickerColor()=app->state_.picker.color;app->ApplySelectedProperties(true);app->Invalidate();}return 0;}
            const int id=ui::HitTest(ToolbarControls(app->state_,app->document_),point);
            const int steps=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*((GET_KEYSTATE_WPARAM(wp)&MK_SHIFT)?4:1);
            if(AdjustPropertyValue(app->state_,id,steps)){app->ApplySelectedProperties();app->RememberProperties();app->Invalidate();}
            return 0;
        }
        case WM_LBUTTONUP:app->PointerUp(*view,GlobalPoint(view->bounds,lp));return 0;
        case WM_LBUTTONDBLCLK:app->PointerDoubleClick(*view,GlobalPoint(view->bounds,lp));return 0;
        case WM_CHAR:if(app->state_.picker.open){app->state_.picker.Type(static_cast<wchar_t>(wp));app->state_.PickerColor()=app->state_.picker.color;app->ApplySelectedProperties(true);app->Invalidate();}return 0;
        case WM_MOUSELEAVE:{POINT cursor{};GetCursorPos(&cursor);if(!Contains(app->state_.toolbar.bounds,{float(cursor.x),float(cursor.y)})){app->state_.hover=-1;app->toolbar_pressed_=-1;app->TickToolbarMotion();}return 0;}
        case WM_KILLFOCUS:case WM_CANCELMODE:app->state_.hover=-1;app->toolbar_pressed_=-1;app->TickToolbarMotion();break;
        case WM_CAPTURECHANGED:app->toolbar_pressed_=-1;app->TickToolbarMotion();app->state_.picker.drag=-1;app->state_.property_drag=-1;if(app->property_original_&&!app->state_.picker.open)app->ApplySelectedProperties();app->RememberProperties();return 0;
        case WM_KEYDOWN:if((wp==VK_RETURN||wp==VK_ESCAPE)&&(lp&(LPARAM(1)<<30)))return 0;app->Key(*view,wp);return 0;
        case WM_RBUTTONUP:if(app->state_.dropdown.Open()){app->state_.dropdown.Close();app->Invalidate();}else if(app->state_.picker.open)app->ClosePicker(true);else if(!app->line_vertices_.empty())app->EndPolyline(false);else app->Cancel();return 0;
        case WM_CLOSE:case WM_DISPLAYCHANGE:PostMessageW(app->main_,kCancel,0,0);return 0;
        case WM_SETCURSOR: {
            POINT cursor{};GetCursorPos(&cursor);const Point p{float(cursor.x),float(cursor.y)};
            const auto& state=app->state_;
            const bool selecting=!state.selected||(!state.busy&&!state.picker.open&&
                !Contains(state.toolbar.bounds,p)&&!Contains(state.selection,p,7*state.toolbar.scale));
            LPCWSTR cursor_id=selecting?IDC_CROSS:(state.tool==Tool::Text&&Contains(state.selection,p)?IDC_IBEAM:IDC_ARROW);
            const auto edit_cursor=ui::SelectionEditCursor(app->document_,state,p,GetDpiForWindow(window),app->mark_moving_?app->mark_handle_:-1);
            SetCursor(edit_cursor?edit_cursor:LoadCursorW(nullptr,cursor_id));return TRUE;
        }
        case WM_DPICHANGED:PostMessageW(app->main_,kCancel,0,0);return 0;
        default:break;
        }
    }catch(const std::exception& error){app->Notice(ErrorText(error));PostMessageW(app->main_,kCancel,0,0);}
    return DefWindowProcW(window,message,wp,lp);
}

}












