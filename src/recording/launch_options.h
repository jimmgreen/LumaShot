#pragma once
#include <windows.h>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lumashot::recording {
// Desktop coordinates remain physical pixels, including negative monitor origins.
inline bool ValidRecordingRegion(RECT r) {
    const auto width=int64_t(r.right)-r.left,height=int64_t(r.bottom)-r.top;
    return width>=8&&height>=8&&width<=std::numeric_limits<int>::max()&&height<=std::numeric_limits<int>::max();
}
inline std::wstring RecordingRegionArgument(RECT r) {
    if(!ValidRecordingRegion(r))throw std::invalid_argument("Invalid recording selection");
    return L" --region="+std::to_wstring(r.left)+L","+std::to_wstring(r.top)+L","+std::to_wstring(r.right)+L","+std::to_wstring(r.bottom);
}
inline std::optional<RECT> ParseRecordingRegion(std::wstring_view value) {
    std::string ascii;ascii.reserve(value.size());
    for(wchar_t c:value){if(c>127)return {};ascii.push_back(static_cast<char>(c));}
    RECT r{};LONG* fields[]={&r.left,&r.top,&r.right,&r.bottom};
    const char* next=ascii.data();const char* end=next+ascii.size();
    for(int i=0;i<4;++i){
        const auto parsed=std::from_chars(next,end,*fields[i]);
        if(parsed.ec!=std::errc{})return {};
        next=parsed.ptr;
        if(i<3){if(next==end||*next!=',')return {};++next;}
    }
    if(next!=end||!ValidRecordingRegion(r))return {};
    return r;
}
inline std::optional<RECT> ClipRecordingRegion(RECT requested,RECT monitor) {
    if(!ValidRecordingRegion(requested))return {};
    RECT clipped{};
    if(!IntersectRect(&clipped,&requested,&monitor)||!ValidRecordingRegion(clipped))return {};
    return clipped;
}
}
