#pragma once
#include "model/document.h"
#include "model/tag_appearance.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace lumashot {
inline constexpr std::array<uint32_t,7> NoteColors{0xff374151,0xffb42318,0xffa54800,0xff166534,0xff7e22ce,0xff000000,0xffffffff};
struct NoteAppearance {uint32_t ink;std::optional<uint32_t> background;uint32_t border;};
inline bool AnnotationBackdropDark(const Frame& frame,Box box){
    double luminance=0;int count=0;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){
        const int px=int(box.left+(box.right-box.left)*(x+.5f)/8)-frame.bounds.left,py=int(box.top+(box.bottom-box.top)*(y+.5f)/8)-frame.bounds.top;
        if(px>=0&&py>=0&&px<frame.Width()&&py<frame.Height()){luminance+=NoteLuminance(frame.pixels[static_cast<size_t>(py)*frame.Width()+px]);++count;}
    }
    return count&&luminance/count<.18;
}
inline NoteAppearance ResolveTextAppearance(const Frame& frame,const Mark& mark){
    if(IsTextTag(mark.text_background)){const auto c=ResolveTagColors(frame,mark,Normalize(mark.a,mark.b),mark.text_background);return {c.ink,c.background,c.border};}
    if(mark.text_background==TextBackground::None)return {mark.color,std::nullopt,mark.color};
    if(static_cast<int>(mark.text_background)<=3)return {mark.color,TextBackgroundColor(mark.text_background),mark.color};
    const bool dark=mark.text_background==TextBackground::ToneDark||(mark.text_background==TextBackground::Automatic&&AnnotationBackdropDark(frame,Normalize(mark.a,mark.b)));
    const auto tones=AnnotationTones(mark.color,dark);return {tones.ink,tones.background,mark.color};
}
inline NoteAppearance ResolveNoteAppearance(const Frame& frame,const Mark& mark){
    const auto c=ResolveTagColors(frame,mark,NumberDetailBounds(mark));
    if(mark.number_text_preset==-2)return {c.ink,c.background,c.border};
    const bool preset=mark.number_text_preset>=0&&mark.number_text_preset<int(NoteColors.size());
    if(!preset)return {mark.number_text_color|0xff000000,c.background,c.border};
    Mark inkMark=mark;inkMark.color=NoteColors[mark.number_text_preset];
    const auto sample=SampleTagBackdrop(frame,NumberDetailBounds(mark),mark);
    const bool dark=(c.background>>24)>40;
    uint32_t ink=inkMark.color;
    for(int step=0;step<=100;++step){
        ink=MixNoteColor(inkMark.color,dark?0xffffffffu:0xff000000u,float(step)/100);
        double minimum=21;for(size_t i=0;i<sample.count;++i)minimum=std::min(minimum,NoteContrast(NoteLuminance(ink),NoteLuminance(TagComposite(c.background,sample.pixels[i]))));
        if(minimum>=4.5)break;
    }
    return {ink,c.background,c.border};
}
}