#include "ui/magnifier.h"
#include "ui/color_picker.h"
#include "export/png.h"
#include <objbase.h>
#include <iostream>
using namespace lumashot;
namespace lumashot {
struct MagnifierTest {
    static Frame Snapshot(const Magnifier& lens){
        auto result=MakeFrame({0,0,lens.size_.cx,lens.size_.cy});
        std::copy_n(lens.surface_->Pixels(),result.pixels.size(),result.pixels.begin());
        return result;
    }
};
}
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int failures{};auto expect=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    const RECT monitor{-300,-200,100,100};auto frame=MakeFrame(monitor);
    for(int y=0;y<frame.Height();++y)for(int x=0;x<frame.Width();++x)
        frame.pixels[static_cast<size_t>(y)*frame.Width()+x]=0xff000000|static_cast<uint32_t>((x%2?0xcccc00:0x224400)+(y%2?0x99:0x33));
    auto lens=MakeFrame({0,0,kMagnifierSize,kMagnifierSize});
    MagnifierPixels(frame,monitor,{-100,-80},lens.pixels);
    expect(lens.pixels[1*kMagnifierSize+1]==frame.pixels[110*frame.Width()+190],"physical source coordinates with negative monitor origin");
    expect(lens.pixels[6*kMagnifierSize+6]==lens.pixels[1*kMagnifierSize+1],"nearest-neighbor 6x pixel block");
    expect(lens.pixels[1*kMagnifierSize+7]==frame.pixels[110*frame.Width()+191],"next source pixel has its own block");
    expect(lens.pixels[61*kMagnifierSize+61]==0xff1686ff&&lens.pixels[0]==0xff1686ff,"blue crosshair and border");
    const auto edge=MagnifierBounds(monitor,{99,99});expect(edge.left>=monitor.left&&edge.top>=monitor.top&&edge.right<99&&edge.bottom<99,"lens flips away from cursor at bottom right");
    MagnifierPixels(frame,monitor,{99,99},lens.pixels);
    expect(lens.pixels[62*kMagnifierSize+62]==frame.pixels.back(),"crosshair tracks sampled pixel at monitor edge");
    const auto tiny=MakeFrame({-1,-1,0,0},0xffabcdef);MagnifierPixels(tiny,tiny.bounds,{-1,-1},lens.pixels);
    expect(lens.pixels[kMagnifierSize+1]==0xffabcdef,"tiny source stays in bounds");
    MagnifierPixels(frame,monitor,{-100,-80},lens.pixels);SavePng(lens,L"magnifier-preview.png");
    HWND owner=CreateWindowExW(0,L"STATIC",L"Magnifier test owner",WS_POPUP,0,0,300,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    {Magnifier magnifier;magnifier.Show(owner,frame,monitor,{-100,-80});
        const HWND window=FindWindowW(L"STATIC",L"LumaShot.Magnifier");
        const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
        expect((style&(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE),"lens passes mouse input and does not activate");
        magnifier.Hide();expect(!IsWindowVisible(window),"lens hides during selection");
        magnifier.Show(owner,frame,monitor,{-120,-90});expect(IsWindowVisible(window),"lens can reappear without reallocation");
        auto swatches=MakeFrame(monitor,0xff00a1ff);
        for(int y=0;y<swatches.Height();++y)
            std::fill_n(swatches.pixels.data()+y*swatches.Width()+swatches.Width()/2,swatches.Width()/2,0xffffffff);
        const POINT blue{-103,-90},white{-98,-90};
        magnifier.Show(owner,swatches,monitor,blue,SamplePixelColor(swatches,blue));
        const auto first=MagnifierTest::Snapshot(magnifier);
        SavePng(first,L"color-readout-preview.png");
        magnifier.Show(owner,swatches,monitor,white,SamplePixelColor(swatches,white));
        const auto next=MagnifierTest::Snapshot(magnifier);
        const auto footer=first.Width()*kMagnifierSize;
        expect(first.Height()>kMagnifierSize&&!std::equal(first.pixels.begin()+footer,first.pixels.end(),next.pixels.begin()+footer),"live color changes update footer text");
        const RECT wide{-900,-600,0,0};
        const auto solid=MakeFrame(wide,0xff00a1ff);
        for(LONG x:{-800L,-2L})for(LONG y:{-500L,-2L}){
            const POINT p{x,y};
            magnifier.Show(owner,solid,wide,p,0x00a1ff);
            RECT popup{};GetWindowRect(window,&popup);
            const auto actual=MagnifierTest::Snapshot(magnifier);
            const int near_x=x==-2?actual.Width()-1:0,near_y=y==-2?actual.Height()-1:0;
            expect(actual.pixels[near_y*actual.Width()+near_x]==0xff1686ff,"visible lens border faces pointer in every quadrant");
            expect(std::abs(popup.left+near_x-x)<=11&&std::abs(popup.top+near_y-y)<=11&&!PtInRect(&popup,p),"visible lens stays close without covering sampled point");
        }
        magnifier.Show(owner,frame,monitor,{-120,-90});
        expect(MagnifierTest::Snapshot(magnifier).Height()==kMagnifierSize,"plain capture modes hide the color readout");
        magnifier.Close();expect(!IsWindow(window),"closing releases lens window");}
    DestroyWindow(owner);CoUninitialize();return failures?1:0;
}
