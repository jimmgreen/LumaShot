#include "ui/magnifier.h"
#include "export/png.h"
#include <objbase.h>
#include <iostream>
using namespace lumashot;
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
    const auto edge=MagnifierBounds(monitor,{99,99});expect(edge.right==monitor.right&&edge.bottom==monitor.bottom,"lens stays inside monitor at bottom right");
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
        magnifier.Close();expect(!IsWindow(window),"closing releases lens window");}
    DestroyWindow(owner);CoUninitialize();return failures?1:0;
}
