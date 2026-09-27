#include "app/application.h"
#include "model/note_color.h"
#include "model/number_label.h"
#include "model/mark_properties.h"
#include <commctrl.h>
#include <imm.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <windowsx.h>

namespace lumashot {
namespace {
constexpr UINT kFinishText=WM_APP+21;
COLORREF NativeColor(uint32_t color){return RGB((color>>16)&255,(color>>8)&255,color&255);}
std::wstring EditorText(HWND editor){
    const int length=GetWindowTextLengthW(editor);
    std::wstring text(static_cast<size_t>(length)+1,L'\0');
    GetWindowTextW(editor,text.data(),length+1);text.resize(length);return text;
}
}

void Application::BeginText(View& view,Point p,int existing,bool badge_label){
    CommitText();edit_badge_label_=badge_label;
    // An edit session supersedes any unfinished pointer transform. A delayed
    // mouse-up must not restore the pre-edit drag snapshot over the new text.
    ReleaseCapture();moving_=resizing_=mark_moving_=false;edit_original_.reset();mark_handle_=-1;
    const Mark* mark=existing>=0?&document_.marks[existing]:nullptr;
    const bool label=mark&&mark->tool==Tool::Number&&!badge_label;
    Box box{};
    if(mark&&!badge_label){
        box=label?NumberDetailBounds(*mark):Bounds(*mark);
        if(label)box=TagContentBounds(box,TextTagMetrics(mark->number_text_size));
        else if(IsTextTag(mark->text_background))box=TextContentBounds(*mark);
        p={box.left,box.top};
    }
    const float scale=badge_label?std::min(state_.toolbar.scale,float(view.bounds.right-view.bounds.left)/400.f):state_.toolbar.scale;
    if(mark&&!label&&!badge_label){
        if(state_.tool!=Tool::Text)Command(5);
        state_.font_family=mark->font_family;
        state_.font_size=mark->font_size/scale;state_.text_bold=mark->text_bold;state_.text_align=mark->text_align;state_.text_background=mark->text_background;state_.color=mark->color;
    }
    edit_family_=label?L"Segoe UI":(mark?mark->font_family:state_.font_family);
    edit_color_=mark?mark->color:state_.ActiveColor();
    edit_size_=mark?mark->font_size:state_.font_size*scale;
    edit_bold_=mark?mark->text_bold:state_.text_bold;
    edit_align_=mark?mark->text_align:state_.text_align;
    edit_background_=mark?mark->text_background:state_.text_background;
    if(label){
        const auto appearance=ResolveNoteAppearance(*frame_,*mark);
        edit_color_=appearance.ink;
        edit_size_=mark->number_text_size;edit_bold_=false;edit_align_=TextAlign::Left;
    }
    if(badge_label){edit_family_=L"Segoe UI";edit_size_=14*scale;edit_bold_=false;edit_align_=TextAlign::Left;edit_color_=state_.dark?0xffedf4ff:0xff243142;}
    if(!mark&&!badge_label&&IsTextTag(edit_background_)){const auto padding=TextTagMetrics(edit_size_);p.x+=padding.x;p.y+=padding.y;}
    // The input panel always follows the UI theme, independently of the
    // annotation's Tag fill. Preview ink may adapt; stored hue stays exact.
    edit_surface_color_=TextEditorFrame::Background(state_.dark);edit_display_color_=edit_color_;
    const double minimum=edit_size_/scale>=24?3.:4.5;
    for(int step=1;step<=20&&NoteContrast(NoteLuminance(edit_display_color_),NoteLuminance(edit_surface_color_))<minimum;++step)
        edit_display_color_=MixNoteColor(edit_color_,state_.dark?0xffffffffu:0xff000000u,float(step)/20);
    const int preferred_width=badge_label?int(240*scale):mark?int(std::ceil(mark->text_auto_size&&mark->text_wrap_width>0?mark->text_wrap_width:box.right-box.left)):int(300*scale);
    const int width=badge_label?preferred_width:std::min({preferred_width,int(std::min(state_.selection.right,float(view.bounds.right))-p.x),int(view.bounds.right-view.bounds.left-118*scale)});
    edit_max_height_=badge_label?int(28*scale):std::min(int(std::min(state_.selection.bottom,float(view.bounds.bottom))-p.y),int(view.bounds.bottom-view.bounds.top-100*scale));
    const int line_height=int(std::ceil(edit_size_*1.4f))+2;
    if(width<24*scale||edit_max_height_<line_height){edit_badge_label_=false;Notice(L"这里空间不足，请在选区内稍靠左上方的位置输入文字。");return;}
    edit_min_height_=std::min(edit_max_height_,mark&&!badge_label?std::max(line_height,int(std::ceil(box.bottom-box.top))):line_height);
    edit_index_=existing;edit_point_=p;edit_owner_=view.window;
    edit_previous_hint_=state_.hint;
    state_.hint=badge_label?L"自定义标签 · Enter 确认 · Esc 取消 · 留空恢复自动序号":L"编辑文字 · Enter 完成 · Shift+Enter 换行 · Esc 取消本次修改";
    edit_work_=view.bounds;
    edit_initializing_=true;edit_composing_=false;
    try{
        const HINSTANCE instance=GetModuleHandleW(nullptr);
        WNDCLASSW wc{};wc.lpfnWndProc=TextHostProc;wc.hInstance=instance;wc.lpszClassName=L"LumaShot.TextEditor";
        wc.hCursor=LoadCursorW(nullptr,IDC_IBEAM);
        CheckWin32(RegisterClassW(&wc)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Register text editor");
        edit_backdrop_=CreateSolidBrush(NativeColor(edit_surface_color_));
        CheckWin32(edit_backdrop_!=nullptr,"Create text editor background");
        edit_frame_=std::make_unique<TextEditorFrame>();
        edit_frame_->Create(view.window,[this]{const HWND owner=edit_owner_;CommitText();SetFocus(owner);},badge_label);
        const RECT editor_bounds=edit_frame_->Arrange(p,{width,edit_min_height_},edit_work_,scale,state_.dark);
        // A separate owned HWND is essential: flip-model DXGI presentation can
        // cover GDI child controls and their carets inside the overlay HWND.
        edit_host_=CreateWindowExW(WS_EX_TOOLWINDOW,L"LumaShot.TextEditor",badge_label?L"自定义标签 · Enter 确认，留空恢复序号，Esc 取消":L"文字编辑 · Enter 完成，Shift+Enter 换行，Esc 取消",
            WS_POPUP|WS_CLIPCHILDREN,editor_bounds.left,editor_bounds.top,width,edit_min_height_,edit_frame_->Window(),nullptr,instance,this);
        CheckWin32(edit_host_!=nullptr,"Create text editor host");
        const DWORD alignment=edit_align_==TextAlign::Center?ES_CENTER:(edit_align_==TextAlign::Right?ES_RIGHT:ES_LEFT);
        edit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(badge_label?ES_AUTOHSCROLL:(ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN))|alignment,
            0,0,width,edit_min_height_,edit_host_,nullptr,instance,nullptr);
        CheckWin32(edit_!=nullptr,"Create text editor");
        edit_font_=CreateFontW(-std::max(1,int(std::lround(edit_size_))),0,0,0,edit_bold_?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,edit_family_.c_str());
        CheckWin32(edit_font_!=nullptr,"Create text editor font");
        SendMessageW(edit_,WM_SETFONT,reinterpret_cast<WPARAM>(edit_font_),FALSE);
        SendMessageW(edit_,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
        SendMessageW(edit_,EM_SETLIMITTEXT,badge_label?NumberLabelLimit:4096,0);
        if(badge_label)SendMessageW(edit_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"留空使用自动序号"));
        TEXTMETRICW metrics{};HDC dc=GetDC(edit_);const auto old=SelectObject(dc,edit_font_);GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(edit_,dc);
        const int caret_width=std::max(2,int(std::lround(2*scale)));
        // Native bitmap carets are XORed with the field. This bitmap yields a
        // blue caret while retaining the EDIT control's movement and blinking.
        std::vector<uint32_t> caret_pixels(static_cast<size_t>(caret_width)*metrics.tmHeight,(edit_surface_color_^0xff2684ff)&0xffffff);
        edit_caret_=CreateBitmap(caret_width,metrics.tmHeight,1,32,caret_pixels.data());
        CheckWin32(SetWindowSubclass(edit_,EditProc,1,reinterpret_cast<DWORD_PTR>(this))!=FALSE,"Subclass text editor");
        if(badge_label)SetWindowTextW(edit_,mark?mark->number_label.c_str():state_.number_label.c_str());else if(mark)SetWindowTextW(edit_,mark->text.c_str());
        edit_initializing_=false;ResizeTextEditor();Invalidate();
        edit_frame_->Show();ShowWindow(edit_host_,SW_SHOW);SetFocus(edit_);
        // Reopening selects the existing text, just like entering a text layer.
        SendMessageW(edit_,EM_SETSEL,0,mark||badge_label?-1:0);
        SendMessageW(edit_,EM_SCROLLCARET,0,0);
    }catch(...){CommitText(true);throw;}
}

void Application::ResizeTextEditor(){
    if(!edit_||edit_initializing_)return;
    if(edit_badge_label_){SendMessageW(edit_,EM_SCROLLCARET,0,0);return;}
    RECT current{};GetClientRect(edit_,&current);
    auto text=EditorText(edit_);text+=L' '; // Measure the empty line after a trailing newline, too.
    HDC dc=GetDC(edit_);const auto old=SelectObject(dc,edit_font_);
    RECT measured{0,0,current.right,0};
    DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_EDITCONTROL|DT_NOPREFIX);
    TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
    SelectObject(dc,old);ReleaseDC(edit_,dc);
    const int lines=static_cast<int>(SendMessageW(edit_,EM_GETLINECOUNT,0,0));
    const int height=std::clamp(std::max(int(measured.bottom)+2,lines*int(metrics.tmHeight)+2),edit_min_height_,edit_max_height_);
    if(height!=current.bottom){
        const RECT bounds=edit_frame_->Arrange(edit_point_,{current.right,height},edit_work_,state_.toolbar.scale,state_.dark);
        SetWindowPos(edit_host_,nullptr,bounds.left,bounds.top,current.right,height,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(edit_,nullptr,0,0,current.right,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(edit_host_,nullptr,TRUE);
    }
    SendMessageW(edit_,EM_SCROLLCARET,0,0);
}

void Application::PaintTextEditor(HDC dc){
    using Microsoft::WRL::ComPtr;
    RECT area{};GetClientRect(edit_,&area);if(area.right<=0||area.bottom<=0)return;
    if(!edit_text_factory_)CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,edit_text_factory_.GetAddressOf())),"Create input renderer");
    if(!edit_text_writer_)CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(edit_text_writer_.GetAddressOf()))),"Create input layout factory");
    if(!edit_text_target_){auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));CheckWin32(SUCCEEDED(edit_text_factory_->CreateDCRenderTarget(&props,&edit_text_target_)),"Create input surface");}
    auto* target=edit_text_target_.Get();CheckWin32(SUCCEEDED(target->BindDC(dc,&area)),"Bind input surface");
    // Native EDIT positions/font heights are already physical pixels.
    target->SetDpi(96,96);target->BeginDraw();target->Clear(D2D1::ColorF(edit_surface_color_&0xffffff));
    try{
        ComPtr<IDWriteTextFormat> format;
        CheckWin32(SUCCEEDED(edit_text_writer_->CreateTextFormat(edit_family_.c_str(),nullptr,edit_bold_?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,float(std::max(1L,std::lround(edit_size_))),L"zh-CN",&format)),"Create input format");
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        TEXTMETRICW metrics{};const auto previous=SelectObject(dc,edit_font_);GetTextMetricsW(dc,&metrics);SelectObject(dc,previous);
        RECT native_clip{};SendMessageW(edit_,EM_GETRECT,0,reinterpret_cast<LPARAM>(&native_clip));
        const D2D1_RECT_F clip{float(native_clip.left),float(native_clip.top),float(native_clip.right),float(native_clip.bottom)};
        target->PushAxisAlignedClip(clip,D2D1_ANTIALIAS_MODE_ALIASED);
        // IME implementations may expose their transient preedit through EDIT.
        // Splice composition only into the snapshot captured before it started,
        // so a native preedit can never be rasterized twice.
        auto value=edit_composing_&&!edit_ime_text_.empty()?edit_ime_original_:EditorText(edit_);
        DWORD first{},last{};SendMessageW(edit_,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        ComPtr<ID2D1SolidColorBrush> brush;CheckWin32(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0x267aff),&brush)),"Create input selection brush");
        const bool single=edit_badge_label_;
        const int first_line=single?0:static_cast<int>(SendMessageW(edit_,EM_GETFIRSTVISIBLELINE,0,0));
        const int line_count=single?1:static_cast<int>(SendMessageW(edit_,EM_GETLINECOUNT,0,0));
        for(int line=first_line;line<line_count;++line){
            const int index=single?static_cast<int>(SendMessageW(edit_,EM_GETFIRSTVISIBLELINE,0,0)):static_cast<int>(SendMessageW(edit_,EM_LINEINDEX,line,0));
            if(index<0||size_t(index)>value.size())continue;
            const int length=single?static_cast<int>(value.size())-index:static_cast<int>(SendMessageW(edit_,EM_LINELENGTH,index,0));
            const auto position=SendMessageW(edit_,EM_POSFROMCHAR,index,0);
            const float x=position==-1?float(native_clip.left):float(GET_X_LPARAM(position));
            const float y=position==-1?float(native_clip.top):float(GET_Y_LPARAM(position));
            if(y>=clip.bottom)break;
            auto text=value.substr(static_cast<size_t>(index),static_cast<size_t>(length));
            const bool composition=edit_composing_&&!edit_ime_text_.empty()&&edit_ime_first_>=DWORD(index)&&edit_ime_first_<=DWORD(index+length);
            if(composition){const size_t begin=edit_ime_first_-index;const size_t removed=std::min(size_t(edit_ime_last_-edit_ime_first_),text.size()-begin);text.replace(begin,removed,edit_ime_text_);}
            const bool cue=single&&value.empty()&&!composition;
            if(cue)text=L"留空使用自动序号";
            if(text.empty())continue;
            ComPtr<IDWriteTextLayout> layout;
            CheckWin32(SUCCEEDED(edit_text_writer_->CreateGdiCompatibleTextLayout(text.data(),static_cast<UINT32>(text.size()),format.Get(),100000,float(metrics.tmHeight),1,nullptr,FALSE,&layout)),"Create native-compatible input layout");
            DWRITE_LINE_METRICS line_metrics{};UINT32 count{};layout->GetLineMetrics(&line_metrics,1,&count);
            const D2D1_POINT_2F origin{x,y+float(metrics.tmAscent)-line_metrics.baseline};
            std::vector<DWRITE_HIT_TEST_METRICS> selected;
            if(!composition&&GetFocus()==edit_&&first!=last){
                const DWORD a=std::max(first,DWORD(index)),b=std::min(last,DWORD(index+length));
                if(a<b){layout->HitTestTextRange(a-index,b-a,origin.x,origin.y,nullptr,0,&count);selected.resize(count);if(count)layout->HitTestTextRange(a-index,b-a,origin.x,origin.y,selected.data(),count,&count);}
                for(const auto& part:selected)target->FillRectangle({part.left,y,part.left+part.width,y+float(metrics.tmHeight)},brush.Get());
            }
            edit_text_glyphs_+=edit_text_renderer_.DrawLayout(target,edit_text_writer_.Get(),layout.Get(),origin,clip,D2D1::ColorF(cue?0x7e899a:edit_display_color_&0xffffff)).freetype_glyphs;
            for(const auto& part:selected)edit_text_renderer_.DrawLayout(target,edit_text_writer_.Get(),layout.Get(),origin,{part.left,y,part.left+part.width,y+float(metrics.tmHeight)},D2D1::ColorF(0xffffff));
            if(composition){
                layout->HitTestTextRange(edit_ime_first_-index,static_cast<UINT32>(edit_ime_text_.size()),origin.x,origin.y,nullptr,0,&count);std::vector<DWRITE_HIT_TEST_METRICS> parts(count);
                if(count)layout->HitTestTextRange(edit_ime_first_-index,static_cast<UINT32>(edit_ime_text_.size()),origin.x,origin.y,parts.data(),count,&count);
                for(const auto& part:parts)target->DrawLine({part.left,y+float(metrics.tmHeight)-1},{part.left+part.width,y+float(metrics.tmHeight)-1},brush.Get());
            }
        }
        target->PopAxisAlignedClip();
    }catch(...){target->EndDraw();edit_text_target_.Reset();throw;}
    const auto result=target->EndDraw();if(result==D2DERR_RECREATE_TARGET)edit_text_target_.Reset();
    CheckWin32(SUCCEEDED(result),"Paint input text");
}

LRESULT CALLBACK Application::TextHostProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,wp,lp);
    switch(message){
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps{};const HDC dc=BeginPaint(window,&ps);RECT rect{};GetClientRect(window,&rect);
        FillRect(dc,&rect,app->edit_backdrop_);
        EndPaint(window,&ps);return 0;
    }
    case WM_CTLCOLOREDIT:{
        const HDC dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,NativeColor(app->edit_surface_color_));SetBkColor(dc,NativeColor(app->edit_surface_color_));SetBkMode(dc,OPAQUE);
        return reinterpret_cast<LRESULT>(app->edit_backdrop_);
    }
    case WM_COMMAND:
        if(reinterpret_cast<HWND>(lp)!=app->edit_)break;
        if(HIWORD(wp)==EN_CHANGE)app->ResizeTextEditor();
        // Defer destruction until the native control has completed its focus
        // notification. IME candidate windows do not end the edit session.
        if(HIWORD(wp)==EN_KILLFOCUS)PostMessageW(window,kFinishText,0,0);
        return 0;
    case kFinishText:
        if(app->edit_&&GetFocus()!=app->edit_&&!app->edit_composing_)app->CommitText();
        return 0;
    case WM_CLOSE:app->CommitText();return 0;
    default:break;
    }
    return DefWindowProcW(window,message,wp,lp);
}

LRESULT CALLBACK Application::EditProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    auto* app=reinterpret_cast<Application*>(data);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT||message==WM_PRINTCLIENT||message==WM_PRINT){
        PAINTSTRUCT ps{};const HDC dc=message==WM_PAINT?BeginPaint(window,&ps):reinterpret_cast<HDC>(wp);
        try{app->PaintTextEditor(dc);}catch(...){RECT client{};GetClientRect(window,&client);FillRect(dc,&client,app->edit_backdrop_);}
        if(message==WM_PAINT)EndPaint(window,&ps);return 0;
    }
    if(message==WM_SETFOCUS){
        const auto result=DefSubclassProc(window,message,wp,lp);
        if(app->edit_caret_){POINT position{};GetCaretPos(&position);DestroyCaret();CreateCaret(window,app->edit_caret_,0,0);SetCaretPos(position.x,position.y);ShowCaret(window);}
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
        return result;
    }
    if(message==WM_IME_STARTCOMPOSITION){app->edit_composing_=true;app->edit_ime_text_.clear();app->edit_ime_original_=EditorText(window);SendMessageW(window,EM_GETSEL,reinterpret_cast<WPARAM>(&app->edit_ime_first_),reinterpret_cast<LPARAM>(&app->edit_ime_last_));}
    if(message==WM_IME_COMPOSITION){if(const HIMC context=ImmGetContext(window)){
        if(lp&GCS_COMPSTR){const LONG bytes=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0);if(bytes>=0&&bytes<=8192){app->edit_ime_text_.resize(static_cast<size_t>(bytes)/sizeof(wchar_t));if(bytes)ImmGetCompositionStringW(context,GCS_COMPSTR,app->edit_ime_text_.data(),static_cast<DWORD>(bytes));}}
        if(lp&GCS_RESULTSTR)app->edit_ime_text_.clear();ImmReleaseContext(window,context);
    }}
    if(message==WM_IME_ENDCOMPOSITION){app->edit_composing_=false;app->edit_ime_text_.clear();app->edit_ime_original_.clear();PostMessageW(app->edit_host_,kFinishText,0,0);}
    if(message==WM_KEYDOWN&&!app->edit_composing_){
        if(wp==VK_ESCAPE||(wp==VK_RETURN&&(app->edit_badge_label_||!(GetKeyState(VK_SHIFT)&0x8000)))){
            const HWND owner=app->edit_owner_;app->CommitText(wp==VK_ESCAPE);SetFocus(owner);return 0;
        }
        if(wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(window,EM_SETSEL,0,-1);return 0;}
    }
    if(message==WM_CHAR&&(wp==1||wp==10))return 0;
    if(message==WM_CHAR&&wp==VK_RETURN&&!app->edit_composing_&&!(GetKeyState(VK_SHIFT)&0x8000))return 0;
    // EDIT also paints synchronously while processing selection and input.
    // Suppress that GDI path, then repaint once with the current native layout.
    const bool changes=message==WM_TIMER||message==EM_SCROLL||message==WM_KILLFOCUS||message==WM_CHAR||message==WM_KEYDOWN||message==WM_KEYUP||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_LBUTTONDBLCLK||message==WM_MOUSEMOVE||message==WM_MOUSEWHEEL||message==WM_HSCROLL||message==WM_VSCROLL||message==WM_SETTEXT||message==WM_SETFONT||message==WM_SIZE||message==EM_SETSEL||message==EM_REPLACESEL||message==EM_SCROLLCARET||message==EM_LINESCROLL||message==WM_CUT||message==WM_PASTE||message==WM_CLEAR||message==WM_UNDO||message==EM_UNDO||message==WM_IME_STARTCOMPOSITION||message==WM_IME_COMPOSITION||message==WM_IME_ENDCOMPOSITION;
    const bool visible=changes&&IsWindowVisible(window)&&(message!=WM_MOUSEMOVE||(wp&MK_LBUTTON));
    if(visible)DefSubclassProc(window,WM_SETREDRAW,FALSE,0);
    const auto result=DefSubclassProc(window,message,wp,lp);
    if(visible&&IsWindow(window)){
        DefSubclassProc(window,WM_SETREDRAW,TRUE,0);
        // EDIT defers caret placement while redraw is disabled. Once its native
        // text/selection layout is current, restore the insertion caret too.
        if(GetFocus()==window){
            DefSubclassProc(window,EM_SCROLLCARET,0,0);
            RECT client{};GetClientRect(window,&client);
            if(client.right>0&&client.bottom>0){
                // EDIT updates its active-end caret while painting. Run that
                // native bookkeeping offscreen; these GDI pixels are never shown.
                if(!app->edit_native_surface_||app->edit_native_size_.cx!=client.right||app->edit_native_size_.cy!=client.bottom){app->edit_native_surface_=std::make_unique<DibSurface>(client.right,client.bottom);app->edit_native_size_={client.right,client.bottom};}
                DefSubclassProc(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(app->edit_native_surface_->Dc()),PRF_CLIENT);
            }
        }
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    }
    return result;
}

void Application::CommitText(bool cancel){
    if(!edit_&&!edit_host_&&!edit_initializing_)return;
    // Clicking the no-activate return button must finish the current IME phrase
    // before reading the control, just as leaving the native field would.
    if(!cancel&&edit_&&edit_composing_){
        if(const HIMC context=ImmGetContext(edit_)){ImmNotifyIME(context,NI_COMPOSITIONSTR,CPS_COMPLETE,0);ImmReleaseContext(edit_,context);}
    }
    const HWND editor=std::exchange(edit_,nullptr),host=std::exchange(edit_host_,nullptr);
    const auto text=editor?EditorText(editor):std::wstring{};
    RECT bounds{};
    if(editor){GetWindowRect(editor,&bounds);RemoveWindowSubclass(editor,EditProc,1);DestroyWindow(editor);}
    if(host)DestroyWindow(host);
    edit_frame_.reset();
    if(edit_caret_){DeleteObject(edit_caret_);edit_caret_=nullptr;}
    if(edit_font_){DeleteObject(edit_font_);edit_font_=nullptr;}
    if(edit_backdrop_){DeleteObject(edit_backdrop_);edit_backdrop_=nullptr;}
    edit_text_target_.Reset();edit_text_writer_.Reset();edit_text_factory_.Reset();edit_ime_text_.clear();edit_ime_original_.clear();
    edit_native_surface_.reset();edit_native_size_={};
    if(!cancel&&editor){
        if(edit_badge_label_){
            const auto label=NormalizeNumberLabel(text);
            if(edit_index_>=0&&static_cast<size_t>(edit_index_)<document_.marks.size()){
                auto& mark=document_.marks[edit_index_];const float scale=std::max(.1f,state_.toolbar.scale);
                auto properties=PropertiesOfMark(mark,scale,state_);properties.number_label=label;
                auto changed=WithMarkProperties(mark,properties,scale);if(changed!=mark){document_.Checkpoint();mark=std::move(changed);}document_.selected=edit_index_;
            }
            state_.number_label=label;RememberProperties();
        }else if(edit_index_>=0){
            auto& mark=document_.marks[edit_index_];
            if(text.empty()&&mark.tool==Tool::Text){
                document_.Checkpoint();
                document_.marks.erase(document_.marks.begin()+edit_index_);document_.selected=-1;
            }else{
                Mark updated=mark;updated.text=text;
                if(updated.tool==Tool::Text&&updated.text_auto_size){
                    // Use the wrapping width actually available in this edit
                    // session, including after a scaled object meets an edge.
                    updated.text_wrap_width=float(bounds.right-bounds.left);
                    FitTextBounds(updated);
                }
                if(updated!=mark){document_.Checkpoint();mark=std::move(updated);}
            }
            if(!text.empty())document_.selected=edit_index_;
        }else if(!text.empty()){
            Mark mark;mark.tool=Tool::Text;mark.a=edit_point_;mark.b={edit_point_.x+float(bounds.right-bounds.left),edit_point_.y+float(bounds.bottom-bounds.top)};
            if(IsTextTag(edit_background_)){const auto padding=TextTagMetrics(edit_size_);mark.a.x-=padding.x;mark.a.y-=padding.y;mark.b.x+=padding.x;mark.b.y+=padding.y;}
            mark.color=edit_color_;mark.font_size=edit_size_;mark.text=text;
            mark.text_bold=edit_bold_;mark.text_align=edit_align_;mark.text_background=edit_background_;
            mark.font_family=edit_family_;
            mark.text_auto_size=true;mark.text_wrap_width=float(bounds.right-bounds.left);FitTextBounds(mark);
            document_.Add(std::move(mark));
            document_.selected=static_cast<int>(document_.marks.size())-1;
        }
    }
    state_.hint=std::move(edit_previous_hint_);
    edit_index_=-1;edit_owner_=nullptr;edit_composing_=edit_initializing_=edit_badge_label_=false;
    if(!cancel)SyncSelectedProperties();
    Invalidate();
}
}
