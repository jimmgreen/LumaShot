#include "clipboard/native.h"
#include <shellapi.h>
#include <cstring>
#include <limits>
namespace lumashot::clipboard {
namespace {
struct Open {bool ok;explicit Open(HWND w):ok(OpenClipboard(w)!=FALSE){}~Open(){if(ok)CloseClipboard();}};
struct Lock {HGLOBAL handle;void* data;explicit Lock(HGLOBAL h):handle(h),data(h?GlobalLock(h):nullptr){}~Lock(){if(data)GlobalUnlock(handle);}};
std::vector<unsigned char> Bytes(UINT format){
    auto h=GetClipboardData(format);const auto n=h?GlobalSize(h):0;
    if(!n||n>History::MaxItemBytes)return {};
    Lock lock(h);if(!lock.data)return {};const auto* p=static_cast<const unsigned char*>(lock.data);return {p,p+n};
}
bool Private(){
    if(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing")))return true;
    for(auto name:{L"CanIncludeInClipboardHistory",L"CanUploadToCloudClipboard"}){
        const auto b=Bytes(RegisterClipboardFormatW(name));DWORD value=1;if(b.size()>=sizeof(value))std::memcpy(&value,b.data(),sizeof(value));if(!value)return true;
    }return false;
}
}
std::optional<Entry> Read(HWND owner,bool& busy){
    Open open(owner);busy=!open.ok;if(!open.ok||Private())return {};
    Entry e;GetLocalTime(&e.time);
    const UINT png=RegisterClipboardFormatW(L"PNG");
    const auto add=[&](UINT id){auto bytes=Bytes(id);if(bytes.empty())return false;e.formats.push_back({id,std::move(bytes)});return true;};
    // Keep at most one image representation; screenshot publishers also expose
    // CF_HDROP, but those temporary paths are not needed to restore the image.
    if(add(png)||add(CF_DIBV5)||add(CF_DIB)){e.kind=Kind::Image;e.text=L"图片";}
    else if(IsClipboardFormatAvailable(CF_HDROP)){
        if(!add(CF_HDROP))return {};
        const HDROP drop=static_cast<HDROP>(GetClipboardData(CF_HDROP));const UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);
        if(!count||count>256)return {};
        e.kind=Kind::Files;e.file_count=count;
        for(UINT i=0;i<count;++i){const UINT size=DragQueryFileW(drop,i,nullptr,0);if(size>32767)return {};std::wstring name(size+1,L'\0');DragQueryFileW(drop,i,name.data(),size+1);name.resize(size);if(i<e.file_names.size()){const auto slash=name.find_last_of(L"\\/");e.file_names[i]=name.substr(slash==std::wstring::npos?0:slash+1,260);}if(i)e.text+=L"\n";e.text+=name;}
    }else if(add(CF_UNICODETEXT)){
        e.kind=Kind::Text;const auto& b=e.formats.front().bytes;if(b.size()%sizeof(wchar_t))return {};
        std::wstring value(b.size()/sizeof(wchar_t),L'\0');std::memcpy(value.data(),b.data(),b.size());
        const auto end=value.find(L'\0');if(end==std::wstring::npos)return {};value.resize(end);if(value.empty())return {};e.text=std::move(value);
        // Keep the original rich representations alongside the complete Unicode
        // payload. Reject oversized entries before allocating another format.
        if(e.Bytes()>History::MaxItemBytes)return {};
        for(const auto* name:{L"HTML Format",L"Rich Text Format"}){
            const UINT format=RegisterClipboardFormatW(name);
            if(!IsClipboardFormatAvailable(format))continue;
            const auto handle=GetClipboardData(format);
            if(handle&&GlobalSize(handle)>History::MaxItemBytes-e.Bytes())return {};
            add(format);
        }
    }else return {};
    if(e.Bytes()>History::MaxItemBytes)return {};return e;
}
bool Write(HWND owner,const Entry& entry,bool plain_text){
    if(plain_text&&entry.kind!=Kind::Text)return false;
    // Allocate before EmptyClipboard so allocation failure cannot clear content.
    std::vector<std::pair<UINT,HGLOBAL>> copies;
    for(const auto& f:entry.formats){if(plain_text&&f.id!=CF_UNICODETEXT)continue;HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,f.bytes.size());if(!h){for(auto v:copies)GlobalFree(v.second);return false;}Lock lock(h);if(!lock.data){GlobalFree(h);for(auto v:copies)GlobalFree(v.second);return false;}std::memcpy(lock.data,f.bytes.data(),f.bytes.size());copies.emplace_back(f.id,h);}
    Open open(owner);bool ok=open.ok&&!copies.empty();if(ok)ok=EmptyClipboard()!=FALSE;
    for(auto [id,h]:copies){if(ok&&SetClipboardData(id,h))continue;GlobalFree(h);ok=false;}return ok;
}
static Microsoft::WRL::ComPtr<IWICBitmapSource> ThumbnailFormat(const Entry& entry,const Format& f,UINT edge,uint64_t max_pixels){
    using Microsoft::WRL::ComPtr;ComPtr<IWICBitmapSource> result;if(entry.kind!=Kind::Image||entry.formats.empty())return result;
    auto bytes=f.bytes;
    if(f.id==CF_DIB||f.id==CF_DIBV5){
        if(bytes.size()<sizeof(BITMAPINFOHEADER))return result;BITMAPINFOHEADER h{};std::memcpy(&h,bytes.data(),sizeof(h));
        if(h.biSize<sizeof(h)||h.biSize>bytes.size()||h.biBitCount>32||h.biPlanes!=1)return result;
        const uint64_t colors=h.biClrUsed?h.biClrUsed:(h.biBitCount<=8?(1ull<<h.biBitCount):0);
        const uint64_t offset=sizeof(BITMAPFILEHEADER)+h.biSize+colors*4+((h.biSize==40&&h.biCompression==BI_BITFIELDS)?12:0);
        if(offset>bytes.size()+sizeof(BITMAPFILEHEADER))return result;
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfSize=static_cast<DWORD>(bytes.size()+sizeof(file));file.bfOffBits=static_cast<DWORD>(offset);
        bytes.insert(bytes.begin(),sizeof(file),0);std::memcpy(bytes.data(),&file,sizeof(file));
    }
    ComPtr<IWICImagingFactory> factory;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return result;
    ComPtr<IWICStream> stream;if(FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(bytes.data(),static_cast<DWORD>(bytes.size()))))return result;
    ComPtr<IWICBitmapDecoder> decoder;if(FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)))return result;
    ComPtr<IWICBitmapFrameDecode> frame;if(FAILED(decoder->GetFrame(0,&frame)))return result;
    UINT w{},h{};if(FAILED(frame->GetSize(&w,&h))||!w||!h||w>32768||h>32768||uint64_t(w)*h>max_pixels)return result;
    const double ratio=std::min(1.,static_cast<double>(edge)/std::max(w,h));ComPtr<IWICBitmapScaler> scaler;
    if(FAILED(factory->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(frame.Get(),std::max(1u,static_cast<UINT>(w*ratio)),std::max(1u,static_cast<UINT>(h*ratio)),WICBitmapInterpolationModeFant)))return result;
    ComPtr<IWICFormatConverter> converter;if(FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return result;
    ComPtr<IWICBitmap> cached;if(SUCCEEDED(factory->CreateBitmapFromSource(converter.Get(),WICBitmapCacheOnLoad,&cached)))cached.As(&result);return result;
}
Microsoft::WRL::ComPtr<IWICBitmapSource> Thumbnail(const Entry& entry,UINT edge,uint64_t max_pixels){
    for(const auto& format:entry.formats)if(auto image=ThumbnailFormat(entry,format,edge,max_pixels))return image;
    return {};
}

}
