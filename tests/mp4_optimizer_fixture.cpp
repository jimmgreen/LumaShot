#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
int wmain(int argc,wchar_t** argv){
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const auto folder=std::filesystem::path(module).parent_path();std::string mode;std::ifstream(folder/L"mode.txt")>>mode;
    if(mode=="timeout"){Sleep(10000);return 1;}
    if(mode=="memory"){std::vector<char> memory(512ull*1024*1024,1);Sleep(10000);return memory[0];}
    const bool quality=std::wstring(GetCommandLineW()).find(L"ssim=")!=std::wstring::npos;
    if(mode=="progress"){{std::ofstream progress("progress.txt");progress<<"frame=300\nout_time_us="<<(quality?300:10000000)<<"\n";}Sleep(650);}
    std::ofstream(folder/L"calls.txt",std::ios::app)<<(quality?"quality":"encode")<<'\n';
    if(quality){std::ofstream stats("quality.txt");for(int i=0;i<(mode=="shortquality"?599:600);++i)stats<<"n:"<<i+1<<" All:"<<(mode=="quality"?"0.95":mode=="meanquality"?"0.989":(mode=="tailquality"&&i<10||mode=="rarequality"&&i==0)?"0.97":"1.0")<<'\n';return 0;}
    if(argc<2)return 1;const std::filesystem::path output=argv[argc-1];
    if(mode=="invalid"||mode=="failure"){std::ofstream(output)<<"partial MP4";return mode=="failure"?1:0;}
    if(mode=="larger"){for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"-i"){std::filesystem::copy_file(argv[i+1],output);return 0;}return 1;}
    std::filesystem::copy_file(folder/(mode=="mismatch"?L"alternate.mp4":L"candidate.mp4"),output);return 0;
}
