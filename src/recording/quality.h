#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>

namespace lumashot::recording {
inline unsigned VideoQualityValue(int preset){return preset<=0?65u:preset>=2?95u:85u;}
inline unsigned VideoBitrate(int width,int height,int fps,int preset){
    const long long pixels=static_cast<long long>(width)*height*fps;
    return static_cast<unsigned>(std::clamp(pixels/(preset<=0?10:preset>=2?3:5),
        preset<=0?750000LL:1500000LL,preset<=0?20000000LL:preset>=2?80000000LL:40000000LL));
}
struct GifPreset {int width,fps;};
inline GifPreset GifQualityPreset(int preset){return preset<=0?GifPreset{640,10}:preset>=2?GifPreset{0,25}:GifPreset{960,15};}
inline std::wstring EstimateMegabytes(double bytes){
    const double mb=std::max(.1,bytes/1000000.);
    const auto tenths=static_cast<uint64_t>(std::ceil(mb*10));
    return std::to_wstring(tenths/10)+L"."+std::to_wstring(tenths%10);
}
// A range, not a quota: GIF delta compression and video rate control depend on
// motion and texture. This calculation never scans frames on the UI thread.
inline std::wstring ExportSizeEstimate(bool gif,int width,int height,int fps,double seconds,int quality,bool audio){
    if(width<=0||height<=0||fps<=0||seconds<=0)return L"选择区域后显示体积预估";
    const double amount=gif?double(width)*height*fps*seconds:
        (VideoBitrate(width,height,fps,quality)+(audio?160000.:0.))*seconds/8.;
    return L"预估 "+EstimateMegabytes(amount*(gif?.04:.35))+L"–"+
        EstimateMegabytes(amount*(gif?.8:1.5))+L" MB · 以导出为准";
}
}
