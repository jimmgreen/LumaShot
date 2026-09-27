#include "ui/selection_render.h"
#include <wrl/client.h>
namespace lumashot {
void DrawSelectionEditor(ID2D1RenderTarget* target,const Mark& mark,EditPart part,float scale){
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;target->CreateSolidColorBrush(D2D1::ColorF(0x0784ff),&brush);
    const auto handles=EditHandles(mark,part,scale);auto pt=[](Point p){return D2D1::Point2F(p.x,p.y);};
    if(part!=EditPart::Leader&&handles.size()>=8){for(int i=0;i<8;i+=2)target->DrawLine(pt(handles[i].point),pt(handles[(i+2)%8].point),brush.Get(),1);if(handles.size()>8&&handles[8].id==8)target->DrawLine(pt(handles[1].point),pt(handles[8].point),brush.Get(),1);}
    D2D1_MATRIX_3X2_F original;target->GetTransform(&original);
    for(auto handle:handles){const auto p=handle.point;const float r=(handle.id>=9&&handle.id<=12?3.5f:4.f)*scale;brush->SetColor(D2D1::ColorF(0xffffff));
        if(handle.id<8)target->SetTransform(D2D1::Matrix3x2F::Rotation(mark.rotation,pt(p))*original);
        if(handle.id>=8)target->FillEllipse(D2D1::Ellipse(pt(p),r,r),brush.Get());else target->FillRectangle({p.x-r,p.y-r,p.x+r,p.y+r},brush.Get());
        brush->SetColor(D2D1::ColorF(handle.id>=9&&handle.id<=12?0xffa321:0x0784ff));if(handle.id>=8)target->DrawEllipse(D2D1::Ellipse(pt(p),r,r),brush.Get(),1.2f);else target->DrawRectangle({p.x-r,p.y-r,p.x+r,p.y+r},brush.Get(),1.2f);
        target->SetTransform(original);
    }
}
}
