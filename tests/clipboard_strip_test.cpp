// Folded clipboard strip: hide by dragging onto the close target, restore through
// the API used by the tray and settings. Real panel messages in test mode with
// synthetic state only; never samples clipboard contents, reads personal
// preferences, or injects system input.
#include "../src/clipboard/panel.cpp"
#include "../src/clipboard/strip_dismiss.cpp"
#include "capture/frame.h"
#include "export/png.h"
#include <iostream>
#include <string>

namespace {
int checks{},failures{};
void Expect(bool ok,const char* message){++checks;if(!ok){++failures;std::cout<<"FAIL "<<message<<'\n';}}
void Pump(ULONGLONG milliseconds){
    const auto end=GetTickCount64()+milliseconds;MSG msg{};
    while(GetTickCount64()<end){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(5);}
}
}
namespace lumashot::clipboard {
struct DismissTargetTest {
    static void Tracker(){
        using Z=DismissZone;
        for(const RECT work:{RECT{0,0,1920,1040},RECT{-2560,-200,0,1240},RECT{1920,0,3200,760}})for(float scale:{1.f,1.5f,2.f}){
            const Z zone{work,scale};const auto c=zone.Center();const auto w=zone.Window();
            Expect(w.left<c.x&&w.right>c.x&&w.top<c.y&&w.bottom==work.bottom&&w.top>=work.top,"target window contains the target and rests on the work-area bottom");
            DismissTracker t;
            const POINT start{work.right-20,work.top+160};t.Begin(start);
            Expect(!t.Update({start.x-LONG(5*scale),start.y},work,scale)&&!t.Active(),"target stays hidden before the reveal travel");
            Expect(!t.Update({start.x-LONG(40*scale),start.y+LONG(20*scale)},work,scale)&&t.Active(),"target appears once the strip is really moved");
            Expect(t.Proximity()<.05f,"distant pointer has no proximity");
            const POINT near_edge{c.x+LONG((Z::ArmDistance-4)*scale),c.y};
            Expect(t.Update(near_edge,work,scale)&&t.Armed()&&t.Proximity()==1.f,"pointer inside the arm radius arms the target");
            Expect(t.Update({c.x+LONG((Z::DisarmDistance-4)*scale),c.y},work,scale),"hysteresis keeps it armed until the disarm radius");
            Expect(!t.Update({c.x+LONG((Z::DisarmDistance+6)*scale),c.y},work,scale),"leaving the disarm radius disarms");
            Expect(!t.Update({c.x+LONG((Z::ArmDistance+8)*scale),c.y},work,scale)&&t.Proximity()>.9f,"re-arming needs the smaller arm radius; proximity grows nearby");
            for(const SIZE strip:{SIZE{LONG(28*scale),LONG(96*scale)},SIZE{LONG(64*scale),LONG(132*scale)}}){
                const RECT m=t.Magnet(strip.cx,strip.cy);
                Expect(m.left>=work.left&&m.right<=work.right&&m.top>=work.top&&m.bottom<=work.bottom,"magnetised strip stays inside the work area");
                Expect(std::abs((m.left+m.right)/2-c.x)<=1&&std::abs((m.top+m.bottom)/2-c.y)<=1,"magnetised strip is centred in the target");
            }
            const RECT body=t.Magnet(LONG(28*scale),LONG(96*scale));
            Expect(FindLiquidDock(LiquidRect(body),work,scale).edge==LiquidEdge::None,"magnetised strip does not start docking to the bottom edge");
            // A strip docked at the bottom centre starts inside the zone: a nudge must not hide it.
            DismissTracker docked;const POINT inside{c.x,work.bottom-LONG(48*scale)};docked.Begin(inside);
            Expect(zone.Distance(inside)<=Z::DisarmDistance,"bottom-docked strip starts inside the zone");
            Expect(!docked.Update({inside.x+LONG(20*scale),inside.y},work,scale)&&docked.Active(),"nudging a bottom-docked strip cannot arm");
            Expect(!docked.Update({c.x,c.y},work,scale),"moving onto the target without leaving first still cannot arm");
            docked.Update({c.x,c.y-LONG((Z::DisarmDistance+20)*scale)},work,scale);
            Expect(docked.Update({c.x,c.y},work,scale),"after leaving the zone once, the target arms normally");
            docked.Reset();Expect(!docked.Update({c.x,c.y},work,scale)&&!docked.Active(),"reset tracker ignores updates until the next drag");
        }
        // Tiny work areas keep the geometry inside the monitor.
        const Z tiny{{0,0,300,180},2.f};const auto w=tiny.Window();
        Expect(w.top>=0&&tiny.Center().y>=0&&tiny.Center().y<=180,"short work area clamps target geometry");
    }
    static uint32_t Pixel(DismissTarget& target,float x,float y){
        auto& p=*target.impl_;const float s=p.zone.Scale();
        const int px=int(x*s),py=int(y*s);if(!p.surface||px<0||py<0||px>=p.surface_w||py>=p.surface_h)return 0;
        return p.surface->Pixels()[py*p.surface_w+px];
    }
    static void Save(DismissTarget& target,const std::wstring& name){
        auto& p=*target.impl_;if(!p.surface)return;
        auto frame=MakeFrame({0,0,p.surface_w,p.surface_h});
        // Composite over a synthetic gradient so the translucent target is reviewable.
        for(int y=0;y<p.surface_h;++y)for(int x=0;x<p.surface_w;++x){
            const auto src=p.surface->Pixels()[y*p.surface_w+x];const UINT inverse=255-(src>>24);
            const UINT bg=p.dark?(0x1a2230+UINT(x*20/p.surface_w)):(0xc8d4e6+UINT(y*16/p.surface_h));uint32_t out=0xff000000;
            for(UINT shift:{0u,8u,16u})out|=std::min(255u,((src>>shift)&255)+((((bg>>shift)&255)*inverse+127)/255))<<shift;
            frame.pixels[y*p.surface_w+x]=out;
        }
        SavePng(frame,name.c_str());
    }
    static void Visual(){
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
        for(bool dark:{false,true})for(float scale:{1.f,1.5f}){
            const DismissZone zone{monitor.rcWork,scale};DismissTarget target(nullptr);
            auto& p=*target.impl_;
            target.Show(zone,dark,nullptr);
            const HWND window=target.Window();
            Expect(window&&IsWindowVisible(window),"showing creates the visible target window");
            const auto ex=GetWindowLongPtrW(window,GWL_EXSTYLE);
            Expect((ex&WS_EX_LAYERED)&&(ex&WS_EX_TRANSPARENT)&&(ex&WS_EX_NOACTIVATE)&&(ex&WS_EX_TOPMOST),"target is a click-through, non-activating topmost layer");
            Expect(SendMessageW(window,WM_NCHITTEST,0,0)==HTTRANSPARENT,"target never takes pointer input");
            Expect(GetForegroundWindow()!=window,"target never becomes foreground");
            RECT r{};GetWindowRect(window,&r);const RECT expected=zone.Window();Expect(EqualRect(&r,&expected),"target window sits at the zone rectangle");
            // Land on the final frames directly for deterministic snapshots.
            p.StopTimer();p.Snap();p.Paint();
            const auto c=zone.Center();const float cx=float(c.x-expected.left)/zone.Scale(),cy=float(c.y-expected.top)/zone.Scale();
            Expect((Pixel(target,cx+DismissZone::RestRadius*.6f,cy)>>24)>200,"rest target renders an opaque disc");
            Expect((Pixel(target,2,2)>>24)==0,"target corners stay fully transparent");
            const std::wstring tag=std::wstring(dark?L"dark-":L"light-")+std::to_wstring(int(scale*100));
            Save(target,L"strip-target-rest-"+tag+L".png");
            target.SetState(true,1);p.StopTimer();p.Snap();p.Paint();
            const auto armed=Pixel(target,cx+DismissZone::ArmedRadius*.88f,cy);const UINT a=armed>>24;
            Expect(a>240&&((armed>>16)&255)>((armed>>8)&255)+60,"armed target turns red and grows past the rest radius");
            Save(target,L"strip-target-armed-"+tag+L".png");
            // Absorb a synthetic strip: premultiplied opaque pixels at the magnet position.
            const int gw=int(64*scale),gh=int(132*scale);std::vector<uint32_t> ghost(size_t(gw)*gh,dark?0xff2a3a50u:0xfff4f8ffu);
            const RECT gr{c.x-gw/2,c.y-gh/2,c.x-gw/2+gw,c.y-gh/2+gh};
            target.Dismiss(ghost.data(),gw,gh,gr);
            BOOL effects=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&effects,0);
            if(effects){
                Expect(target.Animating()&&target.Visible(),"dismiss plays an absorb animation");
                p.dismiss_start=GetTickCount64()-80;p.Paint();Save(target,L"strip-target-absorb-"+tag+L".png");
                const auto started=GetTickCount64();while(target.Window()&&GetTickCount64()-started<2000)Pump(20);
            }
            Expect(!target.Window()&&!target.Animating(),"dismiss releases the window and stops its timer");
            Expect(!p.surface&&!p.target&&p.ghost.empty(),"dismiss frees the surface, renderer and strip snapshot");
            // Plain hide fades out and frees everything as well.
            target.Show(zone,dark,nullptr);target.SetState(false,.5f);target.Hide();
            const auto started=GetTickCount64();while(target.Window()&&GetTickCount64()-started<2000)Pump(20);
            Expect(!target.Window()&&!p.surface,"hiding fades out and releases the target");
        }
    }
};
}
namespace lumashot {
struct ClipboardPanelTest {
    static int Run(){
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int dismissed=0;
        ClipboardPanel panel(nullptr,[]{});
        panel.SetStripDismissedHandler([&]{++dismissed;});
        auto& p=*panel.impl_;p.test_mode=true;
        if(!p.Enable(true,false)){std::cout<<"FAIL create isolated hidden panel\n";return 1;}
        p.Fold();
        Expect(!p.expanded&&p.window==p.folded_window,"panel starts folded on the strip window");
        // Screen-space drag helpers: WM_MOUSEMOVE carries client coordinates of the moving window.
        const auto send=[&](UINT message,POINT screen){
            RECT r{};GetWindowRect(p.window,&r);
            return SendMessageW(p.window,message,message==WM_LBUTTONUP?0:MK_LBUTTON,MAKELPARAM(screen.x-r.left,screen.y-r.top));
        };
        const auto grab=[&]{RECT r{};GetWindowRect(p.window,&r);return POINT{(r.left+r.right)/2,r.top+(r.bottom-r.top)/3};};
        const auto zone_for=[&](POINT at){MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromPoint(at,MONITOR_DEFAULTTONEAREST),&m);return clipboard::DismissZone{m.rcWork,GetDpiForWindow(p.window)/96.f};};

        // 1. A drag that ends away from the target just moves the strip.
        {POINT at=grab();const POINT before=p.anchor;send(WM_LBUTTONDOWN,at);
         at.x-=120;at.y+=40;send(WM_MOUSEMOVE,at);
         Expect(p.dragged&&p.dismiss_tracker.Active()&&!p.dismiss_tracker.Armed(),"moving the strip reveals the target without arming it");
         send(WM_LBUTTONUP,at);
         Expect(!p.strip_hidden&&dismissed==0&&!p.expanded,"releasing away from the target keeps the strip");
         Expect(p.anchor.x!=before.x||p.anchor.y!=before.y,"ordinary drag still repositions the strip");
         Expect(!p.dismiss_tracker.Active(),"release resets the dismiss tracker");}

        // 2. Capture loss while armed cancels instead of hiding.
        {POINT at=grab();send(WM_LBUTTONDOWN,at);at.x-=100;send(WM_MOUSEMOVE,at);
         const auto c=zone_for(at).Center();send(WM_MOUSEMOVE,{c.x-150,c.y-150});send(WM_MOUSEMOVE,c);
         Expect(p.dismiss_tracker.Armed(),"dragging onto the target arms it");
         RECT r{};GetWindowRect(p.window,&r);
         Expect(std::abs((r.left+r.right)/2-c.x)<=1&&std::abs((r.top+r.bottom)/2-c.y)<=1,"armed strip is pulled into the target");
         const POINT remembered=p.anchor;ReleaseCapture();
         Expect(!p.drag_pending&&!p.dismiss_tracker.Active()&&!p.strip_hidden&&dismissed==0,"capture loss cancels without hiding");
         GetWindowRect(p.window,&r);Expect(std::abs(r.right-remembered.x)<=2&&std::abs(r.top-remembered.y)<=2,"cancelled armed strip returns to its remembered place");}

        // 3. Releasing on the target hides the strip and keeps its old position.
        p.Place();
        {POINT at=grab();const POINT before=p.anchor;send(WM_LBUTTONDOWN,at);at.x-=100;send(WM_MOUSEMOVE,at);
         const auto c=zone_for(at).Center();send(WM_MOUSEMOVE,{c.x,c.y-200});send(WM_MOUSEMOVE,c);
         Expect(p.dismiss_tracker.Armed(),"target armed before release");
         send(WM_LBUTTONUP,c);
         Expect(p.strip_hidden&&dismissed==1,"release on the target hides the strip and notifies the host once");
         Expect(p.anchor.x==before.x&&p.anchor.y==before.y,"hidden strip remembers its pre-drag position");
         Expect(!IsWindowVisible(p.folded_window)&&!IsWindowVisible(p.native_window)&&GetCapture()!=p.window,"no clipboard window stays visible or captured");
         Expect(!p.composition&&!p.parked_composition&&!p.target,"hidden strip releases composition and renderer");
         Expect(p.enabled&&p.listening,"clipboard history keeps listening while the strip is hidden");}

        // 4. Hidden strip: repaint requests and folding never bring it back.
        p.Present();p.Invalidate();p.Place();
        Expect(!IsWindowVisible(p.folded_window)&&p.strip_hidden,"repaint and placement keep the strip hidden");
        p.Show();
        Expect(p.expanded&&p.window==p.native_window,"shortcut/tray open still expands the full panel");
        p.Fold();
        Expect(!p.expanded&&p.strip_hidden&&!IsWindowVisible(p.folded_window)&&!p.composition,"folding returns to the hidden state");

        // 5. Policy survives disable/enable and restores through SetStripVisible.
        p.Enable(false,false);Expect(p.strip_hidden,"disabling the clipboard keeps the strip policy");
        p.Enable(true,false);Expect(!p.expanded&&p.strip_hidden&&!IsWindowVisible(p.folded_window),"re-enabled clipboard stays hidden");
        panel.SetStripVisible(true);
        Expect(!p.strip_hidden&&p.window==p.folded_window,"restoring from tray or settings brings back the strip");
        RECT shown{};GetWindowRect(p.folded_window,&shown);
        Expect(shown.right==p.anchor.x||std::abs(shown.right-p.anchor.x)<=2,"restored strip returns to its remembered position");
        panel.SetStripVisible(false);Expect(p.strip_hidden&&!IsWindowVisible(p.folded_window),"host can hide the strip directly");
        panel.SetStripVisible(false);Expect(dismissed==1,"host-driven changes do not re-notify the host");
        panel.SetStripVisible(true);

        // 6. Starting on a bottom-centre dock cannot hide the strip by a nudge.
        {const auto zone=zone_for(grab());const auto c=zone.Center();const float s=zone.Scale();
         const LONG w=LONG(28*s),h=LONG(96*s);
         SetWindowPos(p.window,nullptr,c.x-w/2,zone.work.bottom-h,w,h,SWP_NOZORDER|SWP_NOACTIVATE);p.compact_positioned=false;
         POINT at=grab();send(WM_LBUTTONDOWN,at);at.x+=LONG(24*s);send(WM_MOUSEMOVE,at);send(WM_MOUSEMOVE,{c.x,c.y});
         Expect(!p.dismiss_tracker.Armed(),"nudging a strip docked at the bottom centre does not arm");
         send(WM_LBUTTONUP,{c.x,c.y});
         Expect(!p.strip_hidden&&dismissed==1,"releasing that nudge keeps the strip");}

        // 7. The expanded header drag never involves the close target.
        p.Show();
        {RECT r{};GetWindowRect(p.window,&r);POINT at{(r.left+r.right)/2,r.top+int(68*p.scale)};
         send(WM_LBUTTONDOWN,at);const auto c=zone_for(at).Center();send(WM_MOUSEMOVE,{at.x-60,at.y+60});send(WM_MOUSEMOVE,c);
         Expect(!p.dismiss_tracker.Active()&&!p.dismiss_tracker.Armed(),"expanded panel drag shows no close target");
         send(WM_LBUTTONUP,c);Expect(p.expanded&&!p.strip_hidden,"expanded panel is never hidden by dragging");}
        p.Fold();
        p.Enable(false,false);
        Expect(!p.dismiss_target&&!p.dismiss_tracker.Active(),"disabling releases the dismiss target");
        return 0;
    }
};
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    lumashot::clipboard::DismissTargetTest::Tracker();
    lumashot::ClipboardPanelTest::Run();
    lumashot::clipboard::DismissTargetTest::Visual();
    CoUninitialize();
    std::cout<<"STRIP checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
