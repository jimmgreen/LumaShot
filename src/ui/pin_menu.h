#pragma once
#include "ui/render.h"
#include <array>
namespace lumashot {
struct PinMenuModel {
    static constexpr int Count=9;
    static constexpr std::array<int,Count> Commands{7,8,1,2,9,3,4,5,6};
    std::array<bool,Count> enabled{true,true,true,true,true,true,true,true,true};
    std::wstring status;
    bool dark{},locked{},recognized{},table{};
    bool ocr_available{true};
    static constexpr float Width=224.f;
    static constexpr float Shadow=20.f;
    bool Visible(int i)const{return (i!=4||table)&&(i!=7||ocr_available);}
    float Height()const{return 376.f+(table?42.f:0.f)-(ocr_available?0.f:42.f)+(status.empty()?0.f:36.f);}
    float RowTop(int i)const{return 8.f+(status.empty()?0.f:36.f)+42.f*(i-(!table&&i>4?1:0)-(!ocr_available&&i>7?1:0))+(i>=2?8.f:0.f)+(i>=5?8.f:0.f)+(i>=8?8.f:0.f);}
    int Hit(float x,float y)const;
    int Next(int current,int direction)const;
};
RECT PlacePinMenu(POINT anchor,RECT work,float scale,const PinMenuModel& model);
void DrawPinMenu(ID2D1RenderTarget* target,const PinMenuModel& model,int hover);
void DrawPinMenuSurface(ID2D1RenderTarget* target,const PinMenuModel& model,int hover);
int TrackPinMenu(HWND owner,POINT anchor,const PinMenuModel& model);
}

