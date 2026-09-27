#define LUMASHOT_RECORDING_UI_TEST
#include "../src/recording/worker.cpp"
#include <condition_variable>
#include <iostream>
#include <wincodec.h>
using namespace lumashot::recording;
namespace {
void Expect(bool ok,const char* label){if(!ok)throw std::runtime_error(label);std::cout<<"PASS "<<label<<std::endl;}
Status Synthetic(const std::filesystem::path& file){
 std::mutex m;std::condition_variable cv;Status final;bool done=false;Session session;Options options;
 options.synthetic=true;options.synthetic_frames=30;options.software=true;options.width=320;options.system_audio=false;
 session.Start(options,file,[&](Status s){if(s.state==State::Failed||s.state==State::Preview){std::lock_guard lock(m);final=std::move(s);done=true;cv.notify_one();}});
 std::unique_lock lock(m);if(!cv.wait_for(lock,std::chrono::seconds(30),[&]{return done;}))throw std::runtime_error("Synthetic recording timed out");
 Expect(final.state==State::Preview,"synthetic frames only, no desktop/audio capture");return final;
}
void Pump(Ui& u){const auto until=GetTickCount64()+90000;while(u.exporting&&GetTickCount64()<until){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessageW(&msg);}}Sleep(10);}Expect(!u.exporting,"result export completes without save dialog");}
void Window(Ui& u){WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Ui::Proc;wc.lpszClassName=L"LumaShot.ResultFixture";RegisterClassW(&wc);u.window=CreateWindowW(wc.lpszClassName,L"Synthetic hidden fixture",WS_POPUP,0,0,640,480,nullptr,nullptr,wc.hInstance,&u);Expect(u.window!=nullptr,"isolated hidden fixture created");}
}
int main(){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);MFStartup(MF_VERSION);int result=0;
try{
 auto root=std::filesystem::current_path()/(L"record-result-fixture-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root);
 for(bool gif:{false,true}){Ui u;u.client_directory=root/(gif?L"gif":L"video");std::filesystem::create_directory(u.client_directory);u.folder=u.client_directory/L"work";std::filesystem::create_directory(u.folder);u.temporary=u.folder/L"capture.mp4";u.status=Synthetic(u.temporary);u.gif=gif;u.fps=10;u.width=160;u.trim_end=u.status.time;Window(u);
  u.Export(gif);Pump(u);Expect(u.client_exit==0&&!IsWindow(u.window),"successful export closes worker with result status");
  const auto output=u.client_directory/(gif?L"result.gif":L"result.mp4");Expect(std::filesystem::file_size(output)>32,"result file exists");
  std::filesystem::path blocked;Expect(!u.SavePath(gif,blocked),"existing result is never overwritten");
  if(gif){lumashot::recording::ComPtr<IWICImagingFactory> f;lumashot::recording::Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)),"WIC");lumashot::recording::ComPtr<IWICBitmapDecoder> d;lumashot::recording::Check(f->CreateDecoderFromFilename(output.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&d),"GIF");UINT frames{};d->GetFrameCount(&frames);Expect(frames>1,"returned GIF is animated");}
 }
 {Ui u;u.client_directory=root/L"cancel";std::filesystem::create_directory(u.client_directory);u.folder=u.client_directory/L"work";std::filesystem::create_directory(u.folder);Window(u);u.Close();Expect(u.client_exit==2&&!IsWindow(u.window),"cancel has distinct status and no export");Expect(!std::filesystem::exists(u.client_directory/L"result.mp4"),"cancel inserts no media");}
 bool invalid=false;try{RunUi(false,false,{},root/L"missing");}catch(...){invalid=true;}Expect(invalid,"missing result directory rejected before recording UI");
 std::cout<<"All recording result tests passed; fixtures under build."<<std::endl;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;result=1;}MFShutdown();CoUninitialize();return result;}
