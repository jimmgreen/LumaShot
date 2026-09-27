#include "pin/paper.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
namespace lumashot {
using Microsoft::WRL::ComPtr;
PaperLayout MakePaperLayout(int w,int h,float dpi,PinStyle style){
    const float scale=std::max(1.f,dpi)/96;
    style=NormalizePinStyle(static_cast<int>(style));
    const auto px=[&](float dip){return int(std::ceil(dip*scale));};
    if(style==PinStyle::None)return {w,h,0,0,0,0,style,scale};
    if(style==PinStyle::Simple){const int inset=px(1);return {w+2*inset,h+2*inset,inset,0,0,px(14),style,scale};}
    if(style==PinStyle::Rounded){const int inset=px(12);return {w+2*inset,h+2*inset,inset,px(10),0,px(24),style,scale};}
    if(style==PinStyle::Polaroid){const int inset=px(14);return {w+2*inset,h+2*inset+px(30),inset,px(2),0,px(24),style,scale};}
    const float extent=std::clamp(float(std::min(w,h))/scale*.18f,28.f,44.f);
    const int inset=int(std::ceil(extent*scale));
    const int width=w+2*inset,height=h+2*inset;
    const int fold=std::min({int(std::ceil(inset*1.82f)),width/2,height/2});
    return {width,height,inset,int(std::ceil(6*scale)),fold,int(std::ceil(std::max(48*scale,fold*.95f))),style,scale};
}
HRGN PaperRegion(const PaperLayout& p){
    if(!p.fold){HRGN r=p.radius?CreateRoundRectRgn(0,0,p.width+1,p.height+1,p.radius*2,p.radius*2):CreateRectRgn(0,0,p.width,p.height);CheckWin32(r!=nullptr,"Create pin region");return r;}
    HRGN region=CreateRoundRectRgn(0,0,p.width+1,p.height+1,p.radius*2,p.radius*2);
    HRGN square=CreateRectRgn(p.width-p.fold,p.height-p.fold,p.width+1,p.height+1);
    HRGN ellipse=CreateEllipticRgn(p.width-2*p.fold,p.height-2*p.fold,p.width+1,p.height+1);
    if(!region||!square||!ellipse){if(region)DeleteObject(region);if(square)DeleteObject(square);if(ellipse)DeleteObject(ellipse);CheckWin32(false,"Create paper region");}
    CombineRgn(square,square,ellipse,RGN_DIFF);CombineRgn(region,region,square,RGN_DIFF);DeleteObject(square);DeleteObject(ellipse);return region;
}
void DrawPaper(ID2D1RenderTarget* target,const PaperLayout& p){
    if(p.style==PinStyle::None)return;
    const float w=float(p.width),h=float(p.height),r=float(p.radius),f=float(p.fold),edge=.5f;
    if(p.style!=PinStyle::Curl){
        ComPtr<ID2D1SolidColorBrush> brush;
        CheckWin32(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(p.style==PinStyle::Simple?0xaab3bf:p.style==PinStyle::Polaroid?0xfffcf4:0xfcfcfb),&brush)),"Create pin frame brush");
        const auto rect=D2D1::RectF(0,0,w,h);
        if(p.radius)target->FillRoundedRectangle(D2D1::RoundedRect(rect,r,r),brush.Get());else target->FillRectangle(rect,brush.Get());
        if(p.style!=PinStyle::Simple){brush->SetColor(D2D1::ColorF(0xc8cbd0,.7f));target->DrawRoundedRectangle(D2D1::RoundedRect({edge,edge,w-edge,h-edge},r,r),brush.Get(),1);}
        return;
    }
    ComPtr<ID2D1Factory> factory;target->GetFactory(&factory);ComPtr<ID2D1PathGeometry> paper;factory->CreatePathGeometry(&paper);ComPtr<ID2D1GeometrySink> sink;paper->Open(&sink);
    const float k=.55228475f*r;
    sink->BeginFigure({r,edge},D2D1_FIGURE_BEGIN_FILLED);sink->AddLine({w-r,edge});sink->AddBezier(D2D1::BezierSegment({w-r+k,edge},{w-edge,r-k},{w-edge,r}));
    sink->AddLine({w-edge,h-f});sink->AddBezier(D2D1::BezierSegment({w-edge,h-f+f*.5523f},{w-f+f*.5523f,h-edge},{w-f,h-edge}));sink->AddLine({r,h-edge});sink->AddBezier(D2D1::BezierSegment({r-k,h-edge},{edge,h-r+k},{edge,h-r}));sink->AddLine({edge,r});sink->AddBezier(D2D1::BezierSegment({edge,r-k},{r-k,edge},{r,edge}));sink->EndFigure(D2D1_FIGURE_END_CLOSED);sink->Close();
    ComPtr<ID2D1SolidColorBrush> brush;target->CreateSolidColorBrush(D2D1::ColorF(0xfcfcfb),&brush);target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),paper.Get()),nullptr);target->FillRectangle(D2D1::RectF(0,0,w,h),brush.Get());
    ComPtr<ID2D1PathGeometry> curl;factory->CreatePathGeometry(&curl);sink.Reset();curl->Open(&sink);
    // A continuous S-shaped lip joins the flat bottom and right edges. The
    // lens between this curve and the outer arc is the exposed paper back.
    const auto rise=D2D1::BezierSegment({w-f*.55f,h-edge},{w-f*.72f,h-f*.42f},{w-f*.48f,h-f*.54f});
    const auto lip=D2D1::BezierSegment({w-f*.24f,h-f*.66f},{w-edge,h-f*.42f},{w-edge,h-f});
    sink->BeginFigure({w-f,h-edge},D2D1_FIGURE_BEGIN_FILLED);sink->AddBezier(rise);sink->AddBezier(lip);
    // Extend past the exterior and apply its alpha coverage only once, via
    // the paper layer. Overlapping antialiased edges create a pale fringe.
    sink->AddLine({w+1,h-f});sink->AddLine({w+1,h+1});sink->AddLine({w-f,h+1});sink->EndFigure(D2D1_FIGURE_END_CLOSED);sink->Close();
    ComPtr<ID2D1PathGeometry> crease;factory->CreatePathGeometry(&crease);sink.Reset();crease->Open(&sink);sink->BeginFigure({w-f,h-edge},D2D1_FIGURE_BEGIN_HOLLOW);sink->AddBezier(rise);sink->AddBezier(lip);sink->EndFigure(D2D1_FIGURE_END_OPEN);sink->Close();
    const float scale=float(p.radius)/6;
    D2D1_MATRIX_3X2_F transform{};target->GetTransform(&transform);
    target->SetTransform(D2D1::Matrix3x2F::Translation(.7f*scale,1.5f*scale)*transform);
    for(int i=5;i>0;--i){brush->SetColor(D2D1::ColorF(0x302d29,.025f));target->DrawGeometry(crease.Get(),brush.Get(),float(i)*1.1f*scale);}
    target->SetTransform(transform);
    const D2D1_GRADIENT_STOP stops[]={{0,D2D1::ColorF(0xffffff)},{.12f,D2D1::ColorF(0xf5f3ef)},{.45f,D2D1::ColorF(0xd4cfc7)},{.78f,D2D1::ColorF(0xaaa298)},{1,D2D1::ColorF(0x82796e)}};
    ComPtr<ID2D1GradientStopCollection> collection;target->CreateGradientStopCollection(stops,5,&collection);ComPtr<ID2D1LinearGradientBrush> gradient;target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({w-f*.62f,h-f*.58f},{w-f*.22f,h-f*.26f}),collection.Get(),&gradient);
    target->FillGeometry(curl.Get(),gradient.Get());brush->SetColor(D2D1::ColorF(0xffffff,.9f));target->DrawGeometry(crease.Get(),brush.Get(),.7f*scale);
    target->PopLayer();
}
static float Distance(const PaperLayout& p,float x,float y){
    const float r=float(p.radius),qx=std::abs(x-float(p.width)/2)-(float(p.width)/2-r),qy=std::abs(y-float(p.height)/2)-(float(p.height)/2-r);
    const float rounded=std::hypot(std::max(qx,0.f),std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-r;
    if(x>p.width-p.fold&&y>p.height-p.fold)return std::max(rounded,std::hypot(x-float(p.width-p.fold),y-float(p.height-p.fold))-p.fold);
    return rounded;
}
Frame PaperShadowImage(const PaperLayout& p,bool beneath_paper){
    if(!p.shadow)return {};
    Frame image=MakeFrame({0,0,p.width+2*p.shadow,p.height+2*p.shadow},0);
    const float scale=p.scale,sigma=(p.style==PinStyle::Simple?3.f:8.f)*scale,shift=(p.style==PinStyle::Simple?2.f:5.f)*scale;
    for(int y=0;y<image.Height();++y)for(int x=0;x<image.Width();++x){
        const float px=float(x-p.shadow)+.5f,py=float(y-p.shadow)+.5f;
        // The owned shadow sits above the pin, so its entire paper interior is transparent.
        if(px>=p.radius+4*sigma&&px<p.width-p.fold-4*sigma&&py>=p.radius+4*sigma&&py<p.height-p.radius-4*sigma)continue;
        if(!beneath_paper&&Distance(p,px,py)<=0)continue;
        const float d=std::max(0.f,Distance(p,px,py-shift));
        float curl=0;
        if(p.fold){const float dx=(px-(p.width-p.fold*.28f+shift*.5f))/(p.fold*.32f),dy=(py-(p.height-p.fold*.22f+shift))/(p.fold*.30f);curl=48*std::exp(-(dx*dx+dy*dy)*.5f);}
        const float contact_sigma=1.8f*scale;
        const float contact=24*std::exp(-d*d/(2*contact_sigma*contact_sigma));
        const auto alpha=static_cast<uint32_t>(std::lround(std::min(100.f,contact+30*std::exp(-d*d/(2*sigma*sigma))+curl)));
        const float boundary=float(std::min({x,y,image.Width()-1-x,image.Height()-1-y}));
        const float fade=std::clamp(boundary/(8*scale),0.f,1.f);
        image.pixels[static_cast<size_t>(y)*image.Width()+x]=uint32_t(std::lround(alpha*fade*fade*(3-2*fade)))<<24;
    }return image;
}
// Keep corners (including the curl Gaussian) at native resolution. Only the
// uniform straight-edge strips stretch; the source image is never resampled.
PaperLayout PaperShadowPatchLayout(const PaperLayout& paper){
    auto patch=paper;
    const float sigma=(paper.style==PinStyle::Simple?3.f:8.f)*paper.scale;
    const float shift=(paper.style==PinStyle::Simple?2.f:5.f)*paper.scale;
    // Enclose the rounded-distance support and >4.5 sigma of the curl lobe
    // independently, rather than summing both extents and rebuilding at 720p.
    const int corner=std::max(paper.radius+paper.fold+int(std::ceil(4*sigma+shift)),int(std::ceil(1.75f*paper.fold)));
    patch.width=std::min(paper.width,2*corner+1);
    patch.height=std::min(paper.height,2*corner+1);
    return patch;
}
void DrawPaperShadowPatch(ID2D1RenderTarget* target,ID2D1Bitmap* bitmap,const PaperLayout& paper){
    const auto size=bitmap->GetPixelSize();const float w=float(size.width),h=float(size.height);
    const float left=std::floor((w-1)/2),top=std::floor((h-1)/2);
    const float sx[]={0,left,left+1,w},sy[]={0,top,top+1,h};
    const float dw=float(paper.width+2*paper.shadow),dh=float(paper.height+2*paper.shadow);
    if(w==dw&&h==dh){target->DrawBitmap(bitmap);return;}
    const auto antialias=target->GetAntialiasMode();target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    const float dx[]={0,left,dw-(w-left-1),dw},dy[]={0,top,dh-(h-top-1),dh};
    for(int y=0;y<3;++y)for(int x=0;x<3;++x){
        if(x==1&&y==1&&dw>w&&dh>h)continue; // Fully transparent interior.
        const auto source=D2D1::RectF(sx[x],sy[y],sx[x+1],sy[y+1]);
        const auto destination=D2D1::RectF(dx[x],dy[y],dx[x+1],dy[y+1]);
        target->DrawBitmap(bitmap,destination,1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,source);
    }
    target->SetAntialiasMode(antialias);
}
PaperShadow::~PaperShadow(){Close();}
void PaperShadow::Close(){if(window_&&IsWindow(window_))DestroyWindow(window_);window_=nullptr;}
void PaperShadow::Update(HWND owner,const PaperLayout& p){
    if(moving_)return;
    RECT rect{};GetWindowRect(owner,&rect);POINT position{rect.left-p.shadow,rect.top-p.shadow};
    if(!window_)window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"STATIC",L"",WS_POPUP,position.x,position.y,1,1,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    CheckWin32(window_!=nullptr,"Create paper shadow");
    if(width_!=p.width||height_!=p.height||inset_!=p.inset){
        const Frame shadow=PaperShadowImage(p);DibSurface surface(shadow.Width(),shadow.Height());std::copy(shadow.pixels.begin(),shadow.pixels.end(),surface.Pixels());
        SIZE size{shadow.Width(),shadow.Height()};POINT source{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        CheckWin32(UpdateLayeredWindow(window_,nullptr,&position,&size,surface.Dc(),&source,0,&blend,ULW_ALPHA)!=FALSE,"Upload paper shadow");
        width_=p.width;height_=p.height;inset_=p.inset;++builds_;
    }else SetWindowPos(window_,nullptr,position.x,position.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(window_,SW_SHOWNOACTIVATE);
}
void PaperShadow::MoveOwner(HWND owner,POINT origin,const PaperLayout& p){
    if(!window_)Update(owner,p);
    struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}};
    // Submit both window positions together; WM_MOVE must not perform a second,
    // independent shadow move while this batch is being committed.
    {Guard guard(moving_);HDWP batch=BeginDeferWindowPos(2);CheckWin32(batch!=nullptr,"Begin paper move");
    constexpr UINT flags=SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE;
    batch=DeferWindowPos(batch,owner,nullptr,origin.x,origin.y,0,0,flags);CheckWin32(batch!=nullptr,"Move paper");
    batch=DeferWindowPos(batch,window_,nullptr,origin.x-p.shadow,origin.y-p.shadow,0,0,flags);CheckWin32(batch!=nullptr,"Move paper shadow");
    CheckWin32(EndDeferWindowPos(batch)!=FALSE,"Commit paper move");}
    // WM_DPICHANGED can replace the layout synchronously during the batch.
    if(width_!=p.width||height_!=p.height||inset_!=p.inset)Update(owner,p);
}
}
