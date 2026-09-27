#include "export/clipboard.h"
#include "export/png.h"
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <algorithm>
#include <stdexcept>
namespace lumashot {
namespace {std::filesystem::path ClipboardDirectory(){std::error_code error;auto temp=std::filesystem::temp_directory_path(error);return error?std::filesystem::path{}:temp/L"LumaShot-Clipboard";}}
size_t CleanupClipboardFiles(std::chrono::hours age,const std::filesystem::path& keep){
    const auto directory=ClipboardDirectory();if(directory.empty())return 0;
    const DWORD attributes=GetFileAttributesW(directory.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return 0;
    FILETIME now_time{};GetSystemTimeAsFileTime(&now_time);
    const ULONGLONG now=(ULONGLONG(now_time.dwHighDateTime)<<32)|now_time.dwLowDateTime,limit=ULONGLONG(age.count())*3600ull*10000000ull;
    WIN32_FIND_DATAW data{};
    const HANDLE first=FindFirstFileW((directory/L"LumaShot-*").c_str(),&data);
    if(first==INVALID_HANDLE_VALUE)return 0;
    const std::unique_ptr<void,decltype(&FindClose)> find(first,&FindClose);
    size_t removed=0;
    do{
        if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))continue;
        const ULONGLONG written=(ULONGLONG(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;
        if(written>now||now-written<limit)continue;
        const auto path=directory/data.cFileName;
        if(!keep.empty()&&_wcsicmp(path.c_str(),keep.lexically_normal().c_str())==0)continue;
        if(DeleteFileW(path.c_str()))++removed;
    }while(FindNextFileW(find.get(),&data));
    return removed;
}
std::filesystem::path CurrentClipboardFile(HWND owner){
    if(!IsClipboardFormatAvailable(CF_HDROP)||!OpenClipboard(owner))return {};
    std::filesystem::path result;
    // Parse DROPFILES directly (wide lists only; LumaShot always publishes wide paths).
    if(const HANDLE drop=GetClipboardData(CF_HDROP)){
        const SIZE_T bytes=GlobalSize(drop);
        if(const auto* data=static_cast<const DROPFILES*>(GlobalLock(drop))){
            if(data->fWide&&bytes>=sizeof(DROPFILES)&&data->pFiles>=sizeof(DROPFILES)&&data->pFiles<bytes){
                const auto* first=reinterpret_cast<const wchar_t*>(reinterpret_cast<const BYTE*>(data)+data->pFiles);
                const size_t limit=(bytes-data->pFiles)/sizeof(wchar_t);size_t length=0;while(length<limit&&first[length])++length;
                if(length<limit)result=std::wstring(first,length);
            }
            GlobalUnlock(drop);
        }
    }
    CloseClipboard();return result;
}
std::shared_ptr<ClipboardFile> PrepareClipboardFile(const Frame& frame,int format){
    if(format<0||format>2)throw std::invalid_argument("Invalid clipboard file format");
    const auto directory=std::filesystem::temp_directory_path()/L"LumaShot-Clipboard";
    std::filesystem::create_directories(directory);
    CleanupClipboardFiles();
    GUID id{};CheckWin32(SUCCEEDED(CoCreateGuid(&id)),"Create clipboard file name");wchar_t name[40]{};StringFromGUID2(id,name,40);
    auto file=std::make_shared<ClipboardFile>();file->path=directory/(std::wstring(L"LumaShot-")+name+(format==1?L".jpg":format==2?L".bmp":L".png"));
    SaveClipboardImageFile(frame,file->path,format);return file;
}
std::unique_ptr<ClipboardImage> PrepareClipboardImage(const Frame& frame,const std::shared_ptr<ClipboardFile>& file) {
    if(frame.Width()<=0||frame.Height()<=0||frame.pixels.size()!=static_cast<size_t>(frame.Width())*frame.Height()||frame.pixels.size()>(MAXDWORD-sizeof(BITMAPINFOHEADER))/4)
        throw std::invalid_argument("Invalid clipboard image dimensions");
    auto result=std::make_unique<ClipboardImage>();result->file=file;
    if(file){
        const auto path=file->path.wstring();result->drop=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t));CheckWin32(result->drop!=nullptr,"Allocate clipboard file");
        auto* data=static_cast<DROPFILES*>(GlobalLock(result->drop));CheckWin32(data!=nullptr,"Lock clipboard file");data->pFiles=sizeof(DROPFILES);data->fWide=TRUE;
        std::copy(path.begin(),path.end(),reinterpret_cast<wchar_t*>(data+1));GlobalUnlock(result->drop);
    }
    const size_t bytes=sizeof(BITMAPINFOHEADER)+frame.pixels.size()*4;
    result->image=GlobalAlloc(GMEM_MOVEABLE,bytes);
    CheckWin32(result->image!=nullptr,"Allocate clipboard image");
    auto* header=static_cast<BITMAPINFOHEADER*>(GlobalLock(result->image));
    CheckWin32(header!=nullptr,"Lock clipboard image");
    *header={};header->biSize=sizeof(*header);header->biWidth=frame.Width();header->biHeight=frame.Height();
    header->biPlanes=1;header->biBitCount=32;header->biCompression=BI_RGB;
    header->biSizeImage=static_cast<DWORD>(frame.pixels.size()*4);
    auto* pixels=reinterpret_cast<uint32_t*>(header+1);
    for(int y=0;y<frame.Height();++y)std::copy_n(frame.pixels.data()+static_cast<size_t>(frame.Height()-1-y)*frame.Width(),
        frame.Width(),pixels+static_cast<size_t>(y)*frame.Width());
    GlobalUnlock(result->image);return result;
}
void PublishClipboardImage(HWND owner,ClipboardImage& prepared){
    if(!prepared.image)throw std::invalid_argument("Clipboard image already published");
    if(!OpenClipboard(owner))throw std::runtime_error("Clipboard is busy. Please try Copy again.");
    const bool success=EmptyClipboard()&&SetClipboardData(CF_DIB,prepared.image)!=nullptr;
    if(success)prepared.image=nullptr;
    const bool file_success=success&&(!prepared.file||SetClipboardData(CF_HDROP,prepared.drop)!=nullptr);
    if(prepared.file&&file_success){prepared.drop=nullptr;prepared.file->published=true;}
    CloseClipboard();
    if(!success)throw std::runtime_error("Unable to copy image. Please try again.");
    if(!file_success)throw std::runtime_error("Unable to copy screenshot file. Please try again.");
}
void CopyImage(HWND owner,const Frame& frame,const std::shared_ptr<ClipboardFile>& file){
    auto prepared=PrepareClipboardImage(frame,file);PublishClipboardImage(owner,*prepared);
}
void CopyText(HWND owner,const std::wstring& text) {
    if(text.empty())return;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
    CheckWin32(memory!=nullptr,"Allocate clipboard text");
    auto* data=static_cast<wchar_t*>(GlobalLock(memory));
    if(!data){GlobalFree(memory);CheckWin32(false,"Lock clipboard text");}
    std::copy_n(text.c_str(),text.size()+1,data);GlobalUnlock(memory);
    if(!OpenClipboard(owner)){GlobalFree(memory);throw std::runtime_error("Clipboard is busy. Please retry.");}
    const bool ok=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory);CloseClipboard();
    if(!ok){GlobalFree(memory);throw std::runtime_error("Cannot copy text. Please retry.");}
}
}


