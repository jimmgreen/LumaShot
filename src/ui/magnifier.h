#pragma once
#include "capture/frame.h"
#include <memory>
#include <optional>
#include <span>

namespace lumashot {
inline constexpr int kMagnifierSize=122;
RECT MagnifierBounds(RECT monitor,POINT pointer,SIZE size={kMagnifierSize,kMagnifierSize});
void MagnifierPixels(const Frame& frame,RECT monitor,POINT pointer,std::span<uint32_t> output);

// A small opaque layered window: moving the lens never presents the full-screen
// Direct2D target. Windows supplies the backdrop; no desktop pixels are cached.
class Magnifier {
public:
    Magnifier();
    ~Magnifier();
    // Creates the hidden window and its first layered frame while the capture
    // overlay is still being prepared, so the first visible hover only moves it.
    void Prepare(HWND owner,const Frame& frame,RECT monitor,POINT pointer,std::optional<uint32_t> color={});
    void Show(HWND owner,const Frame& frame,RECT monitor,POINT pointer,std::optional<uint32_t> color={});
    void Hide();
    void Close();
private:
    friend struct MagnifierTest;
    struct Readout;
    void Update(HWND owner,const Frame& frame,RECT monitor,POINT pointer,std::optional<uint32_t> color);
    std::unique_ptr<Readout> readout_;
    SIZE size_{};
    HWND window_{};
    std::unique_ptr<DibSurface> surface_;
};
}
