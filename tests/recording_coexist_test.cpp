#include "app/application.h"
#include <iostream>
namespace lumashot {
struct RecordingCoexistTest {
    static int Run(){Application app;app.demo_=true;app.diagnostic_session_=true;const auto instance=GetModuleHandleW(nullptr);WNDCLASSW host{};host.hInstance=instance;host.lpfnWndProc=Application::MainProc;host.lpszClassName=L"LumaShot.CoexistHost";RegisterClassW(&host);app.main_=CreateWindowW(host.lpszClassName,L"Synthetic host",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,&app);WNDCLASSW overlay{};overlay.hInstance=instance;overlay.lpfnWndProc=Application::OverlayProc;overlay.lpszClassName=L"LumaShot.Overlay";RegisterClassW(&overlay);if(!app.recording_process_.Start(true,false)){std::cerr<<"worker unavailable"<<std::endl;return 1;}const bool live=app.recording_process_.Active();SendMessageW(app.main_,WM_HOTKEY,1,0);const bool okay=live&&app.active_&&!app.views_.empty()&&app.recording_process_.Active();std::cout<<(okay?"PASS ":"FAIL ")<<"screenshot hotkey opens synthetic capture while recording worker remains alive"<<std::endl;app.Cancel(false);return okay?0:1;}
};
}
int main(){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;try{result=lumashot::RecordingCoexistTest::Run();}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;}CoUninitialize();return result;}
