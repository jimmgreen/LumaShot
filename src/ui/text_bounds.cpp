#include "ui/text_renderer.h"
#include "model/selection.h"
#include "model/tag_appearance.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
namespace lumashot {
void FitTextBounds(Mark& mark){
    if(mark.tool!=Tool::Text||!mark.text_auto_size||mark.text.empty())return;
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),"Text measurement factory");
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    CheckWin32(SUCCEEDED(factory->CreateTextFormat(mark.font_family.c_str(),nullptr,mark.text_bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,mark.font_size,L"zh-CN",&format)),"Text measurement font");
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    const auto box=Normalize(mark.a,mark.b);const auto padding=IsTextTag(mark.text_background)?TextTagMetrics(mark.font_size):TagMetrics{};
    const float limit=std::max(1.f,mark.text_wrap_width>0?mark.text_wrap_width:box.right-box.left-2*padding.x);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    CheckWin32(SUCCEEDED(factory->CreateTextLayout(mark.text.data(),static_cast<UINT32>(mark.text.size()),format.Get(),limit,1000000,&layout)),"Measure annotation text");
    DWRITE_TEXT_METRICS metrics{};CheckWin32(SUCCEEDED(layout->GetMetrics(&metrics)),"Read annotation text size");
    // Round outward and leave a subpixel guard so the last glyph never wraps
    // just because the measured layout was tightened to its content width.
    const float width=std::min(limit,std::max(1.f,std::ceil(metrics.widthIncludingTrailingWhitespace)+1));
    layout->SetMaxWidth(width);CheckWin32(SUCCEEDED(layout->GetMetrics(&metrics)),"Measure fitted text height");
    const float height=std::max(1.f,std::ceil(metrics.height)+1)+2*padding.y;
    const float outer_width=width+2*padding.x;
    const float alignment=mark.text_align==TextAlign::Center?.5f:mark.text_align==TextAlign::Right?1.f:0.f;
    const Point anchor{box.left+(box.right-box.left)*alignment,box.top};
    const auto world=RotatePoint(anchor,MarkCenter(mark),mark.rotation);
    mark.a={anchor.x-outer_width*alignment,box.top};mark.b={mark.a.x+outer_width,mark.a.y+height};
    const auto fitted=RotatePoint(anchor,MarkCenter(mark),mark.rotation);
    Translate(mark,{world.x-fitted.x,world.y-fitted.y});
}

}
