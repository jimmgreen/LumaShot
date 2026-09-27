#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <stdexcept>
namespace lumashot::recording {
// Full 8-bit weighted median cut over the bounded cross-frame sample set.
// WIC's palette generation can discard low color bits even with spare entries.
inline std::vector<uint32_t> GifPalette(std::span<const uint8_t> bgra){
    struct Color {uint32_t rgb;uint64_t count;};
    struct Box {size_t begin,end;double error{};int axis{};};
    std::vector<uint32_t> sorted;sorted.reserve(bgra.size()/4);
    for(size_t i=0;i+3<bgra.size();i+=4)sorted.push_back((uint32_t(bgra[i+2])<<16)|(uint32_t(bgra[i+1])<<8)|bgra[i]);
    if(sorted.empty())throw std::invalid_argument("Empty GIF color samples");
    std::sort(sorted.begin(),sorted.end());std::vector<Color> colors;
    for(auto c:sorted){if(!colors.empty()&&colors.back().rgb==c)++colors.back().count;else colors.push_back({c,1});}
    const auto channel=[](uint32_t rgb,int axis){return int((rgb>>(axis*8))&255);};
    const auto measure=[&](Box& box){
        uint64_t count=0;std::array<double,3> sum{},square{};
        for(size_t i=box.begin;i<box.end;++i){count+=colors[i].count;for(int c=0;c<3;++c){const double v=channel(colors[i].rgb,c),weight=double(colors[i].count);sum[c]+=v*weight;square[c]+=v*v*weight;}}
        constexpr int weights[]{1,4,2};double largest=-1;box.error=0;
        for(int c=0;c<3;++c){const double variance=std::max(0.,square[c]-sum[c]*sum[c]/double(count))*weights[c];box.error+=variance;if(variance>largest){largest=variance;box.axis=c;}}
        if(box.end-box.begin<2)box.error=0;
    };
    std::vector<Box> boxes{{0,colors.size()}};measure(boxes[0]);
    while(boxes.size()<255){
        auto it=std::max_element(boxes.begin(),boxes.end(),[](const Box& a,const Box& b){return a.error<b.error;});
        if(it->error<=0)break;
        const auto box=*it;const int axis=box.axis;
        std::sort(colors.begin()+box.begin,colors.begin()+box.end,[&](const Color& a,const Color& b){const int ca=channel(a.rgb,axis),cb=channel(b.rgb,axis);return ca==cb?a.rgb<b.rgb:ca<cb;});
        uint64_t total=0,partial=0;for(size_t i=box.begin;i<box.end;++i)total+=colors[i].count;
        size_t cut=box.begin;do{partial+=colors[cut++].count;}while(cut<box.end-1&&partial<total/2);
        *it={box.begin,cut};measure(*it);Box next{cut,box.end};measure(next);boxes.push_back(next);
    }
    std::vector<uint32_t> palette;palette.reserve(boxes.size());
    for(const auto& box:boxes){uint64_t count=0;std::array<uint64_t,3> sum{};
        for(size_t i=box.begin;i<box.end;++i){count+=colors[i].count;for(int c=0;c<3;++c)sum[c]+=uint64_t(channel(colors[i].rgb,c))*colors[i].count;}
        uint32_t rgb=0xff000000;for(int c=0;c<3;++c)rgb|=uint32_t((sum[c]+count/2)/count)<<(c*8);palette.push_back(rgb);
    }
    return palette;
}
}
