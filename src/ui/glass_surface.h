#pragma once
#include "ui/acrylic.h"
#include "ui/controls.h"

namespace lumashot::ui {
inline constexpr float ToolPanelRadius=14.f;
inline constexpr wchar_t ToolFont[]=L"Segoe UI";
// DWM owns the acrylic material. Never lay a tinted scrim over it.
// A single alpha quantum is ONLY the layered-window hit-test coverage: fully
// zero alpha makes empty panel areas click through to the application below.
inline void DrawGlassSurface(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,
    D2D1_RECT_F box,bool dark,bool acrylic,float radius=ToolPanelRadius,float scale=1.f) {
    brush->SetColor(acrylic?D2D1::ColorF(0,1.f/255.f):D2D1::ColorF(PanelBackground(dark)));
    target->FillRoundedRectangle(D2D1::RoundedRect(box,radius*scale,radius*scale),brush);
    const Theme theme{scale,dark};const auto border=theme.Border();
    brush->SetColor(D2D1::ColorF(border&0xffffff,float(border>>24)/255.f));
    const float inset=.5f*scale;
    box={box.left+inset,box.top+inset,box.right-inset,box.bottom-inset};
    target->DrawRoundedRectangle(D2D1::RoundedRect(box,radius*scale,radius*scale),brush,scale);
}
}
