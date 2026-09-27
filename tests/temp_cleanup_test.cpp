// Synthetic files only: TMP is redirected into a private folder under the build directory.
#include "export/clipboard.h"
#include "recording/temp_cleanup.h"
#include <objbase.h>
#include <fstream>
#include <iostream>
using namespace lumashot;
namespace {
int failures{};
void Expect(bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;failures+=!ok;}
void Touch(const std::filesystem::path& path){std::ofstream(path,std::ios::binary)<<"synthetic";}
bool Age(const std::filesystem::path& path,ULONGLONG hundred_ns,bool directory=false){
    const HANDLE h=CreateFileW(path.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,directory?FILE_FLAG_BACKUP_SEMANTICS:0,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    FILETIME now{};GetSystemTimeAsFileTime(&now);ULARGE_INTEGER value{};value.LowPart=now.dwLowDateTime;value.HighPart=now.dwHighDateTime;value.QuadPart-=hundred_ns;
    const FILETIME old{value.LowPart,value.HighPart};const bool ok=SetFileTime(h,nullptr,nullptr,&old)!=FALSE;CloseHandle(h);return ok;
}
constexpr ULONGLONG Minute=60ull*10000000ull,Hour=60*Minute;
}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const auto temp=std::filesystem::current_path()/(L"temp-cleanup-test-"+std::to_wstring(GetCurrentProcessId()));
    struct Cleanup{std::filesystem::path root;~Cleanup(){std::error_code ec;std::filesystem::remove_all(root,ec);}}cleanup{temp};
    std::filesystem::create_directories(temp);SetEnvironmentVariableW(L"TMP",(temp.wstring()+L"\\").c_str());SetEnvironmentVariableW(L"TEMP",(temp.wstring()+L"\\").c_str());
    Expect(std::filesystem::equivalent(std::filesystem::temp_directory_path(),temp),"temporary directory redirected to private test folder");
    // Clipboard export files.
    const auto clip=temp/L"LumaShot-Clipboard";std::filesystem::create_directories(clip);
    const auto old_file=clip/L"LumaShot-{old}.png",kept=clip/L"LumaShot-{kept}.png",fresh=clip/L"LumaShot-{fresh}.png",foreign=clip/L"other.png";
    for(const auto& f:{old_file,kept,fresh,foreign})Touch(f);
    Expect(Age(old_file,25*Hour)&&Age(kept,25*Hour)&&Age(foreign,25*Hour)&&Age(fresh,23*Hour),"fixture timestamps set");
    Expect(CleanupClipboardFiles(std::chrono::hours(24),kept)==1,"exactly one stale clipboard export removed");
    Expect(!std::filesystem::exists(old_file)&&std::filesystem::exists(kept)&&std::filesystem::exists(fresh)&&std::filesystem::exists(foreign),"cleanup keeps current clipboard file, recent files and non-LumaShot files");
    Expect(CleanupClipboardFiles(std::chrono::hours(24))==1&&!std::filesystem::exists(kept),"age-only cleanup removes previously kept stale file");
    // Recording worker folders.
    const auto root=recording::RecordingTempRoot();Expect(root==temp/L"LumaShot-Recording","recording temp root follows TMP");
    const auto live=root/L"{live}",orphan_leased=root/L"{killed}",orphan_old=root/L"{legacy-old}",young=root/L"{legacy-young}",nested=root/L"{nested}";
    for(const auto& d:{live,orphan_leased,orphan_old,young,nested}){std::filesystem::create_directories(d);Touch(d/L"capture.mp4");}
    std::filesystem::create_directories(nested/L"child");
    {
        recording::RecordingLease lease(live);Expect(lease.Held(),"worker lease acquired");
        Touch(orphan_leased/L"lease");  // stale lease left without a live holder
        Expect(Age(orphan_old,11*Minute,true)&&Age(nested,11*Minute,true)&&Age(young,2*Minute,true),"folder timestamps set");
        Expect(recording::CleanupOrphanRecordingFolders(root)==2,"orphaned recording folders removed");
        Expect(std::filesystem::exists(live/L"capture.mp4"),"folder with a held lease is never touched");
        Expect(!std::filesystem::exists(orphan_leased)&&!std::filesystem::exists(orphan_old),"stale-lease and old unleased folders removed");
        Expect(std::filesystem::exists(young)&&std::filesystem::exists(nested/L"child"),"recent unleased and non-flat folders are kept");
    }
    Expect(!std::filesystem::exists(live/L"lease"),"lease file disappears when its holder closes");
    CoUninitialize();return failures?1:0;
}
