#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace lumashot {
// Six distinct hue families; shared by stroke, fill, highlighter and note swatches.
inline constexpr std::array<uint32_t,6> AnnotationColors{
    0xffe0529c,0xffe87040,0xffe8b339,0xff6abe39,0xff3b85e1,0xff9859d6};
inline constexpr std::array<int,6> AnnotationColorIds{20,21,22,23,29,30};
inline constexpr std::array<int,6> AnnotationFillIds{41,42,43,44,45,82};
inline uint32_t MixNoteColor(uint32_t a,uint32_t b,float amount){uint32_t c=0xff000000;for(int shift:{0,8,16})c|=uint32_t(std::lround(float((a>>shift)&255)*(1-amount)+float((b>>shift)&255)*amount))<<shift;return c;}
inline double NoteLuminance(uint32_t color){
    static const auto linear=[](){std::array<double,256> result{};for(int i=0;i<256;++i){const double v=i/255.;result[i]=v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}return result;}();
    return .2126*linear[(color>>16)&255]+.7152*linear[(color>>8)&255]+.0722*linear[color&255];
}
inline double NoteContrast(double a,double b){return (std::max(a,b)+.05)/(std::min(a,b)+.05);}
struct TonalColors {uint32_t ink,background;};
inline TonalColors AnnotationTones(uint32_t hue,bool dark){
    const uint32_t background=MixNoteColor(dark?0xff141414u:0xffffffffu,hue,dark?.12f:.10f);
    uint32_t ink=hue|0xff000000;
    for(int step=1;step<=100&&NoteContrast(NoteLuminance(ink),NoteLuminance(background))<4.5;++step)
        ink=MixNoteColor(hue,dark?0xffffffffu:0xff000000u,float(step)/100);
    return {ink,background};
}
}
