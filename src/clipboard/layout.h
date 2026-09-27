#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>
#include <string>
#include <algorithm>
#include <cstdlib>
namespace lumashot::clipboard {
inline constexpr LONG PanelMinWidthDip=280,PanelMinHeightDip=480,PanelMaxSizeDip=4096;
// Work-area coordinates are physical pixels; magnet distances scale with DPI.
inline LONG SnapAxis(LONG value,LONG low,LONG high,float scale,int& edge){
    const LONG enter=static_cast<LONG>(24*scale),leave=static_cast<LONG>(36*scale);
    if(edge<0&&std::abs(value-low)<=leave)return low;
    if(edge>0&&std::abs(value-high)<=leave)return high;
    edge=0;
    if(std::abs(value-low)<=enter&&std::abs(value-low)<=std::abs(value-high)){edge=-1;return low;}
    if(std::abs(value-high)<=enter){edge=1;return high;}
    return std::clamp(value,low,high);
}
inline std::optional<POINT> LoadPosition(const std::filesystem::path& path){
    if(path.empty()||GetPrivateProfileIntW(L"Panel",L"Version",0,path.c_str())!=1)return {};
    const int x=static_cast<int>(GetPrivateProfileIntW(L"Panel",L"Right",999999,path.c_str()));
    const int y=static_cast<int>(GetPrivateProfileIntW(L"Panel",L"Top",999999,path.c_str()));
    if(x < -200000||x > 200000||y < -200000||y > 200000)return {};
    return POINT{x,y};
}
inline std::optional<SIZE> LoadPanelSize(const std::filesystem::path& path){
    if(path.empty())return {};
    const auto width=GetPrivateProfileIntW(L"Panel",L"Width",0,path.c_str()),height=GetPrivateProfileIntW(L"Panel",L"Height",0,path.c_str());
    if(width<static_cast<UINT>(PanelMinWidthDip)||width>static_cast<UINT>(PanelMaxSizeDip)||height<static_cast<UINT>(PanelMinHeightDip)||height>static_cast<UINT>(PanelMaxSizeDip))return {};
    return SIZE{static_cast<LONG>(width),static_cast<LONG>(height)};
}
inline bool SavePosition(const std::filesystem::path& path,POINT point,std::optional<SIZE> dimensions={}){
    if(path.empty())return false;std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return false;
    const auto temporary=std::filesystem::path(path.native()+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp");
    if(!dimensions)dimensions=LoadPanelSize(path);
    bool written=WritePrivateProfileStringW(L"Panel",L"Version",L"1",temporary.c_str())&&WritePrivateProfileStringW(L"Panel",L"Right",std::to_wstring(point.x).c_str(),temporary.c_str())&&WritePrivateProfileStringW(L"Panel",L"Top",std::to_wstring(point.y).c_str(),temporary.c_str());
    if(dimensions)written=written&&WritePrivateProfileStringW(L"Panel",L"Width",std::to_wstring(dimensions->cx).c_str(),temporary.c_str())&&WritePrivateProfileStringW(L"Panel",L"Height",std::to_wstring(dimensions->cy).c_str(),temporary.c_str());
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
    const bool saved=written&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!saved)std::filesystem::remove(temporary,ec);return saved;
}
}
