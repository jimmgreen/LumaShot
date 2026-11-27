#include "ui/magnifier.h"
#include "app/diagnostics.h"
#include "ui/color_picker.h"
#include "ui/text_renderer.h"
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <stdexcept>

namespace lumashot {
RECT MagnifierBounds(RECT monitor,POINT pointer,SIZE size) {
    constexpr LONG gap=10;
    const LONG x=pointer.x+gap+size.cx<=monitor.right?pointer.x+gap:pointer.x-gap-size.cx;
    const LONG y=pointer.y+gap+size.cy<=monitor.bottom?pointer.y+gap:pointer.y-gap-size.cy;
    const LONG left=std::clamp(x,monitor.left,std::max(monitor.left,monitor.right-size.cx));
    const LONG top=std::clamp(y,monitor.top,std::max(monitor.top,monitor.bottom-size.cy));
    return {left,top,left+size.cx,top+size.cy};
}
void MagnifierPixels(const Frame& frame,RECT monitor,POINT pointer,std::span<uint32_t> output) {
    if(output.size()!=kMagnifierSize*kMagnifierSize)throw std::invalid_argument("Invalid magnifier surface");
    RECT source{};
    if(!IntersectRect(&source,&monitor,&frame.bounds))throw std::invalid_argument("Magnifier outside screenshot");
    const LONG sx=pointer.x-10;
    const LONG sy=pointer.y-10;
    for(int y=0;y<kMagnifierSize;++y)for(int x=0;x<kMagnifierSize;++x) {
        uint32_t color=0xff1686ff;
        if(x>0&&y>0&&x<121&&y<121&&x!=61&&y!=61) {
            const LONG px=std::clamp(sx+(x-1)/6,source.left,source.right-1)-frame.bounds.left;
            const LONG py=std::clamp(sy+(y-1)/6,source.top,source.bottom-1)-frame.bounds.top;
            color=frame.pixels[static_cast<size_t>(py)*frame.Width()+px]|0xff000000;
        }
        output[static_cast<size_t>(y)*kMagnifierSize+x]=color;
    }
}
struct Magnifier::Readout {
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
    Microsoft::WRL::ComPtr<IDWriteFactory> fonts;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    TextRenderer renderer;
    std::optional<uint32_t> last_color;
    POINT lens_origin{-1,-1};
    Readout() {
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf())), "Create readout factory");
        const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        CheckWin32(SUCCEEDED(factory->CreateDCRenderTarget(&props,&target)), "Create readout target");
        CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(fonts.GetAddressOf()))), "Create readout fonts");
        CheckWin32(SUCCEEDED(fonts->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13,L"zh-CN",&format)), "Create readout format");
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    void Paint(DibSurface& surface,SIZE size,UINT dpi,uint32_t color) {
        if(last_color==color)return;
        const LONG text_top=lens_origin.y==0?kMagnifierSize:0;
        RECT footer{0,text_top,size.cx,text_top+size.cy-kMagnifierSize};
        CheckWin32(SUCCEEDED(target->BindDC(surface.Dc(),&footer)), "Bind readout");
        target->SetDpi(static_cast<float>(dpi),static_cast<float>(dpi));
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0x20242c));
        const auto line=[&](const std::wstring& value,const wchar_t* hint,float y){
            renderer.Draw(target.Get(),fonts.Get(),value,format.Get(),{9,y,134,y+22},D2D1::ColorF(0xffffff));
            renderer.Draw(target.Get(),fonts.Get(),hint,format.Get(),{138,y,218,y+22},D2D1::ColorF(0xbad9ff));
        };
        line(ColorHex(color),L"C 复制 HEX",2);
        line(ColorRgb(color),L"R 复制 RGB",24);
        CheckWin32(SUCCEEDED(target->EndDraw()), "Draw color readout");
        GdiFlush();
        last_color=color;
    }
};
Magnifier::Magnifier()=default;
Magnifier::~Magnifier(){Close();}
void Magnifier::Prepare(HWND owner,const Frame& frame,RECT monitor,POINT pointer,std::optional<uint32_t> color) {
    TraceScope trace("magnifier_prepare");
    // Only a head start: on failure Show() creates everything again when needed.
    try{Update(owner,frame,monitor,pointer,color);}catch(...){Close();}
}
void Magnifier::Show(HWND owner,const Frame& frame,RECT monitor,POINT pointer,std::optional<uint32_t> color) {
    TraceScope trace("magnifier_update");
    Update(owner,frame,monitor,pointer,color);
    if(!IsWindowVisible(window_))ShowWindow(window_,SW_SHOWNOACTIVATE);
}
void Magnifier::Update(HWND owner,const Frame& frame,RECT monitor,POINT pointer,std::optional<uint32_t> color) {
    const UINT dpi=std::max(GetDpiForWindow(owner),96u);
    SIZE size{color?MulDiv(220,dpi,96):kMagnifierSize,kMagnifierSize+(color?MulDiv(48,dpi,96):0)};
    if(!surface_||size.cx!=size_.cx||size.cy!=size_.cy){
        surface_=std::make_unique<DibSurface>(size.cx,size.cy);
        size_=size;
        std::fill_n(surface_->Pixels(),size.cx*size.cy,0u);
        if(readout_)readout_->last_color.reset();
    }
    if(!IsWindow(window_)) {
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
            L"STATIC",L"LumaShot.Magnifier",WS_POPUP,0,0,kMagnifierSize,kMagnifierSize,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
        CheckWin32(window_!=nullptr,"Create magnifier");
    }
    const RECT bounds=MagnifierBounds(monitor,pointer,size);
    if(color){
        if(!readout_)readout_=std::make_unique<Readout>();
        // Keep the visible lens at the corner facing the pointer. The wider
        // readout extends away from the sampled pixel, including at screen edges.
        const POINT lens_origin{bounds.right<=pointer.x?size.cx-kMagnifierSize:0,
            bounds.bottom<=pointer.y?size.cy-kMagnifierSize:0};
        if(lens_origin.x!=readout_->lens_origin.x||lens_origin.y!=readout_->lens_origin.y){
            std::fill_n(surface_->Pixels(),size.cx*size.cy,0u);
            readout_->last_color.reset();
            readout_->lens_origin=lens_origin;
        }
        readout_->Paint(*surface_,size,dpi,*color);
        std::array<uint32_t,kMagnifierSize*kMagnifierSize> lens;
        MagnifierPixels(frame,monitor,pointer,lens);
        for(int y=0;y<kMagnifierSize;++y)
            std::copy_n(lens.data()+y*kMagnifierSize,kMagnifierSize,surface_->Pixels()+(y+lens_origin.y)*size.cx+lens_origin.x);
    }else MagnifierPixels(frame,monitor,pointer,{surface_->Pixels(),kMagnifierSize*kMagnifierSize});
    POINT position{bounds.left,bounds.top},source{};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    CheckWin32(UpdateLayeredWindow(window_,nullptr,&position,&size,surface_->Dc(),&source,0,&blend,ULW_ALPHA)!=FALSE,"Update magnifier");
}
void Magnifier::Hide(){if(IsWindow(window_)&&IsWindowVisible(window_))ShowWindow(window_,SW_HIDE);}
void Magnifier::Close(){if(IsWindow(window_))DestroyWindow(window_);window_=nullptr;readout_.reset();surface_.reset();}
}
