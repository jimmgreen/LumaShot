#include "clipboard/strip_dismiss.h"
#include "clipboard/liquid_motion.h"
#include "capture/frame.h"
#include "ui/text_renderer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace lumashot::clipboard {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT_PTR TimerId=1;
constexpr float FadeInSeconds=.14f,FadeOutSeconds=.16f,DismissSeconds=.34f;
constexpr UINT32 ArmedFill=0xe5484d;
bool MotionEnabled(){BOOL effects=TRUE;return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&effects,0)&&effects;}
UINT32 Mix(UINT32 a,UINT32 b,float t){
    t=std::clamp(t,0.f,1.f);UINT32 out=0;
    for(int shift:{0,8,16}){const float x=float((a>>shift)&255),y=float((b>>shift)&255);out|=UINT32(std::lround(x+(y-x)*t))<<shift;}
    return out;
}
}
struct DismissTarget::Impl {
    HWND owner{},window{},above{};DismissZone zone{};
    bool dark{},armed{},dismissing{},timer{};
    float alpha{},alpha_goal{},mix{},radius_goal{DismissZone::RestRadius};LiquidSpring radius{DismissZone::RestRadius,0};
    ULONGLONG last{},dismiss_start{};
    std::vector<uint32_t> ghost;int ghost_w{},ghost_h{};RECT ghost_rect{};
    std::unique_ptr<DibSurface> surface;int surface_w{},surface_h{};
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;ComPtr<IDWriteFactory> dw;ComPtr<IDWriteTextFormat> format;
    ComPtr<ID2D1SolidColorBrush> brush;ComPtr<ID2D1StrokeStyle> round;ComPtr<ID2D1Bitmap> ghost_bitmap;TextRenderer text;

    static LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
        if(m==WM_NCCREATE)SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCHITTEST)return HTTRANSPARENT;
        if(m==WM_TIMER&&wp==TimerId&&self){self->Tick(GetTickCount64());return 0;}
        if(m==WM_NCDESTROY&&self){SetWindowLongPtrW(w,GWLP_USERDATA,0);self->window=nullptr;self->timer=false;}
        return DefWindowProcW(w,m,wp,lp);
    }
    bool Create(){
        if(window)return true;
        WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=Proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumaShotClipboardDismissTarget";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        const RECT r=zone.Window();
        window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"LumaShot 隐藏剪贴板侧边条",WS_POPUP,
            r.left,r.top,r.right-r.left,r.bottom-r.top,owner,nullptr,wc.hInstance,this);
        return window!=nullptr;
    }
    void StartTimer(){if(window&&!timer){timer=SetTimer(window,TimerId,15,nullptr)!=0;last=GetTickCount64();}}
    void StopTimer(){if(window&&timer)KillTimer(window,TimerId);timer=false;}
    // Frees the HWND, surface and renderer; the next drag recreates them.
    void Release(){
        StopTimer();dismissing=armed=false;alpha=alpha_goal=mix=0;radius={DismissZone::RestRadius,0};radius_goal=DismissZone::RestRadius;
        std::vector<uint32_t>().swap(ghost);ghost_w=ghost_h=0;ghost_bitmap.Reset();
        brush.Reset();round.Reset();format.Reset();target.Reset();dw.Reset();factory.Reset();surface.reset();surface_w=surface_h=0;
        if(window){const HWND old=window;window=nullptr;DestroyWindow(old);}
    }
    bool Settled()const{
        return std::abs(alpha-alpha_goal)<.002f&&std::abs(radius.value-radius_goal)<.05f&&std::abs(radius.velocity)<.5f&&std::abs(mix-(armed?1.f:0.f))<.01f;
    }
    void Tick(ULONGLONG now){
        const float dt=std::clamp(float(now-last)/1000.f,0.f,.05f);last=now;
        if(dismissing){if(float(now-dismiss_start)/1000.f>=DismissSeconds){Release();return;}}
        else{
            const float rate=dt/(alpha_goal>alpha?FadeInSeconds:FadeOutSeconds);
            alpha=alpha_goal>alpha?std::min(alpha_goal,alpha+rate):std::max(alpha_goal,alpha-rate);
            radius=radius.At(radius_goal,dt,26.f,.58f);
            mix+=((armed?1.f:0.f)-mix)*(1-std::exp(-dt/.055f));
        }
        if(!Paint()){Release();return;}
        if(!dismissing&&alpha<=0&&alpha_goal<=0){Release();return;}
        if(!dismissing&&Settled()){alpha=alpha_goal;radius={radius_goal,0};mix=armed?1.f:0.f;Paint();StopTimer();}
    }
    // Without animation effects every change lands on its final frame at once.
    void Snap(){alpha=alpha_goal;radius={radius_goal,0};mix=armed?1.f:0.f;}
    void Update(){
        if(!MotionEnabled()){StopTimer();Snap();if(alpha<=0){Release();return;}if(!Paint())Release();return;}
        if(!Paint()){Release();return;}
        StartTimer();
    }
    bool EnsureRenderer(int w,int h){
        if(!surface||surface_w!=w||surface_h!=h){surface=std::make_unique<DibSurface>(w,h);surface_w=w;surface_h=h;}
        if(!factory&&FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf())))return false;
        if(!dw&&FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dw.GetAddressOf()))))return false;
        if(!target){const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            if(FAILED(factory->CreateDCRenderTarget(&props,&target)))return false;brush.Reset();ghost_bitmap.Reset();}
        if(!round){const auto props=D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND);if(FAILED(factory->CreateStrokeStyle(props,nullptr,0,&round)))return false;}
        if(!format){
            if(FAILED(dw->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,12,L"zh-CN",&format)))return false;
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        return true;
    }
    bool Paint(){
        if(!window&&!Create())return false;
        const RECT r=zone.Window();const int w=r.right-r.left,h=r.bottom-r.top;if(w<=0||h<=0)return false;
        const float s=zone.Scale();
        try{
            if(!EnsureRenderer(w,h))return false;
            const RECT bounds{0,0,w,h};if(FAILED(target->BindDC(surface->Dc(),&bounds)))return false;
            if(!brush&&FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0),&brush)))return false;
            const float t=dismissing?std::clamp(float(GetTickCount64()-dismiss_start)/1000.f/DismissSeconds,0.f,1.f):0.f;
            if(dismissing&&!ghost_bitmap&&!ghost.empty())
                target->CreateBitmap(D2D1::SizeU(UINT32(ghost_w),UINT32(ghost_h)),ghost.data(),UINT32(ghost_w)*4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&ghost_bitmap);
            target->SetDpi(96*s,96*s);target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));
            const auto c=zone.Center();const D2D1_POINT_2F center{float(c.x-r.left)/s,float(c.y-r.top)/s};
            const auto color=[&](UINT32 rgb,float a){brush->SetColor(D2D1::ColorF(rgb,std::clamp(a,0.f,1.f)));return brush.Get();};
            // The absorbed strip gulps the ring outwards once, then everything fades.
            const float gulp=dismissing?std::sin(3.14159265f*std::min(1.f,t/.55f))*.14f:0.f;
            const float fade=dismissing?1.f-LiquidSmooth((t-.45f)/.55f):1.f;
            const float rad=std::max(4.f,radius.value)*(1+gulp),m=dismissing?1.f:mix;
            for(int i=4;i>0;--i)target->FillEllipse(D2D1::Ellipse({center.x,center.y+1.5f},rad+float(i)*1.6f,rad+float(i)*1.6f),color(0x06101f,(dark?.07f:.035f)*fade));
            const UINT32 rest_fill=dark?0x1d2735:0xffffff,rest_ink=dark?0xe3ebf7:0x45556b,rest_ring=dark?0x3b4b61:0xd3dce8;
            target->FillEllipse(D2D1::Ellipse(center,rad,rad),color(Mix(rest_fill,ArmedFill,m),(.9f+.1f*m)*fade));
            if(m<1)target->DrawEllipse(D2D1::Ellipse(center,rad-.5f,rad-.5f),color(rest_ring,(1-m)*fade),1);
            const float arm=rad*.3f;const auto ink=color(Mix(rest_ink,0xffffff,m),fade);
            target->DrawLine({center.x-arm,center.y-arm},{center.x+arm,center.y+arm},ink,2.f,round.Get());
            target->DrawLine({center.x-arm,center.y+arm},{center.x+arm,center.y-arm},ink,2.f,round.Get());
            if(dismissing&&ghost_bitmap){
                const float g=LiquidSmooth(t/.7f),k=1-.86f*g;
                const D2D1_POINT_2F from{(float(ghost_rect.left+ghost_rect.right)/2-float(r.left))/s,(float(ghost_rect.top+ghost_rect.bottom)/2-float(r.top))/s};
                const D2D1_POINT_2F at{from.x+(center.x-from.x)*g,from.y+(center.y-from.y)*g};
                const float hw=float(ghost_w)/s/2*k,hh=float(ghost_h)/s/2*k;
                target->DrawBitmap(ghost_bitmap.Get(),{at.x-hw,at.y-hh,at.x+hw,at.y+hh},1-g,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            const float label_alpha=dismissing?std::max(0.f,1-t*3):1.f;
            if(label_alpha>0){
                const std::wstring label=armed||dismissing?L"松开即可隐藏":L"拖到这里隐藏侧边条";
                const float top=center.y+DismissZone::ArmedRadius+12,half=armed||dismissing?50.f:70.f;
                const D2D1_RECT_F pill{center.x-half,top,center.x+half,top+26};
                target->FillRoundedRectangle(D2D1::RoundedRect(pill,13,13),color(rest_fill,.9f*label_alpha));
                target->DrawRoundedRectangle(D2D1::RoundedRect({pill.left+.5f,pill.top+.5f,pill.right-.5f,pill.bottom-.5f},12.5f,12.5f),color(rest_ring,label_alpha),1);
                text.Draw(target.Get(),dw.Get(),label,format.Get(),pill,D2D1::ColorF(armed||dismissing?(dark?0xff8a8d:0xd23a40):rest_ink,label_alpha));
            }
            const HRESULT hr=target->EndDraw();
            if(hr==D2DERR_RECREATE_TARGET){target.Reset();brush.Reset();ghost_bitmap.Reset();return true;}
            if(FAILED(hr))return false;
        }catch(...){return false;}
        POINT position{r.left,r.top},source{};SIZE size{w,h};
        BLENDFUNCTION blend{AC_SRC_OVER,0,BYTE(std::lround(std::clamp(dismissing?1.f:alpha,0.f,1.f)*255)),AC_SRC_ALPHA};
        if(!UpdateLayeredWindow(window,nullptr,&position,&size,surface->Dc(),&source,0,&blend,ULW_ALPHA))return false;
        const HWND after=above&&IsWindow(above)?above:HWND_TOPMOST;
        SetWindowPos(window,after,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|(IsWindowVisible(window)?0:SWP_SHOWWINDOW));
        return true;
    }
};
DismissTarget::DismissTarget(HWND owner):impl_(std::make_unique<Impl>()){impl_->owner=owner;}
DismissTarget::~DismissTarget(){impl_->Release();}
void DismissTarget::Show(const DismissZone& zone,bool dark,HWND above){
    auto& p=*impl_;if(p.dismissing)p.Release();
    const bool changed=!(p.zone==zone)||p.dark!=dark||p.alpha_goal<1;
    p.zone=zone;p.dark=dark;p.above=above;p.alpha_goal=1;
    if(changed)p.Update();
}
void DismissTarget::SetState(bool armed,float proximity){
    auto& p=*impl_;if(p.dismissing||p.alpha_goal<=0)return;
    const float goal=armed?DismissZone::ArmedRadius:DismissZone::RestRadius+4*std::clamp(proximity,0.f,1.f);
    if(armed==p.armed&&std::abs(goal-p.radius_goal)<.05f)return;
    p.armed=armed;p.radius_goal=goal;p.Update();
}
void DismissTarget::Hide(){
    auto& p=*impl_;if(p.dismissing||!p.window)return;
    p.alpha_goal=0;p.armed=false;p.radius_goal=DismissZone::RestRadius;p.Update();
}
void DismissTarget::Dismiss(const uint32_t* pixels,int width,int height,RECT screen){
    auto& p=*impl_;
    if(!p.window||!MotionEnabled()){p.Release();return;}
    p.ghost.clear();p.ghost_w=p.ghost_h=0;p.ghost_bitmap.Reset();
    if(pixels&&width>0&&height>0&&screen.right-screen.left==width&&screen.bottom-screen.top==height){
        p.ghost.assign(pixels,pixels+size_t(width)*size_t(height));p.ghost_w=width;p.ghost_h=height;p.ghost_rect=screen;
    }
    p.armed=true;p.mix=1;p.alpha=p.alpha_goal=1;p.dismissing=true;p.dismiss_start=GetTickCount64();
    if(!p.Paint()){p.Release();return;}
    p.StartTimer();if(!p.timer)p.Release();
}
void DismissTarget::Reset(){impl_->Release();}
HWND DismissTarget::Window()const{return impl_->window;}
bool DismissTarget::Visible()const{return impl_->window&&IsWindowVisible(impl_->window);}
bool DismissTarget::Animating()const{return impl_->timer;}
}
