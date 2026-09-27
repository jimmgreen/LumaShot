#pragma once
// Synthetic fixtures only; no desktop capture, user preferences or real media.
struct PolishRenderResult {UINT width{},height{};std::vector<uint32_t> pixels;std::vector<PanelButton> buttons;};
PolishRenderResult PolishRender(const PanelState& state,float scale,const std::wstring& file={}){
    const auto size=PanelSize(state);PolishRenderResult result;result.width=UINT(size.cx*scale);result.height=UINT(size.cy*scale);
    ComPtr<IWICImagingFactory> wic;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"Polish WIC");
    ComPtr<IWICBitmap> bitmap;Check(wic->CreateBitmap(result.width,result.height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Polish bitmap");
    ComPtr<ID2D1Factory> factory;Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Polish shapes");
    ComPtr<IDWriteFactory> fonts;Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(fonts.GetAddressOf())),"Polish fonts");
    ComPtr<ID2D1RenderTarget> target;Check(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE),&target),"Polish target");
    target->SetDpi(96*scale,96*scale);target->BeginDraw();result.buttons=DrawPanel(target.Get(),fonts.Get(),state);Check(target->EndDraw(),"Polish render");
    result.pixels.resize(size_t(result.width)*result.height);Check(bitmap->CopyPixels(nullptr,result.width*4,result.width*result.height*4,reinterpret_cast<BYTE*>(result.pixels.data())),"Polish pixels");
    if(!file.empty()){
        ComPtr<IWICStream> stream;Check(wic->CreateStream(&stream),"Polish stream");Check(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE),"Polish output");
        ComPtr<IWICBitmapEncoder> encoder;Check(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Polish PNG");Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Polish encoder");
        ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr),"Polish frame");Check(frame->Initialize(nullptr),"Polish initialize");Check(frame->WriteSource(bitmap.Get(),nullptr),"Polish write");Check(frame->Commit(),"Polish commit");Check(encoder->Commit(),"Polish finish");
    }
    return result;
}
bool SamePolishHits(const PolishRenderResult& a,const PolishRenderResult& b){
    if(a.buttons.size()!=b.buttons.size())return false;
    for(size_t i=0;i<a.buttons.size();++i){const auto x=a.buttons[i],y=b.buttons[i];if(x.id!=y.id||x.box.left!=y.box.left||x.box.top!=y.box.top||x.box.right!=y.box.right||x.box.bottom!=y.box.bottom)return false;}
    return true;
}
int RecordingPolishTest(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{
        lumashot::ui::InteractionMotion<96> motion;std::array<bool,96> selected{};
        motion.Update(-1,-1,selected,100,true);Expect(!motion.Active(100),"new feedback is settled without an idle timer");
        motion.Update(20,20,selected,200,true);auto frame=motion.Sample(235);
        Expect(frame.hover[20]>0&&frame.hover[20]<1&&frame.press[20]>0&&frame.press[20]<1,"hover and press have independent bounded easing");
        const auto before=motion.Sample(240);motion.Update(-1,-1,selected,240,true);const auto after=motion.Sample(240);
        Expect(before.hover==after.hover&&before.press==after.press,"rapid feedback reversal is continuous");
        selected[8]=true;motion.Update(-1,-1,selected,300,true);Expect(motion.Sample(390).selected[8]>.4f&&motion.Sample(390).selected[8]<.6f,"switch thumb interpolates rather than jumping");
        Expect(!motion.Active(1000)&&motion.Sample(1000).selected[8]==1,"feedback always settles");
        motion.Update(90,90,selected,1100,false);Expect(!motion.Active(1100)&&motion.Sample(1100).press[90]==1,"reduced-motion mode snaps immediately");
        motion.Reset();Expect(!motion.Sample(1200).ready&&!motion.Active(1200),"reset removes all feedback work");
        for(bool dark:{false,true})for(bool gif:{false,true})for(auto state:{State::Ready,State::Starting,State::Recording,State::Paused,State::Finishing,State::Preview,State::Failed})for(float dpi:{1.f,1.25f,1.5f,2.f}){
            PanelState s;s.dark=dark;s.gif=gif;s.state=state;s.acrylic=true;s.time=100000000;s.trim_end=s.time;
            const auto glass=PolishRender(s,dpi);const auto size=PanelSize(s);const auto pixel=glass.pixels[size_t(glass.height/2)*glass.width+UINT((size.cx-4)*dpi)];
            Expect(pixel==0x01000000,"every recording state keeps clear acrylic hit coverage at every DPI");
            Expect(glass.pixels[0]==0&&glass.pixels[glass.width-1]==0&&glass.pixels.back()==0&&glass.pixels[(glass.height-1)*glass.width]==0,"all rounded corners remain transparent");
            bool valid=true;for(const auto& b:glass.buttons)valid&=b.box.left>=0&&b.box.top>=0&&b.box.right<=size.cx&&b.box.bottom<=size.cy;
            Expect(valid,"all interactive geometry stays within its panel");
            s.acrylic=false;const auto fallback=PolishRender(s,dpi);
            Expect((fallback.pixels[size_t(fallback.height/2)*fallback.width+UINT((size.cx-4)*dpi)]>>24)==255,"unsupported acrylic uses an opaque fallback");
            Expect(SamePolishHits(glass,fallback),"material choice never changes hit geometry");
        }
        for(bool dark:{false,true})for(float dpi:{1.f,1.25f,1.5f,2.f}){
            PanelState s;s.dark=dark;s.state=State::Recording;
            motion.Reset();selected.fill(false);motion.Update(-1,-1,selected,0,true);motion.Update(20,20,selected,100,true);
            s.interaction=motion.Sample(100);const auto a=PolishRender(s,dpi);s.interaction=motion.Sample(150);const auto b=PolishRender(s,dpi);
            Expect(a.pixels!=b.pixels&&SamePolishHits(a,b),"animated control-bar icon changes pixels but never its hit rectangle");
            s.state=State::Ready;s.system=true;motion.Reset();motion.Update(-1,-1,selected,0,true);selected[8]=true;motion.Update(-1,-1,selected,100,true);
            s.interaction=motion.Sample(150);const auto mid=PolishRender(s,dpi);s.interaction=motion.Sample(500);const auto end=PolishRender(s,dpi);
            Expect(mid.pixels!=end.pixels&&SamePolishHits(mid,end),"switch animation is visible and geometry-stable");
        }
        {
            Ui u;WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.lpszClassName=L"LumaShot.PolishFixture";RegisterClassW(&wc);
            const auto panel_size=PanelSize(u.Model());
            u.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE,wc.lpszClassName,L"Synthetic polish fixture",WS_POPUP,-20000,-20000,panel_size.cx,panel_size.cy,nullptr,nullptr,wc.hInstance,&u);
            Check(u.window?S_OK:E_FAIL,"Polish owner");ShowWindow(u.window,SW_SHOWNOACTIVATE);u.scale=1;u.tab_effects=true;u.Invalidate();u.Paint();
            const auto system_switch=std::find_if(u.buttons.begin(),u.buttons.end(),[](const auto& button){return button.id==8;});
            Expect(system_switch!=u.buttons.end(),"system sound switch is available in the current layout");
            const auto& switch_box=system_switch->box;
            const LPARAM switch_point=MAKELPARAM(int((switch_box.left+switch_box.right)/2),int((switch_box.top+switch_box.bottom)/2));
            SendMessageW(u.window,WM_MOUSEMOVE,0,switch_point);SendMessageW(u.window,WM_LBUTTONDOWN,0,switch_point);
            Expect(u.pressed==8&&GetCapture()==u.window&&u.feedback_timer,"real switch press owns feedback and pointer capture");
            SendMessageW(u.window,WM_LBUTTONUP,0,switch_point);
            Expect(!u.system&&u.pressed==-1&&GetCapture()!=u.window&&!u.session,"switch action stays immediate and never starts capture or an encoder");
            u.SyncFeedback(GetTickCount64()+1000);Expect(!u.feedback_timer,"owner stops the feedback timer after settling");
            SendMessageW(u.window,WM_LBUTTONDOWN,0,switch_point);SendMessageW(u.window,WM_CANCELMODE,0,0);
            Expect(u.pressed==-1&&GetCapture()!=u.window&&!u.feedback_timer,"cancel clears capture and feedback work");
            SendMessageW(u.window,WM_MOUSEMOVE,0,switch_point);SendMessageW(u.window,WM_KILLFOCUS,0,0);
            Expect(!u.feedback_timer&&u.hover==-1,"focus loss leaves no feedback timer");
            u.tab_effects=false;u.hover=8;u.Invalidate();Expect(!u.feedback_timer&&!u.Model().interaction.effects,"owner honors reduced-motion settings");
            u.tab_effects=true;u.hover=9;u.Invalidate();ShowWindow(u.window,SW_HIDE);Expect(!u.feedback_timer&&!u.Model().interaction.ready,"hidden panel has no feedback timer");
            DestroyWindow(u.window);u.window=nullptr;Expect(!u.feedback_timer,"destruction stops feedback work");
        }
        // Renderer-only previews; the acrylic images intentionally contain no DWM backdrop.
        std::filesystem::create_directories(L"polish-previews");
        PreviewImages images;images.poster=Poster();images.thumbnails.assign(8,images.poster);
        for(bool dark:{false,true})for(bool gif:{false,true})for(auto state:{State::Ready,State::Recording,State::Preview}){
            PanelState s;s.dark=dark;s.gif=gif;s.state=state;s.acrylic=true;s.images=&images;s.time=100000000;s.trim_end=80000000;s.trim_begin=10000000;s.selected=true;
            const std::wstring name=L"polish-previews/"+std::wstring(dark?L"dark-":L"light-")+(gif?L"gif-":L"video-")+std::to_wstring(int(state));
            PolishRender(s,1,name+L"-clear.png");s.acrylic=false;PolishRender(s,1,name+L"-fallback.png");
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}
    CoUninitialize();std::cout<<"RECORDING_POLISH failures="<<failures<<std::endl;return failures?1:0;
}
