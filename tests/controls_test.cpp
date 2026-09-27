#include "ui/render.h"
#include "export/png.h"
#include <iostream>
#include <cmath>
using namespace lumashot;
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures{};
    const auto expect=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    ui::LinearTransition transition;
    expect(!transition.Active(100)&&transition.Value(100)==0,"collapsed transition does no idle work");
    transition.Target(1,100);
    expect(transition.Value(140)==.25f&&transition.Value(180)==.5f,"expansion progresses linearly over time");
    transition.Target(0,180);
    expect(transition.Value(180)==.5f&&transition.Value(260)==.25f,"reversing mid-animation preserves current height");
    expect(!transition.Active(340)&&transition.Value(1000)==0,"finished animation stops at exact collapsed height");
    for(float scale:{1.f,1.5f,2.f})for(float bottom:{200.f,900.f}) {
        const Box selection{-800,100,-100,bottom};const RECT monitor{-1280,0,0,1080};
        const auto compact=PlaceToolbar(selection,monitor,scale,0),middle=PlaceToolbar(selection,monitor,scale,.5f),full=PlaceToolbar(selection,monitor,scale,1);
        const auto height=[](Box b){return b.bottom-b.top;};
        expect(std::abs(height(middle.bounds)-(height(compact.bounds)+height(full.bounds))/2)<.01f,"panel height interpolates linearly at DPI and display edges");
        ViewState partial;partial.selected=true;partial.tool=Tool::Rectangle;partial.toolbar=compact;Document empty;
        const auto properties=ToolbarControls(partial,empty);
        bool hidden=true;for(const auto& c:properties)if(c.id>=20)hidden=hidden&&!c.Interactive();
        expect(hidden,"hidden properties cannot receive clicks during expansion");
    }
    for(float s:{1.f,1.5f,2.f}) {
        const Box viewport{-1000*s,0,1000*s,800*s},anchor{-200*s,100*s,-172*s,128*s};
        const auto tip=ui::TooltipBounds(anchor,viewport,{50*s,16*s},s);
        expect(std::abs(tip.right-tip.left-68*s)<.01f&&std::abs((tip.left+tip.right)-(anchor.left+anchor.right))<.01f&&tip.bottom<anchor.top,"tooltip fits measured text and centers above button at every DPI");
        const auto edge=ui::TooltipBounds({980*s,2*s,998*s,30*s},viewport,{90*s,16*s},s);
        expect(edge.right<=viewport.right&&edge.left>=viewport.left&&edge.top>=30*s,"tooltip remains on screen and falls below only when top has no room");
        const auto compact=PlaceToolbar({100*s,100*s,900*s,300*s},{0,0,LONG(1280*s),LONG(800*s)},s,0);
        bool centered=true;for(int id=0;id<14;++id){const auto button=compact.Button(id);centered=centered&&std::abs((button.top+button.bottom)-(compact.bounds.top+compact.bounds.bottom))<.01f;}
        expect(centered,"all compact toolbar buttons share the panel vertical center");
        const Box b{-300*s,-20*s,-100*s,8*s};
        for(float value:{0.f,.25f,.5f,1.f})expect(std::abs(ui::SliderValue(b,ui::SliderThumb(b,value,s),s)-value)<.0001f,"slider input and painting share geometry at DPI and negative origin");
        expect(ui::SliderValue(b,{-10000,0},s)==0&&ui::SliderValue(b,{10000,0},s)==1,"captured slider drag clamps outside its bounds");
    }
    ui::Control button;button.id=2;button.kind=ui::Kind::Button;button.bounds={0,0,80,32};
    std::vector<ui::Control> items{button};expect(ui::HitTest(items,{20,20})==2,"button hit");items[0].enabled=false;expect(ui::HitTest(items,{20,20})==-1,"disabled button is not interactive");
    ViewState state;state.selected=true;state.tool=Tool::Rectangle;state.toolbar=PlaceToolbar({0,0,900,300},{0,0,1280,800},1);
    expect(!SetToolbarSlider(state,46,{900,700}),"fill transparency disabled without fill");state.fill_color=ToolbarColor(42);
    const auto track=ui::SliderTrack(state.toolbar.Property(46),1);SetToolbarSlider(state,46,{track.right,track.top});expect(state.fill_opacity==0,"100 percent transparency produces zero opacity");
    SetToolbarSlider(state,47,{10000,0});expect(state.corner_radius==32,"corner radius clamps");
    state.tool=Tool::Ellipse;expect(!SetToolbarSlider(state,47,{0,0}),"ellipse has no corner control");
    try {
        auto frame=MakeFrame({0,0,100,80},0xffffffff);Document document;Mark mark;mark.a={10,10};mark.b={90,70};mark.color=0xff000000;mark.width=2;mark.fill_color=0xff0000ff;mark.fill_opacity=.5f;mark.corner_radius=20;document.Add(mark);
        Renderer renderer;const auto image=renderer.Flatten(frame,document,frame.bounds);
        const uint32_t center=image.pixels[40*100+50];expect((center&255)==255&&((center>>16)&255)>=126&&((center>>16)&255)<=129,"fill alpha blends into export");
        expect(image.pixels[11*100+11]==0xffffffff,"rounded corner does not fill outside rounded shape");
        expect(frame.pixels[40*100+50]==0xffffffff,"render leaves frozen source immutable");SavePng(image,L"rounded-fill-preview.png");
        document.Undo();expect(document.marks.empty(),"undo stores new shape properties");document.Redo();expect(document.marks[0]==mark,"redo restores fill opacity and radius");
        for(bool dark:{false,true}) {
            const auto preview=renderer.Demo(true,dark,Tool::Rectangle);
            const auto layout=PlaceToolbar({180,90,1110,600},preview.bounds,1);const auto wheel=layout.Property(28);
            const int x=int((wheel.left+wheel.right)/2)-10,y=int((wheel.top+wheel.bottom)/2)-10;
            const uint32_t corner=preview.pixels[y*preview.Width()+x];
            expect(((corner>>16)&255)+((corner>>8)&255)+(corner&255)>100,"custom color wheel transparent corner shows panel instead of black");
        }
    }catch(const std::exception& e){std::cout<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}
