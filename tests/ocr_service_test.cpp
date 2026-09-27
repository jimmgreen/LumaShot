#include "ocr/service.h"
#include "ui/render.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <tlhelp32.h>
using namespace lumashot;
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;
    try {
        auto frame=MakeFrame({0,0,1920,1080},0xffffffff);Document document;
        for(int i=0;i<20;++i){Mark m;m.tool=Tool::Text;m.a={24,float(12+i*48)};m.b={1000,m.a.y+36};m.font_size=22;m.text=L"LumaShot 中英文字选择复制 "+std::to_wstring(i);m.color=0xff202733;document.Add(m);}
        Renderer renderer;auto image=std::make_shared<Frame>(renderer.Flatten(frame,document,frame.bounds));std::vector<double> cold;
        for(int run=0;run<10;++run){const auto t=std::chrono::steady_clock::now();ocr::Service service(nullptr,WM_APP+20);service.Submit(1,1,image);
            const auto deadline=GetTickCount64()+10000;bool complete=false;
            while(GetTickCount64()<deadline){auto results=service.Take();if(!results.empty()){complete=results.front().error.empty()&&!results.front().text.lines.empty();if(!complete)std::wcerr<<results.front().error<<'\n';break;}Sleep(5);}
            if(!complete)++failures;
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();cold.push_back(ms);std::cout<<"cold_ipc_ms="<<ms<<std::endl;
        }
        std::sort(cold.begin(),cold.end());std::cout<<"cold_1080p_20lines_P95_ms="<<cold.back()<<std::endl;
        std::cout<<(failures?"FAIL":"PASS")<<" repeated cold launches with real model and shared memory"<<std::endl;
        // Canceled queued results must never be observable, even with reused ids.
        ocr::Service service(nullptr,WM_APP+20);service.Submit(7,1,image);service.Cancel(7);service.Submit(7,2,image);
        const auto deadline=GetTickCount64()+10000;bool latest=false;
        while(GetTickCount64()<deadline){for(auto& r:service.Take()){if(r.version!=2)++failures;latest=r.version==2&&r.error.empty();}if(latest)break;Sleep(5);}
        std::cout<<(latest?"PASS":"FAIL")<<" canceled version never replaces latest"<<std::endl;if(!latest)++failures;
        service.Submit(7,3,image);
        {ocr::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W entry{sizeof(entry)};
            if(Process32FirstW(snapshot.Get(),&entry))do{if(entry.th32ParentProcessID==GetCurrentProcessId()&&std::wstring(entry.szExeFile)==L"lumashot_ocr_worker.exe") {
                ocr::Handle process(OpenProcess(PROCESS_TERMINATE,FALSE,entry.th32ProcessID));if(process.Get())TerminateProcess(process.Get(),99);
            }}while(Process32NextW(snapshot.Get(),&entry));}
        bool failed=false;const auto crash_deadline=GetTickCount64()+5000;
        while(GetTickCount64()<crash_deadline){auto results=service.Take();if(!results.empty()){failed=!results.front().error.empty();break;}Sleep(5);}
        std::cout<<(failed?"PASS":"FAIL")<<" worker crash becomes a recoverable error"<<std::endl;if(!failed)++failures;
        service.Submit(7,4,image);bool recovered=false;const auto retry_deadline=GetTickCount64()+10000;
        while(GetTickCount64()<retry_deadline){auto results=service.Take();if(!results.empty()){recovered=results.front().error.empty()&&results.front().version==4;break;}Sleep(5);}
        std::cout<<(recovered?"PASS":"FAIL")<<" retry starts a new worker after crash"<<std::endl;if(!recovered)++failures;
    }catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<std::endl;++failures;}
    CoUninitialize();return failures?1:0;
}
