#pragma once
#include "recording_preview_dialog_test.h"
int RecordingPlaybackTest(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Playback COM");
    try{
        Ui u;WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.lpszClassName=L"LumaShot.PlaybackFixture";RegisterClassW(&wc);
        u.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Synthetic playback fixture",WS_POPUP,0,0,640,550,nullptr,nullptr,wc.hInstance,&u);
        wc.lpfnWndProc=Ui::Video;wc.lpszClassName=L"LumaShot.PlaybackVideoFixture";RegisterClassW(&wc);
        u.video=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP,0,0,1,1,u.window,nullptr,wc.hInstance,nullptr);
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,u.factory.GetAddressOf());DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(u.write.GetAddressOf()));
        const auto pump=[](DWORD ms){const auto start=GetTickCount64();while(GetTickCount64()-start<ms){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}};
        u.folder=std::filesystem::current_path()/L"playback-test-temp";std::filesystem::create_directories(u.folder);u.temporary=u.folder/L"capture.mp4";
        {Session fixture;Options options;options.synthetic=true;options.synthetic_frames=45;options.synthetic_size={640,360};options.width=320;options.software=true;options.system_audio=false;
            std::atomic<bool> done{};Status result;
            fixture.Start(options,u.temporary,[&](Status value){if(value.state==State::Preview||value.state==State::Failed){result=std::move(value);done=true;}});
            for(int i=0;i<1000&&!done;++i)pump(20);
            if(!done||result.state!=State::Preview)throw std::runtime_error("Synthetic playback fixture failed");}

        u.screen=MonitorFromWindow(u.window,MONITOR_DEFAULTTONEAREST);u.status.state=State::Preview;u.status.time=15000000;u.Preview();SetForegroundWindow(u.window);
        const auto settled=[&]{for(int i=0;i<200&&(!u.player_ready||u.seek_inflight||u.seek_pending);++i)pump(20);pump(100);};
        settled();Expect(u.player_ready&&IsWindowVisible(u.video),"paused first frame uses real video surface");
        const auto pixel=[&]() -> COLORREF {u.LayoutVideo();pump(100);DwmFlush();RECT r{};GetWindowRect(u.video,&r);POINT point{(r.left+r.right)/2,(r.top+r.bottom)/2};if(WindowFromPoint(point)!=u.video)return CLR_INVALID;HDC dc=GetDC(nullptr);const auto color=GetPixel(dc,point.x,point.y);ReleaseDC(nullptr,dc);return color;};
        u.Seek(1000000);settled();const auto first=pixel();u.Seek(8000000);settled();const auto second=pixel();
        std::cout<<"paused pixels="<<first<<","<<second<<std::endl;
        Expect(first!=CLR_INVALID&&second!=CLR_INVALID&&first!=second,"paused seeking changes actual rendered frame pixels");
        PROPVARIANT position{};u.player->GetPosition(MFP_POSITIONTYPE_100NS,&position);Expect(std::abs(position.hVal.QuadPart-8000000)<500000,"paused seek reaches requested media time");PropVariantClear(&position);
        u.Seek(0);settled();u.Action(22);pump(450);Expect(u.playing&&u.position>=2000000,"play continuously advances beyond asynchronous startup");
        u.Action(22);pump(100);const auto paused=u.position;pump(180);Expect(!u.playing&&u.position==paused&&IsWindowVisible(u.video),"pause retains current frame and stops progress updates");
        const auto mouse=[&](UINT message,int x){SendMessageW(u.window,message,MK_LBUTTON,MAKELPARAM(int(std::round(x*u.scale)),int(std::round(389*u.scale))));};
        u.Paint();mouse(WM_LBUTTONDOWN,80);mouse(WM_MOUSEMOVE,400);
        const auto dragged=u.position;Expect(u.seek_drag&&dragged>8000000,"drag updates progress before mouse release");
        SendMessageW(u.window,WM_MOUSELEAVE,0,0);Expect(u.seek_drag&&GetCapture()==u.window,"drag keeps mouse capture outside the track");
        for(int x=100;x<=500;x+=10)mouse(WM_MOUSEMOVE,x);
        mouse(WM_LBUTTONUP,500);settled();u.player->GetPosition(MFP_POSITIONTYPE_100NS,&position);
        Expect(!u.seek_drag&&!u.seek_pending&&!u.seek_inflight&&std::abs(position.hVal.QuadPart-u.position)<500000,"rapid drag settles on latest requested frame");PropVariantClear(&position);
        u.Action(22);pump(600);Expect(!u.playing&&u.position==u.status.time,"end of playback stops cleanly");u.Action(22);pump(350);Expect(u.playing&&u.position>1000000&&u.position<7000000,"play from end restarts and advances");
        PreviewDialogTest(u);u.PausePreview();u.StopPreview();DestroyWindow(u.window);u.window=nullptr;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}
    CoUninitialize();return failures?1:0;
}
