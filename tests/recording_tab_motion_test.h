#pragma once
// Included by recording_ui_test.cpp to exercise the actual worker, not a mock.
struct TabRenderResult {std::vector<uint32_t> pixels;std::vector<PanelButton> buttons;};
TabRenderResult TabRender(const PanelState& state,float dpi,const std::wstring& file={}){
    const UINT width=UINT(600*dpi),height=UINT(220*dpi);
    ComPtr<IWICImagingFactory> wic;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"Tab WIC");
    ComPtr<IWICBitmap> bitmap;Check(wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Tab bitmap");
    ComPtr<ID2D1Factory> factory;Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Tab shapes");
    ComPtr<IDWriteFactory> fonts;Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(fonts.GetAddressOf())),"Tab fonts");
    ComPtr<ID2D1RenderTarget> target;Check(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE),&target),"Tab target");
    target->SetDpi(96*dpi,96*dpi);target->SetTransform(D2D1::Matrix3x2F::Translation((600-PanelSize(state).cx)/2.f,30));
    target->BeginDraw();TabRenderResult result;result.buttons=DrawPanel(target.Get(),fonts.Get(),state);Check(target->EndDraw(),"Tab render");
    result.pixels.resize(size_t(width)*height);Check(bitmap->CopyPixels(nullptr,width*4,width*height*4,reinterpret_cast<BYTE*>(result.pixels.data())),"Tab pixels");
    if(!file.empty()){
        ComPtr<IWICStream> stream;Check(wic->CreateStream(&stream),"Tab stream");Check(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE),"Tab file");
        ComPtr<IWICBitmapEncoder> encoder;Check(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Tab PNG");Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Tab encode");
        ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr),"Tab frame");Check(frame->Initialize(nullptr),"Tab initialize");Check(frame->WriteSource(bitmap.Get(),nullptr),"Tab write");Check(frame->Commit(),"Tab frame commit");Check(encoder->Commit(),"Tab commit");
    }
    return result;
}
int RecordingTabTest(bool preview){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{
        const auto boxes=TabSlots();
        for(bool gif:{false,true})for(int hot:{-1,40,1,2}){
            lumashot::ui::ToolbarMotion motion;const int selected=gif?2:3,next=gif?3:2;
            motion.Update(boxes,selected,-1,-1,0,true);motion.Update(boxes,next,TabSlot(hot),next,100,true);
            Expect(motion.Sample(100).indicator==boxes[size_t(selected)],"tab begins at visible previous slot");
            const auto stretch=motion.Sample(210).indicator;Expect(stretch.right-stretch.left>150,"tab stretches before it contracts");
            const auto prior=motion.Sample(240).indicator;motion.Update(boxes,selected,-1,-1,240,true);
            Expect(motion.Sample(240).indicator==prior,"rapid reverse retargets without a jump");
            Expect(!motion.Active(1000)&&motion.Sample(1000).indicator==boxes[size_t(selected)],"tab settles exactly without idle animation");
            motion.Update(boxes,next,TabSlot(hot),next,1010,false);Expect(!motion.Active(1010)&&motion.Sample(1010).indicator==boxes[size_t(next)],"reduced motion snaps selection and feedback");
            motion.Reset();Expect(!motion.Sample(1100).ready&&!motion.Active(1100),"reset clears tab motion");
        }
        for(bool dark:{false,true})for(float dpi:{1.f,1.25f,1.5f,2.f}){
            PanelState state;state.dark=dark;state.gif=true;state.fps=15;const auto baseline=TabRender(state,dpi);
            lumashot::ui::ToolbarMotion motion;motion.Update(boxes,3,-1,-1,0,true);motion.Update(boxes,2,-1,-1,100,true);
            state.tab_motion=motion.Sample(210);const auto mid=TabRender(state,dpi);state.tab_motion=motion.Sample(500);const auto end=TabRender(state,dpi);
            Expect(mid.pixels!=end.pixels,"production renderer visibly animates in both themes at every DPI");
            Expect(end.pixels==baseline.pixels,"settled tab retains original static appearance");
            bool same=mid.buttons.size()==end.buttons.size();for(size_t i=0;same&&i<mid.buttons.size();++i){const auto a=mid.buttons[i],b=end.buttons[i];same=a.id==b.id&&a.box.left==b.box.left&&a.box.top==b.box.top&&a.box.right==b.box.right&&a.box.bottom==b.box.bottom;}
            Expect(same,"animated paint never changes any hit rectangle");Expect(end.pixels.front()==0,"layered corners remain transparent");
        }
        {
            Ui u;WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.lpszClassName=L"LumaShot.TabMotionFixture";RegisterClassW(&wc);
            u.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE,wc.lpszClassName,L"Synthetic recording tab fixture",WS_POPUP,-20000,-20000,544,158,nullptr,nullptr,wc.hInstance,&u);
            Check(u.window?S_OK:E_FAIL,"Tab owner window");ShowWindow(u.window,SW_SHOWNOACTIVATE);u.tab_effects=true;u.screen=MonitorFromWindow(u.window,MONITOR_DEFAULTTONEAREST);u.Size(0,0);u.Paint();RECT before{};GetWindowRect(u.window,&before);
            SendMessageW(u.window,WM_MOUSEMOVE,0,MAKELPARAM(int(270*u.scale),int(22*u.scale)));SendMessageW(u.window,WM_LBUTTONDOWN,0,MAKELPARAM(int(270*u.scale),int(22*u.scale)));
            Expect(u.tab_pressed==1&&GetCapture()==u.window&&u.tab_timer,"real tab pointer down owns press and animation timer");
            SendMessageW(u.window,WM_LBUTTONUP,0,MAKELPARAM(int(270*u.scale),int(22*u.scale)));
            // GIF defaults follow the balanced preset: 960 px at 15 fps (recording/quality.h GifQualityPreset(1)).
            Expect(u.gif&&u.fps==15&&u.width==960&&!u.session&&u.tab_pressed==-1,"real mouse action preserves GIF defaults without creating capture");
            RECT after{};GetWindowRect(u.window,&after);Expect(before.top==after.top&&std::abs((before.left+before.right)-(after.left+after.right))<=2,"mode resize preserves the tab row screen anchor");
            SetWindowPos(u.window,nullptr,after.left+12,after.top-20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);GetWindowRect(u.window,&before);
            u.Action(2);GetWindowRect(u.window,&after);Expect(before.top==after.top&&std::abs((before.left+before.right)-(after.left+after.right))<=2,"mode switch preserves a manually moved panel anchor");Expect(!u.gif&&u.tab_timer&&!u.session,"real reverse mode action animates without encoder work");
            u.SyncTabs(GetTickCount64()+1000);Expect(!u.tab_timer,"owner kills timer at rest");
            u.tab_effects=false;u.Action(1);Expect(!u.tab_timer&&!u.Model().tab_motion.effects,"owner respects disabled animations");
            u.tab_effects=true;u.Action(2);SendMessageW(u.window,WM_KILLFOCUS,0,0);Expect(!u.tab_timer&&u.tab_pressed==-1,"focus loss clears transient feedback");
            u.Paint();SendMessageW(u.window,WM_LBUTTONDOWN,0,MAKELPARAM(int(270*u.scale),int(22*u.scale)));SendMessageW(u.window,WM_CANCELMODE,0,0);
            Expect(GetCapture()!=u.window&&u.tab_pressed==-1&&!u.tab_timer,"cancel mode releases tab capture and timer");
            SendMessageW(u.window,WM_LBUTTONDOWN,0,MAKELPARAM(int(270*u.scale),int(22*u.scale)));SendMessageW(u.window,WM_MOUSELEAVE,0,0);
            Expect(GetCapture()!=u.window&&u.tab_pressed==-1,"pointer leave cannot strand mouse capture");
            u.status.state=State::Preview;u.trim_drag=1;SetCapture(u.window);SendMessageW(u.window,WM_CANCELMODE,0,0);
            Expect(GetCapture()!=u.window,"tab cancellation preserves default mouse capture release for preview drags");u.trim_drag=0;u.status.state=State::Ready;
            u.Action(1);ShowWindow(u.window,SW_HIDE);Expect(!u.tab_timer&&!u.Model().tab_motion.ready,"hidden panel has no animation timer");
            ShowWindow(u.window,SW_SHOWNOACTIVATE);u.status.state=State::Starting;u.Invalidate();Expect(!u.tab_timer&&!u.Model().tab_motion.ready&&!u.session,"countdown state stops tab animation without starting capture in fixture");
            DestroyWindow(u.window);u.window=nullptr;Expect(!u.tab_timer,"destroy cleans up tab timer");
        }
        if(preview){
            std::filesystem::create_directories(L"frames");lumashot::ui::ToolbarMotion motion;PanelState state;state.acrylic=true;state.fps=30;
            for(int i=0;i<120;++i){const uint64_t now=uint64_t(i)*1000/30;
                state.gif=(i>=20&&i<49)||(i>=57&&i<82);state.fps=state.gif?15:30;
                const int hover=i<12?-1:i<38?1:i<57?2:i<75?1:i<105?2:-1;
                const int pressed=(i>=18&&i<22)||(i>=55&&i<59)?1:(i>=47&&i<51)||(i>=80&&i<84)?2:-1;
                motion.Update(boxes,state.gif?2:3,TabSlot(hover),TabSlot(pressed),now,true);state.tab_motion=motion.Sample(now);
                TabRender(state,1,L"frames/frame-"+std::to_wstring(1000+i)+L".png");
            }
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}
    CoUninitialize();std::cout<<"RECORDING_TAB failures="<<failures<<std::endl;return failures?1:0;
}
