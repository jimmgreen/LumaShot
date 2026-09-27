#pragma once
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <vector>
namespace lumashot::recording {
// %TEMP%\LumaShot-Recording holds one GUID folder per standalone recording worker.
inline std::filesystem::path RecordingTempRoot(){
    std::error_code error;auto temp=std::filesystem::temp_directory_path(error);
    return error?std::filesystem::path{}:temp/L"LumaShot-Recording";
}
// Held for the worker's lifetime. The OS deletes the lease even when the worker is
// killed (for example by the host's KILL_ON_JOB_CLOSE job), so a surviving folder
// without a held lease is known to be orphaned.
class RecordingLease {
    HANDLE handle_{INVALID_HANDLE_VALUE};
public:
    explicit RecordingLease(const std::filesystem::path& folder){
        if(!folder.empty())handle_=CreateFileW((folder/L"lease").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
            FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    }
    ~RecordingLease(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
    RecordingLease(const RecordingLease&)=delete;RecordingLease& operator=(const RecordingLease&)=delete;
    bool Held()const{return handle_!=INVALID_HANDLE_VALUE;}
};
inline bool PlainRecordingDirectory(const std::filesystem::path& path){
    const DWORD attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
// Removes orphaned worker folders. A folder whose lease is held is in use and skipped;
// a folder without a lease (older builds) is removed only once it is older than
// unleased_age. Only flat folders are touched; reparse points are never followed.
inline size_t CleanupOrphanRecordingFolders(const std::filesystem::path& root,std::chrono::minutes unleased_age=std::chrono::minutes(10)){
    if(root.empty()||!PlainRecordingDirectory(root))return 0;
    std::error_code error;std::vector<std::filesystem::path> folders;
    for(const auto& entry:std::filesystem::directory_iterator(root,error))folders.push_back(entry.path());
    if(error)return 0;
    FILETIME now_time{};GetSystemTimeAsFileTime(&now_time);
    const ULONGLONG now=(ULONGLONG(now_time.dwHighDateTime)<<32)|now_time.dwLowDateTime;
    const ULONGLONG limit=ULONGLONG(unleased_age.count())*60ull*10000000ull;
    size_t removed=0;
    for(const auto& folder:folders){
        if(!PlainRecordingDirectory(folder))continue;
        const HANDLE probe=CreateFileW((folder/L"lease").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(probe!=INVALID_HANDLE_VALUE)CloseHandle(probe);
        else if(GetLastError()!=ERROR_FILE_NOT_FOUND)continue;
        else{
            WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(folder.c_str(),GetFileExInfoStandard,&info))continue;
            const ULONGLONG written=(ULONGLONG(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
            if(written>now||now-written<limit)continue;
        }
        std::vector<std::filesystem::path> files;bool flat=true;
        for(const auto& entry:std::filesystem::directory_iterator(folder,error)){
            const DWORD attributes=GetFileAttributesW(entry.path().c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){flat=false;break;}
            files.push_back(entry.path());
        }
        if(error||!flat){error.clear();continue;}
        for(const auto& file:files){std::error_code ignored;std::filesystem::remove(file,ignored);}
        if(std::filesystem::remove(folder,error))++removed;
        error.clear();
    }
    return removed;
}
}
