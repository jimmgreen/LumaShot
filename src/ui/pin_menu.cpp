#include "ui/pin_menu.h"
#include "ui/text_renderer.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
namespace lumashot {
int PinMenuModel::Hit(float x,float y)const{if(x<8||x>=Width-8)return -1;for(int i=0;i<PinMenuModel::Count;++i)if(Visible(i)&&y>=RowTop(i)&&y<RowTop(i)+42)return enabled[i]?i:-1;return -1;}
int PinMenuModel::Next(int current,int direction)const{for(int n=0;n<Count;++n){current=(current+direction+Count)%Count;if(Visible(current)&&enabled[current])return current;}return -1;}
RECT PlacePinMenu(POINT anchor,RECT work,float scale,const PinMenuModel& model){
    const LONG w=LONG(std::ceil((PinMenuModel::Width+2*PinMenuModel::Shadow)*scale)),h=LONG(std::ceil((model.Height()+2*PinMenuModel::Shadow)*scale));
    LONG x=anchor.x-LONG(PinMenuModel::Shadow*scale),y=anchor.y-LONG(PinMenuModel::Shadow*scale);if(x+w>work.right)x-=w;if(y+h>work.bottom)y-=h;
    x=std::clamp(x,work.left,std::max(work.left,work.right-w));y=std::clamp(y,work.top,std::max(work.top,work.bottom-h));return {x,y,x+w,y+h};
}
void DrawPinMenu(ID2D1RenderTarget* t,const PinMenuModel& m,int hover){
    ComPtr<ID2D1SolidColorBrush> b;CheckWin32(SUCCEEDED(t->CreateSolidColorBrush(D2D1::ColorF(0),&b)),"Menu brush");
    auto color=[&](UINT32 c){b->SetColor(D2D1::ColorF(c));};
    const UINT32 ink=m.dark?0xedf4ff:0x243142,muted=m.dark?0x8491a4:0x8793a4,border=m.dark?0x3d4858:0xdce2eb;
    color(m.dark?0x252c38:0xf7f9fc);t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0,0,PinMenuModel::Width,m.Height()),12,12),b.Get());
    color(border);t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(.5f,.5f,PinMenuModel::Width-.5f,m.Height()-.5f),12,12),b.Get());
    ComPtr<IDWriteFactory> dw;CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dw.GetAddressOf()))),"Menu text factory");
    ComPtr<IDWriteTextFormat> format;dw->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14,L"zh-CN",&format);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    TextRenderer text_renderer;
    auto text=[&](const wchar_t* s,D2D1_RECT_F r,UINT32 c){text_renderer.Draw(t,dw.Get(),s,format.Get(),r,D2D1::ColorF(c));};
    if(!m.status.empty())text(m.status.c_str(),D2D1::RectF(18,8,PinMenuModel::Width-18,40),muted);
    const wchar_t* labels[]={L"标注",m.locked?L"解锁":L"锁定",L"复制所选文字",L"复制全部文字",L"复制表格",L"复制图片",L"保存图片…",m.recognized?L"重新识别":L"识别文字",m.translated?L"翻译面板":L"翻译",L"关闭贴图"};
    for(int i=0;i<PinMenuModel::Count;++i){if(!m.Visible(i))continue;const float y=m.RowTop(i);const bool active=i==hover&&m.enabled[i];
        if(i==2||i==5||i==9){color(border);t->DrawLine({18,y-4},{PinMenuModel::Width-18,y-4},b.Get());}
        if(active){color(m.dark?0x183e66:0xe2efff);t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(8,y,PinMenuModel::Width-8,y+42),7,7),b.Get());}
        const UINT32 c=!m.enabled[i]?muted:active?(m.dark?0x69b3ff:0x0784ff):ink;
        text(labels[i],D2D1::RectF(42,y,(i<=2||i==8?PinMenuModel::Width-77:PinMenuModel::Width-18),y+42),c);
        if(i<=2||i==8){format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);text(i==0?L"空格":i==1?L"L":i==8?L"T":L"Ctrl+C",D2D1::RectF(PinMenuModel::Width-77,y,PinMenuModel::Width-20,y+42),muted);format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);}
        color(c);const float x=20,cy=y+14;
        D2D1_MATRIX_3X2_F previous;t->GetTransform(&previous);
        t->SetTransform(D2D1::Matrix3x2F::Scale(.8f,.8f,D2D1::Point2F(x,cy))*previous);
        auto line=[&](float a,float q,float z,float v){t->DrawLine({x+a,cy+q},{x+z,cy+v},b.Get(),1.5f);};
        auto rect=[&](float a,float q,float z,float v){t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x+a,cy+q,x+z,cy+v),1,1),b.Get(),1.5f);};
        const int icon=PinMenuModel::Commands[i]-1;
        if(icon==0||icon==1){rect(3,3,16,17);line(0,12,0,0);line(0,0,12,0);line(6,7,13,7);line(6,11,13,11);if(icon==1)line(6,14,11,14);}
        if(icon==2){rect(0,0,17,16);t->DrawEllipse(D2D1::Ellipse({x+5,cy+5},1.5f,1.5f),b.Get(),1.3f);line(2,14,7,9);line(7,9,10,12);line(10,12,13,8);line(13,8,16,12);}
        if(icon==3){rect(0,0,17,17);rect(4,0,12,6);rect(4,10,13,17);}
        if(icon==4){
            ComPtr<ID2D1Factory> factory;t->GetFactory(&factory);
            ComPtr<ID2D1PathGeometry> arc;CheckWin32(SUCCEEDED(factory->CreatePathGeometry(&arc)),"Refresh icon path");
            ComPtr<ID2D1GeometrySink> sink;CheckWin32(SUCCEEDED(arc->Open(&sink)),"Refresh icon arc");
            sink->BeginFigure({x+15,cy+10},D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment({x+14,cy+4},{7,7},0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_LARGE));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);CheckWin32(SUCCEEDED(sink->Close()),"Refresh icon geometry");
            t->DrawGeometry(arc.Get(),b.Get(),1.5f);
            line(14,-1,14,4);line(14,4,9,4);
        }
        if(icon==5){line(2,2,15,15);line(15,2,2,15);}
        if(icon==6){line(1,16,5,15);line(5,15,16,4);line(16,4,12,0);line(12,0,1,11);line(1,11,1,16);line(9,3,13,7);}
        if(icon==8){rect(0,0,17,17);line(0,6,17,6);line(0,12,17,12);line(6,0,6,17);line(12,0,12,17);}
        if(icon==9){line(0,2,9,2);line(4.5f,-1,4.5f,2);line(1.5f,4,7.5f,11);line(7.5f,4,1.5f,11);line(9,17,13,6);line(13,6,17,17);line(10.4f,13,15.6f,13);}
        if(icon==7){rect(1,7,16,17);line(8,11,8,14);line(4,7,4,3);line(4,3,6,0);line(6,0,11,0);line(11,0,13,3);if(!m.locked)line(13,3,13,7);}
        t->SetTransform(previous);
    }
}
void DrawPinMenuSurface(ID2D1RenderTarget* t,const PinMenuModel& model,int hover){
    D2D1_MATRIX_3X2_F previous;t->GetTransform(&previous);
    t->SetTransform(D2D1::Matrix3x2F::Translation(PinMenuModel::Shadow,PinMenuModel::Shadow)*previous);
    ComPtr<ID2D1SolidColorBrush> brush;CheckWin32(SUCCEEDED(t->CreateSolidColorBrush(D2D1::ColorF(0,0.f),&brush)),"Menu shadow brush");
    // Integrate a Gaussian falloff from the outside in. The padding contains
    // its full tail; there is no native shadow or binary rounded-window clip.
    float accumulated=0;
    for(int i=32;i>=0;--i){
        const float spread=i*.5f,opacity=.12f*std::exp(-spread*spread/72.f);
        brush->SetColor(D2D1::ColorF(0,(opacity-accumulated)/(1-accumulated)));accumulated=opacity;
        t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(-spread,3-spread,PinMenuModel::Width+spread,model.Height()+3+spread),12+spread,12+spread),brush.Get());
    }
    DrawPinMenu(t,model,hover);t->SetTransform(previous);
}
namespace {
struct Popup {
    PinMenuModel model;HWND window{};int hover{-1},down{-1},command{};bool done{};float scale{1};
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;std::unique_ptr<DibSurface> surface;
    static LRESULT CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
        auto* p=reinterpret_cast<Popup*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){p=static_cast<Popup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));p->window=w;}
        if(!p)return DefWindowProcW(w,msg,wp,lp);
        switch(msg){
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(w,&ps);try{
            if(!p->factory)CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,p->factory.GetAddressOf())),"Menu renderer");
            RECT r{};GetClientRect(w,&r);
            if(!p->surface)p->surface=std::make_unique<DibSurface>(r.right,r.bottom);
            if(!p->target){const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96*p->scale,96*p->scale);
                CheckWin32(SUCCEEDED(p->factory->CreateDCRenderTarget(&props,&p->target)),"Menu alpha target");}
            CheckWin32(SUCCEEDED(p->target->BindDC(p->surface->Dc(),&r)),"Bind menu surface");
            p->target->BeginDraw();p->target->Clear(D2D1::ColorF(0,0.f));DrawPinMenuSurface(p->target.Get(),p->model,p->hover);
            const HRESULT result=p->target->EndDraw();
            if(result==D2DERR_RECREATE_TARGET){p->target.Reset();InvalidateRect(w,nullptr,FALSE);}
            else{CheckWin32(SUCCEEDED(result),"Render menu");POINT source{};SIZE size{r.right,r.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
                CheckWin32(UpdateLayeredWindow(w,nullptr,nullptr,&size,p->surface->Dc(),&source,0,&blend,ULW_ALPHA)!=FALSE,"Present soft menu shadow");}
        }catch(...){p->done=true;}EndPaint(w,&ps);return 0;}
        case WM_MOUSEMOVE:p->hover=p->model.Hit(GET_X_LPARAM(lp)/p->scale-PinMenuModel::Shadow,GET_Y_LPARAM(lp)/p->scale-PinMenuModel::Shadow);InvalidateRect(w,nullptr,FALSE);return 0;
        case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:{RECT r{};GetClientRect(w,&r);POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const float x=point.x/p->scale-PinMenuModel::Shadow,y=point.y/p->scale-PinMenuModel::Shadow;if(x<0||y<0||x>=PinMenuModel::Width||y>=p->model.Height())p->done=true;else p->down=p->model.Hit(x,y);return 0;}
        case WM_LBUTTONUP:case WM_RBUTTONUP:{const int hit=p->model.Hit(GET_X_LPARAM(lp)/p->scale-PinMenuModel::Shadow,GET_Y_LPARAM(lp)/p->scale-PinMenuModel::Shadow);if(hit>=0&&hit==p->down){p->command=PinMenuModel::Commands[hit];p->done=true;}p->down=-1;return 0;}
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE||wp==VK_F10)p->done=true;
            else if(wp==VK_UP||wp==VK_DOWN)p->hover=p->model.Next(p->hover<0?(wp==VK_UP?0:PinMenuModel::Count-1):p->hover,wp==VK_UP?-1:1);
            else if(wp==VK_HOME)p->hover=p->model.Next(PinMenuModel::Count-1,1);
            else if(wp==VK_END)p->hover=p->model.Next(0,-1);
            else if(wp==VK_RETURN){if(p->hover>=0){p->command=PinMenuModel::Commands[p->hover];p->done=true;}}
            else if(wp=='C'&&(GetKeyState(VK_CONTROL)&0x8000)&&p->model.enabled[2]){p->command=1;p->done=true;}
            else if(wp==VK_SPACE&&p->model.enabled[0]){p->command=7;p->done=true;}
            else if(wp=='L'&&p->model.enabled[1]){p->command=8;p->done=true;}
            InvalidateRect(w,nullptr,FALSE);return 0;
        case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE)p->done=true;return 0;
        case WM_CAPTURECHANGED:case WM_CANCELMODE:case WM_CLOSE:p->done=true;return 0;
        case WM_NCDESTROY:p->window=nullptr;p->done=true;SetWindowLongPtrW(w,GWLP_USERDATA,0);break;
        }return DefWindowProcW(w,msg,wp,lp);
    }
};
}
int TrackPinMenu(HWND owner,POINT anchor,const PinMenuModel& model){
    Popup p;p.model=model;p.scale=float(GetDpiForWindow(owner))/96;
    MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&mi);
    p.scale=std::min({p.scale,float(mi.rcWork.right-mi.rcWork.left)/(PinMenuModel::Width+2*PinMenuModel::Shadow),float(mi.rcWork.bottom-mi.rcWork.top)/(model.Height()+2*PinMenuModel::Shadow)});
    const RECT r=PlacePinMenu(anchor,mi.rcWork,p.scale,model);
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Popup::Proc;wc.lpszClassName=L"LumaShot.PinMenu";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.style=0;
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 0;
    HWND w=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"贴图菜单",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,owner,nullptr,wc.hInstance,&p);if(!w)return 0;

    ShowWindow(w,SW_SHOW);SetForegroundWindow(w);SetFocus(w);SetCapture(w);
    MSG msg{};while(!p.done){const BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(!result)PostQuitMessage(int(msg.wParam));break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(GetCapture()==w)ReleaseCapture();if(p.window)DestroyWindow(w);if(IsWindow(owner)&&GetForegroundWindow()==owner)SetFocus(owner);return p.command;
}
}



