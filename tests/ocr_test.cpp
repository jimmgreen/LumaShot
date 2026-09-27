#include "ocr/engine.h"
#include "export/png.h"
#include "ui/render.h"
#include "ocr/protocol.h"
#include <chrono>
#include <iostream>
#include <numeric>
#include <fstream>
using namespace lumashot;
int main(int argc,char** argv) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int failures=0;
    auto expect=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    try {
        std::vector<float> scores(10*4,0);
        const int ids[]={0,1,1,0,2,0,0,0,3,0};
        for(size_t i=0;i<10;++i)scores[i*4+ids[i]]=1;
        auto line=ocr::DecodeCtc(scores,10,4,{L"A",L"中",L"B"},{0,0,100,20});
        ocr::Text text{{line}};
        expect(ocr::Selected(text,ocr::All(text))==L"A中B","CTC blanks and duplicates");
        expect(line.glyphs[1].box.right-line.glyphs[1].box.left!=line.glyphs[0].box.right-line.glyphs[0].box.left,"CTC positions are not equal-width");
        expect(ocr::Selected(text,{{0,3},{0,1}})==L"中B","reverse selection");
        expect(ocr::Selected(text,{{0,1},{0,1}}).empty(),"empty selection");
        auto second=line;second.box={0,30,100,50};for(auto& g:second.glyphs){g.box.top+=30;g.box.bottom+=30;}text.lines.push_back(second);
        expect(ocr::Selected(text,{{1,1},{0,1}})==L"中B\r\nA","cross-line reverse selection preserves newline");
        expect(ocr::Highlights(text,{{0,1},{1,1}}).size()==3,"highlight ranges match copied glyphs");
        std::vector<float> combining(5*4,0);const int combining_ids[]={1,0,2,0,3};for(size_t i=0;i<5;++i)combining[i*4+combining_ids[i]]=1;
        auto clustered=ocr::DecodeCtc(combining,5,4,{L"e",L"\u0301",L"\U0001f600"},{0,0,100,20});
        expect(clustered.glyphs.size()==2&&clustered.glyphs[0].text==L"e\u0301"&&clustered.glyphs[1].text.size()==2,"combining marks and surrogate pairs stay intact");
        auto encoded=ocr::Encode(text,L"");std::wstring error;auto decoded=ocr::Decode(encoded,error);
        expect(ocr::Selected(decoded,ocr::All(decoded))==ocr::Selected(text,ocr::All(text)),"IPC preserves text and geometry");
        encoded.data.pop_back();encoded.position=0;bool rejected=false;try{ocr::Decode(encoded,error);}catch(const std::exception&){rejected=true;}
        expect(rejected,"truncated IPC response is rejected");
        expect(!ocr::ErrorMessage(std::runtime_error("\xff")).empty(),"non-UTF8 system errors cannot escape the background error handler");
        ocr::Text columns{{second,line}};columns.lines[0].box={200,0,300,20};ocr::ReadingOrder(columns);
        expect(columns.lines.front().box.left==0,"reading order separates columns");
        if(argc>1) {
            const auto start=std::chrono::steady_clock::now();
            const auto folder=std::filesystem::absolute(argv[0]).parent_path();ocr::Engine engine(folder/L"ocr");
            const auto invalid=folder/(L"ocr-invalid-test-"+std::to_wstring(GetCurrentProcessId()));
            std::filesystem::create_directory(invalid);
            bool missing=false,corrupt=false;
            try{ocr::Engine unavailable(invalid);}catch(const std::exception&){missing=true;}
            {std::ofstream file(invalid/L"det.onnx",std::ios::binary);file<<"invalid synthetic model";}
            try{ocr::Engine unavailable(invalid);}catch(const std::exception&){corrupt=true;}
            std::filesystem::remove(invalid/L"det.onnx");std::filesystem::remove(invalid);
            expect(missing&&corrupt,"missing and corrupt models report errors without changing installed assets");
            std::cout<<"load_ms "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<'\n';
            Renderer renderer;Frame image=argc>2?ReadPng(argv[2]):renderer.Demo(false);
            auto t=std::chrono::steady_clock::now();auto result=engine.Recognize(image);
            std::cout<<"ocr_ms "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count()<<" lines "<<result.lines.size()<<'\n';
            const auto wide=ocr::Selected(result,ocr::All(result));
            const int n=WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
            std::string utf8(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),utf8.data(),n,nullptr,nullptr);std::cout<<utf8<<'\n';
            expect(!result.lines.empty(),"real offline model detects synthetic text");
        }
    }catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}
