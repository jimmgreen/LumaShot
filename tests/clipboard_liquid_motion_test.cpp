// Real production renderer; all content is synthetic. No desktop/clipboard capture,
// personal settings, global input injection, installation or user-window activation.
#include "../src/clipboard/panel.cpp"
#include "export/png.h"
#include "clipboard/liquid_neck.h"
#include <iostream>
#include <string_view>
#include <thread>

namespace lumashot {
struct ClipboardPanelTest {
    static void Seed(ClipboardPanel::Impl& p){
        p.W=360;p.H=600;p.ListBottom=p.H-62;
        for(const wchar_t* text:{L"让每一次展开，都像自然生长。",L"const motion = spring({ damping: 0.82 });",L"拖动 · 黏连 · 轻轻回弹",L"合成测试内容 / No personal clipboard"}){
            clipboard::Entry entry;entry.kind=clipboard::Kind::Text;entry.text=text;GetLocalTime(&entry.time);
            const auto* bytes=reinterpret_cast<const unsigned char*>(entry.text.c_str());
            entry.formats.push_back({CF_UNICODETEXT,{bytes,bytes+(entry.text.size()+1)*sizeof(wchar_t)}});p.history.Add(std::move(entry));
        }
        p.Filter();p.selected=p.visible.empty()?0:p.visible.front();
    }
    static void Cache(ClipboardPanel::Impl& p,clipboard::LiquidSurface& renderer){
        const bool open=p.expanded;
        p.expanded=false;p.Render(int(p.CW*p.scale),int(p.CH*p.scale),true);renderer.Cache(false,p.width,p.height,p.surface->Pixels());
        p.expanded=true;p.Render(int(p.W*p.scale),int(p.H*p.scale),true);renderer.Cache(true,p.width,p.height,p.surface->Pixels());p.expanded=open;
    }
    static Frame Composite(const clipboard::LiquidSurface& renderer,RECT viewport){
        auto frame=MakeFrame({0,0,720,780});
        for(int y=0;y<frame.Height();++y)for(int x=0;x<frame.Width();++x){
            const float t=float(y)/780.f;
            const auto mix=[&](int a,int b){return UINT(a+(b-a)*t);};
            UINT32 bg=0xff000000|(mix(222,244)<<16)|(mix(234,225)<<8)|mix(245,211);
            if(x<190&&y>72&&y<668)bg=0xffeef3f8;
            frame.pixels[size_t(y)*720+size_t(x)]=bg;
        }
        for(int y=0;y<renderer.Height();++y)for(int x=0;x<renderer.Width();++x){
            const int dx=x+viewport.left,dy=y+viewport.top;if(dx<0||dy<0||dx>=720||dy>=780)continue;
            const UINT32 src=renderer.Pixels()[size_t(y)*renderer.Width()+size_t(x)],a=src>>24;
            auto& destination=frame.pixels[size_t(dy)*720+size_t(dx)];UINT32 pixel=0xff000000;
            for(int shift:{0,8,16})pixel|=std::min(255u,((src>>shift)&255)+((((destination>>shift)&255)*(255-a)+127)/255))<<shift;
            destination=pixel;
        }
        return frame;
    }
    static int Preview(ClipboardPanel::Impl& p,bool detach=false){
        p.scale=1;p.dark=false;p.status.clear();std::filesystem::create_directories("frames");clipboard::LiquidSurface renderer;Cache(p,renderer);
        const RECT work{0,0,720,780},compact{548,300,576,396},open{216,70,576,670};
        clipboard::LiquidTween grow,shrink;grow.Reset(compact,0,0);grow.Target(open,1,0,350);
        shrink.Reset(open,1,0);shrink.Target(compact,0,0,340);
        clipboard::LiquidPull recoil;recoil.Reset(0);auto previous=clipboard::LiquidEdge::Right;
        for(int i=0;i<150;++i){
            const float t=float(i)/30.f;clipboard::LiquidFrame f;f.work=work;f.scale=1;f.dark=false;
            float right=720,y=300;D2D1_POINT_2F pull{};
            if(t<.45f){}
            else if(t<1.35f){const float u=clipboard::LiquidSmooth((t-.45f)/.9f);right=720-144*u;y=300-24*std::sin(u*3.14159265f);pull={-6*std::sin(u*3.14159265f),-3*std::sin(u*6.2831853f)};}
            else if(t<1.65f){right=576;pull.x=-2*std::exp(-12*(t-1.35f))*std::cos(24*(t-1.35f));}
            else if(t<2.12f){f.pose=grow.Sample(ULONGLONG((t-1.65f)*1000));}
            else if(t<3.0f){f.pose={clipboard::LiquidRect(open),1};}
            else if(t<3.42f){f.pose=shrink.Sample(ULONGLONG((t-3.0f)*1000));}
            else if(t<3.65f){right=576;}
            else if(t<4.5f){const float u=clipboard::LiquidSmooth((t-3.65f)/.85f);right=576+144*u;pull={6*std::sin(u*3.14159265f),0};}
            if(t<1.65f||t>=3.42f)f.pose={{right-28,y,right,y+96},0};
            if(detach){
                // Five seconds: shoulder -> neck -> rupture/recoil -> return.
                const float gap=t<.5f?0.f:t<2.f?60*clipboard::LiquidSmooth((t-.5f)/1.5f):
                    t<2.8f?60.f:t<4.5f?60*(1-clipboard::LiquidSmooth((t-2.8f)/1.7f)):0.f;
                right=720-gap;f.pose={{right-28,300,right,396},0};pull={};
                const auto edge=clipboard::FindLiquidDock(f.pose.bounds,work,1).edge;
                if(previous!=clipboard::LiquidEdge::None&&edge==clipboard::LiquidEdge::None)recoil.Recoil(180,0,ULONGLONG(i)*1000/30);
                previous=edge;pull=recoil.Sample(ULONGLONG(i)*1000/30);
            }
            f.pull=pull;f.viewport=clipboard::LiquidViewport(f.pose.bounds,work,1);renderer.Render(f);
            auto frame=Composite(renderer,f.viewport);const auto name="frames/frame-"+std::to_string(1000+i)+".png";
            // Encoding stays off the UI thread, even for the bounded synthetic preview.
            std::exception_ptr error;
            std::thread writer([frame=std::move(frame),name,&error]{
                const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
                try{if(FAILED(init))throw std::runtime_error("Preview encoder COM initialization failed");SavePng(frame,std::filesystem::path(name));}catch(...){error=std::current_exception();}
                if(SUCCEEDED(init))CoUninitialize();
            });writer.join();if(error)std::rethrow_exception(error);
        }
        std::cout<<"PREVIEW 150 native-rendered synthetic frames in frames/"<<std::endl;return 0;
    }
    static int Run(bool preview,bool detach=false){
        int failures{},checks{};const auto expect=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::cout<<"FAIL "<<name<<std::endl;}};
        using namespace clipboard;
        // Analytic sampling does not depend on how many timer ticks were delivered.
        LiquidTween a,b;const RECT from{600,200,628,296},to{268,120,628,720};
        a.Reset(from,0,0);b.Reset(from,0,0);a.Target(to,1,0);b.Target(to,1,0);
        const auto direct=a.Sample(117);for(ULONGLONG t=0;t<117;t+=7)b.Sample(t);const auto stepped=b.Sample(117);
        expect(std::abs(direct.bounds.left-stepped.bounds.left)<.001f,"frame-rate-independent spring");
        expect(direct.bounds.right==628&&direct.bounds.left<600&&direct.bounds.left>240,"right anchor remains fixed while shell opens");
        const auto before=a.Sample(130);a.Target(from,0,130,340);const auto reversed=a.Sample(130);
        expect(std::abs(before.bounds.left-reversed.bounds.left)<.001f&&std::abs(before.open-reversed.open)<.001f,"rapid reversal preserves displayed geometry and content phase");
        const auto done=a.Sample(600);const auto done_bounds=LiquidPixels(done.bounds);expect(EqualRect(&done_bounds,&from),"spring settles at exact target");
        expect(!LiquidBudget(from,to,false)&&!LiquidBudget({0,0,4000,4000},to,true),"reduced-motion and large-surface budgets reject animation");
        LiquidPull pull;pull.Reset(0);pull.Impulse(10000,-10000,0);const auto moving=pull.Sample(32);
        expect(moving.x>0&&moving.y<0&&std::abs(moving.x)<9&&std::abs(moving.y)<11,"drag deformation is directional and bounded");
        pull.Release(35);pull.Sample(900);expect(pull.Settled(),"drag springs become idle after release");
        pull.Reset(0);pull.Recoil(180,0,10);const auto rebound=pull.Sample(42);
        expect(rebound.x>1&&rebound.x<5&&rebound.y==0,"rupture supplies a bounded directional recoil without mouse release");
        pull.Sample(900);expect(pull.Settled(),"rupture recoil becomes idle");
        for(float dpi:{1.f,1.25f,1.5f,2.f}){
            const auto attached=LiquidNeckProfile(14*dpi,48*dpi,8*dpi,0,dpi);
            const auto neck=LiquidNeckProfile(14*dpi,48*dpi,8*dpi,18*dpi,dpi);
            const auto thinning=LiquidNeckProfile(14*dpi,48*dpi,8*dpi,40*dpi,dpi);
            const auto broken=LiquidNeckProfile(14*dpi,48*dpi,8*dpi,44*dpi,dpi);
            expect(std::abs(attached[0].y+48*dpi)<.001f&&std::abs(attached[6].y+56*dpi)<.001f,"resting shoulder dimensions are preserved");
            expect(std::abs(neck[3].y)<std::abs(neck[0].y)&&std::abs(neck[3].y)<std::abs(neck[6].y),"detached neck has a narrow waist and wider ports at both ends");
            expect(std::abs(thinning[3].y)<std::abs(neck[3].y)*.2f&&broken[3].y==0,"neck thins continuously to zero before disappearing");
        }
        for(float dpi:{1.f,1.25f,1.5f,2.f}){
            const RECT work{-1920,-200,0,1400};
            for(auto edge:{LiquidEdge::Left,LiquidEdge::Top,LiquidEdge::Right,LiquidEdge::Bottom}){
                D2D1_RECT_F body{-1200,300,-1200+28*dpi,300+96*dpi};
                if(edge==LiquidEdge::Left){body.left=float(work.left);body.right=body.left+28*dpi;}
                if(edge==LiquidEdge::Top){body.top=float(work.top);body.bottom=body.top+96*dpi;}
                if(edge==LiquidEdge::Right){body.right=0;body.left=-28*dpi;}
                if(edge==LiquidEdge::Bottom){body.bottom=float(work.bottom);body.top=body.bottom-96*dpi;}
                const auto dock=FindLiquidDock(body,work,dpi);const auto view=LiquidViewport(body,work,dpi);
                expect(dock.edge==edge&&dock.strength>.999f,"all four edges attach at every DPI and negative monitor origins");
                expect(view.left>=work.left&&view.top>=work.top&&view.right<=work.right&&view.bottom<=work.bottom,"liquid envelope never covers outside the work area");
            }
        }
        ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;p.test_mode=true;
        expect(p.Enable(true,false),"create isolated production panel with no clipboard listener or settings access");
        if(!p.window)return 1;Seed(p);expect(p.history.entries.size()==4,"four synthetic payloads populate the actual clipboard rows");
        if(preview)return Preview(p,detach);
        LiquidSurface renderer;Cache(p,renderer);
        for(bool dark:{false,true})for(float dpi:{1.f,1.25f,1.5f,2.f}){
            p.scale=dpi;p.dark=dark;Cache(p,renderer);
            for(auto edge:{LiquidEdge::None,LiquidEdge::Left,LiquidEdge::Top,LiquidEdge::Right,LiquidEdge::Bottom}){
                const RECT work{0,0,1200,1400};D2D1_RECT_F body{500,500,500+28*dpi,500+96*dpi};
                if(edge==LiquidEdge::Left){body.left=0;body.right=28*dpi;}
                if(edge==LiquidEdge::Right){body.right=1200;body.left=1200-28*dpi;}
                if(edge==LiquidEdge::Top){body.top=0;body.bottom=96*dpi;}
                if(edge==LiquidEdge::Bottom){body.bottom=1400;body.top=1400-96*dpi;}
                LiquidFrame frame;frame.pose={body,0};frame.viewport=LiquidViewport(body,work,dpi);frame.work=work;frame.scale=dpi;frame.dark=dark;
                renderer.Render(frame);size_t transparent{},partial{};bool premultiplied=true;
                for(int i=0;i<renderer.Width()*renderer.Height();++i){const auto pixel=renderer.Pixels()[i],alpha=pixel>>24;transparent+=alpha==0;partial+=alpha>0&&alpha<240;for(int shift:{0,8,16})premultiplied&=((pixel>>shift)&255)<=alpha;}
                expect(transparent>0&&partial>20&&premultiplied,"native liquid texture has transparent exterior, antialiasing and valid premultiplied alpha");
                const HRGN mask=renderer.CreateInputRegion();bool preserves_alpha=mask!=nullptr;
                for(int y=0;y<renderer.Height();++y)for(int x=0;x<renderer.Width();++x){const auto alpha=renderer.Pixels()[size_t(y)*renderer.Width()+x]>>24;
                    if(alpha>0&&alpha<240)preserves_alpha&=PtInRegion(mask,x,y)!=FALSE;}
                expect(preserves_alpha&&!PtInRegion(mask,0,0),"native input mask excludes transparent padding without cutting any antialiased pixel");DeleteObject(mask);
                const POINT center{LONG((body.left+body.right)/2-frame.viewport.left),LONG((body.top+body.bottom)/2-frame.viewport.top)};
                expect(renderer.Contains(center),"liquid hit test includes the body, not just its bounding rectangle");
                if(edge!=LiquidEdge::None){
                    const bool horizontal=edge==LiquidEdge::Left||edge==LiquidEdge::Right;
                    const float sign=edge==LiquidEdge::Left||edge==LiquidEdge::Top?-1.f:1.f;
                    const float radial=(horizontal?body.right-body.left:body.bottom-body.top)/2;
                    const float half=(horizontal?body.bottom-body.top:body.right-body.left)/2;
                    const auto at=[&](float normal,float tangent){
                        const float cx=(body.left+body.right)/2-frame.viewport.left;
                        const float cy=(body.top+body.bottom)/2-frame.viewport.top;
                        return POINT{LONG(std::lround(cx+(horizontal?sign*normal:tangent))),
                            LONG(std::lround(cy+(horizontal?tangent:sign*normal)))};
                    };
                    for(float side:{-1.f,1.f}){
                        expect(renderer.Contains(at(radial-2*dpi,side*(half-dpi))),
                            "docked shoulder is filled all the way to the edge, not a middle neck");
                        expect(renderer.Contains(at(radial-dpi,side*(half+2*dpi))),
                            "concave shoulder flares beyond both ends at the boundary");
                        expect(!renderer.Contains(at(radial-7*dpi,side*(half+6*dpi))),
                            "outside of concave shoulder remains transparent and noninteractive");
                    }
                    const auto attached=body;
                    for(float distance:{6.f,18.f,30.f,48.f}){
                        body=attached;const float shift=-sign*distance*dpi;
                        if(horizontal){body.left+=shift;body.right+=shift;}
                        else{body.top+=shift;body.bottom+=shift;}
                        frame.pose.bounds=body;frame.viewport=LiquidViewport(body,work,dpi);
                        for(float tug:{0.f,-6.f,6.f}){
                            frame.pull={tug,tug*.5f};renderer.Render(frame);
                            bool connected=true;
                            for(float u:{0.f,.2f,.4f,.6f,.8f})connected&=renderer.Contains(at(radial+distance*dpi*u,0));
                            if(distance<44)expect(connected,"stretching neck stays connected to the deforming body across all edges and DPI");
                            else expect(!renderer.Contains(at(radial+distance*dpi*.6f,0)),"ruptured bridge leaves no invisible hit target");
                            if(distance==18&&tug==0)expect(!renderer.Contains(at(radial+distance*dpi*.52f,8*dpi)),"detachment waist is visibly pinched rather than a wide triangular wedge");
                        }
                    }
                }
            }
        }
        p.scale=1;p.expanded=false;p.compact_positioned=false;
        const auto work=p.WorkArea(p.VisualBodyBounds());const LONG x=work.left+40,y=work.top+40;
        const RECT closed{x+360-28,y+100,x+360,y+196},opened{x,y,x+360,y+600};
        SetWindowPos(p.window,nullptr,closed.left,closed.top,28,96,SWP_NOZORDER|SWP_NOACTIVATE);p.Present();
        const HWND foreground=GetForegroundWindow();
        expect(SendMessageW(p.window,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"folded drag never steals the destination focus");
        RECT viewport{};GetWindowRect(p.window,&viewport);expect(p.ResizeHit({viewport.left,viewport.top})==HTTRANSPARENT,"padding is not an interactive rectangular halo");
        const HWND folded=p.window,native=p.native_window;
        expect(folded==p.folded_window&&folded!=native,"folded and native panels have distinct persistent HWNDs");
        p.expanded=true;expect(p.PresentAt(opened),"reveal independent native panel");
        expect(!p.liquid_drag_active&&p.window==native&&GetParent(p.search)==native,"expanded panel uses its permanent HWND and search parent without shape animation");
        expect(GetForegroundWindow()==foreground,"material handoff does not activate a window");
        expect((GetWindowLongPtrW(native,GWL_STYLE)&WS_THICKFRAME)!=0,"native panel keeps its system frame");
        HRGN region=CreateRectRgn(0,0,0,0);
        expect(GetWindowRgn(native,region)==ERROR,"native panel never receives the folded liquid region");DeleteObject(region);
        for(int i=0;i<20;++i){
            p.expanded=false;expect(p.PresentAt(closed)&&p.window==folded,"fold reuses original liquid HWND");
            p.expanded=true;expect(p.PresentAt(opened)&&p.window==native&&!p.liquid_drag_active,"reopen reuses native HWND without material tween");
        }
        p.expanded=false;p.PresentAt(closed);
        const auto body=p.VisualBodyBounds();expect(EqualRect(&body,&closed)&&!p.liquid_drag_timer,"fold returns exact compact body with no animation timer");
        p.expanded=true;p.PresentAt(opened);expect(!p.liquid_drag_active,"native reveal needs no animation timer");p.expanded=false;p.PresentAt(closed);
        p.expanded=false;p.drag_pending=true;p.drag_start={closed.left+10,closed.top+30};p.liquid_pointer=p.drag_start;
        RECT moved=closed;OffsetRect(&moved,-70,40);const auto now=GetTickCount64();
        p.MoveLiquidDrag(moved,{p.drag_start.x-70,p.drag_start.y+40},work,now);
        p.LiquidDragTick(now+48);expect(p.liquid_drag_active&&p.liquid_drag_timer,"folded drag animates a bounded elastic shell");
        p.LiquidDragTick(now+1200);expect(!p.liquid_drag_timer,"stationary held drag stops repainting");
        p.drag_pending=false;p.ReleaseLiquidDrag();p.LiquidDragTick(now+1600);
        expect(!p.liquid_drag_active&&!p.liquid_drag_timer,"release settles and leaves no continuous render loop");
        p.compact_positioned=false;const RECT docked{work.right-28,y+100,work.right,y+196};
        SetWindowPos(p.window,nullptr,docked.left,docked.top,28,96,SWP_NOZORDER|SWP_NOACTIVATE);p.Present();
        p.drag_pending=true;p.drag_start={docked.left+10,docked.top+30};
        RECT detached=docked;OffsetRect(&detached,-64,0);const auto start=GetTickCount64();
        p.MoveLiquidDrag(detached,{p.drag_start.x-64,p.drag_start.y},work,start);
        expect(p.liquid_drag_edge==LiquidEdge::Right,"production drag remembers the starting attached edge");
        p.LiquidDragTick(start+20);expect(p.liquid_drag_edge==LiquidEdge::Right,"early drag retains the neck");
        p.LiquidDragTick(start+300);
        expect(p.liquid_drag_edge==LiquidEdge::None&&!p.liquid_pull.Settled()&&p.liquid_drag_timer,"production rupture starts recoil before pointer release");
        p.LiquidDragTick(start+1500);expect(!p.liquid_drag_timer,"holding still after rupture stops the render timer");
        p.drag_pending=false;p.ReleaseLiquidDrag();p.LiquidDragTick(start+1800);
        expect(!p.liquid_drag_active&&p.liquid_drag_edge==LiquidEdge::None,"release clears attachment state");
        p.Enable(false,false);expect(!IsWindow(native)&&!IsWindow(folded)&&!p.parked_composition&&!p.liquid_surface&&!p.liquid_drag_active&&!p.liquid_drag_active,"disable frees motion surfaces and cancels timers");
        std::cout<<"LIQUID_MOTION checks="<<checks<<" failures="<<failures<<std::endl;
        return failures?1:0;
    }
};
}
int main(int argc,char** argv){
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const bool detach=argc>1&&std::string_view(argv[1])=="--detach-preview";
    const int result=lumashot::ClipboardPanelTest::Run(detach||(argc>1&&std::string_view(argv[1])=="--preview"),detach);
    CoUninitialize();return result;
}
