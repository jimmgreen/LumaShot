#include <stdexcept>
#include "clipboard/preview_window.h"
#include "ui/memory_target.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace lumashot::clipboard {

static void CheckWin32(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void PreviewWindow::ResetContent() {
    highlight_worker_.reset();highlight_={};syntax_mask_.Reset();language_menu_=false;++highlight_generation_;
    copy_action_={};composition_.reset();selection_anchor_=selection_end_=0;selecting_=false;
    image_bitmap_.Reset();
    text_layout_.Reset();
    title_font_.Reset();
    body_font_.Reset();
    meta_font_.Reset();
    brush_.Reset();
    target_.Reset();
    writer_.Reset();
    factory_.Reset();
    surface_.reset();
    width_ = height_ = 0;
    current_id_ = 0;
    content_ = {};text_scroll_=text_scroll_max_=0;
    loading_ = false;
}

void PreviewWindow::Close() {
    idle_release_pending_=false;
    if(window_)KillTimer(window_,IdleTimer);
    highlight_worker_.reset();
    composition_.reset();
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    ResetContent();
}

void PreviewWindow::Hide() {
    language_menu_=false;selecting_=false;if(GetCapture()==window_)ReleaseCapture();
    if (window_) {
        ShowWindow(window_, SW_HIDE);
        idle_release_pending_=SetTimer(window_,IdleTimer,IdleDelayMs,nullptr)!=0;
        if(!idle_release_pending_)Close();
    }
}

bool PreviewWindow::ForwardWheel(WPARAM key, LPARAM position) {
    if(!IsOpen()||kind_==Kind::Image)return false;
    RECT rect{};GetWindowRect(window_,&rect);const POINT point{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
    if(!PtInRect(&rect,point))return false;
    SendMessageW(window_,WM_MOUSEWHEEL,key,position);return true;
}

LRESULT CALLBACK PreviewWindow::Proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    auto* p = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        p = static_cast<PreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        p->window_ = w;
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    }
    if (!p) return DefWindowProcW(w, m, wp, lp);

    switch (m) {
    case WM_TIMER:
        if(wp==IdleTimer){if(p->idle_release_pending_&&!p->IsOpen())p->Close();return 0;}
        break;
    case HighlightWorker::Ready:p->ReceiveHighlight();return 0;
    case WM_NCCALCSIZE:if(wp)return 0;break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(w, &ps);
        try {
            p->Render();
            if (p->surface_) {
                if(!p->composition_)p->composition_=std::make_unique<ClipboardComposition>();
                p->composition_->Present(w,p->width_,p->height_,p->surface_->Pixels());
            }
        } catch (...) {
            EndPaint(w, &ps);
            return 0;
        }
        EndPaint(w, &ps);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if(p->kind_!=Kind::Image){p->text_scroll_=std::clamp(p->text_scroll_-GET_WHEEL_DELTA_WPARAM(wp)/float(WHEEL_DELTA)*54.f,0.f,p->text_scroll_max_);p->Invalidate();return 0;}break;
    case WM_SETCURSOR:if(p->language_menu_){SetCursor(LoadCursorW(nullptr,IDC_ARROW));return TRUE;}if(LOWORD(lp)==HTCLIENT&&p->text_layout_){POINT point{};GetCursorPos(&point);ScreenToClient(w,&point);const float y=point.y/p->scale_;if(y>=p->text_box_.top&&y<p->text_box_.bottom){SetCursor(LoadCursorW(nullptr,IDC_IBEAM));return TRUE;}}break;
    case WM_MOUSEACTIVATE:
        return p->kind_==Kind::Image?MA_NOACTIVATE:MA_ACTIVATE;
    case WM_KEYDOWN:
        if(p->kind_!=Kind::Image&&(wp==VK_NEXT||wp==VK_PRIOR||wp==VK_HOME||wp==VK_END)){
            if(wp==VK_HOME)p->text_scroll_=0;else if(wp==VK_END)p->text_scroll_=p->text_scroll_max_;else p->text_scroll_=std::clamp(p->text_scroll_+(wp==VK_NEXT?1.f:-1.f)*(p->text_box_.bottom-p->text_box_.top),0.f,p->text_scroll_max_);p->Invalidate();return 0;
        }
        if((GetKeyState(VK_CONTROL)&0x8000)&&wp=='A'){p->selection_anchor_=0;p->selection_end_=static_cast<UINT32>(p->content_.text.size());p->Invalidate();return 0;}
        if((GetKeyState(VK_CONTROL)&0x8000)&&wp=='C'){p->CopyText();return 0;}
        if(wp==VK_ESCAPE&&p->language_menu_){p->language_menu_=false;p->Invalidate();return 0;}
        if (wp == VK_ESCAPE || wp == VK_SPACE) {
            p->Hide();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:{
        const float x=GET_X_LPARAM(lp)/p->scale_,y=GET_Y_LPARAM(lp)/p->scale_,width=p->width_/p->scale_,height=p->height_/p->scale_;
        if(p->language_menu_){
            if(x>=width-200&&x<=width-48&&y>=40&&y<400){p->language_=static_cast<CodeLanguage>(static_cast<int>((y-40)/30));p->RequestHighlight();}
            p->language_menu_=false;p->Invalidate();return 0;
        }
        if(p->kind_==Kind::Text&&y<40&&x>=width-200&&x<width-48){p->language_menu_=true;p->Invalidate();return 0;}
        if(y<40&&x>width-44){p->Hide();return 0;}
        if(y>height-36&&x>width-100){p->CopyText();return 0;}
        if(p->text_layout_&&y>=p->text_box_.top&&y<p->text_box_.bottom){SetFocus(w);p->selection_end_=p->HitText(x,y);if(!(GetKeyState(VK_SHIFT)&0x8000))p->selection_anchor_=p->selection_end_;p->selecting_=true;SetCapture(w);p->Invalidate();return 0;}
        if(y<40){SendMessageW(w,WM_NCLBUTTONDOWN,HTCAPTION,lp);return 0;}return 0;
    }
    case WM_MOUSEMOVE:if(p->selecting_){const float y=GET_Y_LPARAM(lp)/p->scale_;if(y<p->text_box_.top)p->text_scroll_=std::max(0.f,p->text_scroll_-18);if(y>p->text_box_.bottom)p->text_scroll_=std::min(p->text_scroll_max_,p->text_scroll_+18);p->selection_end_=p->HitText(GET_X_LPARAM(lp)/p->scale_,std::clamp(y,p->text_box_.top,p->text_box_.bottom));p->Invalidate();}return 0;
    case WM_LBUTTONUP:p->selecting_=false;if(GetCapture()==w)ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:p->selecting_=false;return 0;
    case WM_CLOSE:
        p->Hide();
        return 0;
    case WM_NCDESTROY:
        p->window_ = nullptr;
        break;
    }
    return DefWindowProcW(w, m, wp, lp);
}

void PreviewWindow::Layout(RECT panel_rect, float scale) {
    scale_ = scale;

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&panel_rect, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT work = monitor.rcWork;
    int preview_w = std::max(1, std::min(static_cast<int>(540 * scale_), static_cast<int>(work.right - work.left - 8)));
    int preview_h = std::max(1, std::min(static_cast<int>(460 * scale_), static_cast<int>(work.bottom - work.top - 8)));
    if(content_.image){
        const float max_w=std::max(1.f,std::min(800.f*scale_,float(work.right-work.left-40))-32*scale_);
        const float max_h=std::max(1.f,std::min(720.f*scale_,float(work.bottom-work.top-40))-84*scale_);
        const float factor=std::min({scale_,max_w/content_.image->Width(),max_h/content_.image->Height()});
        preview_w=static_cast<int>(std::min(float(work.right-work.left-8),std::max(240*scale_,content_.image->Width()*factor+32*scale_)));
        preview_h=static_cast<int>(std::min(float(work.bottom-work.top-8),std::max(160*scale_,content_.image->Height()*factor+84*scale_)));
    }

    int x = panel_rect.left - preview_w - static_cast<int>(14 * scale_);
    if (x < work.left + 8) {
        x = panel_rect.right + static_cast<int>(14 * scale_);
    }
    x = std::clamp(x, static_cast<int>(work.left + 4), std::max(static_cast<int>(work.left + 4), static_cast<int>(work.right - preview_w - 4)));
    int y = panel_rect.top + (panel_rect.bottom - panel_rect.top - preview_h) / 2;
    y = std::clamp(y, static_cast<int>(work.top + 4), std::max(static_cast<int>(work.top + 4), static_cast<int>(work.bottom - preview_h - 4)));

    SetWindowPos(window_, HWND_TOPMOST, x, y, preview_w, preview_h, SWP_NOACTIVATE);


}

void PreviewWindow::Show(HWND owner, const Entry& entry, RECT panel_rect, bool dark, float scale, PreviewData data) {
    idle_release_pending_=false;
    if(window_)KillTimer(window_,IdleTimer);
    const bool update_backdrop=!window_||dark_!=dark;
    if(current_id_!=entry.id)language_=CodeLanguage::Auto;language_menu_=false;
    owner_ = owner;panel_rect_=panel_rect;selection_anchor_=selection_end_=0;selecting_=false;
    dark_ = dark;
    scale_ = scale;
    current_id_ = entry.id;text_scroll_=text_scroll_max_=0;
    kind_ = entry.kind;
    if (content_.image != data.image) image_bitmap_.Reset();
    content_ = std::move(data);
    loading_ = !content_.image && content_.text.empty() && content_.error.empty();
    text_layout_.Reset();

    if (!window_) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"LumaShot.QuickLook";
        RegisterClassExW(&wc);

        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName,
                                 L"LumaShot 预览", WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,
                                 0, 0, 10, 10, owner_, nullptr, wc.hInstance, this);
        if (!window_) return;
        const BOOL yes = TRUE;
        DwmSetWindowAttribute(window_, DWMWA_TRANSITIONS_FORCEDISABLED, &yes, sizeof(yes));
    }

    if(update_backdrop){
    const DWORD corner=2,border=0xfffffffe,backdrop=3;const BOOL night=dark_;
    DwmSetWindowAttribute(window_,33,&corner,sizeof(corner));DwmSetWindowAttribute(window_,34,&border,sizeof(border));
    DwmSetWindowAttribute(window_,20,&night,sizeof(night));acrylic_=SUCCEEDED(DwmSetWindowAttribute(window_,38,&backdrop,sizeof(backdrop)));
    const MARGINS margins{-1};DwmExtendFrameIntoClientArea(window_,&margins);
    }
    Layout(panel_rect, scale);
    RequestHighlight();
    Invalidate();
    // Populate the backing surface before exposing a newly created/reused popup.
    UpdateWindow(window_);
    ShowWindow(window_, SW_SHOWNOACTIVATE);
}

void PreviewWindow::SetContent(uint64_t id, PreviewData data) {
    if (!IsOpen() || current_id_ != id) return;
    content_ = std::move(data);text_scroll_=text_scroll_max_=0;
    loading_ = false;RequestHighlight();selection_anchor_=selection_end_=0;Layout(panel_rect_,scale_);
    image_bitmap_.Reset();
    text_layout_.Reset();
    Invalidate();
}

void PreviewWindow::RequestHighlight(){
    highlight_={};++highlight_generation_;syntax_mask_.Reset();
    if(kind_!=Kind::Text||content_.text.empty())return;
    if(!highlight_worker_)highlight_worker_=std::make_unique<HighlightWorker>(window_);
    highlight_worker_->Request(content_.text,language_,highlight_generation_);
}
void PreviewWindow::ReceiveHighlight(){
    if(!highlight_worker_)return;auto result=highlight_worker_->Take();if(result&&result->generation==highlight_generation_){highlight_=std::move(*result);Invalidate();}
}
void PreviewWindow::DrawSyntax(D2D1_RECT_F bounds,D2D1_COLOR_F ink){
    // Rasterize the original layout once with LumaText. Recolour its coverage
    // mask by token rectangles; glyph shaping and selection geometry stay shared.
    if(!syntax_mask_)CheckWin32(SUCCEEDED(target_->CreateCompatibleRenderTarget(D2D1::SizeF(width_/scale_,height_/scale_),&syntax_mask_)),"Code glyph mask");
    syntax_mask_->BeginDraw();syntax_mask_->Clear(D2D1::ColorF(0,0.f));
    text_glyphs_+=text_renderer_.DrawLayout(syntax_mask_.Get(),writer_.Get(),text_layout_.Get(),{bounds.left,bounds.top-text_scroll_},bounds,D2D1::ColorF(0xffffff)).freetype_glyphs;
    CheckWin32(SUCCEEDED(syntax_mask_->EndDraw()),"Code glyph rendering");ComPtr<ID2D1Bitmap> mask;syntax_mask_->GetBitmap(&mask);
    static constexpr UINT32 light[]={0x243042,0x7131aa,0x9e3c18,0x34734a,0x096a91,0x075c9f,0x596479,0x965800};
    static constexpr UINT32 dark[]={0xe6edf8,0xd2a8ff,0xffbf96,0x8fcb99,0x82d8ed,0x91caff,0xb6c4d9,0xf0cc85};
    const auto old=target_->GetAntialiasMode();target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    const UINT32 first=HitText(bounds.left,bounds.top),last=HitText(bounds.right,bounds.bottom);
    std::vector<DWRITE_HIT_TEST_METRICS> parts;
    for(const auto& span:highlight_.spans){if(span.start+span.length<first||span.start>last)continue;UINT32 count{};
        text_layout_->HitTestTextRange(span.start,span.length,bounds.left,bounds.top-text_scroll_,nullptr,0,&count);parts.resize(count);if(!count)continue;
        text_layout_->HitTestTextRange(span.start,span.length,bounds.left,bounds.top-text_scroll_,parts.data(),count,&count);
        brush_->SetColor(span.color==CodeColor::Text?ink:D2D1::ColorF((dark_?dark:light)[static_cast<size_t>(span.color)]));
        for(const auto& part:parts){D2D1_RECT_F rect{std::max(bounds.left,part.left),std::max(bounds.top,part.top),std::min(bounds.right,part.left+part.width),std::min(bounds.bottom,part.top+part.height)};if(rect.right>rect.left&&rect.bottom>rect.top)target_->FillOpacityMask(mask.Get(),brush_.Get(),D2D1_OPACITY_MASK_CONTENT_GRAPHICS,&rect,&rect);}
    }
    target_->SetAntialiasMode(old);
}

UINT32 PreviewWindow::HitText(float x,float y){
    if(!text_layout_)return 0;BOOL trailing{},inside{};DWRITE_HIT_TEST_METRICS hit{};
    text_layout_->HitTestPoint(x-text_box_.left,y-text_box_.top+text_scroll_,&trailing,&inside,&hit);
    return std::min(static_cast<UINT32>(content_.text.size()),hit.textPosition+(trailing?hit.length:0));
}
std::wstring PreviewWindow::SelectedText() const{
    const auto first=std::min(selection_anchor_,selection_end_),last=std::max(selection_anchor_,selection_end_);
    return first==last?content_.text:content_.text.substr(first,last-first);
}
void PreviewWindow::CopyText(){
    if(selection_anchor_==selection_end_&&copy_action_){copy_action_();return;}
    const auto text=SelectedText();if(text.empty())return;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));if(!memory)return;
    void* bytes=GlobalLock(memory);if(!bytes){GlobalFree(memory);return;}memcpy(bytes,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);
    if(OpenClipboard(window_)){if(EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory))memory=nullptr;CloseClipboard();}if(memory)GlobalFree(memory);
}

void PreviewWindow::DrawText(const wchar_t* text,UINT32 length,IDWriteTextFormat* format,D2D1_RECT_F bounds,ID2D1SolidColorBrush* brush,D2D1_DRAW_TEXT_OPTIONS){
    text_glyphs_+=text_renderer_.Draw(target_.Get(),writer_.Get(),std::wstring_view(text,length),format,bounds,brush->GetColor()).freetype_glyphs;
}

void PreviewWindow::Render() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    if (!client.right || !client.bottom) return;

    if (!surface_ || width_ != client.right || height_ != client.bottom) {
        width_ = client.right;
        height_ = client.bottom;
        syntax_mask_.Reset();
        text_layout_.Reset();
        // D2D draws in place into the presented DIB; a resized DIB needs a new
        // target. The decoded image moves over without another upload.
        auto surface = std::make_unique<DibSurface>(width_, height_);
        if (factory_) {
            auto target = CreateMemoryRenderTarget(factory_.Get(), surface->Pixels(), width_, height_, width_);
            ShareBitmap(target.Get(), image_bitmap_);
            brush_.Reset();
            CheckWin32(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0), &brush_)), "QuickLook brush");
            target_ = std::move(target);
        }
        surface_ = std::move(surface);
    }

    if (!factory_) {
        CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf())), "QuickLook factory");
    }
    if (!writer_) {
        CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(writer_.GetAddressOf()))), "QuickLook writer");
    }
    if (!target_) {
        target_ = CreateMemoryRenderTarget(factory_.Get(), surface_->Pixels(), width_, height_, width_);
        CheckWin32(SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_)), "QuickLook brush");
    }

    target_->SetDpi(96 * scale_, 96 * scale_);
    target_->BeginDraw();
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    const float W = width_ / scale_;
    const float H = height_ / scale_;

    const UINT32 bg = dark_ ? 0x1b2432 : 0xf8fafd;
    const UINT32 ink = dark_ ? 0xe6edf8 : 0x243042;
    const UINT32 sub = dark_ ? 0x8fa2bc : 0x76879e;
    target_->Clear(D2D1::ColorF(bg,acrylic_?.12f:1.f));

    if (!title_font_) {
        writer_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.f, L"zh-CN", &title_font_);
        if (title_font_) title_font_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (!meta_font_) {
        writer_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.f, L"zh-CN", &meta_font_);
        if (meta_font_) meta_font_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (!body_font_) {
        writer_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &body_font_);
    }

    std::wstring title=kind_==Kind::Image?L"图片预览":kind_==Kind::Files?L"文件列表":highlight_.language!=CodeLanguage::Plain?L"代码预览":L"文本预览";
    brush_->SetColor(D2D1::ColorF(ink));
    DrawText(title.data(),static_cast<UINT32>(title.size()),title_font_.Get(),{20,8,W-50,36},brush_.Get());
    brush_->SetColor(D2D1::ColorF(sub));
    target_->DrawLine({W-29,16},{W-19,26},brush_.Get(),1.2f);target_->DrawLine({W-19,16},{W-29,26},brush_.Get(),1.2f);
    if(kind_==Kind::Text){const std::wstring label=language_==CodeLanguage::Auto?std::wstring(L"自动 · ")+LanguageName(highlight_.language):LanguageName(language_);DrawText(label.data(),static_cast<UINT32>(label.size()),meta_font_.Get(),{W-194,8,W-58,36},brush_.Get());target_->DrawLine({W-64,19},{W-60,23},brush_.Get());target_->DrawLine({W-60,23},{W-56,19},brush_.Get());}
    const D2D1_RECT_F body_rect{16,44,W-16,H-40};

    if (loading_ || !content_.error.empty()) {
        const std::wstring message = loading_ ? L"正在加载预览…" : content_.error;
        brush_->SetColor(D2D1::ColorF(sub));
        if (title_font_) DrawText(message.data(), static_cast<UINT32>(message.size()),
            title_font_.Get(), {28, 64, W - 28, H - 54}, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    } else if (kind_ == Kind::Image) {
        if (!image_bitmap_ && content_.image) {
            const auto& frame = *content_.image;
            const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            target_->CreateBitmap(D2D1::SizeU(frame.Width(), frame.Height()), frame.pixels.data(),
                frame.Width() * 4, props, &image_bitmap_);
        }
        if (image_bitmap_) {
            const auto size = image_bitmap_->GetSize();
            const float max_w = body_rect.right - body_rect.left;
            const float max_h = body_rect.bottom - body_rect.top;
            const float factor = std::min(max_w / size.width, max_h / size.height);
            const float draw_w = size.width * factor;
            const float draw_h = size.height * factor;
            const float cx = (body_rect.left + body_rect.right) / 2.f;
            const float cy = (body_rect.top + body_rect.bottom) / 2.f;
            const D2D1_RECT_F img_rect{cx - draw_w / 2.f, cy - draw_h / 2.f, cx + draw_w / 2.f, cy + draw_h / 2.f};
            target_->DrawBitmap(image_bitmap_.Get(), img_rect, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            brush_->SetColor(D2D1::ColorF(sub));
            std::wstring loading = L"图片显示失败，请关闭后重试";
            DrawText(loading.data(), static_cast<UINT32>(loading.size()), title_font_.Get(), body_rect, brush_.Get());
        }
    } else {
        const auto& full_text = content_.text;
        brush_->SetColor(D2D1::ColorF(ink));
        const D2D1_RECT_F text_box{body_rect.left + 4, body_rect.top + 4, body_rect.right - 4, body_rect.bottom - 4};text_box_=text_box;
        if (!text_layout_ && writer_ && body_font_) {
            writer_->CreateTextLayout(full_text.data(), static_cast<UINT32>(std::min(full_text.size(), (kind_==Kind::Files?PreviewData::MaxFileChars:PreviewData::MaxTextChars + 1))),
                                      body_font_.Get(), text_box.right - text_box.left, 2000000.f, &text_layout_);
        }
        if (text_layout_) {
            DWRITE_TEXT_METRICS metrics{};text_layout_->GetMetrics(&metrics);text_scroll_max_=std::max(0.f,metrics.height-(text_box.bottom-text_box.top));text_scroll_=std::min(text_scroll_,text_scroll_max_);
            target_->PushAxisAlignedClip(text_box,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const UINT32 first=std::min(selection_anchor_,selection_end_),length=std::max(selection_anchor_,selection_end_)-first;
            if(length){UINT32 count{};text_layout_->HitTestTextRange(first,length,text_box.left,text_box.top-text_scroll_,nullptr,0,&count);std::vector<DWRITE_HIT_TEST_METRICS> parts(count);if(count)text_layout_->HitTestTextRange(first,length,text_box.left,text_box.top-text_scroll_,parts.data(),count,&count);brush_->SetColor(D2D1::ColorF(0x3984ff,.32f));for(const auto& part:parts)target_->FillRectangle({part.left,part.top,part.left+part.width,part.top+part.height},brush_.Get());brush_->SetColor(D2D1::ColorF(ink));}
            if(!highlight_.spans.empty()&&highlight_.language!=CodeLanguage::Plain)DrawSyntax(text_box,brush_->GetColor());
            else text_glyphs_+=text_renderer_.DrawLayout(target_.Get(),writer_.Get(),text_layout_.Get(),{text_box.left,text_box.top-text_scroll_},text_box,brush_->GetColor()).freetype_glyphs;
            target_->PopAxisAlignedClip();
            if(text_scroll_max_>0){const float track=text_box.bottom-text_box.top,thumb=std::max(24.f,track*track/(track+text_scroll_max_)),top=text_box.top+(track-thumb)*text_scroll_/text_scroll_max_;brush_->SetColor(D2D1::ColorF(sub,.45f));target_->FillRoundedRectangle(D2D1::RoundedRect({W-10,top,W-7,top+thumb},1.5f,1.5f),brush_.Get());}
        }
    }

    brush_->SetColor(D2D1::ColorF(sub));
    const std::wstring hint=content_.truncated?L"空格 / Esc 关闭 · 内容过长，显示前 65536 字符":L"空格 / Esc 关闭";
    DrawText(hint.data(),static_cast<UINT32>(hint.size()),meta_font_.Get(),{20,H-32,W-120,H-8},brush_.Get());
    if(copy_action_||!content_.text.empty()){
        const std::wstring copy=selection_anchor_!=selection_end_?L"复制选中":L"复制";
        brush_->SetColor(D2D1::ColorF(0x267aff));DrawText(copy.data(),static_cast<UINT32>(copy.size()),meta_font_.Get(),{W-90,H-32,W-16,H-8},brush_.Get());
    }

    if(language_menu_){brush_->SetColor(D2D1::ColorF(dark_?0x263244:0xf5f8fc));target_->FillRoundedRectangle(D2D1::RoundedRect({W-200,40,W-48,400},6,6),brush_.Get());for(int i=0;i<12;++i){const auto language=static_cast<CodeLanguage>(i);if(language==language_){brush_->SetColor(D2D1::ColorF(0x267aff,.18f));target_->FillRectangle({W-198,40.f+i*30,W-50,70.f+i*30},brush_.Get());}brush_->SetColor(D2D1::ColorF(ink));const std::wstring name=LanguageName(language);DrawText(name.data(),static_cast<UINT32>(name.size()),meta_font_.Get(),{W-188,40.f+i*30,W-58,70.f+i*30},brush_.Get());}}
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        syntax_mask_.Reset();
        image_bitmap_.Reset();
        brush_.Reset();
        target_.Reset();
        Invalidate();
    }
}

} // namespace lumashot::clipboard
