// Real hidden panel messages, synthetic state only. Never samples or publishes
// clipboard contents, reads personal preferences, or injects system input.
#include "../src/clipboard/panel.cpp"
#include <array>
#include <iostream>

namespace lumashot {
struct ClipboardPanelTest {
    static int Run() {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int checks=0,failures=0,settings_calls=0;
        float case_scale=1,case_width=400;
        const auto expect=[&](bool ok,const char* message) {
            ++checks;
            if(!ok) {
                ++failures;
                std::cout<<"FAIL scale="<<case_scale<<" width="<<case_width<<" "<<message<<'\n';
            }
        };
        ClipboardPanel panel(nullptr,[&]{++settings_calls;});
        auto& p=*panel.impl_;
        p.test_mode=true;
        if(!p.Enable(true,false)) {
            std::cout<<"FAIL create isolated hidden panel\n";
            return 1;
        }
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(p.window,MONITOR_DEFAULTTONEAREST),&monitor);
        const auto mouse=[&](UINT message,float x,float y) {
            const auto px=static_cast<int>(std::lround(x*p.scale));
            const auto py=static_cast<int>(std::lround(y*p.scale));
            return SendMessageW(p.window,message,message==WM_LBUTTONUP?0:MK_LBUTTON,MAKELPARAM(px,py));
        };
        const auto prepare=[&] {
            if(GetCapture()==p.window)ReleaseCapture();
            p.CloseMore();p.expanded=true;p.pinned=false;p.UpdateActivationPolicy();
            p.scale=case_scale;p.W=case_width;p.H=480;p.ListBottom=p.H-62;
            p.tab=0;SetWindowTextW(p.search,L"");p.Filter();
            SetWindowPos(p.window,nullptr,monitor.rcWork.left+32,monitor.rcWork.top+32,
                static_cast<int>(std::lround(p.W*p.scale)),static_cast<int>(std::lround(p.H*p.scale)),
                SWP_NOZORDER|SWP_NOACTIVATE);
            p.LayoutSearch();p.Render();
        };
        const auto click_action=[&](int action) {
            const auto found=std::find_if(p.hits.begin(),p.hits.end(),
                [action](const auto& hit){return hit.action==action;});
            if(found==p.hits.end()) {expect(false,"expected control exists");return;}
            const auto rect=found->rect;
            mouse(WM_LBUTTONDOWN,(rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
            expect(!p.drag_pending,"interactive control is not a drag handle");
            mouse(WM_LBUTTONUP,(rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        };
        for(float scale:{1.f,1.25f,1.5f,2.f})for(float width:{280.f,320.f,400.f,560.f}) {
            case_scale=scale;case_width=width;prepare();
            // Cover branding, shortcut text, broad central whitespace, both side
            // margins, button gaps, and whitespace above/below the button row.
            const std::array<float,10> xs{8,36,90,175,p.W/2,p.W-120,p.W-85,p.W-51,p.W-17,p.W-8};
            for(float y:{8.f,23.f,40.f,56.f,65.f,75.f})for(float x:xs) {
                const bool control=std::any_of(p.hits.begin(),p.hits.end(),[&](const auto& hit) {
                    const auto r=hit.rect;
                    return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;
                });
                if(control)continue;
                mouse(WM_LBUTTONDOWN,x,y);
                expect(p.drag_pending&&GetCapture()==p.window,"full-width header press starts drag capture");
                mouse(WM_LBUTTONUP,x,y);
                expect(!p.drag_pending&&GetCapture()!=p.window&&p.expanded,"header release stays open and releases capture");
            }
            prepare();
            RECT before{},after{};
            GetWindowRect(p.window,&before);
            mouse(WM_LBUTTONDOWN,p.W/2,68);
            mouse(WM_MOUSEMOVE,p.W/2,68);
            expect(p.drag_pending&&!p.dragged,"stationary header press does not cross drag threshold");
            mouse(WM_MOUSEMOVE,p.W/2-24,92);
            GetWindowRect(p.window,&after);
            expect(p.drag_pending&&p.dragged&&!EqualRect(&before,&after),"blank-header mouse movement moves the native window");
            expect(p.anchor.x==after.right&&p.anchor.y==after.top,"native drag updates remembered anchor");
            mouse(WM_LBUTTONUP,p.W/2-24,92);
            expect(!p.drag_pending&&GetCapture()!=p.window,"moved header releases capture");

            mouse(WM_LBUTTONDOWN,175,40);
            ReleaseCapture();
            expect(!p.drag_pending,"capture loss cancels header drag");
            prepare();
            for(const D2D1_POINT_2F point:{D2D1_POINT_2F{p.W/2,76},D2D1_POINT_2F{p.W/2,98},D2D1_POINT_2F{10,125},D2D1_POINT_2F{p.W-8,180}}) {
                mouse(WM_LBUTTONDOWN,point.x,point.y);
                expect(!p.drag_pending,"search, category and content areas do not start header drag");
                mouse(WM_LBUTTONUP,point.x,point.y);
            }
            SetWindowTextW(p.search,L"synthetic header drag query");
            expect(p.query==L"synthetic header drag query"&&!p.drag_pending,"native search edit still updates query");
            SetWindowTextW(p.search,L"");p.Render();
            click_action(21);
            expect(p.tab==1,"category tab still switches normally");

            for(int action:{10,11,12}) {
                prepare();
                const int settings_before=settings_calls;
                click_action(action);
                if(action==10)expect(p.pinned,"pin button still toggles pinning");
                if(action==11)expect(settings_calls==settings_before+1&&!p.expanded,"settings button still invokes callback");
                if(action==12)expect(p.menu_open,"more button still opens menu");
            }
            prepare();
            p.OpenMore();
            mouse(WM_LBUTTONDOWN,p.W/2,12);
            expect(!p.menu_open&&!p.drag_pending,"outside menu click dismisses without starting a drag");
            mouse(WM_LBUTTONUP,p.W/2,12);
            prepare();p.pinned=true;p.UpdateActivationPolicy();
            const HWND foreground=GetForegroundWindow();
            expect(SendMessageW(p.window,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"pinned panel keeps no-activate policy");
            mouse(WM_LBUTTONDOWN,p.W/2,68);
            expect(p.drag_pending&&p.pinned&&GetForegroundWindow()==foreground,"pinned blank-header drag does not steal focus");
            mouse(WM_LBUTTONUP,p.W/2,68);

            GetWindowRect(p.window,&before);
            expect(SendMessageW(p.window,WM_NCHITTEST,0,MAKELPARAM(before.left+1,before.top+1))==HTTOPLEFT,
                "top corner keeps native resize hit testing");
            expect(SendMessageW(p.window,WM_NCHITTEST,0,MAKELPARAM((before.left+before.right)/2,before.top+1))==HTTOP,
                "top edge keeps native resize hit testing");
            std::cout<<"CASE scale="<<scale<<" width="<<width<<" complete\n";
        }
        prepare();p.Fold();
        mouse(WM_LBUTTONDOWN,10,40);
        expect(p.drag_pending,"folded strip remains draggable");
        mouse(WM_MOUSEMOVE,-22,64);
        expect(p.dragged,"folded strip movement still crosses drag threshold");
        mouse(WM_LBUTTONUP,-22,64);
        expect(!p.expanded&&!p.drag_pending,"dragging folded strip does not expand it");
        p.Enable(false,false);
        std::cout<<"HEADER_DRAG checks="<<checks<<" failures="<<failures<<'\n';
        return failures?1:0;
    }
};
}
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const int result=lumashot::ClipboardPanelTest::Run();
    CoUninitialize();
    return result;
}
