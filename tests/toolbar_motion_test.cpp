// Synthetic-only: no capture, clipboard access, user settings or input injection.
#include "app/application.h"
#include "export/png.h"
#include <wincodec.h>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

namespace lumashot {
struct ToolbarMotionTest {
    static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Toolbar preview render failed");}
    static std::array<Box,ui::ToolbarSlots> Boxes(const ViewState& state){
        std::array<Box,ui::ToolbarSlots> boxes{};const auto origin=state.toolbar.bounds;
        for(const auto& c:ToolbarControls(state,Document{}))if(c.id>=0&&c.id<int(ui::ToolbarSlots)&&c.Interactive())
            boxes[size_t(c.id)]={c.bounds.left-origin.left,c.bounds.top-origin.top,c.bounds.right-origin.left,c.bounds.bottom-origin.top};
        return boxes;
    }
    static void Visual(ViewState& s,const ui::ToolbarMotion& motion,uint64_t now){
        s.toolbar_visual=motion.Sample(now);auto& b=s.toolbar_visual.indicator;
        b.left+=s.toolbar.bounds.left;b.right+=s.toolbar.bounds.left;b.top+=s.toolbar.bounds.top;b.bottom+=s.toolbar.bounds.top;
    }
    static Frame Native(ViewState state,Renderer& renderer,int width=1080,int height=250){
        auto frame=MakeFrame({0,0,width,height},state.dark?0xff202833:0xffe7edf4);
        ComPtr<IWICImagingFactory> wic;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
        ComPtr<IWICBitmap> bitmap;Check(wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
        renderer.brush_.Reset();renderer.acrylic_bitmap_.Reset();renderer.color_wheel_bitmap_.Reset();
        Check(renderer.factory_->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&renderer.target_));
        renderer.acrylic_bitmap_=renderer.Bitmap(frame);renderer.monitor_=frame.bounds;
        renderer.target_->BeginDraw();renderer.target_->Clear(D2D1::ColorF(state.dark?0x202833:0xe7edf4));
        renderer.Toolbar(frame,Document{},state);Check(renderer.target_->EndDraw());
        Check(bitmap->CopyPixels(nullptr,width*4,UINT(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data())));
        return frame;
    }
    static int Preview(){
        std::filesystem::create_directories("frames");Renderer renderer;ui::ToolbarMotion motion;
        ViewState state;state.selected=true;state.tool=Tool::Rectangle;state.selection={60,0,1020,30};
        for(int i=0;i<120;++i){
            const uint64_t now=uint64_t(i)*1000/30;
            const int selected=i<22?1:i<49?4:i<58?2:i<85?3:6;
            const int hover=i<12?-1:i<22?4:i<43?4:i<49?2:i<55?2:i<78?3:i<85?6:i<105?6:-1;
            const int down=(i>=22&&i<25)?4:(i>=49&&i<52)?2:(i>=58&&i<61)?3:(i>=85&&i<88)?6:-1;
            state.tool=static_cast<Tool>(selected);state.hover=hover;
            state.toolbar=PlaceToolbar(state.selection,{0,0,1080,700},1,1,state.tool);
            motion.Update(Boxes(state),selected,hover,down,now,true);Visual(state,motion,now);
            auto frame=Native(state,renderer);const auto path=std::filesystem::path("frames")/("frame-"+std::to_string(1000+i)+".png");
            std::exception_ptr error;
            std::thread writer([frame=std::move(frame),path,&error]{const HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
                try{Check(hr);SavePng(frame,path);}catch(...){error=std::current_exception();}if(SUCCEEDED(hr))CoUninitialize();});
            writer.join();if(error)std::rethrow_exception(error);
        }
        std::cout<<"PREVIEW 120 native-rendered frames, 1080x250, 30fps"<<std::endl;return 0;
    }
    static int Run(){
        int checks{},failures{};const auto expect=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::cout<<"FAIL "<<name<<std::endl;}};
        for(float dpi:{1.f,1.25f,1.5f,2.f})for(int width:{320,720,1280}){
            ViewState s;s.selected=true;s.tool=Tool::Rectangle;s.selection={-width+20.f,100,-20,200};
            s.toolbar=PlaceToolbar(s.selection,{-width,-100,0,1100},dpi,1,s.tool);
            const auto boxes=Boxes(s),original=boxes;ui::ToolbarMotion motion;
            motion.Update(boxes,1,-1,-1,0,true);expect(!motion.Active(0),"initial selection is immediately settled");
            motion.Update(boxes,6,6,6,100,true);
            const bool sameRow=std::abs(boxes[1].top-boxes[6].top)<.5f;
            expect(motion.Sample(100).indicator==(sameRow?boxes[1]:boxes[6]),"cross-row selection snaps safely; same-row selection starts without a jump");
            const auto span=motion.Sample(215).indicator;
            if(sameRow)expect(span.left<=boxes[1].left+.01f&&span.right>=boxes[6].right-.01f,"leading edge stretches to span both slots before trailing edge follows");
            expect(motion.Sample(500).indicator==boxes[6]&&!motion.Active(500),"all geometry and feedback settle exactly with no idle animation");
            const auto prior=motion.Sample(180).indicator;
            motion.Update(boxes,3,3,-1,180,true);
            if(sameRow&&std::abs(prior.top-boxes[3].top)<.5f)expect(motion.Sample(180).indicator==prior,"rapid reversal starts from current visible shape");
            motion.Update(boxes,2,2,2,190,false);
            expect(motion.Sample(190).indicator==boxes[2]&&!motion.Active(190),"reduced-motion settles all channels immediately");
            motion.Update(boxes,0,-1,-1,200,true);expect(motion.Sample(200).indicator==boxes[0],"selection action does not sweep across unrelated action buttons");
            motion.Update(boxes,4,4,4,210,true);motion.Update(boxes,4,-1,-1,250,true);
            expect(motion.Sample(600).hover[4]==0&&motion.Sample(600).press[4]==0&&!motion.Active(600),"leave/cancel clears hover and press feedback");
            auto resized=boxes;for(auto& b:resized){b.left*=.8f;b.right*=.8f;}
            motion.Update(resized,4,-1,-1,610,true);expect(motion.Sample(610).indicator==resized[4],"DPI or layout change rebases without flying across the screen");
            bool stable=boxes==original;
            for(const auto& c:ToolbarControls(s,Document{}))if(c.id>=0&&c.id<int(ui::ToolbarSlots)&&c.Interactive())
                stable&=ui::HitTest(ToolbarControls(s,Document{}),{(c.bounds.left+c.bounds.right)/2,(c.bounds.top+c.bounds.bottom)/2})==c.id;
            expect(stable,"visual animation never mutates the original click targets");
            motion.Reset();expect(!motion.Active(700)&&!motion.Sample(700).ready,"reset clears every visual state");
        }
        {
            ViewState s;s.selected=true;s.tool=Tool::Rectangle;s.selection={0,0,1000,200};s.toolbar=PlaceToolbar(s.selection,{0,0,1280,900},1,1,s.tool);
            ui::ToolbarMotion a,b;const auto boxes=Boxes(s);a.Update(boxes,1,-1,-1,0,true);b.Update(boxes,1,-1,-1,0,true);
            a.Update(boxes,4,4,-1,100,true);b.Update(boxes,4,4,-1,100,true);
            for(uint64_t t=100;t<300;t+=7)b.Sample(t);
            expect(a.Sample(300).indicator==b.Sample(300).indicator,"motion sampling is independent of timer delivery rate");
        }
        Renderer renderer;
        for(bool dark:{false,true})for(float dpi:{1.f,1.25f,1.5f,2.f}){
            ViewState s;s.selected=true;s.dark=dark;s.tool=Tool::Arrow;s.selection={20,0,1220,20};s.toolbar=PlaceToolbar(s.selection,{0,0,1280,900},dpi,1,s.tool);
            ui::ToolbarMotion motion;const auto boxes=Boxes(s);motion.Update(boxes,1,-1,-1,0,true);motion.Update(boxes,3,3,3,10,true);
            Visual(s,motion,100);const auto mid=Native(s,renderer,1280,550);Visual(s,motion,600);const auto end=Native(s,renderer,1280,550);
            expect(mid.pixels!=end.pixels,"production renderer paints distinct intermediate and settled pixels in both themes at every DPI");
            expect(std::all_of(end.pixels.begin(),end.pixels.end(),[](uint32_t p){return (p>>24)==255;}),"native output remains opaque over the synthetic desktop");
        }
        {
            Application app;app.diagnostic_session_=true;app.active_=true;app.toolbar_effects_=true;
            app.main_=CreateWindowExW(0,L"STATIC",L"Toolbar motion test",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
            auto& s=app.state_;s.selected=true;s.tool=Tool::Rectangle;s.selection={20,20,1000,350};
            s.toolbar=PlaceToolbar(s.selection,{0,0,1280,900},1,1,s.tool);app.toolbar_monitor_={0,0,1280,900};
            app.SyncToolbarMotion(1000);s.tool=Tool::Pen;app.SyncToolbarMotion(1100);
            expect(app.toolbar_motion_timer_,"production owner schedules a timer for selection motion");
            app.TickToolbarMotion(1600);expect(!app.toolbar_motion_timer_&&!KillTimer(app.main_,22),"production owner removes timer after final frame");
            s.hover=3;app.toolbar_pressed_=3;app.SyncToolbarMotion(1700);expect(app.toolbar_motion_timer_,"hover and press wake an idle toolbar");
            s.busy=true;app.SyncToolbarMotion(1800);expect(!app.toolbar_motion_timer_&&app.toolbar_pressed_==-1&&!s.toolbar_visual.ready,"busy state cancels feedback and stale press");
            s.busy=false;app.SyncToolbarMotion(1900);s.tool=Tool::Mosaic;app.SyncToolbarMotion(2000);app.toolbar_effects_=false;app.TickToolbarMotion(2010);
            expect(!app.toolbar_motion_timer_&&!s.toolbar_visual.effects,"live reduced-motion change cancels an in-flight animation");
            app.StopToolbarMotion();app.toolbar_effects_=true;s.tool=Tool::Rectangle;app.SyncToolbarMotion();
            app.Command(4);expect(s.tool==Tool::Pen&&app.toolbar_motion_timer_,"actual command route changes the tool immediately while its indicator animates");
            app.StopToolbarMotion();s.selected=false;app.SyncToolbarMotion();expect(!s.toolbar_visual.ready&&!app.toolbar_motion_timer_,"new selection/session hides stale motion");
            app.active_=false;DestroyWindow(app.main_);app.main_=nullptr;
        }
        std::cout<<"TOOLBAR_MOTION checks="<<checks<<" failures="<<failures<<std::endl;return failures?1:0;
    }
};
}
int main(int argc,char** argv){
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=1;try{result=argc>1&&std::string_view(argv[1])=="--preview"?lumashot::ToolbarMotionTest::Preview():lumashot::ToolbarMotionTest::Run();}
    catch(const std::exception& e){std::cerr<<e.what()<<std::endl;}CoUninitialize();return result;
}
