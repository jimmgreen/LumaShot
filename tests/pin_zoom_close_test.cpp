// Exercise production window handlers with synthetic images and OCR data only.
#include "../src/pin/pin.cpp"
#include <iostream>
#include <limits>
namespace lumashot {
struct PinTest {
    static int Run(){
        int failures{};auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failures+=!ok;};
        for(auto style:{PinStyle::None,PinStyle::Simple,PinStyle::Rounded,PinStyle::Polaroid,PinStyle::Curl})
        for(float dpi:{96.f,144.f,192.f})for(POINT size:{POINT{1,1},POINT{320,180},POINT{3840,2160},POINT{12000,40}}){
            const float limit=PinZoomLimit(size.x,size.y,dpi,style);
            const auto p=MakePaperLayout(std::max(1,int(std::lround(size.x*limit))),std::max(1,int(std::lround(size.y*limit))),dpi,style);
            const uint64_t w=uint64_t(p.width)+2*p.shadow+2,h=uint64_t(p.height)+2*p.shadow+2;
            expect(limit>0&&std::isfinite(limit)&&w<=PinSurfaceMaxEdge&&h<=PinSurfaceMaxEdge&&w*h<=PinSurfaceMaxPixels,"resource guard includes frame and shadow at every style and DPI");
            const float high=PinZoomGoal(1,std::numeric_limits<int>::max(),size.x,size.y,dpi,style);
            const float low=PinZoomGoal(1,std::numeric_limits<int>::min(),size.x,size.y,dpi,style);
            expect(std::isfinite(high)&&high<=limit&&low>0&&low<=.1f,"extreme wheel input cannot overflow or produce invalid scale");
        }
        expect(PinZoomLimit(320,180,96,PinStyle::Simple)>4,"small images have no fixed 4x cap");
        expect(PinZoomLimit(3840,2160,96,PinStyle::Simple)*3840>4096,"large images can exceed the old 4096px cap");
        HWND host=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        {
            PinManager manager(host);auto image=MakeFrame({0,0,900,24},0xffcce5ff);
            manager.Create(image,image,{100,100},std::nullopt,{},false);auto& zoomed=*manager.pins_.rbegin()->second;
            const HWND zoom_window=zoomed.window;POINT anchor{zoomed.Inset()+100,zoomed.Inset()+10};ClientToScreen(zoom_window,&anchor);
            SendMessageW(zoom_window,WM_MOUSEWHEEL,MAKEWPARAM(0,16*WHEEL_DELTA),MAKELPARAM(anchor.x,anchor.y));
            expect(zoomed.zoom_goal>4,"real wheel handler targets beyond 4x");
            zoomed.zoom_started=GetTickCount64()-200;SendMessageW(zoom_window,WM_TIMER,101,0);
            RECT r{};GetWindowRect(zoom_window,&r);const auto q=zoomed.WindowPoint({100,10});
            expect(zoomed.zoom>4&&zoomed.CanvasWidth()>4096&&zoomed.surface_width==zoomed.CanvasWidth(),"real layered renderer presents a surface beyond both former caps");
            expect(std::abs(r.left+q.x-anchor.x)<.1f&&std::abs(r.top+q.y-anchor.y)<.1f,"extended zoom preserves cursor anchor");
            expect(zoomed.image->pixels==image.pixels&&!zoomed.ocr_enabled,"extended zoom preserves source and does not start OCR");
            SendMessageW(zoom_window,WM_MOUSEWHEEL,MAKEWPARAM(0,-16*WHEEL_DELTA),MAKELPARAM(anchor.x,anchor.y));
            zoomed.zoom_started=GetTickCount64()-200;SendMessageW(zoom_window,WM_TIMER,101,0);
            expect(std::abs(zoomed.zoom-1)<.001f,"reverse zoom returns to original size");
            for(int mode=0;mode<4;++mode){
                auto fixture=MakeFrame({0,0,160,90},0xffedf3fa);manager.Create(fixture,fixture,{180,180},std::nullopt,{},false);
                auto& selected=*manager.pins_.rbegin()->second;const HWND window=selected.window;const auto id=selected.id;
                if(mode!=0){ocr::Line line;line.box={10,10,60,30};line.glyphs.push_back({L"test",line.box,1});selected.text.lines.push_back(line);selected.selection=ocr::All(selected.text);manager.UpdateTools(selected);}
                if(mode==3)selected.locked=true;
                const HWND tools=selected.tools;
                if(mode!=0)expect(tools&&IsWindowVisible(tools)&&!selected.selection.Empty(),"synthetic text selection shows real tools");
                if(mode==0){manager.Zoom(selected,120,{200,200});expect(selected.zoom_animating,"animation active before close");}
                const HWND focus=mode==2?tools:window;SetFocus(focus);
                SendMessageW(focus,WM_KEYDOWN,VK_ESCAPE,0);
                expect(!IsWindow(window)&&(!tools||!IsWindow(tools))&&!selected.zoom_animating,"Escape closes focused pin and owned tools, including text-selected and locked pins");
                expect(IsWindow(zoom_window),"Escape leaves other pins untouched");
                manager.Ready();expect(!manager.pins_.contains(id),"closed pin is removed without stale state");
            }
            SendMessageW(zoom_window,WM_KEYDOWN,VK_ESCAPE,1LL<<30);
            expect(IsWindow(zoom_window),"held Escape does not cascade-close another pin");
            SendMessageW(zoom_window,WM_KEYDOWN,VK_ESCAPE,0);manager.Ready();expect(manager.pins_.empty(),"fresh Escape closes remaining focused pin");
        }
        DestroyWindow(host);return failures?1:0;
    }
};
}
int main(){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;try{result=lumashot::PinTest::Run();}catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<std::endl;}CoUninitialize();return result;}
