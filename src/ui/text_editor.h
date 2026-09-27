#pragma once
#include "ui/render.h"
#include <functional>

namespace lumashot {
// The alpha-composited frame and the native text control use separate HWNDs.
// Native editing/IME stays intact without putting GDI inside a flip-model window.
class TextEditorFrame {
public:
    ~TextEditorFrame();
    void Create(HWND owner,std::function<void()> confirm,bool single_line=false);
    RECT Arrange(Point anchor,SIZE text_size,RECT work,float scale,bool dark);
    void Show();
    HWND Window()const{return window_;}
    RECT TextRect()const{return text_;}
    RECT ButtonRect()const{return button_;}
    Frame Snapshot()const;
    static uint32_t Background(bool dark){return dark?0xff252f3cu:0xfff4f9ffu;}
private:
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
    void Paint();
    HWND window_{};
    RECT bounds_{},text_{},button_{};
    float scale_{1};bool dark_{},hover_{},pressed_{},single_line_{};
    std::function<void()> confirm_;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1DCRenderTarget> target_;
    TextRenderer text_renderer_;
    std::unique_ptr<DibSurface> surface_;
};
}
