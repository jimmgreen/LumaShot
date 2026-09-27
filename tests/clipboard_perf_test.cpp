#include "export/clipboard.h"
#include <objbase.h>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <vector>
using Clock=std::chrono::steady_clock;
double Ms(Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();}
double Median(std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];}
template<class F> double TimeSuccessful(F action){for(int retry=0;;++retry){const auto start=Clock::now();try{action();return Ms(start);}catch(const std::exception& e){if(retry>=100||std::string(e.what()).find("Clipboard is busy")==std::string::npos)throw;Sleep(10);}}}
int main(){
 using namespace lumashot;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 HWND owner=CreateWindowExW(0,L"STATIC",L"Synthetic clipboard benchmark",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);int result=0;
 try{for(const auto size:{POINT{1920,1080},POINT{3840,2160}}){
 auto frame=MakeFrame({0,0,size.x,size.y});for(size_t i=0;i<frame.pixels.size();++i)frame.pixels[i]=0xff000000|static_cast<uint32_t>(i*7919&0xffffff);
 std::vector<double> sync,prepare,publish;for(int run=0;run<8;++run){
 Sleep(40);const double old=TimeSuccessful([&]{CopyImage(owner,frame);});
 Sleep(40);auto start=Clock::now();auto staged=PrepareClipboardImage(frame);const double prep=Ms(start);
 const double ui=TimeSuccessful([&]{PublishClipboardImage(owner,*staged);});
 if(run){sync.push_back(old);prepare.push_back(prep);publish.push_back(ui);}}
 std::cout<<size.x<<'x'<<size.y<<" median synchronous_ui_ms="<<Median(sync)<<" background_prepare_ms="<<Median(prepare)<<" new_ui_publish_ms="<<Median(publish)<<" image_MiB="<<frame.pixels.size()*4./1048576<<std::endl;
 }}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;result=1;}
 if(OpenClipboard(owner)){EmptyClipboard();CloseClipboard();}DestroyWindow(owner);CoUninitialize();return result;
}



