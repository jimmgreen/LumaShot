#pragma once
#include "capture/frame.h"
#include "ui/glass_surface.h"
#include "ui/controls.h"
#include "ui/text_renderer.h"
#include <dwrite.h>
#include <windowsx.h>
#include <array>
#include <wrl/client.h>
#include <stdexcept>
#include <cstdio>

namespace lumashot::ui {
namespace message_detail {
using Microsoft::WRL::ComPtr;
inline void Check(HRESULT result,const char* text){if(FAILED(result))throw std::runtime_error(text);}
struct Dialog {
    HWND window{};
    bool dark{},acrylic{},confirm{},done{},accepted{};
    float scale{1};
    int focus{},hover{-1},pressed{-1};
    std::wstring title,body,action,cancel;
    std::array<Control,2> Buttons()const {
        std::array<Control,2> buttons{};
        buttons[0].id=0;buttons[0].kind=Kind::Button;
        buttons[0].bounds={confirm?208.f:336.f,156,confirm?328.f:456.f,192};
        buttons[0].text=confirm?cancel:L"知道了";buttons[0].primary=!confirm;
        buttons[1].id=1;buttons[1].kind=Kind::Button;buttons[1].bounds={336,156,456,192};
        buttons[1].text=action;buttons[1].primary=true;buttons[1].enabled=confirm;
        return buttons;
    }
    int Hit(LPARAM lp)const {const auto b=Buttons();return HitTest(std::span(b.data(),confirm?2:1),{GET_X_LPARAM(lp)/scale,GET_Y_LPARAM(lp)/scale});}
    void Choose(int id){if(id<0||id>(confirm?1:0))return;accepted=confirm&&id==1;done=true;}
    void Paint(){
        PAINTSTRUCT ps{};BeginPaint(window,&ps);
        try {
            RECT r{};GetClientRect(window,&r);
            ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;ComPtr<IDWriteFactory> fonts;ComPtr<ID2D1SolidColorBrush> brush;
            Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Message factory");
            Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(fonts.GetAddressOf())),"Message fonts");
            const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            Check(factory->CreateDCRenderTarget(&props,&target),"Message target");
            DibSurface surface(r.right,r.bottom);Check(target->BindDC(surface.Dc(),&r),"Message surface");
            Check(target->CreateSolidColorBrush(D2D1::ColorF(0),&brush),"Message brush");
            target->SetDpi(96*scale,96*scale);target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));
            DrawGlassSurface(target.Get(),brush.Get(),{0,0,480,216},dark,acrylic);
            TextRenderer renderer;PaintContext p;p.target=target.Get();
            p.brush=[&](uint32_t c){brush->SetColor(D2D1::ColorF(c&0xffffff,float(c>>24)/255));return brush.Get();};
            p.text=[&](const std::wstring& value,Box b,float size,uint32_t color){
                ComPtr<IDWriteTextFormat> format;
                Check(fonts->CreateTextFormat(ToolFont,nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&format),"Message text");
                format->SetParagraphAlignment(b.top>=150?DWRITE_PARAGRAPH_ALIGNMENT_NEAR:DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                renderer.Draw(target.Get(),fonts.Get(),value,format.Get(),{b.left,b.top,b.right,b.bottom},D2D1::ColorF(color&0xffffff));
            };
            p.measure_text=[&](const std::wstring& value,float size){
                ComPtr<IDWriteTextFormat> format;ComPtr<IDWriteTextLayout> layout;
                Check(fonts->CreateTextFormat(ToolFont,nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&format),"Message measure format");
                Check(fonts->CreateTextLayout(value.data(),static_cast<UINT32>(value.size()),format.Get(),1000,1000,&layout),"Message measure layout");
                DWRITE_TEXT_METRICS metrics{};Check(layout->GetMetrics(&metrics),"Message measure");return Point{metrics.widthIncludingTrailingWhitespace,metrics.height};
            };
            const Theme theme{1,dark};
            p.text(title,{24,16,456,50},17,theme.Ink());
            p.text(body,{24,58,456,140},13,theme.Ink());
            const auto buttons=Buttons();DrawControls(p,theme,std::span(buttons.data(),confirm?2:1),hover,pressed);
            target->DrawRoundedRectangle(D2D1::RoundedRect({buttons[focus].bounds.left-3,153,buttons[focus].bounds.right+3,195},9,9),p.brush(theme.Accent()),1);
            Check(target->EndDraw(),"Message paint");POINT origin{};SIZE size{r.right,r.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            if(!UpdateLayeredWindow(window,nullptr,nullptr,&size,surface.Dc(),&origin,0,&blend,ULW_ALPHA)){std::fprintf(stderr,"Message composition failed: %lu\n",GetLastError());done=true;}
        }catch(const std::exception& error){std::fprintf(stderr,"Message rendering failed: %s\n",error.what());done=true;}
        EndPaint(window,&ps);
    }
    static LRESULT CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
        auto* d=reinterpret_cast<Dialog*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){d=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);d->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));}
        if(!d)return DefWindowProcW(w,msg,wp,lp);
        switch(msg){
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:d->Paint();return 0;
        case WM_CLOSE:case WM_DESTROY:d->done=true;return 0;
        case WM_MOUSEMOVE:d->hover=d->Hit(lp);InvalidateRect(w,nullptr,FALSE);return 0;
        case WM_LBUTTONDOWN:d->pressed=d->Hit(lp);if(d->pressed>=0)d->focus=d->pressed;SetCapture(w);InvalidateRect(w,nullptr,FALSE);return 0;
        case WM_LBUTTONUP:{const int hit=d->Hit(lp),pressed=d->pressed;d->pressed=-1;ReleaseCapture();if(hit==pressed)d->Choose(hit);InvalidateRect(w,nullptr,FALSE);return 0;}
        case WM_CAPTURECHANGED:d->pressed=-1;return 0;
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE){d->done=true;return 0;}
            if(wp==VK_TAB||wp==VK_LEFT||wp==VK_RIGHT){if(d->confirm)d->focus=1-d->focus;InvalidateRect(w,nullptr,FALSE);return 0;}
            if(wp==VK_RETURN||wp==VK_SPACE){d->Choose(d->focus);return 0;}break;
        case WM_DPICHANGED:{d->scale=HIWORD(wp)/96.f;const auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,int(480*d->scale),int(216*d->scale),SWP_NOZORDER|SWP_NOACTIVATE);ResizeBorderlessWindow(w,d->scale,ToolPanelRadius);InvalidateRect(w,nullptr,FALSE);return 0;}
        }
        return DefWindowProcW(w,msg,wp,lp);
    }
};
}
// False is always the safe/default result, including creation/render failure and WM_QUIT.
inline bool ShowThemedMessage(HWND owner,bool dark,const std::wstring& title,const std::wstring& body,
                              const std::wstring& action={},const std::wstring& cancel=L"继续录制"){
    message_detail::Dialog d;d.dark=dark;d.title=title;d.body=body;d.action=action;d.cancel=cancel;d.confirm=!action.empty();
    d.scale=owner?GetDpiForWindow(owner)/96.f:GetDpiForSystem()/96.f;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=message_detail::Dialog::Proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"LumaShot.ThemedMessage";RegisterClassW(&wc);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTOPRIMARY),&monitor);
    RECT anchor=monitor.rcWork;if(owner)GetWindowRect(owner,&anchor);
    const int width=int(480*d.scale),height=int(216*d.scale);
    const int x=std::max(int(monitor.rcWork.left),std::min(int((anchor.left+anchor.right-width)/2),int(monitor.rcWork.right)-width));
    const int y=std::max(int(monitor.rcWork.top),std::min(int((anchor.top+anchor.bottom-height)/2),int(monitor.rcWork.bottom)-height));
    HWND w=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED,wc.lpszClassName,title.c_str(),WS_POPUP,x,y,width,height,owner,nullptr,wc.hInstance,&d);
    if(!w)return false;
    ConfigureBorderlessWindow(w);ResizeBorderlessWindow(w,d.scale,ToolPanelRadius);d.acrylic=SetSettingsAcrylic(w,dark);SetWindowDisplayAffinity(w,WDA_EXCLUDEFROMCAPTURE);
    const HWND previous=GetFocus();const bool enabled=owner&&IsWindowEnabled(owner);if(enabled)EnableWindow(owner,FALSE);
    ShowWindow(w,SW_SHOW);SetForegroundWindow(w);SetFocus(w);
    MSG msg{};while(!d.done){const BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(!result)PostQuitMessage(int(msg.wParam));break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(GetCapture()==w)ReleaseCapture();if(enabled&&IsWindow(owner))EnableWindow(owner,TRUE);
    if(IsWindow(w))DestroyWindow(w);if(IsWindow(owner)){SetForegroundWindow(owner);SetFocus(IsWindow(previous)?previous:owner);}
    return d.accepted;
}
}
