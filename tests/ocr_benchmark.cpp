#include "ocr/engine.h"
#include "ui/render.h"
#include "export/png.h"
#include <psapi.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <atomic>
#include <thread>
using namespace lumashot;
static std::wstring Normalize(std::wstring s){std::erase_if(s,[](wchar_t c){return c==L'\r'||c==L'\n';});return s;}
static size_t Distance(const std::wstring& a,const std::wstring& b){std::vector<size_t> row(b.size()+1);std::iota(row.begin(),row.end(),size_t(0));for(size_t i=0;i<a.size();++i){size_t prev=row[0];row[0]=i+1;for(size_t j=0;j<b.size();++j){size_t old=row[j+1];row[j+1]=std::min({row[j]+1,row[j+1]+1,prev+(a[i]!=b[j]?1u:0u)});prev=old;}}return row.back();}
static Frame Fixture(int index,int lines,std::wstring& expected,bool full=false) {
    const bool dark=index%2!=0;auto frame=MakeFrame({0,0,full?1920:760,full?1080:240},dark?0xff202733:0xffffffff);
    Document document;const float size=full?22.0f:float(16+(index%5)*2);
    const std::wstring samples[]={L"截图文字可以直接选择复制",L"LumaShot offline text selection",L"版本 Version 2026",L"文件名称 Screenshot.png",L"精度与资源占用均衡",L"Hello Windows 1234567890",L"保存图片并继续工作",L"Copy text with Ctrl+C"};
    for(int i=0;i<lines;++i){Mark m;m.tool=Tool::Text;m.a={24,float(12+i*(full?48:52))};m.b={float(frame.Width()-20),m.a.y+size+12};m.font_size=size;m.color=dark?0xfff2f4f8:0xff202733;m.text=samples[(index+i)%8];if(i==0)m.text+=L" "+std::to_wstring(1000+index);document.Add(m);expected+=m.text;}
    Renderer renderer;return renderer.Flatten(frame,document,frame.bounds);
}
static Frame DenseFixture() {
    auto frame=MakeFrame({0,0,1920,1080},0xfff7f8fa);Document document;
    const std::wstring labels[]={L"复制",L"保存",L"设置",L"撤销",L"完成",L"关闭",L"编辑",L"文本",L"文件",L"图片",L"选择",L"导出",L"Copy",L"Save",L"Open",L"Help"};
    for(int column=0;column<4;++column)for(int row=0;row<16;++row){Mark m;m.tool=Tool::Text;m.a={float(30+column*460),float(40+row*60)};m.b={m.a.x+400,m.a.y+40};m.font_size=float(18+column*2);m.color=0xff202733;m.text=labels[(column+row)%16];document.Add(m);}
    Renderer renderer;return renderer.Flatten(frame,document,frame.bounds);
}
int main(int argc,char** argv) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int code=0;
    std::atomic_size_t peak_private{};
    std::jthread sampler([&](std::stop_token stop){while(!stop.stop_requested()){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)))peak_private.store(std::max(peak_private.load(),m.PrivateUsage));Sleep(5);}});
    try {
        const auto folder=std::filesystem::absolute(argv[0]).parent_path();
        SavePng(DenseFixture(),folder/L"ocr-dense-ui.png");
        const auto start=std::chrono::steady_clock::now();ocr::Engine engine(folder/L"ocr");
        std::cout<<"model_load_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;
        size_t errors=0,characters=0;std::vector<double> times;
        const int count=argc>1?std::stoi(argv[1]):100;
        for(int i=0;i<count;++i){std::wstring expected;auto frame=Fixture(i,4,expected);const auto t=std::chrono::steady_clock::now();const auto result=engine.Recognize(frame);const auto got=Normalize(ocr::Selected(result,ocr::All(result)));const auto distance=Distance(expected,got);errors+=distance;characters+=expected.size();times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());
            if(i<2)SavePng(frame,folder/(i?L"ocr-fixture-dark.png":L"ocr-fixture-light.png"));
            if(i%10==9)std::cout<<"completed="<<i+1<<" CER="<<double(errors)/double(characters)<<std::endl;
        }
        std::sort(times.begin(),times.end());
        std::cout<<"screenshots="<<count<<" characters="<<characters<<" errors="<<errors<<" CER="<<double(errors)/double(characters)<<" fixture_P95_ms="<<times[static_cast<size_t>(double(times.size()-1)*0.95)]<<std::endl;
        std::vector<double> full;
        for(int i=0;i<10;++i){std::wstring expected;auto frame=Fixture(i,20,expected,true);const auto t=std::chrono::steady_clock::now();ocr::Timings stages;const auto result=engine.Recognize(frame,&stages);if(i==0)std::cout<<"stages preprocess="<<stages.preprocess_ms<<" detect="<<stages.detection_ms<<" rec="<<stages.recognition_ms<<" decode="<<stages.decode_ms<<std::endl;full.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());}
        std::sort(full.begin(),full.end());PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
        std::cout<<"1080p_20lines_P95_ms="<<full.back()<<" peak_working_set_MB="<<double(memory.PeakWorkingSetSize)/1048576<<" private_MB="<<double(memory.PrivateUsage)/1048576<<std::endl;
        std::cout<<"sampled_peak_private_MB="<<double(peak_private.load())/1048576<<std::endl;
        if(double(errors)/double(characters)>0.02)code=2;
    }catch(const std::exception& e){std::cout<<e.what()<<std::endl;code=1;}
    CoUninitialize();return code;
}
