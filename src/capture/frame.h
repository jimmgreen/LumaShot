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
