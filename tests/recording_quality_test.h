#pragma once
int RecordingQualityTest(){
    Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Quality test COM");
    try{
        Ui u;u.selected=true;u.region={0,0,1920,1080};u.fps=30;u.width=1280;
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.lpszClassName=L"LumaShot.QualityFixture";RegisterClassW(&wc);
        u.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Synthetic quality fixture",WS_POPUP,0,0,544,220,nullptr,nullptr,wc.hInstance,&u);
        struct Cleanup{Ui& u;~Cleanup(){DestroyWindow(u.window);u.window=nullptr;}} cleanup{u};
        for(int preset=0;preset<3;++preset){
            u.ApplySetting(15,preset);const auto options=u.CaptureOptions();
            Expect(options.quality==preset&&options.width==1280&&options.fps==30,"video quality reaches capture without changing independent size or fps");
        }
        u.SetMode(true);Expect(u.width==960&&u.fps==15,"GIF starts balanced");
        for(int preset=0;preset<3;++preset){
            u.ApplySetting(15,preset);const auto config=GifQualityPreset(preset);
            Expect(u.width==config.width&&u.fps==config.fps&&u.gif_preset==preset,"GIF preset updates export size and fps together");
            Expect(u.CaptureOptions().fps==25&&u.CaptureOptions().width==0&&u.CaptureOptions().quality==2,"GIF source retains detail and frames for changing preset after capture");
        }
        u.ApplySetting(13,1280);u.ApplySetting(12,20);Expect(u.gif_preset==-1,"manual export adjustments show custom preset");
        u.SetMode(false);Expect(u.width==1280&&u.fps==30&&u.quality==2,"video settings survive GIF switch");
        u.SetMode(true);Expect(u.width==1280&&u.fps==20&&u.gif_preset==-1,"custom GIF settings survive mode switch");
        u.ApplyGifPreset(1);u.status.state=State::Preview;u.recorded_size={1920,1080};u.trim_end=100000000;
        const auto full=u.SizeEstimate();u.trim_end=50000000;Expect(u.SizeEstimate()!=full,"trimming GIF immediately updates estimated size");
        u.ApplyGifPreset(0);Expect(u.SizeEstimate()!=full,"GIF dimensions and fps affect estimate");
        u.SetMode(false);u.recorded_bytes=1234567;Expect(u.SizeEstimate().find(L"1.3 MB")!=std::wstring::npos,"high-quality preview reports actual source bytes");
        for(bool gif:{false,true})for(bool dark:{false,true})for(bool preview:{false,true}){
            u.SetMode(gif);u.dark=dark;u.status.state=preview?State::Preview:State::Ready;u.trim_begin=0;u.trim_end=u.status.time=100000000;
            u.images=std::make_unique<PreviewImages>();u.images->poster=Poster();u.images->thumbnails.assign(8,u.images->poster);
            const auto state=u.Model();
            for(float dpi:{1.f,1.5f,2.f}){
                const auto file=dpi==1?L"quality-"+std::wstring(gif?L"gif-":L"video-")+(dark?L"dark-":L"light-")+(preview?L"preview.png":L"ready.png"):L"";
                const auto rendered=PolishRender(state,dpi,file);
                const auto size=PanelSize(state);
                for(const auto& b:rendered.buttons)Expect(b.box.left>=0&&b.box.top>=0&&b.box.right<=size.cx&&b.box.bottom<=size.cy,"quality controls fit panel at all tested DPI scales");
                const bool has=std::any_of(rendered.buttons.begin(),rendered.buttons.end(),[](const PanelButton& b){return b.id==15;});
                Expect(has==(!preview||gif),"quality can be edited only before video capture or in GIF export preview");
                if(preview&&!gif&&dpi==1){
                    u.buttons=rendered.buttons;u.scale=1;u.position=u.status.time/2;
                    SendMessageW(u.window,WM_LBUTTONUP,0,MAKELPARAM(16,389));Expect(u.position==0,"video seek starts at visible track origin");
                    SendMessageW(u.window,WM_LBUTTONUP,0,MAKELPARAM(size.cx-16,389));Expect(u.position==u.status.time,"video seek reaches visible track end");
                    Expect(std::count_if(rendered.buttons.begin(),rendered.buttons.end(),[](const PanelButton& b){return b.id==22;})==2,"video preview keeps a separate play/pause control below the picture");
                }
            }
        }
        u.gif=false;u.status={};u.quality=1;u.Size(0,0);menu_test_mode=0;menu_seen=false;SetTimer(u.window,99,30,MenuInput);u.Popup(15);KillTimer(u.window,99);
        Expect(menu_seen&&u.quality==2,"real quality dropdown commits high-quality selection");
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}
    CoUninitialize();return failures?1:0;
}

