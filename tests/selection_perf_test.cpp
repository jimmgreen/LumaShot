#include "ui/render.h"
#include <algorithm>
#include <chrono>
#include <iostream>
using namespace lumashot;
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"LumaShot synthetic rendering test",WS_POPUP,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 2;ShowWindow(window,SW_SHOWNOACTIVATE);
    int result=0;
    try{auto frame=MakeFrame({0,0,1920,1080},0xff345678);auto acrylic=BlurBackdrop(frame);Renderer renderer;Document document;ViewState state;state.dragging=true;state.hint=L"Synthetic selection";
        std::vector<double> samples;
        for(int i=0;i<125;++i){state.selection={100,100,float(400+i*10),float(300+i*4)};
            auto start=std::chrono::steady_clock::now();
            while(!renderer.Paint(window,frame.bounds,frame,acrylic,document,std::nullopt,state)){
                const HANDLE ready=renderer.FrameEvent();if(!ready||WaitForSingleObject(ready,1000)!=WAIT_OBJECT_0)throw std::runtime_error("frame readiness timeout");
                renderer.FrameReady();
            }
            if(i>=5)samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
        std::sort(samples.begin(),samples.end());std::cout<<"1080p synthetic drag render ms: median="<<samples[60]<<" p95="<<samples[113]<<" max="<<samples.back()<<'\n';
    }catch(const std::exception& e){std::cout<<e.what()<<'\n';result=1;}
    DestroyWindow(window);CoUninitialize();return result;
}
