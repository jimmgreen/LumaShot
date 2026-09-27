#pragma once
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <stop_token>
#include <stdexcept>
namespace lumashot::recording {
// Each export owns only flat, generated files in its unique directory.
class ExportDirectory {
    std::filesystem::path path_;
public:
    explicit ExportDirectory(const std::filesystem::path& parent){
        GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Create export directory ID");
        wchar_t name[40]{};StringFromGUID2(id,name,40);path_=parent/(std::wstring(L"gif-export-")+name);
        if(!std::filesystem::create_directory(path_))throw std::runtime_error("Create export directory");
    }
    ExportDirectory(const ExportDirectory&)=delete;
    ExportDirectory& operator=(const ExportDirectory&)=delete;
    ~ExportDirectory(){
        std::error_code error;
        for(std::filesystem::directory_iterator it(path_,error),end;!error&&it!=end;it.increment(error)){
            std::error_code ignored;std::filesystem::remove(it->path(),ignored);
        }
        std::filesystem::remove(path_,error);
    }
    const std::filesystem::path& Path()const{return path_;}
};
// Copy beside the destination and commit only after the copy is complete. This
// preserves an existing user file if encoding, copying or cancellation fails.
inline void SaveOutput(const std::filesystem::path& source,const std::filesystem::path& destination,std::stop_token stop){
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Create save transaction");wchar_t suffix[40]{};StringFromGUID2(id,suffix,40);const auto temporary=std::filesystem::path(destination.native()+suffix+L".part");
    struct Cleanup {std::filesystem::path file;~Cleanup(){std::error_code e;std::filesystem::remove(file,e);}} cleanup{temporary};
    BOOL cancel=FALSE;auto progress=[](LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,DWORD,DWORD,HANDLE,HANDLE,LPVOID value)->DWORD{return static_cast<std::stop_token*>(value)->stop_requested()?PROGRESS_CANCEL:PROGRESS_CONTINUE;};
    if(stop.stop_requested()||!CopyFileExW(source.c_str(),temporary.c_str(),progress,&stop,&cancel,COPY_FILE_FAIL_IF_EXISTS))throw std::runtime_error(stop.stop_requested()?"Save canceled":"Unable to copy recording to destination");
    if(stop.stop_requested())throw std::runtime_error("Save canceled");if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Unable to commit saved recording");
}
}
