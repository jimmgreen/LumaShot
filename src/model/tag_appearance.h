#pragma once
#include "model/document.h"
#include "model/selection.h"
#include "model/annotation_palette.h"
namespace lumashot {
inline bool IsTextTag(TextBackground mode){return mode==TextBackground::TagAutomatic||mode==TextBackground::TagLight||mode==TextBackground::TagDark;}
struct TagMetrics {float x,y,radius,stroke;};
// Annotation sizes are physical pixels, already DPI/scaling adjusted by callers.
inline TagMetrics TextTagMetrics(float font){const float s=std::max(.25f,font/24);return {7*s,3*s,4*s,s};}
inline Box TagContentBounds(Box box,TagMetrics m){
    const float x=std::min(m.x,std::max(0.f,(box.right-box.left-1)/2));
    const float y=std::min(m.y,std::max(0.f,(box.bottom-box.top-1)/2));
    return {box.left+x,box.top+y,box.right-x,box.bottom-y};
}
inline Box TextContentBounds(const Mark& mark){const auto box=Normalize(mark.a,mark.b);return IsTextTag(mark.text_background)?TagContentBounds(box,TextTagMetrics(mark.font_size)):box;}
inline uint32_t TagComposite(uint32_t ink,uint32_t background){
    const unsigned alpha=ink>>24;uint32_t result=0xff000000;
    for(int shift:{0,8,16})result|=((((ink>>shift)&255)*alpha+((background>>shift)&255)*(255-alpha)+127)/255)<<shift;
    return result;
}
struct TagBackdrop {std::array<uint32_t,64> pixels{};size_t count{};};
inline TagBackdrop SampleTagBackdrop(const Frame& frame,Box box,const Mark& mark){
    TagBackdrop sample;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){
        auto p=RotatePoint({box.left+(box.right-box.left)*(x+.5f)/8,box.top+(box.bottom-box.top)*(y+.5f)/8},MarkCenter(mark),mark.rotation);
        const int px=int(std::floor(p.x))-frame.bounds.left,py=int(std::floor(p.y))-frame.bounds.top;
        if(px>=0&&py>=0&&px<frame.Width()&&py<frame.Height())sample.pixels[sample.count++]=frame.pixels[size_t(py)*frame.Width()+px];
    }
    if(!sample.count)sample.pixels[sample.count++]=0xffffffff;
    return sample;
}
struct TagColors {uint32_t ink,background,border;double contrast;};
inline TagColors TagTones(uint32_t hue,const TagBackdrop& sample,bool dark){
    const uint32_t background=(hue&0xffffff)|(dark?0x2e000000u:0x1f000000u);
    std::array<double,64> luminance{};
    for(size_t i=0;i<sample.count;++i)luminance[i]=NoteLuminance(TagComposite(background,sample.pixels[i]));
    const auto score=[&](uint32_t ink){
        std::array<double,64> values{};const double light=NoteLuminance(ink);
        for(size_t i=0;i<sample.count;++i)values[i]=NoteContrast(light,luminance[i]);
        std::sort(values.begin(),values.begin()+sample.count);
        // Evaluate the low percentile, not contrast against an imaginary solid fill.
        return values[(sample.count-1)/10];
    };
    uint32_t ink=hue|0xff000000;double contrast=score(ink);
    for(int step=1;step<=100&&contrast<4.5;++step){ink=MixNoteColor(hue,dark?0xffffffffu:0xff000000u,float(step)/100);contrast=score(ink);}
    return {ink,background,(hue&0xffffff)|(dark?0x52000000u:0x47000000u),contrast};
}
inline TagColors ResolveTagColors(const Frame& frame,const Mark& mark,Box box,TextBackground mode=TextBackground::TagAutomatic){
    const auto sample=SampleTagBackdrop(frame,box,mark);
    std::array<double,64> light{};for(size_t i=0;i<sample.count;++i)light[i]=NoteLuminance(sample.pixels[i]);
    std::sort(light.begin(),light.begin()+sample.count);
    const bool dark=mode==TextBackground::TagDark||(mode==TextBackground::TagAutomatic&&light[(sample.count-1)/2]<.18);
    auto result=TagTones(mark.color,sample,dark);
    if(mode==TextBackground::TagAutomatic&&result.contrast<4.5){const auto other=TagTones(mark.color,sample,!dark);if(other.contrast>result.contrast)result=other;}
    // On arbitrary textures fixed translucency cannot guarantee 4.5 everywhere.
    // Never silently replace the tint with an opaque black/white slab.
    return result;
}
}
