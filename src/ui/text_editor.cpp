#include "ui/text_editor.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace lumashot {
namespace {
constexpr float shadow=20,padding=16,footer=36,button_size=36;
D2D1_COLOR_F Color(uint32_t value,float alpha=1){return D2D1::ColorF(value&0xffffff,alpha);}
}
TextEditorFrame::~TextEditorFrame(){if(window_)DestroyWindow(window_);}
void TextEditorFrame::Create(HWND owner,std::function<void()> confirm,bool single_line){
    single_line_=single_line;
    confirm_=std::move(confirm);
    WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumaShot.TextEditorFrame";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    CheckWin32(RegisterClassW(&wc)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Register text frame");
    window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,single_line?L"自定义标签 · 留空恢复自动序号":L"Enter 完成 · Shift+Enter 换行 · Esc 取消",WS_POPUP,0,0,1,1,owner,nullptr,wc.hInstance,this);
    CheckWin32(window_!=nullptr,"Create text frame");
}
RECT TextEditorFrame::Arrange(Point anchor,SIZE size,RECT work,float scale,bool dark){
    scale_=scale;dark_=dark;
    const auto px=[&](float value){return LONG(std::lround(value*scale));};
    const LONG body_width=std::max(size.cx+px(padding+14+button_size+12),px(340));
    const LONG body_height=std::max(size.cy+px(24),px(60));
    const LONG width=body_width+px(2*shadow),height=body_height+px(2*shadow+footer);
    const LONG x=std::clamp(LONG(anchor.x)-px(shadow+padding),work.left,std::max(work.left,work.right-width));
    const LONG y=std::clamp(LONG(anchor.y)-px(shadow)-(body_height-size.cy)/2,work.top,std::max(work.top,work.bottom-height));
    bounds_={x,y,x+width,y+height};
    text_={px(shadow+padding),px(shadow)+(body_height-size.cy)/2,px(shadow+padding)+size.cx,px(shadow)+(body_height-size.cy)/2+size.cy};
    button_={px(shadow)+body_width-px(12+button_size),px(shadow)+(body_height-px(button_size))/2,px(shadow)+body_width-px(12),px(shadow)+(body_height+px(button_size))/2};
    SetWindowPos(window_,nullptr,x,y,width,height,SWP_NOACTIVATE|SWP_NOZORDER);
    surface_=std::make_unique<DibSurface>(width,height);Paint();
    RECT screen=text_;OffsetRect(&screen,x,y);return screen;
}
void TextEditorFrame::Show(){ShowWindow(window_,SW_SHOWNOACTIVATE);}
Frame TextEditorFrame::Snapshot()const{
    auto result=MakeFrame({0,0,bounds_.right-bounds_.left,bounds_.bottom-bounds_.top},0);
    if(surface_)std::copy_n(surface_->Pixels(),result.pixels.size(),result.pixels.begin());return result;
}
void TextEditorFrame::Paint(){
    if(!surface_)return;
    if(!factory_)CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf())),"Text frame factory");
    if(!target_){const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);CheckWin32(SUCCEEDED(factory_->CreateDCRenderTarget(&props,&target_)),"Text frame target");}
    const RECT rect{0,0,bounds_.right-bounds_.left,bounds_.bottom-bounds_.top};
    CheckWin32(SUCCEEDED(target_->BindDC(surface_->Dc(),&rect)),"Bind text frame");
    target_->BeginDraw();target_->Clear(Color(0,0));target_->SetTransform(D2D1::Matrix3x2F::Scale(scale_,scale_));
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ComPtr<ID2D1SolidColorBrush> brush;CheckWin32(SUCCEEDED(target_->CreateSolidColorBrush(Color(0),&brush)),"Text frame brush");
    const float right=float(rect.right)/scale_-shadow,bottom=float(rect.bottom)/scale_-shadow-footer;
    float accumulated=0;
    for(int i=36;i>=0;--i){
        const float spread=i*.5f,alpha=.1f*std::exp(-spread*spread/80.f);
        brush->SetColor(Color(dark_?0:0x406590,(alpha-accumulated)/(1-accumulated)));accumulated=alpha;
        target_->FillRoundedRectangle(D2D1::RoundedRect({shadow-spread,shadow+4-spread,right+spread,bottom+4+spread},12+spread,12+spread),brush.Get());
    }
    brush->SetColor(Color(Background(dark_)));target_->FillRoundedRectangle(D2D1::RoundedRect({shadow,shadow,right,bottom},12,12),brush.Get());
    brush->SetColor(Color(dark_?0x4b7bab:0xb3d7ff));target_->DrawRoundedRectangle(D2D1::RoundedRect({shadow+.75f,shadow+.75f,right-.75f,bottom-.75f},11.5f,11.5f),brush.Get(),1.5f);
    const D2D1_RECT_F button{button_.left/scale_,button_.top/scale_,button_.right/scale_,button_.bottom/scale_};
    brush->SetColor(Color(pressed_?0x0968da:hover_?0x1680ef:0x2684ff));target_->FillRoundedRectangle(D2D1::RoundedRect(button,9,9),brush.Get());
    const float cx=(button.left+button.right)/2,cy=(button.top+button.bottom)/2;
    ComPtr<ID2D1StrokeStyle> stroke;auto properties=D2D1::StrokeStyleProperties();properties.startCap=properties.endCap=D2D1_CAP_STYLE_ROUND;properties.lineJoin=D2D1_LINE_JOIN_ROUND;
    CheckWin32(SUCCEEDED(factory_->CreateStrokeStyle(properties,nullptr,0,&stroke)),"Return icon stroke");
    brush->SetColor(Color(0xffffff));
    const auto line=[&](float x1,float y1,float x2,float y2){target_->DrawLine({cx+x1,cy+y1},{cx+x2,cy+y2},brush.Get(),2,stroke.Get());};
    line(7,-7,7,4);line(7,4,-7,4);line(-7,4,-2,-1);line(-7,4,-2,9);
    ComPtr<IDWriteFactory> dw;CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dw.GetAddressOf()))),"Text hint factory");
    ComPtr<IDWriteTextFormat> font;CheckWin32(SUCCEEDED(dw->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,11,L"zh-CN",&font)),"Text hint font");
    font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    const wchar_t* keys[]={L"Enter",single_line_?L"留空":L"Shift+Enter",L"Esc"};const wchar_t* labels[]={L"完成",single_line_?L"序号":L"换行",L"取消"};const float widths[]={42,76,30};
    float x=(shadow+right-284)/2;const float y=bottom+10;
    for(int i=0;i<3;++i){
        const auto key=D2D1::RoundedRect({x,y,x+widths[i],y+22},5,5);
        brush->SetColor(Color(dark_?0x303a48:0xf8fafc,.97f));target_->FillRoundedRectangle(key,brush.Get());
        brush->SetColor(Color(dark_?0x526072:0xdce1e9));target_->DrawRoundedRectangle(key,brush.Get(),1);
        const auto ink=Color(dark_?0xc4cedc:0x697586);
        text_renderer_.Draw(target_.Get(),dw.Get(),keys[i],font.Get(),key.rect,ink);
        text_renderer_.Draw(target_.Get(),dw.Get(),labels[i],font.Get(),{x+widths[i]+5,y,x+widths[i]+33,y+22},ink);x+=widths[i]+46;
    }
    const HRESULT result=target_->EndDraw();if(result==D2DERR_RECREATE_TARGET){target_.Reset();InvalidateRect(window_,nullptr,FALSE);return;}CheckWin32(SUCCEEDED(result),"Draw text frame");
    POINT origin{},destination{bounds_.left,bounds_.top};SIZE size{rect.right,rect.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    CheckWin32(UpdateLayeredWindow(window_,nullptr,&destination,&size,surface_->Dc(),&origin,0,&blend,ULW_ALPHA)!=FALSE,"Present text frame");
}
LRESULT CALLBACK TextEditorFrame::Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* frame=reinterpret_cast<TextEditorFrame*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){frame=static_cast<TextEditorFrame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(frame));}
    if(!frame)return DefWindowProcW(window,message,wp,lp);
    switch(message){
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(window,&paint);try{frame->Paint();}catch(...){PostMessageW(GetWindow(window,GW_OWNER),WM_CLOSE,0,0);}EndPaint(window,&paint);return 0;}
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_MOUSEMOVE:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const bool hover=PtInRect(&frame->button_,p)!=FALSE;if(hover!=frame->hover_){frame->hover_=hover;InvalidateRect(window,nullptr,FALSE);}TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);return 0;}
    case WM_MOUSELEAVE:frame->hover_=false;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_LBUTTONDOWN:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(PtInRect(&frame->button_,p)){frame->pressed_=true;SetCapture(window);InvalidateRect(window,nullptr,FALSE);}return 0;}
    case WM_LBUTTONUP:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const bool confirm=frame->pressed_&&PtInRect(&frame->button_,p);frame->pressed_=false;if(GetCapture()==window)ReleaseCapture();if(confirm){auto callback=frame->confirm_;callback();return 0;}InvalidateRect(window,nullptr,FALSE);return 0;}
    case WM_CAPTURECHANGED:frame->pressed_=false;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_NCDESTROY:frame->window_=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
    default:break;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}
