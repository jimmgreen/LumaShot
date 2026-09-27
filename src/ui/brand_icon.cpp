#include "ui/brand_icon.h"
#include "capture/frame.h"
#include <initializer_list>

namespace lumashot {
using Microsoft::WRL::ComPtr;
void BrandIcon::Load(ID2D1RenderTarget* target){
    Reset();ComPtr<ID2D1Factory> factory;target->GetFactory(&factory);
    const auto check=[](HRESULT hr){CheckWin32(SUCCEEDED(hr),"Create vector brand icon");};
    const auto solid=[&](UINT32 rgb,float alpha){ComPtr<ID2D1SolidColorBrush> brush;check(target->CreateSolidColorBrush(D2D1::ColorF(rgb,alpha),&brush));return ComPtr<ID2D1Brush>(brush);};
    const auto gradient=[&](D2D1_POINT_2F from,D2D1_POINT_2F to,std::initializer_list<D2D1_GRADIENT_STOP> stops){
        ComPtr<ID2D1GradientStopCollection> collection;check(target->CreateGradientStopCollection(stops.begin(),static_cast<UINT32>(stops.size()),D2D1_GAMMA_2_2,D2D1_EXTEND_MODE_CLAMP,&collection));
        ComPtr<ID2D1LinearGradientBrush> brush;check(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from,to),collection.Get(),&brush));return ComPtr<ID2D1Brush>(brush);
    };
    const auto rounded=[&](D2D1_ROUNDED_RECT rect){ComPtr<ID2D1RoundedRectangleGeometry> geometry;check(factory->CreateRoundedRectangleGeometry(rect,&geometry));return ComPtr<ID2D1Geometry>(geometry);};
    const auto path=[&](auto draw){ComPtr<ID2D1PathGeometry> geometry;ComPtr<ID2D1GeometrySink> sink;check(factory->CreatePathGeometry(&geometry));check(geometry->Open(&sink));draw(sink.Get());check(sink->Close());return ComPtr<ID2D1Geometry>(geometry);};
    const auto add=[&](ComPtr<ID2D1Geometry> geometry,ComPtr<ID2D1Brush> fill,ComPtr<ID2D1Brush> stroke,float width){items_.push_back({std::move(geometry),std::move(fill),std::move(stroke),width});};
    try {
#include "brand_svg_data.inc"
        target_=target;
    }catch(...){Reset();throw;}
}
void BrandIcon::Draw(ID2D1RenderTarget* target,D2D1_RECT_F bounds,bool trim_margin){
    if(items_.empty()||target_!=target)Load(target);
    D2D1_MATRIX_3X2_F previous;target->GetTransform(&previous);
    const float sx=(bounds.right-bounds.left)/(trim_margin?1004.f:1254.f),sy=(bounds.bottom-bounds.top)/(trim_margin?960.f:1254.f);
    target->SetTransform(D2D1::Matrix3x2F::Scale(sx,sy)*D2D1::Matrix3x2F::Translation(bounds.left-(trim_margin?125*sx:0),bounds.top-(trim_margin?136*sy:0))*previous);
    for(const auto& item:items_){target->FillGeometry(item.geometry.Get(),item.fill.Get());if(item.stroke)target->DrawGeometry(item.geometry.Get(),item.stroke.Get(),item.stroke_width);}
    target->SetTransform(previous);
}
}
