#pragma once
#include "capture/frame.h"
#include <memory>
#include <span>

namespace lumashot {
inline constexpr int kMagnifierSize=122;
RECT MagnifierBounds(RECT monitor,POINT pointer);
void MagnifierPixels(const Frame& frame,RECT monitor,POINT pointer,std::span<uint32_t> output);

// A small opaque layered window: moving the lens never presents the full-screen
// Direct2D target. Windows supplies the backdrop; no desktop pixels are cached.
class Magnifier {
public:
    ~Magnifier(){Close();}
    void Show(HWND owner,const Frame& frame,RECT monitor,POINT pointer);
    void Hide();
    void Close();
private:
    HWND window_{};
    std::unique_ptr<DibSurface> surface_;
};
}
