#pragma once
#include "model/document.h"
#include "model/pin_style.h"
#include <d2d1.h>
namespace lumashot {
struct PaperLayout {
    int width{},height{},inset{},radius{},fold{},shadow{};
    PinStyle style{PinStyle::Curl};float scale{1};
    bool operator==(const PaperLayout&)const=default;
    Point ToImage(Point p)const{return {p.x-inset,p.y-inset};}
    Point ToWindow(Point p)const{return {p.x+inset,p.y+inset};}
};
PaperLayout MakePaperLayout(int image_width,int image_height,float dpi,PinStyle style=PinStyle::Curl);
HRGN PaperRegion(const PaperLayout&);
void DrawPaper(ID2D1RenderTarget*,const PaperLayout&);
Frame PaperShadowImage(const PaperLayout&,bool beneath_paper=false);
PaperLayout PaperShadowPatchLayout(const PaperLayout&);
void DrawPaperShadowPatch(ID2D1RenderTarget*,ID2D1Bitmap*,const PaperLayout&);
class PaperShadow {
public:
    ~PaperShadow();
    void Update(HWND owner,const PaperLayout&);
    void MoveOwner(HWND owner,POINT origin,const PaperLayout&);
    void Close();
    HWND Window()const{return window_;}
    unsigned Builds()const{return builds_;}
private:
    HWND window_{};int width_{},height_{},inset_{};unsigned builds_{};bool moving_{};
};
}
