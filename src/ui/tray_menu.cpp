#include "ui/tray_menu.h"
#include "ui/text_renderer.h"
#include "capture/frame.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <stdexcept>
#include <vector>

namespace lumashot::ui {
namespace tray_detail {
using Microsoft::WRL::ComPtr;
constexpr float Width=312,Edge=6,Padding=8,RowHeight=34,DividerHeight=12;
struct Item {UINT command{};std::wstring label,hint;bool separator{},checked{},enabled{true},toggle{};};
std::vector<Item> Items(HMENU menu){
    std::vector<Item> items;
    for(int i=0;i<GetMenuItemCount(menu);++i){
        MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_ID|MIIM_STATE|MIIM_FTYPE;
        if(!GetMenuItemInfoW(menu,static_cast<UINT>(i),TRUE,&info))continue;
        Item item;item.command=info.wID;item.separator=(info.fType&MFT_SEPARATOR)!=0;item.checked=(info.fState&MFS_CHECKED)!=0;item.enabled=(info.fState&(MFS_DISABLED|MFS_GRAYED))==0;
        wchar_t label[256]{};GetMenuStringW(menu,static_cast<UINT>(i),label,256,MF_BYPOSITION);item.label=label;
        const auto hint=item.label.find(L'（');if(hint!=std::wstring::npos&&item.label.back()==L'）'){item.hint=item.label.substr(hint+1,item.label.size()-hint-2);item.label.resize(hint);}
        item.toggle=item.command==3||item.command==9||item.command==10;
        items.push_back(std::move(item));
    }
    return items;
}
float Height(const std::vector<Item>& items){float height=2*(Edge+Padding);for(const auto& item:items)height+=item.separator?DividerHeight:RowHeight;return height;}
RECT Placement(POINT anchor,RECT work,SIZE size){
    const LONG x=std::clamp(anchor.x-size.cx,work.left,std::max(work.left,work.right-size.cx));
    const LONG preferred=anchor.y-size.cy>=work.top?anchor.y-size.cy:anchor.y;
    const LONG y=std::clamp(preferred,work.top,std::max(work.top,work.bottom-size.cy));
    return {x,y,x+size.cx,y+size.cy};
}
struct Popup {
    TextRenderer text_renderer;
    HWND window{};std::vector<Item> items;float scale{1};bool dark{},done{};UINT choice{};int hover{-1},pressed{-1};
    D2D1_RECT_F Row(int index)const{float top=Edge+Padding;for(int i=0;i<index;++i)top+=items[i].separator?DividerHeight:RowHeight;return {Edge+Padding,top,Width-Edge-Padding,top+(items[index].separator?DividerHeight:RowHeight)};}
    int Hit(float x,float y)const{for(int i=0;i<int(items.size());++i){const auto row=Row(i);if(!items[i].separator&&items[i].enabled&&x>=row.left&&x<row.right&&y>=row.top&&y<row.bottom)return i;}return -1;}
    void Move(int direction){const int count=int(items.size());if(!count)return;int next=hover<0?(direction>0?-1:0):hover;for(int i=0;i<count;++i){next=(next+direction+count)%count;if(!items[next].separator&&items[next].enabled){hover=next;return;}}}
    void Choose(int index){if(index>=0&&index<int(items.size())&&!items[index].separator&&items[index].enabled){choice=items[index].command;done=true;}}
    void Draw(DibSurface& surface,int width,int height){
        ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;ComPtr<IDWriteFactory> fonts;ComPtr<ID2D1SolidColorBrush> brush;
        const auto check=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("Tray menu rendering failed");};
        check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
        check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(fonts.GetAddressOf())));
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        check(factory->CreateDCRenderTarget(&properties,&target));RECT bounds{0,0,width,height};check(target->BindDC(surface.Dc(),&bounds));check(target->CreateSolidColorBrush(D2D1::ColorF(0),&brush));
        target->SetDpi(96*scale,96*scale);target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));
        const auto color=[&](UINT rgb,float alpha=1.f){brush->SetColor(D2D1::ColorF(rgb,alpha));return brush.Get();};
        const float bottom=Height(items)-Edge;
        for(int i=5;i>0;--i){const float spread=float(i);target->FillRoundedRectangle(D2D1::RoundedRect({Edge-spread,Edge-spread+2,Width-Edge+spread,bottom+spread},10+spread,10+spread),color(0x071527,dark?.045f:.025f));}
        const UINT background=dark?0x202d3e:0xf8fbff,ink=dark?0xe6edf6:0x24364d,muted=dark?0x99abc2:0x7e90a8,accent=dark?0x83b7ff:0x247de8;
        target->FillRoundedRectangle(D2D1::RoundedRect({Edge,Edge,Width-Edge,bottom},10,10),color(background));
        target->DrawRoundedRectangle(D2D1::RoundedRect({Edge+.5f,Edge+.5f,Width-Edge-.5f,bottom-.5f},10,10),color(dark?0x3a4b61:0xdce5f0),1);
        auto& renderer=text_renderer;
        const auto text=[&](const std::wstring& value,D2D1_RECT_F rect,float size,UINT rgb,bool right=false){
            ComPtr<IDWriteTextFormat> format;check(fonts->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&format));
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);if(right)format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            renderer.Draw(target.Get(),fonts.Get(),value,format.Get(),rect,D2D1::ColorF(rgb));
        };
        for(int i=0;i<int(items.size());++i){const auto& item=items[i];const auto row=Row(i);const float cy=(row.top+row.bottom)/2;
            if(item.separator){target->DrawLine({24,cy},{Width-24,cy},color(dark?0x354459:0xe1e9f2),1);continue;}
            const bool selected=hover==i&&item.enabled;
            if(selected)target->FillRoundedRectangle(D2D1::RoundedRect({row.left,row.top+1,row.right,row.bottom-1},6,6),color(dark?0x304965:0xe7f0fd));
            const UINT fg=item.enabled?(selected?accent:ink):muted;
            const UINT icon=item.enabled?(selected?accent:muted):(dark?0x59697f:0xb6c1cf);
            const float x=31,y=cy;
            const auto line=[&](float ax,float ay,float bx,float by){target->DrawLine({x+ax,y+ay},{x+bx,y+by},color(icon),1.35f);};
            const auto box=[&](float l,float t,float r,float b,float radius=2.f){target->DrawRoundedRectangle(D2D1::RoundedRect({x+l,y+t,x+r,y+b},radius,radius),color(icon),1.35f);};
            switch(item.command){
            case 1:
                line(-3,-7,-8,-7);line(-8,-7,-8,-2);line(3,-7,8,-7);line(8,-7,8,-2);
                line(-8,2,-8,7);line(-8,7,-3,7);line(8,2,8,7);line(8,7,3,7);
                line(-3,0,3,0);line(0,-3,0,3);break;
            case 2:target->DrawEllipse(D2D1::Ellipse({x,y},7,7),color(icon),1.35f);line(0,-4,0,0);line(0,0,3,2);break;
            case 6:box(-8,-6,8,6);text(L"GIF",{x-7,y-6,x+7,y+6},7.5f,icon);break;
            case 7:box(-8,-5,3,5);line(3,-2,8,-5);line(8,-5,8,5);line(8,5,3,2);break;
            case 3:line(-5,-7,-4,7);line(-5,-7,6,2);line(-4,7,0,3);line(0,3,6,2);break;
            case 8:box(-6,-6,6,7);box(-3,-8,3,-4,1);line(-3,0,3,0);line(-3,3,2,3);break;
            case 9:box(-8,-5,8,5,3);line(-5,0,-1,0);line(-3,-2,-3,2);target->FillEllipse(D2D1::Ellipse({x+3,y-1},1,1),color(icon));target->FillEllipse(D2D1::Ellipse({x+5,y+1},1,1),color(icon));break;
            case 10:
                box(-9,-6,9,6);
                for(int key=0;key<5;++key){const float k=-6.f+key*3;target->FillRectangle({x+k-.65f,y-2.7f,x+k+.65f,y-1.3f},color(icon));}
                line(-6,2,-5,2);line(-3,2,3,2);line(5,2,6,2);
                target->DrawLine({x-9,y+8},{x+9,y-8},color(selected?(dark?0x304965:0xe7f0fd):background),3.5f);
                line(-9,8,9,-8);break;
            case 4:target->DrawEllipse(D2D1::Ellipse({x,y},5,5),color(icon),1.35f);target->DrawEllipse(D2D1::Ellipse({x,y},1.5f,1.5f),color(icon),1.35f);for(int j=0;j<8;++j){const float a=j*3.14159265f/4;line(5*std::cos(a),5*std::sin(a),7.5f*std::cos(a),7.5f*std::sin(a));}break;
            case 5:line(-2,-6,-7,-6);line(-7,-6,-7,6);line(-7,6,-2,6);line(-2,0,7,0);line(3,-4,7,0);line(7,0,3,4);break;
            default:break;
            }
            const float right=Width-26;
            text(item.label,{51,row.top,item.toggle?right-28:item.hint.empty()?right:right-100,row.bottom},13,fg);
            if(!item.hint.empty())text(item.hint,{right-98,row.top,right,row.bottom},11,muted,true);
            if(item.toggle||item.checked){const auto rect=D2D1::RoundedRect({right-16,cy-8,right,cy+8},4,4);
                if(item.checked){target->FillRoundedRectangle(rect,color(accent));target->DrawLine({right-12,cy},{right-9,cy+3},color(dark?0x192637:0xffffff),1.5f);target->DrawLine({right-9,cy+3},{right-4,cy-3},color(dark?0x192637:0xffffff),1.5f);}
                else target->DrawRoundedRectangle(rect,color(dark?0x63758e:0xb7c7db),1);
            }
        }
        check(target->EndDraw());
    }
    void Paint(){PAINTSTRUCT ps{};BeginPaint(window,&ps);try{RECT r{};GetClientRect(window,&r);DibSurface surface(r.right,r.bottom);Draw(surface,r.right,r.bottom);POINT source{};SIZE size{r.right,r.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};if(!UpdateLayeredWindow(window,nullptr,nullptr,&size,surface.Dc(),&source,0,&blend,ULW_ALPHA))done=true;}catch(...){done=true;}EndPaint(window,&ps);}
    static LRESULT CALLBACK Proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
        auto* p=reinterpret_cast<Popup*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(message==WM_NCCREATE){p=static_cast<Popup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);p->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}if(!p)return DefWindowProcW(w,message,wp,lp);
        switch(message){
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:p->Paint();return 0;
        case WM_MOUSEMOVE:{const int hit=p->Hit(GET_X_LPARAM(lp)/p->scale,GET_Y_LPARAM(lp)/p->scale);if(hit!=p->hover){p->hover=hit;InvalidateRect(w,nullptr,FALSE);}return 0;}
        case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:{const float x=GET_X_LPARAM(lp)/p->scale,y=GET_Y_LPARAM(lp)/p->scale;p->pressed=p->Hit(x,y);if(x<Edge||x>=Width-Edge||y<Edge||y>=Height(p->items)-Edge)p->done=true;return 0;}
        case WM_LBUTTONUP:case WM_RBUTTONUP:{const int hit=p->Hit(GET_X_LPARAM(lp)/p->scale,GET_Y_LPARAM(lp)/p->scale);if(hit==p->pressed)p->Choose(hit);p->pressed=-1;return 0;}
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE){p->done=true;return 0;}
            if(wp==VK_RETURN||wp==VK_SPACE){p->Choose(p->hover);return 0;}
            if(wp==VK_UP||wp==VK_DOWN||wp==VK_HOME||wp==VK_END){if(wp==VK_HOME||wp==VK_END)p->hover=-1;p->Move(wp==VK_UP||wp==VK_END?-1:1);InvalidateRect(w,nullptr,FALSE);return 0;}break;
        case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE)p->done=true;return 0;
        case WM_KILLFOCUS:case WM_CAPTURECHANGED:case WM_CANCELMODE:case WM_CLOSE:case WM_DESTROY:p->done=true;return 0;
        }
        return DefWindowProcW(w,message,wp,lp);
    }
};
}
UINT TrackTrayMenu(HWND owner,HMENU commands,POINT anchor,bool dark){
    tray_detail::Popup popup;popup.items=tray_detail::Items(commands);popup.dark=dark;if(popup.items.empty())return 0;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=tray_detail::Popup::Proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"LumaShot.TrayMenu";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 0;
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED,wc.lpszClassName,L"LumaShot",WS_POPUP,anchor.x,anchor.y,1,1,owner,nullptr,wc.hInstance,&popup);if(!window)return 0;
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&monitor);
    popup.scale=std::min({GetDpiForWindow(window)/96.f,(monitor.rcWork.right-monitor.rcWork.left)/tray_detail::Width,(monitor.rcWork.bottom-monitor.rcWork.top)/tray_detail::Height(popup.items)});
    SIZE size{static_cast<LONG>(std::ceil(tray_detail::Width*popup.scale)),static_cast<LONG>(std::ceil(tray_detail::Height(popup.items)*popup.scale))};const auto rect=tray_detail::Placement(anchor,monitor.rcWork,size);
    SetWindowPos(window,HWND_TOPMOST,rect.left,rect.top,size.cx,size.cy,SWP_NOACTIVATE);SetWindowDisplayAffinity(window,WDA_NONE);
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(window);SetCapture(window);InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
    MSG message{};while(!popup.done){const BOOL result=GetMessageW(&message,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(static_cast<int>(message.wParam));break;}TranslateMessage(&message);DispatchMessageW(&message);}
    if(GetCapture()==window)ReleaseCapture();if(IsWindow(window))DestroyWindow(window);return popup.choice;
}
}
