#pragma once
#include "clipboard/preview_data.h"
#include "clipboard/composition.h"
#include "clipboard/syntax_highlight.h"
#include "capture/frame.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <memory>
#include "ui/text_renderer.h"
#include <string>

namespace lumashot::clipboard {
using Microsoft::WRL::ComPtr;

class PreviewWindow {
public:
    PreviewWindow() = default;
    ~PreviewWindow() { Close(); }

    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;

    bool IsOpen() const { return window_ && IsWindowVisible(window_); }
    bool OwnsWindow(HWND window) const { return window && window == window_; }
    uint64_t CurrentId() const { return current_id_; }

    void Show(HWND owner, const Entry& entry, RECT panel_rect, bool dark, float scale, PreviewData data = {});
    void SetCopyAction(std::function<void()> action){copy_action_=std::move(action);}
    void Hide();
    bool ForwardWheel(WPARAM key, LPARAM position);
    void SetContent(uint64_t id, PreviewData data);
    void Close();
    void Invalidate() { if (window_) InvalidateRect(window_, nullptr, FALSE); }

private:
    friend struct PreviewWindowTest;
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Layout(RECT panel_rect, float scale);
    void Render();
    void DrawText(const wchar_t* text,UINT32 length,IDWriteTextFormat* format,D2D1_RECT_F bounds,ID2D1SolidColorBrush* brush,D2D1_DRAW_TEXT_OPTIONS options=D2D1_DRAW_TEXT_OPTIONS_CLIP);
    TextRenderer text_renderer_;uint64_t text_glyphs_{};
    static constexpr UINT_PTR IdleTimer = 1;
    static constexpr UINT IdleDelayMs = 10000;
    bool idle_release_pending_{};
    void ResetContent();
    void RequestHighlight();
    void ReceiveHighlight();
    void DrawSyntax(D2D1_RECT_F bounds,D2D1_COLOR_F ink);
    std::unique_ptr<HighlightWorker> highlight_worker_;
    HighlightResult highlight_;
    CodeLanguage language_{CodeLanguage::Auto};
    uint64_t highlight_generation_{};
    bool language_menu_{};
    ComPtr<ID2D1BitmapRenderTarget> syntax_mask_;
    UINT32 HitText(float x,float y);
    std::wstring SelectedText() const;
    void CopyText();
    std::unique_ptr<ClipboardComposition> composition_;
    std::function<void()> copy_action_;
    RECT panel_rect_{};
    bool acrylic_{},selecting_{};
    UINT32 selection_anchor_{},selection_end_{};
    D2D1_RECT_F text_box_{};

    HWND window_{};
    HWND owner_{};
    bool dark_{};
    float scale_{1.f};
    uint64_t current_id_{};
    Kind kind_{};
    PreviewData content_;
    bool loading_{};float text_scroll_{},text_scroll_max_{};

    std::unique_ptr<lumashot::DibSurface> surface_;
    int width_{}, height_{};
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> writer_;
    ComPtr<ID2D1RenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1Bitmap> image_bitmap_;
    ComPtr<IDWriteTextFormat> title_font_;
    ComPtr<IDWriteTextFormat> body_font_;
    ComPtr<IDWriteTextFormat> meta_font_;
    ComPtr<IDWriteTextLayout> text_layout_;
};

} // namespace lumashot::clipboard
