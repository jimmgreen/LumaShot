#define LUMASHOT_RECORDING_UI_TEST
#include "../src/recording/worker.cpp"
#include <wincodec.h>
#include <iostream>
#include <psapi.h>
#include <dwmapi.h>
using namespace lumashot::recording;
namespace {
int failures{};void Expect(bool ok,const char* what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<std::endl;failures+=!ok;}
PreviewPixels Poster(){lumashot::DibSurface surface(640,360);auto dc=surface.Dc();RECT full{0,0,640,360};HBRUSH b=CreateSolidBrush(RGB(246,250,255));FillRect(dc,&full,b);DeleteObject(b);RECT sidebar{0,0,118,360};b=CreateSolidBrush(RGB(226,238,252));FillRect(dc,&sidebar,b);DeleteObject(b);HFONT font=CreateFontW(-22,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(31,48,71));TextOutW(dc,153,43,L"项目计划",4);SelectObject(dc,old);DeleteObject(font);font=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");old=SelectObject(dc,font);SetTextColor(dc,RGB(100,120,146));TextOutW(dc,153,83,L"让好的想法，变成看得见的成果。",16);for(int i=0;i<3;++i){RECT r{150+i*153,132,290+i*153,299};b=CreateSolidBrush(RGB(229,241,255));FillRect(dc,&r,b);DeleteObject(b);SetTextColor(dc,RGB(0,135,255));const wchar_t* icon=i==0?L"□":i==1?L"</>":L"↗";TextOutW(dc,205+i*153,160,icon,int(wcslen(icon)));SetTextColor(dc,RGB(31,48,71));const wchar_t* label=i==0?L"产品设计":i==1?L"开发实现":L"发布上线";TextOutW(dc,186+i*153,216,label,4);}const wchar_t* labels[]={L"首页",L"文档",L"图片",L"收藏",L"回收站"};for(int i=0;i<5;++i)TextOutW(dc,24,54+i*43,labels[i],int(wcslen(labels[i])));SelectObject(dc,old);DeleteObject(font);PreviewPixels p;p.width=640;p.height=360;p.pixels.assign(surface.Pixels(),surface.Pixels()+640*360);return p;}
void Snapshot(Ui& ui,const wchar_t* file){const auto size=PanelSize(ui.Model());SetWindowPos(ui.window,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ui.scale=1;ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Snapshot factory");ComPtr<IWICBitmap> bitmap;factory->CreateBitmap(size.cx,size.cy,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap);ComPtr<ID2D1RenderTarget> target;Check(ui.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Offscreen production renderer");target->BeginDraw();ui.buttons=DrawPanel(target.Get(),ui.write.Get(),ui.Model());Check(target->EndDraw(),"Render panel");for(const auto& b:ui.buttons)Expect(b.box.left>=0&&b.box.top>=0&&b.box.right<=size.cx&&b.box.bottom<=size.cy,"control within actual panel bounds");ComPtr<IWICStream> stream;factory->CreateStream(&stream);stream->InitializeFromFilename(file,GENERIC_WRITE);ComPtr<IWICBitmapEncoder> encoder;factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);ComPtr<IWICBitmapFrameEncode> frame;encoder->CreateNewFrame(&frame,nullptr);frame->Initialize(nullptr);frame->SetSize(size.cx,size.cy);WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;frame->SetPixelFormat(&format);frame->WriteSource(bitmap.Get(),nullptr);frame->Commit();encoder->Commit();}
}
namespace {
int menu_test_mode{};bool menu_seen{};
void CALLBACK MenuInput(HWND owner,UINT,UINT_PTR timer,DWORD){const auto menu=FindWindowW(L"LumaShot.SharedDropdown",nullptr);if(!menu)return;KillTimer(owner,timer);menu_seen=true;RECT outer{},inner{};GetWindowRect(menu,&outer);GetClientRect(menu,&inner);Expect(!(GetWindowLongPtrW(menu,GWL_STYLE)&(WS_CAPTION|WS_BORDER|WS_THICKFRAME))&&inner.right==outer.right-outer.left&&inner.bottom==outer.bottom-outer.top,"real dropdown has no nonclient border");Expect(GetWindow(menu,GW_OWNER)==owner,"dropdown belongs to recording window");if(menu_test_mode==0){SendMessageW(menu,WM_KEYDOWN,VK_DOWN,0);PostMessageW(menu,WM_KEYDOWN,VK_RETURN,0);}else if(menu_test_mode==1)PostMessageW(menu,WM_KEYDOWN,VK_ESCAPE,0);else PostMessageW(menu,WM_LBUTTONDOWN,0,MAKELPARAM(-10,-10));}
void SelectionPaintTest(Ui& u){WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Selection;wc.lpszClassName=L"LumaShot.SelectionPaintFixture";RegisterClassW(&wc);HWND w=CreateWindowW(wc.lpszClassName,L"Synthetic selection",WS_POPUP,0,0,640,360,nullptr,nullptr,wc.hInstance,&u);u.picking=true;u.drag=true;u.monitor={0,0,640,360};u.region={100,80,400,280};InvalidateRect(w,nullptr,FALSE);SendMessageW(w,WM_PAINT,0,0);auto* backing=u.selection_surface.get();Expect(backing&&(backing->Pixels()[150*640+200]&0xffffff)==0x010101,"selection interior stays color-key transparent in composed frame");u.region={120,90,430,290};InvalidateRect(w,nullptr,FALSE);SendMessageW(w,WM_PAINT,0,0);Expect(u.selection_surface.get()==backing,"drag reuses backing surface without repeated 4K allocation");Expect(SendMessageW(w,WM_ERASEBKGND,0,0)==1,"selection suppresses separate background erase");DestroyWindow(w);u.selection_surface.reset();u.picking=u.drag=false;}
void SelectionTextTest(Ui& u){
    u.picking=true;u.drag=true;u.selected=false;u.monitor={-640,-360,640,360};
    u.region={-440,-180,160,240};
    for(float dpi:{1.f,1.5f,2.f})for(int source:{0,1}){
        u.source=source;u.selection_surface.reset();const int width=1280,height=720;
        u.PaintSelection({0,0,width,height},dpi);const auto* pixels=u.selection_surface->Pixels();
        Expect((pixels[300*width+400]&0xffffff)==0x010101,"DPI-independent selection interior retains exact color key");
        Expect((pixels[300*width+200]&0xffffff)==0x008cff,"selection border stays at physical capture coordinates");
        size_t ink=0,outside=0;
        for(int y=0;y<160;++y)for(int x=0;x<width;++x){
            const auto color=pixels[y*width+x]&0xffffff;
            if(color!=0x121c2d){
                ++ink;
                if(x<int(32*dpi)||y<int(28*dpi)||y>=int(60*dpi))++outside;
            }
        }
        Expect(ink>100&&outside==0,"LumaText selection hint renders inside DPI-scaled DIP bounds");
        ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Selection snapshot factory");
        ComPtr<IWICBitmap> bitmap;Check(factory->CreateBitmapFromMemory(width,height,GUID_WICPixelFormat32bppBGRA,width*4,width*height*4,reinterpret_cast<BYTE*>(u.selection_surface->Pixels()),&bitmap),"Selection snapshot bitmap");
        const auto path=L"recording-selection-"+std::to_wstring(int(dpi*100))+L"-"+std::to_wstring(source)+L".png";
        ComPtr<IWICStream> stream;Check(factory->CreateStream(&stream),"Selection snapshot stream");Check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Selection snapshot file");
        ComPtr<IWICBitmapEncoder> encoder;Check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Selection snapshot encoder");Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Selection snapshot initialize");
        ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr),"Selection snapshot frame");Check(frame->Initialize(nullptr),"Selection snapshot frame initialize");Check(frame->WriteSource(bitmap.Get(),nullptr),"Selection snapshot pixels");Check(frame->Commit(),"Selection snapshot commit");Check(encoder->Commit(),"Selection snapshot finish");
    }
    u.picking=false;u.PaintSelection({0,0,1280,720},1);
    Expect((u.selection_surface->Pixels()[40*1280+40]&0xffffff)==0x010101,"completed selection clears the hint and preserves transparent surround");
    u.selection_surface.reset();u.drag=false;u.source=0;
}
void AlphaTest(Ui& u){lumashot::ConfigureBorderlessWindow(u.window);DWORD corner{};DwmGetWindowAttribute(u.window,33,&corner,sizeof(corner));Expect(corner==2,"recording uses settings DWM rounded corner policy");for(bool dark:{false,true})for(float scale:{1.f,1.25f,1.5f,2.f}){u.dark=dark;u.acrylic=true;u.status.state=State::Ready;const auto size=PanelSize(u.Model());SetWindowPos(u.window,nullptr,0,0,int(size.cx*scale),int(size.cy*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);u.scale=scale;lumashot::ResizeBorderlessWindow(u.window,scale,lumashot::ui::ToolPanelRadius);u.Paint();RECT r{};GetClientRect(u.window,&r);const auto* p=u.surface->Pixels();Expect((p[0]>>24)==0&&(p[r.right-1]>>24)==0&&(p[(r.bottom-1)*r.right]>>24)==0&&(p[r.bottom*r.right-1]>>24)==0,"all four layered corners remain fully transparent across DPI and theme");const auto alpha=p[(r.bottom-3)*r.right+r.right/2]>>24;Expect(alpha==1,"acrylic has only hit-test coverage and no extra translucent color layer");}u.scale=1;}
void LaunchRegionTest(Ui& u){
    for(RECT r:std::initializer_list<RECT>{{-1900,-900,-1000,-400},{20,40,340,220},{-2147483647L-1,0,-2147483600L,8}}){
        const auto parsed=ParseRecordingRegion(std::wstring_view(RecordingRegionArgument(r)).substr(10));
        Expect(parsed&&EqualRect(&*parsed,&r),"recording region arguments round-trip signed physical coordinates");
    }
    for(const wchar_t* bad:{L"",L"0,0,7,8",L"8,0,0,8",L"0,0,8,8,9",L"0,0,8",L"0,0,8,8junk",L"2147483648,0,2147483656,8",L"-2147483648,0,2147483647,8",L"０,0,8,8",L"0,0,999999999999999999999999,8"})Expect(!ParseRecordingRegion(bad),"malformed, too-small and overflowing region arguments are rejected");
    const auto clipped=ClipRecordingRegion({-1200,-300,-800,100},{-1000,-500,0,500});RECT expected{-1000,-300,-800,100};
    Expect(clipped&&EqualRect(&*clipped,&expected),"cross-monitor region clips correctly with negative origins");
    Expect(!ClipRecordingRegion({2000,0,2100,100},{-1000,0,0,500}),"offscreen launch region is not silently recorded");
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Selection;wc.lpszClassName=L"LumaShot.RecordingSelection";RegisterClassW(&wc);
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(u.window,MONITOR_DEFAULTTONEAREST),&info);const RECT m=info.rcMonitor;
    const RECT requested{m.left+30,m.top+40,m.left+350,m.top+220};
    for(bool gif:{false,true}){
        u.status={};u.gif=gif;u.fps=gif?15:30;u.selected=false;u.source=1;u.notice.clear();u.countdown=0;
        Expect(u.InitialRegion(requested)&&u.selected&&!u.picking&&u.source==0&&!u.target&&EqualRect(&u.region,&requested),"screenshot selection initializes ready region without changing physical bounds");
        Expect(u.status.state==State::Ready&&!u.session&&u.countdown==0,"region handoff creates no capture, countdown or encoder");
        if(gif)Expect(u.fps==15,"GIF region handoff retains GIF defaults");
        DWORD affinity{};Expect(GetWindowDisplayAffinity(u.overlay,&affinity)&&affinity==WDA_EXCLUDEFROMCAPTURE,"selection border is excluded from capture");
        Snapshot(u,gif?L"recording-region-gif.png":L"recording-region-video.png");DestroyWindow(u.overlay);u.overlay=nullptr;u.selection_surface.reset();
    }
    u.gif=false;u.selected=false;Expect(u.InitialRegion({m.left-100,m.top+40,m.left+350,m.top+220})&&!u.notice.empty()&&PanelSize(u.Model()).cy==234,"clipped cross-screen selection has a visible preparation notice");
    Snapshot(u,L"recording-region-clipped.png");DestroyWindow(u.overlay);u.overlay=nullptr;u.selection_surface.reset();u.notice.clear();u.Size(0,0);
}
void MenuTest(Ui& u){u.status.state=State::Ready;u.gif=true;u.fps=15;u.width=0;Snapshot(u,L"recording-gif-original.png");for(menu_test_mode=0;menu_test_mode<3;++menu_test_mode){menu_seen=false;SetTimer(u.window,99,30,MenuInput);u.Popup(12);KillTimer(u.window,99);Expect(menu_seen,"real shared popup opens from recording control");Expect(u.fps==20,"keyboard selection commits and dismissal preserves value");Expect(!FindWindowW(L"LumaShot.SharedDropdown",nullptr),"dropdown destroys window and releases capture on close");}Expect(!u.session,"dropdown never starts a capture session");}
}
int PreviewPerformance(const std::filesystem::path& input){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=0;
    try{
        Ui ui;ui.gif=false;ui.fps=60;
        ui.folder=std::filesystem::current_path()/(L"preview-ui-fixture-"+std::to_wstring(GetCurrentProcessId()));
        if(!std::filesystem::create_directory(ui.folder))throw std::runtime_error("Preview fixture directory already exists");
        ui.temporary=ui.folder/L"capture.mp4";std::filesystem::copy_file(input,ui.temporary);
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.lpszClassName=L"LumaShot.PreviewPerf";RegisterClassW(&wc);
        ui.window=CreateWindowExW(WS_EX_LAYERED,wc.lpszClassName,L"Synthetic preview benchmark",WS_POPUP,0,0,640,526,nullptr,nullptr,wc.hInstance,&ui);
        wc.lpfnWndProc=Ui::Video;wc.lpszClassName=L"LumaShot.PreviewPerfVideo";RegisterClassW(&wc);
        ui.video=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP,0,0,1,1,ui.window,nullptr,wc.hInstance,nullptr);
        ui.screen=MonitorFromWindow(ui.window,MONITOR_DEFAULTTONEAREST);ui.status.state=State::Preview;ui.status.time=610000000LL;
        const auto start=GetTickCount64();ui.Preview();const auto enter=GetTickCount64()-start;
        long long poster=-1,ready=-1;
        while(GetTickCount64()-start<15000&&(poster<0||ready<0)){
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            if(poster<0&&ui.images&&!ui.images->poster.pixels.empty())poster=static_cast<long long>(GetTickCount64()-start);
            if(ready<0&&ui.player_ready)ready=static_cast<long long>(GetTickCount64()-start);
            MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        std::cout<<"ui_enter_ms="<<enter<<" poster_ms="<<poster<<" player_ready_ms="<<ready<<std::endl;
        result=poster<0||ready<0?1:0;ui.StopPreview();DestroyWindow(ui.window);ui.window=nullptr;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;result=1;}
    CoUninitialize();return result;
}
int Mp4ProgressUi(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{Ui ui;WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.lpszClassName=L"LumaShot.Mp4ProgressFixture";RegisterClassW(&wc);
        ui.window=CreateWindowExW(WS_EX_LAYERED,wc.lpszClassName,L"Synthetic MP4 progress",WS_POPUP,0,0,544,420,nullptr,nullptr,wc.hInstance,&ui);
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,ui.factory.GetAddressOf());DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(ui.write.GetAddressOf()));
        ui.status.state=State::Preview;ui.status.time=200000000;ui.gif=false;ui.exporting=true;ui.images=std::make_unique<PreviewImages>();ui.images->poster=Poster();
        int image=0;for(bool dark:{false,true})for(const wchar_t* stage:{L"压缩 MP4",L"校验画质",L"重新压缩 · 第 3 次",L"写入文件"}){
            ui.dark=dark;ui.export_stage=stage;ui.progress=std::wstring(stage)==L"写入文件"?-1:50;
            Expect(ui.Model().export_stage==stage&&ui.Model().progress==ui.progress,"worker model preserves phase label and local or indeterminate progress");
            Snapshot(ui,(L"build/mp4-performance/ui-"+std::to_wstring(image++)+L".png").c_str());
            Expect(std::any_of(ui.buttons.begin(),ui.buttons.end(),[](const auto& b){return b.id==31;}),"cancel remains available in every export phase");
        }
        ui.exporting=false;DestroyWindow(ui.window);ui.window=nullptr;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}
    CoUninitialize();return failures?1:0;
}
#include "recording_duration_test.h"
#include "recording_tab_motion_test.h"
#include "recording_polish_test.h"
#include "recording_quality_test.h"
#include "recording_playback_test.h"
int main(int argc,char** argv){if(argc==2&&std::string(argv[1])=="--playback")return RecordingPlaybackTest();if(argc==2&&std::string(argv[1])=="--quality")return RecordingQualityTest();if(argc==2&&std::string(argv[1])=="--panel-polish")return RecordingPolishTest();if(argc==2&&std::string(argv[1])=="--tab-motion")return RecordingTabTest(false);if(argc==2&&std::string(argv[1])=="--tab-preview")return RecordingTabTest(true);if(argc==2&&std::string(argv[1])=="--gif-duration-policy")return GifDurationTest(true);if(argc==2&&std::string(argv[1])=="--gif-duration")return GifDurationTest(false);if(argc==2&&std::string(argv[1])=="--mp4-progress")return Mp4ProgressUi();if(argc==3&&std::string(argv[1])=="--preview-perf")return PreviewPerformance(argv[2]);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{Ui ui;ui.dark=false;WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.lpszClassName=L"LumaShot.RecordingUiTest";RegisterClassW(&wc);ui.window=CreateWindowExW(WS_EX_LAYERED,wc.lpszClassName,L"Synthetic UI",WS_POPUP,0,0,544,158,nullptr,nullptr,wc.hInstance,&ui);ui.fps=30;ui.width=1920;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,ui.factory.GetAddressOf());DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(ui.write.GetAddressOf()));ui.selected=true;ui.region={0,0,960,540};Snapshot(ui,L"recording-ready-light.png");SendMessageW(ui.window,WM_LBUTTONUP,0,MAKELPARAM(270,25));Expect(ui.gif&&ui.fps==15,"actual mouse message selects GIF defaults");Expect(!ui.session,"preparation has no capture session");Snapshot(ui,L"recording-gif-light.png");ui.dark=true;Snapshot(ui,L"recording-gif-dark.png");ui.status.state=State::Recording;ui.status.time=80000000;Snapshot(ui,L"recording-active-dark.png");Expect(PanelSize(ui.Model()).cy==48,"recording collapses to single compact bar");ui.status.state=State::Paused;Snapshot(ui,L"recording-paused-dark.png");ui.status.state=State::Preview;ui.trim_begin=20000000;ui.trim_end=80000000;ui.images=std::make_unique<PreviewImages>();ui.images->poster=Poster();ui.images->thumbnails.assign(8,ui.images->poster);Snapshot(ui,L"recording-preview-dark.png");ui.dark=false;Snapshot(ui,L"recording-preview-light.png");SendMessageW(ui.window,WM_LBUTTONDOWN,0,MAKELPARAM(148,339));SendMessageW(ui.window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(214,339));SendMessageW(ui.window,WM_LBUTTONUP,0,MAKELPARAM(214,339));Expect(ui.trim_begin==30000000&&ui.trim_end==80000000,"timeline handle updates real export range");ui.gif=false;Snapshot(ui,L"recording-video-preview-light.png");ui.status.state=State::Ready;ui.Start();Expect(ui.status.state==State::Starting&&!ui.session&&ui.countdown==3,"countdown creates no capture or encoder");KillTimer(ui.window,1);
const auto fixture=std::filesystem::current_path()/L"recording-test-output"/L"synthetic.mp4";
if(std::filesystem::exists(fixture)){
    ui.folder=std::filesystem::current_path()/L"recording-ui-test-temp";std::filesystem::create_directories(ui.folder);ui.temporary=ui.folder/L"capture.mp4";std::filesystem::copy_file(fixture,ui.temporary,std::filesystem::copy_options::overwrite_existing);
    WNDCLASSW video_class{};video_class.hInstance=GetModuleHandleW(nullptr);video_class.lpfnWndProc=Ui::Video;video_class.lpszClassName=L"LumaShot.PreviewFixture";RegisterClassW(&video_class);ui.video=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,video_class.lpszClassName,L"",WS_POPUP,0,0,300,160,ui.window,nullptr,video_class.hInstance,nullptr);
    ui.screen=MonitorFromWindow(ui.window,MONITOR_DEFAULTTONEAREST);ui.status.state=State::Preview;ui.status.time=15000000;ui.Preview();
    const auto pump=[](DWORD ms){const auto start=GetTickCount64();while(GetTickCount64()-start<ms){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}};
    for(int i=0;i<100&&(!ui.player_ready||!ui.images);++i)pump(50);Expect(ui.player!=nullptr&&ui.player_ready,"real preview player initializes against recorded MP4");Expect(ui.images&&!ui.images->poster.pixels.empty()&&ui.images->thumbnails.empty(),"MP4 preview decodes a poster without unused thumbnails");ui.Action(22);for(int i=0;i<100&&ui.player;++i){MFP_MEDIAPLAYER_STATE current{};ui.player->GetState(&current);if(current==MFP_MEDIAPLAYER_STATE_PLAYING)break;pump(20);}MFP_MEDIAPLAYER_STATE state{};if(ui.player)ui.player->GetState(&state);Expect(state==MFP_MEDIAPLAYER_STATE_PLAYING&&IsWindowVisible(ui.video),"play action uses the live video surface");ui.Action(22);pump(100);Expect(!ui.playing&&IsWindowVisible(ui.video),"pause retains playback surface and stops refresh timer");
}
// Exercise the real export completion message; a modal success dialog would
// hang the test and trigger its CTest timeout rather than silently pass.
ui.status.state=State::Preview;ui.export_as_gif=false;ui.export_error.clear();ui.exporting=true;
SendMessageW(ui.window,ExportMessage,1,0);
Expect(ui.saved&&!ui.exporting&&ui.notice==L"视频已保存","successful save uses a nonmodal in-panel notice");
ui.export_result=Mp4ExportResult{Mp4Encoding::Av1,1000,300};ui.ShowSavedNotice();
Expect(ui.notice.find(L"AV1")!=std::wstring::npos&&ui.notice.find(L"70%")!=std::wstring::npos,"save notice reports codec and measured size reduction");
ui.export_result=Mp4ExportResult{Mp4Encoding::Original,1000,1000};ui.ShowSavedNotice();
Expect(ui.notice.find(L"未压缩")!=std::wstring::npos,"uncompressed fallback is visible rather than silently reported as optimized");
ui.export_result=Mp4ExportResult{Mp4Encoding::Av1,1000,300};ui.ShowSavedNotice();
ui.images=std::make_unique<PreviewImages>();ui.images->poster=Poster();
ui.dark=false;ui.gif=false;Snapshot(ui,L"recording-saved-light.png");
ui.dark=true;Snapshot(ui,L"recording-saved-dark.png");
ui.gif=true;ui.export_as_gif=true;ui.ShowSavedNotice();Snapshot(ui,L"recording-gif-saved-dark.png");
Expect(ui.notice==L"GIF 已保存","GIF save notice matches exported format");
SendMessageW(ui.window,WM_TIMER,4,0);Expect(ui.notice.empty(),"save notice expires without user confirmation");
ui.pending_images=std::make_unique<PreviewImages>();ui.pending_images->poster=Poster();
const auto* current=ui.images.get();SendMessageW(ui.window,PosterMessage,ui.preview_generation+1,0);
Expect(ui.images.get()==current,"stale preview messages cannot replace current recording");
ui.StopPreview();
ui.status.state=State::Preview;ui.pending_images=std::make_unique<PreviewImages>();ui.pending_images->poster=Poster();
ui.preview_loader=std::jthread([]{Sleep(250);});
const auto publishStart=GetTickCount64();SendMessageW(ui.window,PosterMessage,ui.preview_generation,0);
Expect(GetTickCount64()-publishStart<200,"poster publication does not join the active thumbnail loader");
ui.StopPreview();
MenuTest(ui);AlphaTest(ui);SelectionPaintTest(ui);SelectionTextTest(ui);LaunchRegionTest(ui);
for(int i=0;i<8;++i)ui.Paint();PROCESS_MEMORY_COUNTERS_EX warm{},after{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&warm),sizeof(warm));for(int i=0;i<60;++i)ui.Paint();GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after));Expect(after.PrivateUsage<warm.PrivateUsage+16*1024*1024,"LumaText repeated panel paints keep bounded memory");
FILETIME created{},exited{},kernelA{},userA{},kernelB{},userB{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelA,&userA);const auto idleStart=GetTickCount64();while(GetTickCount64()-idleStart<1000){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelB,&userB);const auto ticks=[](FILETIME t){return (ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime;};const auto cpu=ticks(kernelB)+ticks(userB)-ticks(kernelA)-ticks(userA);Expect(cpu<1000000,"idle recording panel consumes under 100ms CPU per second");std::cout<<"Panel private MiB="<<double(after.PrivateUsage)/(1024*1024)<<" warm growth MiB="<<(double(after.PrivateUsage)-double(warm.PrivateUsage))/(1024*1024)<<" idle CPU ms="<<cpu/10000.<<std::endl;
DestroyWindow(ui.window);ui.window=nullptr;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}CoUninitialize();return failures?1:0;}






