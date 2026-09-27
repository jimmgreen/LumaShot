#include "ocr/engine.h"
#include "ocr/protocol.h"
#include "ocr/table.h"
#include <objbase.h>
#include <memory>
int main() {
    using namespace lumashot;using namespace lumashot::ocr;
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    std::unique_ptr<Engine> engine;Request request;
    while(ReadExact(GetStdHandle(STD_INPUT_HANDLE),&request,sizeof(request))) {
        Text text;std::wstring error;
        try {
            Handle mapping(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(request.mapping)));
            if(request.magic!=kProtocol||request.reserved!=0||!request.width||!request.height||request.width>32768||request.height>32768||
                uint64_t(request.width)*request.height>128*1024*1024)throw std::runtime_error("Invalid OCR request");
            Frame frame=MakeFrame({0,0,static_cast<LONG>(request.width),static_cast<LONG>(request.height)});
            {MappingView view(mapping.Get(),FILE_MAP_READ,frame.pixels.size()*4);std::memcpy(frame.pixels.data(),view.data,frame.pixels.size()*4);}
            mapping.Reset();
            if(!engine)engine=std::make_unique<Engine>(std::filesystem::path(path).parent_path()/L"ocr");
            text=engine->Recognize(frame);
            text.table=AnalyzeTable(frame,text);
        }catch(const std::exception& e){error=ErrorMessage(e);}
        try {auto bytes=Encode(text,error);const auto size=static_cast<uint32_t>(bytes.data.size());
            if(!WriteExact(GetStdHandle(STD_OUTPUT_HANDLE),&size,sizeof(size))||!WriteExact(GetStdHandle(STD_OUTPUT_HANDLE),bytes.data.data(),size))break;
        }catch(...){break;}
    }
    engine.reset();if(SUCCEEDED(com))CoUninitialize();return 0;
}


