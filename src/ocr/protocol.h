#pragma once
#include "ocr/text.h"
#include <cstring>
#include <stdexcept>
#include <utility>
#include <cmath>
namespace lumashot::ocr {
class Handle {
public:
    Handle()=default;explicit Handle(HANDLE h):h_(h){}
    ~Handle(){Reset();}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& h) noexcept:h_(std::exchange(h.h_,nullptr)){}
    Handle& operator=(Handle&& h) noexcept {if(this!=&h){Reset();h_=std::exchange(h.h_,nullptr);}return *this;}
    HANDLE Get() const{return h_;}
    void Reset(HANDLE h=nullptr){if(h_&&h_!=INVALID_HANDLE_VALUE)CloseHandle(h_);h_=h;}
private:HANDLE h_{};
};
struct MappingView {
    void* data{};
    explicit MappingView(HANDLE h,DWORD access,size_t bytes=0):data(MapViewOfFile(h,access,0,0,bytes)){if(!data)throw std::runtime_error("Cannot map OCR image");}
    ~MappingView(){UnmapViewOfFile(data);}
    MappingView(const MappingView&)=delete;MappingView& operator=(const MappingView&)=delete;
};
constexpr uint32_t kProtocol=0x4c4f4302,kMaxReply=16*1024*1024;
struct Request {uint32_t magic{kProtocol},width{},height{},reserved{};uint64_t mapping{};};
class Bytes {
public:
    std::vector<uint8_t> data;size_t position{};
    template<class T> void Put(const T& v){if(data.size()>kMaxReply-sizeof(T))throw std::runtime_error("OCR result exceeds limit");const auto* p=reinterpret_cast<const uint8_t*>(&v);data.insert(data.end(),p,p+sizeof(T));}
    template<class T> T Get(){if(position>data.size()||sizeof(T)>data.size()-position)throw std::runtime_error("Truncated OCR reply");T v;std::memcpy(&v,data.data()+position,sizeof(T));position+=sizeof(T);return v;}
    void PutString(const std::wstring& s){if(s.size()>kMaxReply/sizeof(wchar_t))throw std::runtime_error("OCR string exceeds limit");Put(static_cast<uint32_t>(s.size()));for(auto c:s)Put(c);}
    std::wstring GetString(){const auto n=Get<uint32_t>();if(n>(data.size()-position)/sizeof(wchar_t))throw std::runtime_error("Invalid OCR string");std::wstring s;s.reserve(n);for(uint32_t i=0;i<n;++i)s+=Get<wchar_t>();return s;}
};
inline Bytes Encode(const Text& text,const std::wstring& error) {
    Bytes bytes;bytes.Put(kProtocol);bytes.PutString(error);bytes.Put(static_cast<uint32_t>(text.lines.size()));
    for(const auto& line:text.lines){bytes.Put(line.box);bytes.Put(line.confidence);bytes.Put(line.quad);bytes.Put(static_cast<uint32_t>(line.glyphs.size()));
        for(const auto& g:line.glyphs){bytes.Put(g.box);bytes.Put(g.confidence);bytes.PutString(g.text);}}
        bytes.Put(static_cast<uint32_t>(text.table.has_value()));
    if(text.table){const auto& t=*text.table;
        if(t.rows<2||t.rows>256||t.columns<2||t.columns>64||t.cells.size()!=static_cast<size_t>(t.rows)*t.columns)throw std::runtime_error("Invalid table dimensions");
        bytes.Put(t.box);bytes.Put(t.rows);bytes.Put(t.columns);for(const auto& cell:t.cells)bytes.PutString(cell);}
    if(bytes.data.size()>kMaxReply)throw std::runtime_error("OCR result exceeds limit");return bytes;
}
inline void ValidateGeometry(Box box,float confidence) {
    if(!std::isfinite(box.left)||!std::isfinite(box.top)||!std::isfinite(box.right)||!std::isfinite(box.bottom)||box.left<0||box.top<0||box.right<box.left||box.bottom<box.top||box.right>32768||box.bottom>32768||!std::isfinite(confidence)||confidence<0||confidence>1)throw std::runtime_error("Invalid OCR geometry");
}
inline Text Decode(Bytes& b,std::wstring& error) {
    if(b.data.size()>kMaxReply)throw std::runtime_error("OCR reply exceeds limit");
    if(b.Get<uint32_t>()!=kProtocol)throw std::runtime_error("Incompatible OCR worker");
    error=b.GetString();Text text;const auto count=b.Get<uint32_t>();
    if(count>10000)throw std::runtime_error("Invalid OCR line count");
    for(uint32_t l=0;l<count;++l){Line line;line.box=b.Get<Box>();line.confidence=b.Get<float>();ValidateGeometry(line.box,line.confidence);line.quad=b.Get<std::array<Point,4>>();for(auto p:line.quad)if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>32768||p.y>32768)throw std::runtime_error("Invalid OCR quad");const auto n=b.Get<uint32_t>();
        if(n>100000)throw std::runtime_error("Invalid OCR character count");
        for(uint32_t g=0;g<n;++g){Glyph glyph;glyph.box=b.Get<Box>();glyph.confidence=b.Get<float>();ValidateGeometry(glyph.box,glyph.confidence);glyph.text=b.GetString();line.glyphs.push_back(std::move(glyph));}
        text.lines.push_back(std::move(line));}
        const auto hasTable=b.Get<uint32_t>();if(hasTable>1)throw std::runtime_error("Invalid table flag");
    if(hasTable){Table t;t.box=b.Get<Box>();t.rows=b.Get<uint32_t>();t.columns=b.Get<uint32_t>();
        if(!std::isfinite(t.box.left)||!std::isfinite(t.box.top)||!std::isfinite(t.box.right)||!std::isfinite(t.box.bottom)||t.box.left<0||t.box.top<0||t.box.right<=t.box.left||t.box.bottom<=t.box.top||t.box.right>32768||t.box.bottom>32768||t.rows<2||t.rows>256||t.columns<2||t.columns>64)throw std::runtime_error("Invalid table geometry");
        const size_t cells=static_cast<size_t>(t.rows)*t.columns;if(cells>(b.data.size()-b.position)/sizeof(uint32_t))throw std::runtime_error("Truncated table");
        for(size_t i=0;i<cells;++i)t.cells.push_back(b.GetString());text.table=std::move(t);}
    if(b.position!=b.data.size())throw std::runtime_error("Invalid OCR reply length");return text;
}
inline bool ReadExact(HANDLE h,void* buffer,size_t bytes) {
    auto* p=static_cast<uint8_t*>(buffer);
    while(bytes){DWORD n{};if(!ReadFile(h,p,static_cast<DWORD>(std::min(bytes,size_t(65536))),&n,nullptr)||!n)return false;p+=n;bytes-=n;}return true;
}
inline bool WriteExact(HANDLE h,const void* buffer,size_t bytes) {
    const auto* p=static_cast<const uint8_t*>(buffer);
    while(bytes){DWORD n{};if(!WriteFile(h,p,static_cast<DWORD>(std::min(bytes,size_t(65536))),&n,nullptr)||!n)return false;p+=n;bytes-=n;}return true;
}
}


