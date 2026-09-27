#include "pin/session.h"
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <set>
#include <limits>
#include <type_traits>
#include <cmath>
#include <stdexcept>
namespace lumashot {
namespace {
using Microsoft::WRL::ComPtr;
constexpr uint64_t MaxFile=512ull*1024*1024,MaxPixels=64ull*1024*1024;
void Require(bool ok){if(!ok)throw std::runtime_error("Invalid or inaccessible pin session");}
void Check(HRESULT hr){Require(SUCCEEDED(hr));}
ComPtr<IWICImagingFactory> Factory(){ComPtr<IWICImagingFactory> f;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)));return f;}
std::vector<unsigned char> Encode(const Frame& image){
    Require(image.Width()>0&&image.Height()>0&&uint64_t(image.Width())*image.Height()<=MaxPixels&&image.pixels.size()==uint64_t(image.Width())*image.Height());
    auto f=Factory();ComPtr<IStream> stream;Check(CreateStreamOnHGlobal(nullptr,TRUE,&stream));ComPtr<IWICBitmapEncoder> encoder;Check(f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> bag;Check(encoder->CreateNewFrame(&frame,&bag));Check(frame->Initialize(bag.Get()));Check(frame->SetSize(image.Width(),image.Height()));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Check(frame->SetPixelFormat(&format));Require(format==GUID_WICPixelFormat32bppBGRA);
    Check(frame->WritePixels(image.Height(),image.Width()*4,static_cast<UINT>(image.pixels.size()*4),reinterpret_cast<BYTE*>(const_cast<uint32_t*>(image.pixels.data()))));Check(frame->Commit());Check(encoder->Commit());
    STATSTG stat{};Check(stream->Stat(&stat,STATFLAG_NONAME));Require(stat.cbSize.QuadPart<=MaxFile);std::vector<unsigned char> bytes(static_cast<size_t>(stat.cbSize.QuadPart));LARGE_INTEGER zero{};Check(stream->Seek(zero,STREAM_SEEK_SET,nullptr));ULONG read{};Check(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read));Require(read==bytes.size());return bytes;
}
std::shared_ptr<const Frame> Decode(const std::vector<unsigned char>& bytes){
    Require(!bytes.empty()&&bytes.size()<=MaxFile);auto f=Factory();ComPtr<IWICStream> stream;Check(f->CreateStream(&stream));Check(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size())));ComPtr<IWICBitmapDecoder> decoder;Check(f->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder));ComPtr<IWICBitmapFrameDecode> bitmap;Check(decoder->GetFrame(0,&bitmap));UINT w{},h{};Check(bitmap->GetSize(&w,&h));Require(w&&h&&w<=32768&&h<=32768&&uint64_t(w)*h<=MaxPixels);
    auto image=std::make_shared<Frame>(MakeFrame({0,0,static_cast<LONG>(w),static_cast<LONG>(h)}));ComPtr<IWICFormatConverter> converter;Check(f->CreateFormatConverter(&converter));Check(converter->Initialize(bitmap.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));Check(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(image->pixels.size()*4),reinterpret_cast<BYTE*>(image->pixels.data())));return image;
}
template<class A,class M> void MarkFields(A& a,M& m){a(m.rotation);a(m.corner_radii);a(m.number_detail);a(m.number_leader);a(m.tool);a(m.a);a(m.b);a(m.color);a(m.width);a(m.pen_opacity);a(m.pen_mode);a(m.pen_smoothing);a(m.line_style);a(m.arrow_style);a(m.arrow_type);a(m.arrow_head);a(m.arrow_size);a(m.font_size);a(m.number);a(m.number_label);a(m.number_size);a(m.number_shape);a(m.number_combo);a(m.number_text_color);a(m.number_text_preset);a(m.number_text_size);a(m.number_target);a(m.text_bold);a(m.font_family);a(m.text_auto_size);a(m.text_wrap_width);a(m.text_align);a(m.text_background);a(m.mosaic_cell);a(m.mosaic_brush);a(m.mosaic_mode);a(m.mosaic_method);a(m.fill_color);a(m.fill_opacity);a(m.corner_radius);a(m.text);a(m.points);}
#define ENUM_LIMIT(T,V) template<> constexpr int EnumMax<T>(){return int(T::V);}
template<class T> constexpr int EnumMax();
ENUM_LIMIT(Tool,Number) ENUM_LIMIT(PenMode,Highlighter) ENUM_LIMIT(PenSmoothing,High) ENUM_LIMIT(LineStyle,DashDot)
ENUM_LIMIT(ArrowStyle,Double) ENUM_LIMIT(ArrowType,DotStart) ENUM_LIMIT(ArrowHead,None) ENUM_LIMIT(NumberShape,Corner)
ENUM_LIMIT(NumberCombo,Magnify) ENUM_LIMIT(TextAlign,Right) ENUM_LIMIT(TextBackground,TagDark) ENUM_LIMIT(MosaicMode,Blur)
ENUM_LIMIT(MosaicMethod,Rectangle) ENUM_LIMIT(TextDecoration,Strike) ENUM_LIMIT(PinStyle,Curl)
#undef ENUM_LIMIT
struct Writer {
    std::ofstream stream;
    explicit Writer(const std::filesystem::path& path):stream(path,std::ios::binary|std::ios::trunc){Require(bool(stream));}
    void Raw(const void* p,size_t size){stream.write(static_cast<const char*>(p),static_cast<std::streamsize>(size));Require(bool(stream));}
    template<class T> void operator()(const T& value){
        if constexpr(std::is_same_v<T,bool>){const uint8_t b=value?1:0;Raw(&b,1);}
        else if constexpr(std::is_enum_v<T>){const int32_t v=static_cast<int32_t>(value);(*this)(v);}
        else {static_assert(std::is_arithmetic_v<T>);Raw(&value,sizeof(value));}
    }
    void operator()(const std::wstring& value){Require(value.size()<=1000000);(*this)(static_cast<uint32_t>(value.size()));Raw(value.data(),value.size()*sizeof(wchar_t));}
    template<class T> void operator()(const std::vector<T>& value){Require(value.size()<=1000000);(*this)(static_cast<uint32_t>(value.size()));for(const auto& x:value)(*this)(x);}
    void operator()(const std::vector<unsigned char>& bytes){Require(bytes.size()<=MaxFile);(*this)(static_cast<uint32_t>(bytes.size()));Raw(bytes.data(),bytes.size());}
    template<class T,size_t N> void operator()(const std::array<T,N>& value){for(const auto& x:value)(*this)(x);}
    template<class T> void operator()(const std::optional<T>& value){(*this)(value.has_value());if(value)(*this)(*value);}
    void operator()(const Point& p){(*this)(p.x);(*this)(p.y);}
    void operator()(const Box& b){(*this)(b.left);(*this)(b.top);(*this)(b.right);(*this)(b.bottom);}
    void operator()(const Mark& m){MarkFields(*this,m);}
    void operator()(const SelectionMark& m){(*this)(m.kind);(*this)(m.boxes);}
    void Finish(){const std::streamoff size=stream.tellp();Require(size>=0&&uint64_t(size)<=MaxFile);stream.flush();Require(bool(stream));stream.close();}
};
struct Reader {
    std::ifstream stream;uint64_t remaining{};
    explicit Reader(const std::filesystem::path& path):stream(path,std::ios::binary){Require(bool(stream));remaining=std::filesystem::file_size(path);Require(remaining<=MaxFile);}
    void Raw(void* p,size_t size){Require(size<=remaining);stream.read(static_cast<char*>(p),static_cast<std::streamsize>(size));Require(bool(stream));remaining-=size;}
    template<class T> void operator()(T& value){
        if constexpr(std::is_same_v<T,bool>){uint8_t b{};Raw(&b,1);Require(b<=1);value=b!=0;}
        else if constexpr(std::is_enum_v<T>){int32_t v{};(*this)(v);Require(v>=0&&v<=EnumMax<T>());value=static_cast<T>(v);}
        else {static_assert(std::is_arithmetic_v<T>);Raw(&value,sizeof(value));if constexpr(std::is_floating_point_v<T>)Require(std::isfinite(value)&&std::abs(value)<=10000000);}
    }
    uint32_t Count(uint32_t limit=1000000){uint32_t count{};(*this)(count);Require(count<=limit&&count<=remaining);return count;}
    void operator()(std::wstring& value){const auto n=Count();Require(uint64_t(n)*sizeof(wchar_t)<=remaining);value.resize(n);Raw(value.data(),value.size()*sizeof(wchar_t));}
    template<class T> void operator()(std::vector<T>& value){const auto n=Count(std::is_same_v<T,Mark>?10000:1000000);value.resize(n);for(auto& x:value)(*this)(x);}
    void operator()(std::vector<unsigned char>& bytes){const auto n=Count(static_cast<uint32_t>(MaxFile));bytes.resize(n);Raw(bytes.data(),bytes.size());}
    template<class T,size_t N> void operator()(std::array<T,N>& value){for(auto& x:value)(*this)(x);}
    template<class T> void operator()(std::optional<T>& value){bool present{};(*this)(present);if(present){value.emplace();(*this)(*value);}else value.reset();}
    void operator()(Point& p){(*this)(p.x);(*this)(p.y);}
    void operator()(Box& b){(*this)(b.left);(*this)(b.top);(*this)(b.right);(*this)(b.bottom);}
    void operator()(Mark& m){MarkFields(*this,m);}
    void operator()(SelectionMark& m){(*this)(m.kind);(*this)(m.boxes);}
    void End(){Require(remaining==0);}
};
constexpr uint64_t PayloadMagic=0x315441444e49504c,IndexMagic=0x315844494e49504c;
void Header(Writer& w,uint64_t magic){w(magic);w(uint32_t{1});}
void Header(Reader& r,uint64_t expected){uint64_t magic{};uint32_t version{};r(magic);r(version);Require(magic==expected&&version==1);}
std::wstring FileName(uint64_t nonce,uint64_t id,uint64_t revision){return L"pin-"+std::to_wstring(nonce)+L"-"+std::to_wstring(id)+L"-"+std::to_wstring(revision)+L".dat";}
void Commit(const std::filesystem::path& temporary,const std::filesystem::path& final){Require(MoveFileExW(temporary.c_str(),final.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE);}
bool Same(const std::shared_ptr<const Frame>& a,const std::shared_ptr<const Frame>& b){return a==b||(a&&b&&a->Width()==b->Width()&&a->Height()==b->Height()&&a->pixels==b->pixels);}
void SaveContent(const std::filesystem::path& path,const PinSessionRecord& p){
    Require(p.image&&p.ocr_image&&p.base);const auto temp=std::filesystem::path(path.native()+L".tmp");Writer w(temp);Header(w,PayloadMagic);w(Encode(*p.image));
    const bool same_ocr=Same(p.image,p.ocr_image),same_base=Same(p.image,p.base);w(same_ocr);if(!same_ocr)w(Encode(*p.ocr_image));w(same_base);if(!same_base)w(Encode(*p.base));w(p.annotations);w(p.decorations);w.Finish();Commit(temp,path);
}
void LoadContent(const std::filesystem::path& path,PinSessionRecord& p){Reader r(path);Header(r,PayloadMagic);std::vector<unsigned char> bytes;r(bytes);p.image=Decode(bytes);bool same{};r(same);if(same)p.ocr_image=p.image;else{r(bytes);p.ocr_image=Decode(bytes);}r(same);if(same)p.base=p.image;else{r(bytes);p.base=Decode(bytes);}r(p.annotations);r(p.decorations);r.End();Require(p.image->Width()==p.ocr_image->Width()&&p.image->Height()==p.ocr_image->Height()&&p.image->Width()==p.base->Width()&&p.image->Height()==p.base->Height());}
}
PinSessionStore::PinSessionStore(std::filesystem::path directory):directory_(std::move(directory)){GUID guid{};Check(CoCreateGuid(&guid));static_assert(sizeof(guid)==16);const auto* data=reinterpret_cast<const unsigned char*>(&guid);for(int i=0;i<8;++i)nonce_=(nonce_<<8)|data[i];}
std::filesystem::path PinSessionStore::DefaultDirectory(){PWSTR value{};if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&value)))return {};const auto directory=std::filesystem::path(value)/L"LumaShot"/L"pin-session";CoTaskMemFree(value);return directory;}
std::vector<PinSessionRecord> PinSessionStore::Load(unsigned& skipped){
    unresolved_.clear();
    skipped=0;const auto path=directory_/L"session.index";if(!std::filesystem::exists(path))return {};
    Reader r(path);Header(r,IndexMagic);const auto count=r.Count(1000);std::vector<PinSessionRecord> records;std::set<std::pair<uint64_t,uint64_t>> seen;
    for(uint32_t i=0;i<count;++i){PinSessionRecord p;uint64_t nonce{};r(nonce);r(p.id);r(p.revision);r(p.origin);r(p.zoom);r(p.style);r(p.locked);r(p.recognize);Require(p.id&&p.revision&&p.zoom>0&&seen.insert({nonce,p.id}).second);
        try{LoadContent(directory_/FileName(nonce,p.id,p.revision),p);records.push_back(std::move(p));}catch(...){++skipped;p.storage_nonce=nonce;p.image.reset();p.ocr_image.reset();p.base.reset();unresolved_.push_back(std::move(p));}
    }r.End();return records;
}
void PinSessionStore::Save(const std::vector<PinSessionRecord>& records){
    Require(!directory_.empty()&&records.size()+unresolved_.size()<=1000);std::filesystem::create_directories(directory_);std::set<std::wstring> active;
    for(const auto& p:records){Require(p.id&&p.revision);const auto name=FileName(nonce_,p.id,p.revision);active.insert(name);if(!written_.contains(p.id)||written_.at(p.id)!=p.revision||!std::filesystem::exists(directory_/name)){SaveContent(directory_/name,p);written_[p.id]=p.revision;}}
    for(const auto& p:unresolved_)active.insert(FileName(p.storage_nonce,p.id,p.revision));
    const auto temp=directory_/L"session.index.tmp";Writer w(temp);Header(w,IndexMagic);w(static_cast<uint32_t>(records.size()+unresolved_.size()));
    for(const auto& p:records){w(nonce_);w(p.id);w(p.revision);w(p.origin);w(p.zoom);w(p.style);w(p.locked);w(p.recognize);}
    for(const auto& p:unresolved_){w(p.storage_nonce);w(p.id);w(p.revision);w(p.origin);w(p.zoom);w(p.style);w(p.locked);w(p.recognize);}w.Finish();Commit(temp,directory_/L"session.index");
    // Only after the new index is committed can obsolete screenshots be deleted.
    std::error_code error;for(const auto& file:std::filesystem::directory_iterator(directory_)){const auto name=file.path().filename().native();if(name.starts_with(L"pin-")&&(file.path().extension()==L".dat"||file.path().extension()==L".tmp")&&!active.contains(name))std::filesystem::remove(file.path(),error);}
}
}
