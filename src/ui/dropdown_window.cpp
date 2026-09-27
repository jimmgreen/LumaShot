#include "ui/dropdown_window.h"
#include "ui/glass_surface.h"
#include "ui/text_renderer.h"
#include "capture/frame.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <cmath>
namespace lumashot::ui {
namespace {
using Microsoft::WRL::ComPtr;
struct Popup {
    TextRenderer text_renderer;HWND window{};Dropdown menu;bool dark{},acrylic{},done{};std::optional<int> choice;
    void Choose(int index){if(index>=0&&index<int(menu.items.size()))choice=menu.items[index].second;done=true;}
    void Paint(){
        PAINTSTRUCT ps{};BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);
        // No timer and no persistent render device: repaint only for input.
        ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;ComPtr<IDWriteFactory> fonts;ComPtr<ID2D1SolidColorBrush> brush;
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        if(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()))&&SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(fonts.GetAddressOf())))&&SUCCEEDED(factory->CreateDCRenderTarget(&properties,&target))){
            DibSurface surface(r.right,r.bottom);target->BindDC(surface.Dc(),&r);target->CreateSolidColorBrush(D2D1::ColorF(0),&brush);target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));
            PaintContext p;p.target=target.Get();p.brush=[&](uint32_t c){brush->SetColor(D2D1::ColorF(c&0xffffff,float(c>>24)/255));return brush.Get();};const Theme theme{menu.scale,dark};
            DrawGlassSurface(target.Get(),brush.Get(),{0,0,float(r.right),float(r.bottom)},dark,acrylic,8,menu.scale);
            for(int i=0;i<int(menu.items.size());++i){const auto row=menu.Row(i);const float left=DrawDropdownRow(p,theme,row,i==menu.hover,i==menu.selected);ComPtr<IDWriteTextFormat> format;fonts->CreateTextFormat(ToolFont,nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13*menu.scale,L"zh-CN",&format);format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);text_renderer.Draw(target.Get(),fonts.Get(),menu.items[i].first,format.Get(),{left,row.top+5*menu.scale,row.right-8*menu.scale,row.bottom},D2D1::ColorF(theme.Ink()&0xffffff));}
            if(SUCCEEDED(target->EndDraw())){POINT origin{};SIZE size{r.right,r.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(window,nullptr,nullptr,&size,surface.Dc(),&origin,0,&blend,ULW_ALPHA);}
        }EndPaint(window,&ps);
    }
    static LRESULT CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){auto* p=reinterpret_cast<Popup*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(msg==WM_NCCREATE){p=static_cast<Popup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);p->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}if(!p)return DefWindowProcW(w,msg,wp,lp);
        switch(msg){case WM_NCCALCSIZE:return 0;case WM_ERASEBKGND:return 1;case WM_PAINT:p->Paint();return 0;
        case WM_MOUSEMOVE:{const int hit=p->menu.Hit({float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))});if(hit!=p->menu.hover){p->menu.hover=hit;InvalidateRect(w,nullptr,FALSE);}return 0;}
        case WM_LBUTTONDOWN:if(p->menu.Hit({float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))})<0)p->done=true;return 0;
        case WM_LBUTTONUP:p->Choose(p->menu.Hit({float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))}));return 0;
        case WM_KEYDOWN:if(wp==VK_ESCAPE){p->done=true;return 0;}if(wp==VK_RETURN||wp==VK_SPACE){p->Choose(p->menu.hover);return 0;}if(wp==VK_UP||wp==VK_DOWN){const int count=int(p->menu.items.size());p->menu.hover=(p->menu.hover+(wp==VK_UP?count-1:1)+count)%count;InvalidateRect(w,nullptr,FALSE);return 0;}break;
        case WM_KILLFOCUS:case WM_CAPTURECHANGED:case WM_CANCELMODE:case WM_CLOSE:p->done=true;return 0;}
        return DefWindowProcW(w,msg,wp,lp);
    }
};
}
std::optional<int> TrackDropdown(HWND owner,Dropdown menu,bool dark){
    if(menu.items.empty())return {};Popup p;p.menu=std::move(menu);p.dark=dark;const auto bounds=p.menu.bounds;p.menu.bounds={0,0,bounds.right-bounds.left,bounds.bottom-bounds.top};
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Popup::Proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"LumaShot.SharedDropdown";RegisterClassW(&wc);
    const auto w=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED,wc.lpszClassName,L"",WS_POPUP,int(bounds.left),int(bounds.top),int(std::ceil(bounds.right-bounds.left)),int(std::ceil(bounds.bottom-bounds.top)),owner,nullptr,wc.hInstance,&p);if(!w)return {};
    ConfigureBorderlessWindow(w);ResizeBorderlessWindow(w,p.menu.scale,8);p.acrylic=SetSettingsAcrylic(w,dark);SetWindowDisplayAffinity(w,WDA_EXCLUDEFROMCAPTURE);ShowWindow(w,SW_SHOW);SetFocus(w);SetCapture(w);
    MSG msg{};while(!p.done){const BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(int(msg.wParam));break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(GetCapture()==w)ReleaseCapture();DestroyWindow(w);if(IsWindow(owner))SetFocus(owner);return p.choice;
}
}



