#include "model/number_label.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

namespace lumashot {
void FitNumberBadge(Mark& mark){
    if(mark.tool!=Tool::Number)return;
    mark.number_label=NormalizeNumberLabel(mark.number_label);
    const auto old=Normalize(mark.a,mark.b);const Point center{(old.left+old.right)/2,(old.top+old.bottom)/2};
    const float size=std::max(1.f,mark.number_size);
    float width=size*(mark.number_shape==NumberShape::Capsule?1.5f:1.f),height=size*(mark.number_shape==NumberShape::Pin?1.3f:1.f);
    if(!mark.number_label.empty()){
        static thread_local Microsoft::WRL::ComPtr<IDWriteFactory> factory;
        if(!factory)CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),"Label measurement factory");
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        CheckWin32(SUCCEEDED(factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size*.56f,L"zh-CN",&format)),"Label measurement font");
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        CheckWin32(SUCCEEDED(factory->CreateTextLayout(mark.number_label.data(),static_cast<UINT32>(mark.number_label.size()),format.Get(),1000000,1000000,&layout)),"Label measurement layout");
        DWRITE_TEXT_METRICS metrics{};CheckWin32(SUCCEEDED(layout->GetMetrics(&metrics)),"Label measurement metrics");
        height=size;width=std::max(size,metrics.widthIncludingTrailingWhitespace+size*.9f);
    }
    mark.a={center.x-width/2,center.y-height/2};mark.b={center.x+width/2,center.y+height/2};
}
}
