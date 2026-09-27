#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
// Controlled child process. Never accesses user settings or media.
int wmain(int argc,wchar_t** argv){
    std::filesystem::path source,output;
    for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"-o"){source=argv[i-1];output=argv[i+1];break;}
    if(source.empty())return 2;
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
    std::ifstream config(std::filesystem::path(module).parent_path()/L"mode.txt");std::string mode;config>>mode;
    std::ofstream(std::filesystem::path(module).parent_path()/L"inputs.txt",std::ios::app)<<std::filesystem::file_size(source)<<"\n";
    if(mode=="failure")return 1;
    if(mode=="timeout"){Sleep(10000);return 1;}
    if(mode=="memory"){auto* p=static_cast<volatile char*>(VirtualAlloc(nullptr,512ull*1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!p)return 1;p[0]=1;Sleep(10000);return 1;}
    std::ifstream in(source,std::ios::binary);std::vector<char> b{std::istreambuf_iterator<char>(in),{}};
    if(mode=="larger")b.insert(b.end()-1,{0x21,char(-2),1,'x',0});
    else {
        // Remove the fixture's trailing padding comment to make a smaller candidate.
        if(b.size()<4100)return 3;b.resize(b.size()-4100);b.push_back(0x3b);
        if(mode=="invalid")b.resize(15);
        if(mode=="dimensions")b[6]=char(static_cast<unsigned char>(b[6])+1);
        if(mode=="duration")for(size_t i=0;i+7<b.size();++i)if(b[i]==0x21&&static_cast<unsigned char>(b[i+1])==0xf9&&b[i+2]==4){++b[i+4];break;}
        if(mode=="loop")for(size_t i=0;i+18<b.size();++i)if(b[i]==0x21&&static_cast<unsigned char>(b[i+1])==0xff){b[i+16]=1;break;}
    }
    std::ofstream out(output,std::ios::binary);out.write(b.data(),std::streamsize(b.size()));return out?0:4;
}
