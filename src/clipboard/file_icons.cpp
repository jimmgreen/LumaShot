#include "clipboard/file_icons.h"
#include <shellapi.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
namespace lumashot::clipboard {
std::wstring FirstFilePath(const std::wstring& paths){return paths.substr(0,paths.find_first_of(L"\r\n"));}
int FileIconResolution(int pixels){return pixels<=48?48:256;}
namespace {
using Microsoft::WRL::ComPtr;
struct Icon {HICON value{};~Icon(){if(value)DestroyIcon(value);}};
std::shared_ptr<const Frame> Pixels(HICON icon){
    if(!icon)return {};ComPtr<IWICImagingFactory> factory;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return {};
    ComPtr<IWICBitmap> bitmap;if(FAILED(factory->CreateBitmapFromHICON(icon,&bitmap)))return {};UINT width{},height{};if(FAILED(bitmap->GetSize(&width,&height))||!width||!height||width>512||height>512)return {};
    ComPtr<IWICFormatConverter> converter;if(FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(bitmap.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
    auto image=MakeFrame({0,0,static_cast<LONG>(width),static_cast<LONG>(height)},0);if(FAILED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(image.pixels.size()*4),reinterpret_cast<BYTE*>(image.pixels.data()))))return {};
    // Some associations supply a 32px icon inside a transparent 256px shell slot.
    // Trim only transparent padding, so those icons do not become tiny dots.
    RECT occupied{LONG(width),LONG(height),0,0};
    for(LONG y=0;y<LONG(height);++y)for(LONG x=0;x<LONG(width);++x)if(image.pixels[size_t(y)*width+x]>>24){occupied.left=std::min(occupied.left,x);occupied.top=std::min(occupied.top,y);occupied.right=std::max(occupied.right,x+1);occupied.bottom=std::max(occupied.bottom,y+1);}
    if(occupied.right<=occupied.left||occupied.bottom<=occupied.top)return {};
    auto cropped=Crop(image,occupied);cropped.bounds={0,0,cropped.Width(),cropped.Height()};return std::make_shared<Frame>(std::move(cropped));
}
bool LocalPath(const std::wstring& path){
    if(path.size()<3||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))return false;
    const wchar_t root[]={path[0],L':',L'\\',0};const UINT type=GetDriveTypeW(root);
    return type==DRIVE_FIXED||type==DRIVE_RAMDISK;
}
}
std::shared_ptr<const Frame> LoadFileIcon(const std::wstring& value,int pixels){
    const auto path=FirstFilePath(value);if(path.empty())return {};
    DWORD attributes=INVALID_FILE_ATTRIBUTES;
    // Do not activate disconnected shares, mapped drives, removable media or links.
    if(LocalPath(path))attributes=GetFileAttributesW(path.c_str());
    SHFILEINFOW info{};const bool shortcut=path.size()>=4&&(!_wcsicmp(path.c_str()+path.size()-4,L".lnk")||!_wcsicmp(path.c_str()+path.size()-4,L".url"));
    const bool actual=attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&!shortcut;
    UINT flags=SHGFI_SYSICONINDEX;if(!actual)flags|=SHGFI_USEFILEATTRIBUTES;
    DWORD hint=actual?attributes:(!path.empty()&&(path.back()==L'\\'||path.back()==L'/')?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL);
    if(SHGetFileInfoW(path.c_str(),hint,&info,sizeof(info),flags)){
        ComPtr<IImageList> list;const int size=FileIconResolution(pixels)>48?SHIL_JUMBO:SHIL_EXTRALARGE;
        if(SUCCEEDED(SHGetImageList(size,IID_PPV_ARGS(&list)))){Icon icon;if(SUCCEEDED(list->GetIcon(info.iIcon,ILD_TRANSPARENT,&icon.value)))if(auto result=Pixels(icon.value))return result;}
        Icon icon;SHFILEINFOW large{};if(SHGetFileInfoW(path.c_str(),hint,&large,sizeof(large),(flags&~SHGFI_SYSICONINDEX)|SHGFI_ICON|SHGFI_LARGEICON)){icon.value=large.hIcon;if(auto result=Pixels(icon.value))return result;}
    }
    SHSTOCKICONINFO stock{sizeof(stock)};if(SUCCEEDED(SHGetStockIconInfo(hint&FILE_ATTRIBUTE_DIRECTORY?SIID_FOLDER:SIID_DOCNOASSOC,SHGSI_ICON|SHGSI_LARGEICON,&stock))){Icon icon;icon.value=stock.hIcon;return Pixels(icon.value);}return {};
}
FileIconCache::~FileIconCache(){
    {std::lock_guard lock(mutex_);stop_=true;jobs_.clear();}cv_.notify_all();if(worker_.joinable())worker_.join();
}
std::shared_ptr<const Frame> FileIconCache::Get(const std::wstring& value,int pixels,HWND notify){
    Key key{FirstFilePath(value),FileIconResolution(pixels)};if(key.first.empty())return {};
    std::lock_guard lock(mutex_);if(auto it=cache_.find(key);it!=cache_.end()){it->second.use=++clock_;return it->second.image;}
    if(cache_.size()>=MaxEntries){auto oldest=cache_.end();for(auto it=cache_.begin();it!=cache_.end();++it)if(it->second.ready&&(oldest==cache_.end()||it->second.use<oldest->second.use))oldest=it;if(oldest==cache_.end())return {};cache_.erase(oldest);}
    if(!worker_.joinable())worker_=std::thread([this]{Work();});
    cache_.emplace(key,Entry{{},false,++clock_});jobs_.push_back({std::move(key),generation_,notify});cv_.notify_all();return {};
}
void FileIconCache::Work(){
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    for(;;){Job job;{std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stop_||!jobs_.empty();});if(stop_)break;job=std::move(jobs_.front());jobs_.pop_front();busy_=true;}
        std::shared_ptr<const Frame> image;try{if(SUCCEEDED(com))image=LoadFileIcon(job.key.first,job.key.second);}catch(...){/* A broken association must not break the clipboard panel. */}
        {std::lock_guard lock(mutex_);if(!stop_&&job.generation==generation_){auto it=cache_.find(job.key);if(it!=cache_.end()){it->second.image=std::move(image);it->second.ready=true;if(job.notify)PostMessageW(job.notify,FileIconReady,0,0);}}busy_=false;}cv_.notify_all();
    }
    if(SUCCEEDED(com))CoUninitialize();
}
void FileIconCache::Clear(){std::lock_guard lock(mutex_);++generation_;jobs_.clear();cache_.clear();cv_.notify_all();}
bool FileIconCache::WaitIdle(unsigned milliseconds){std::unique_lock lock(mutex_);return cv_.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return jobs_.empty()&&!busy_;});}
size_t FileIconCache::Size(){std::lock_guard lock(mutex_);return cache_.size();}
}
