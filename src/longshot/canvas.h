#pragma once
#include "capture/frame.h"
#include "ui/controls.h"
#include "ui/text_renderer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>

namespace lumashot::longshot {
using Microsoft::WRL::ComPtr;

inline D2D1_RECT_F Rect(Box b){return D2D1::RectF(b.left,b.top,b.right,b.bottom);}
inline D2D1_COLOR_F Color(uint32_t argb){return D2D1::ColorF(argb&0xffffff,float(argb>>24)/255.f);}
inline bool Inside(Box b,Point p){return p.x>=b.left&&p.x<b.right&&p.y>=b.top&&p.y<b.bottom;}
inline float Scale(HWND window){return window?float(GetDpiForWindow(window))/96.f:float(GetDpiForSystem())/96.f;}
// Whether a physical-pixel region contains a point.
inline bool Contains(RECT r,POINT p){return p.x>=r.left&&p.x<r.right&&p.y>=r.top&&p.y<r.bottom;}
// Icon ids used only by the long-capture windows.
enum class Glyph { Pause, Play, Undo, Close, Check, Auto, Manual, Speed, Recapture, Ocr, Pin, Copy, Save,
    Minimize, Maximize, Restore, Warning, Info, Seams, Scrollbar, Trim, Fit, Actual };

// Per-window drawing state: device-independent resources, cached text formats
// and the shared TextRenderer. All coordinates are physical pixels; sizes in
// DIP are multiplied by scale.
class Painter {
public:
    Painter();
    ID2D1Factory* Factory()const{return factory_.Get();}
    IDWriteFactory* Write()const{return write_.Get();}
    void Begin(ID2D1RenderTarget* target,float scale,bool dark);
    void End(){target_=nullptr;brush_.Reset();}
    ID2D1RenderTarget* Target()const{return target_;}
    float S()const{return scale_;}
    bool Dark()const{return dark_;}
    ui::Theme Theme()const{return {scale_,dark_};}
    ID2D1SolidColorBrush* Brush(uint32_t argb);
    // size in DIP.
    void Text(std::wstring_view text,Box bounds,float size,uint32_t color,bool bold=false,
        DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING,DWRITE_PARAGRAPH_ALIGNMENT vertical=DWRITE_PARAGRAPH_ALIGNMENT_NEAR,bool wrap=false);
    Point Measure(std::wstring_view text,float size,bool bold=false);
    void Icon(Glyph glyph,Box bounds,uint32_t color);
    void Fill(Box b,uint32_t color,float radius=0);
    void Stroke(Box b,uint32_t color,float radius=0,float width=1);
    void Line(Point a,Point b,uint32_t color,float width=1);
    // Surface used by acrylic windows (glass) or opaque fallback.
    void Panel(Box b,bool acrylic,float radius_dip=14);
    // Whole-window surface for DWM-rounded layered windows: DWM draws the
    // corners and border; acrylic keeps a single alpha quantum for hit testing.
    void Backdrop(Box b,bool acrylic);
    ui::PaintContext Context(bool acrylic);
private:
    IDWriteTextFormat* Format(float px,bool bold,DWRITE_TEXT_ALIGNMENT align,DWRITE_PARAGRAPH_ALIGNMENT vertical,bool wrap);
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> write_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ID2D1RenderTarget* target_{};
    float scale_{1};bool dark_{};
    TextRenderer text_;
    std::map<std::tuple<int,bool,int,int,bool>,ComPtr<IDWriteTextFormat>> formats_;
};

// Software D2D target bound to a DIB and pushed with UpdateLayeredWindow.
// Used for the small always-on-top session windows (per-pixel alpha, no
// swap chain and no render loop).
class LayeredCanvas {
public:
    void Paint(HWND window,Painter& painter,RECT screen,float scale,bool dark,const std::function<void()>& draw);
    void Reset(){target_.Reset();surface_.reset();size_={};}
private:
    ComPtr<ID2D1DCRenderTarget> target_;
    std::unique_ptr<DibSurface> surface_;
    SIZE size_{};
};

// Area-averaged reduction that keeps the aspect ratio; never upscales.
Frame Downscale(const Frame& source,int max_width,int max_height);
std::wstring Thousands(long long value);
}
