#pragma once
#include "clipboard/liquid_motion.h"
#include "capture/frame.h"
#include <memory>

namespace lumashot::clipboard {
struct LiquidFrame {
    LiquidPose pose;
    RECT viewport{},work{};
    float scale{1},opacity{.96f},detail{1};
    // Physical pixels when specified: DWM uses monitor DPI even when the
    // panel content is scaled down to fit a small work area.
    float expanded_radius_pixels{-1};
    D2D1_POINT_2F pull{};
    bool dark{},dock{true},native_clip{};
};
// Renders only a vector shell and cached content. No text shaping, screen capture,
// timers, input hooks or HWND ownership; all calls stay on the panel's UI thread.
class LiquidSurface {
public:
    LiquidSurface();
    ~LiquidSurface();
    LiquidSurface(const LiquidSurface&)=delete;
    LiquidSurface& operator=(const LiquidSurface&)=delete;
    void Cache(bool expanded,int width,int height,const uint32_t* pixels);
    void ClearExpanded();
    void Render(const LiquidFrame& frame);
    bool Contains(POINT local)const;
    HRGN CreateInputRegion()const; // Caller transfers ownership to SetWindowRgn, or deletes it.
    const uint32_t* Pixels()const;
    int Width()const;
    int Height()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
