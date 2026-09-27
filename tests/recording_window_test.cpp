#include "recording/core.h"
#include "recording/encoder.h"
#include <iostream>
#include <mutex>
#include <mfreadwrite.h>
#include <psapi.h>
using namespace lumashot::recording;
namespace {
int ticks{};
LRESULT CALLBACK Target(HWND w,UINT message,WPARAM wp,LPARAM lp){if(message==WM_TIMER){++ticks;InvalidateRect(w,nullptr,FALSE);return 0;}if(message==WM_PAINT){PAINTSTRUCT ps{};auto dc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);HBRUSH b=CreateSolidBrush(RGB(30,80,180));FillRect(dc,&r,b);DeleteObject(b);RECT moving{20+ticks%180,50,100+ticks%180,150};b=CreateSolidBrush(RGB(240,180,40));FillRect(dc,&moving,b);DeleteObject(b);EndPaint(w,&ps);return 0;}return DefWindowProcW(w,message,wp,lp);}
}
int main(){CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);int code=0;
    try{WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Target;wc.lpszClassName=L"LumaShot.SyntheticRecordingTarget";RegisterClassW(&wc);const HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Synthetic recording test",WS_OVERLAPPEDWINDOW,80,80,640,360,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);SetTimer(window,1,33,nullptr);
        const auto file=std::filesystem::current_path()/L"recording-test-output"/L"wgc-window.mp4";std::filesystem::create_directories(file.parent_path());std::mutex mutex;Status status;Session session;Options options;options.target=window;options.system_audio=false;options.cursor=false;options.width=320;
        session.Start(options,file,[&](Status next){std::lock_guard lock(mutex);status=std::move(next);});const auto start=GetTickCount64();bool paused=false,resumed=false,stop=false;Status result;
        while(GetTickCount64()-start<15000){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}const auto elapsed=GetTickCount64()-start;{std::lock_guard lock(mutex);result=status;}
            if(elapsed>1000&&!paused){session.Pause(true);paused=true;}if(elapsed>1400&&!resumed){session.Pause(false);resumed=true;}if(elapsed>2800&&!stop){session.Stop();stop=true;}if(result.state==State::Preview||result.state==State::Failed)break;MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        session.Stop();KillTimer(window,1);DestroyWindow(window);
        if(result.state!=State::Preview){const int length=WideCharToMultiByte(CP_UTF8,0,result.detail.c_str(),-1,nullptr,0,nullptr,nullptr);std::string detail(size_t(length),0);WideCharToMultiByte(CP_UTF8,0,result.detail.c_str(),-1,detail.data(),length,nullptr,nullptr);std::cerr<<"FAIL real WGC synthetic window: "<<detail<<std::endl;code=1;}else{
            std::cout<<"PASS real WGC -> GPU -> verified hardware H264, frames="<<result.frames<<", media seconds="<<result.time/10000000.<<", dropped="<<result.dropped<<std::endl;
            if(result.frames<20||result.time>27000000||result.time<10000000){std::cerr<<"FAIL pause/resume output duration"<<std::endl;code=1;}else std::cout<<"PASS pause/resume excludes paused wall time"<<std::endl;
            ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(file.c_str(),nullptr,&reader),"Open real WGC output");ComPtr<IMFMediaType> native;Check(reader->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&native),"Output video type");UINT w{},h{};MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&w,&h);if(w!=320||!h){std::cerr<<"FAIL recording output dimensions"<<std::endl;code=1;}else std::cout<<"PASS real WGC file stream dimensions"<<std::endl;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;code=1;}MFShutdown();CoUninitialize();return code;}
