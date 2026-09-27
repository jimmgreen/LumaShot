#pragma once
#include "clipboard/preview_window.h"
namespace lumashot::clipboard {
struct PreviewWindowTest {
    static void ExpireIdle(PreviewWindow& p){SendMessageW(p.window_,WM_TIMER,PreviewWindow::IdleTimer,0);}
    static bool WaitHighlight(PreviewWindow& p){for(int i=0;i<2000;++i){p.ReceiveHighlight();if(p.highlight_.generation==p.highlight_generation_)return true;Sleep(1);}return false;}
    static CodeLanguage Language(const PreviewWindow& p){return p.highlight_.language;}
    static POINT Position(PreviewWindow& p,UINT32 position){FLOAT x{},y{};DWRITE_HIT_TEST_METRICS hit{};p.text_layout_->HitTestTextPosition(position,FALSE,&x,&y,&hit);return {static_cast<LONG>((p.text_box_.left+x+.1f)*p.scale_),static_cast<LONG>((p.text_box_.top+y+hit.height/2-p.text_scroll_)*p.scale_)};}
    static std::wstring Selection(const PreviewWindow& p){return p.SelectedText();}
    static uint64_t TextGlyphs(const PreviewWindow& p){return p.text_glyphs_;}
    static float Scroll(const PreviewWindow& p){return p.text_scroll_;}
    static float ScrollMax(const PreviewWindow& p){return p.text_scroll_max_;}
    static bool Loading(const PreviewWindow& p) { return p.loading_; }
    static bool HasImage(const PreviewWindow& p) { return p.content_.image != nullptr; }
    static const std::wstring& Error(const PreviewWindow& p) { return p.content_.error; }
    static const std::wstring& Text(const PreviewWindow& p) { return p.content_.text; }
    static const void* Renderer(const PreviewWindow& p) { return p.target_.Get(); }
    static const ClipboardComposition* Composition(const PreviewWindow& p){return p.composition_.get();}
    static HWND Window(const PreviewWindow& p) { return p.window_; }
    static bool RenderedImage(PreviewWindow& p) {
        p.Render();
        if (!p.image_bitmap_ || !p.surface_) return false;
        const auto color = p.surface_->Pixels()[(p.height_ / 2) * p.width_ + p.width_ / 2] & 0xffffff;
        return color == 0x4080a0;
    }
    static Frame Snapshot(PreviewWindow& p) {
        p.Render();
        auto frame = MakeFrame({0,0,p.width_,p.height_});
        std::copy_n(p.surface_->Pixels(),frame.pixels.size(),frame.pixels.begin());
        for(auto& pixel:frame.pixels){const auto a=pixel>>24;const uint32_t base=p.dark_?0x263142:0xf0f5fc;uint32_t out=0xff000000;for(int shift:{0,8,16})out|=std::min(255u,((pixel>>shift)&255)+((base>>shift)&255)*(255-a)/255)<<shift;pixel=out;}
        return frame;
    }
    static bool Released(const PreviewWindow& p) {
        return !p.window_ && !p.content_.image && p.content_.text.empty() && p.content_.error.empty()
            && !p.highlight_worker_ && !p.syntax_mask_ && !p.composition_ && !p.image_bitmap_ && !p.target_ && !p.surface_ && !p.factory_ && !p.writer_ && !p.text_layout_;
    }
};
}
