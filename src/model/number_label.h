#pragma once
#include "model/document.h"
#include <string_view>

namespace lumashot {
inline constexpr size_t NumberLabelLimit=64; // UTF-16 units, never split a surrogate pair.
inline std::wstring NormalizeNumberLabel(std::wstring_view text){
    std::wstring result;
    for(size_t i=0;i<text.size()&&result.size()<NumberLabelLimit;++i){
        wchar_t c=text[i];if(c==L'\r'||c==L'\n'||c==L'\t')c=L' ';
        if(c<32||c==127)continue;
        if(c>=0xd800&&c<=0xdbff){
            if(i+1>=text.size()||text[i+1]<0xdc00||text[i+1]>0xdfff)continue;
            if(result.size()+2>NumberLabelLimit)break;
            result+=c;result+=text[++i];
        }else if(c<0xdc00||c>0xdfff)result+=c;
    }
    const auto first=result.find_first_not_of(L' ');if(first==std::wstring::npos)return {};
    return result.substr(first,result.find_last_not_of(L' ')-first+1);
}
inline std::wstring NumberCaption(const Mark& mark){return mark.number_label.empty()?std::to_wstring(mark.number):mark.number_label;}
// Measure on creation/property changes only, never on every pointer move or paint.
// Fits around the current badge center in local coordinates; caller preserves rotation.
void FitNumberBadge(Mark& mark);
}
