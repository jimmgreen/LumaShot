#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace lumashot {

struct Frame {
    RECT bounds{};
    std::vector<uint32_t> pixels;
    int Width() const noexcept { return bounds.right - bounds.left; }
    int Height() const noexcept { return bounds.bottom - bounds.top; }
};

// Non-owning view of a rectangle of pixels; stride is in pixels. Lets export
// code encode a crop without copying it first.
struct PixelView {
    const uint32_t* pixels{};
    int width{}, height{};
    size_t stride{};
    const uint32_t* Row(int y) const noexcept { return pixels + static_cast<size_t>(y) * stride; }
    bool Contiguous() const noexcept { return stride == static_cast<size_t>(width); }
};
inline PixelView PixelsOf(const Frame& frame) noexcept {
    return {frame.pixels.data(), frame.Width(), frame.Height(), static_cast<size_t>(frame.Width())};
}
// rect is in image pixels (0,0 = top-left of the frame) and must lie inside it.
inline PixelView PixelsOf(const Frame& frame, RECT rect) noexcept {
    return {frame.pixels.data() + static_cast<size_t>(rect.top) * frame.Width() + rect.left,
        static_cast<int>(rect.right - rect.left), static_cast<int>(rect.bottom - rect.top), static_cast<size_t>(frame.Width())};
}

Frame MakeFrame(RECT bounds, uint32_t color = 0xff000000);
Frame Crop(const Frame& frame, RECT desktop_rect);
void CheckWin32(bool success, const char* operation);

class DibSurface {
public:
    DibSurface(int width, int height);
    ~DibSurface();
    DibSurface(const DibSurface&) = delete;
    DibSurface& operator=(const DibSurface&) = delete;
    HDC Dc() const noexcept { return dc_; }
    uint32_t* Pixels() const noexcept { return pixels_; }
private:
    HDC dc_{};
    HBITMAP bitmap_{};
    HGDIOBJ old_{};
    uint32_t* pixels_{};
};

}
