#include "clipboard/file_icons.h"
#include "export/png.h"
#include <iostream>
#include <objbase.h>
#include <filesystem>
using namespace lumashot;
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;const auto expect=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;failures+=!ok;};
    const auto directory=std::filesystem::temp_directory_path()/(L"LumaShot-file-icons-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(directory/L"文件夹");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}}cleanup{directory};
    expect(clipboard::FirstFilePath(L"C:\\a.PDF\r\nC:\\b.docx")==L"C:\\a.PDF","multi-file entries use first path, not last file extension");
    expect(clipboard::FileIconResolution(32)==48&&clipboard::FileIconResolution(48)==48&&clipboard::FileIconResolution(64)==256,"physical pixel sizes select extra-large or jumbo system list");
    clipboard::FileIconCache cache;
    std::vector<std::wstring> paths={L"Synthetic.PDF",L"Synthetic.docx",L"Synthetic.xlsx",L"Synthetic.zip",L"Synthetic.unregistered_lumashot",(directory/L"文件夹").native(),L"\\\\not-a-real-server\\share\\Synthetic.pdf"};
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);paths.push_back(executable);
    for(const auto& path:paths){cache.Get(path,32,nullptr);cache.Get(path,64,nullptr);}
    expect(cache.WaitIdle(15000),"asynchronous shell loading completes for types folder executable and offline fallback");
    for(size_t index=0;index<paths.size();++index)for(int size:{32,64}){
        const auto image=cache.Get(paths[index],size,nullptr);expect(image&&image->Width()>0&&image->Height()>0&&image->Width()<=256&&image->Height()<=256,"shell icon returned with bounded nonempty dimensions");
        if(image){bool pixels=false,alpha=true;for(const auto pixel:image->pixels){const auto a=pixel>>24;pixels|=a!=0;for(int shift:{0,8,16})alpha&=((pixel>>shift)&255)<=a;}expect(pixels&&alpha,"icon retains visible premultiplied alpha without black rectangle");expect(cache.Get(paths[index],size,nullptr)==image,"repeat render reuses cached pixels without shell query");SavePng(*image,L"file-icon-"+std::to_wstring(index)+L"-"+std::to_wstring(size)+L".png");}
    }
    expect(cache.Size()==paths.size()*2,"independent DPI buckets cached and deduplicated");cache.Clear();expect(cache.Size()==0,"clear releases paths and CPU icon cache");
    cache.Get(L"cancelled.pdf",64,nullptr);cache.Clear();expect(cache.WaitIdle(15000)&&cache.Size()==0,"late completion cannot resurrect a cleared icon cache");
    for(int i=0;i<145;++i){cache.Get(L"bounded-"+std::to_wstring(i)+L".txt",32,nullptr);if(i%16==0)cache.WaitIdle(15000);}
    expect(cache.WaitIdle(15000)&&cache.Size()<=clipboard::FileIconCache::MaxEntries,"LRU stays bounded while scrolling many files");
    CoUninitialize();return failures?1:0;
}
