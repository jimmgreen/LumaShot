#include "capture/desktop.h"
#include <algorithm>

namespace lumashot {

Frame CaptureRegion(RECT region) {
    Frame result = MakeFrame(region);
    const LONG x = region.left, y = region.top;
    DibSurface surface(result.Width(), result.Height());
    HDC screen = GetDC(nullptr);
    CheckWin32(screen != nullptr, "GetDC desktop");
    const bool copied = BitBlt(surface.Dc(), 0, 0, result.Width(), result.Height(),
        screen, x, y, SRCCOPY | CAPTUREBLT) != FALSE;
    const DWORD error = GetLastError();
    ReleaseDC(nullptr, screen);
    SetLastError(error);
    CheckWin32(copied, "BitBlt desktop");
    GdiFlush();
    std::transform(surface.Pixels(), surface.Pixels() + result.pixels.size(),
        result.pixels.begin(), [](uint32_t pixel) { return pixel | 0xff000000; });
    return result;
}

Frame CaptureDesktop() {
    const LONG x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const LONG y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return CaptureRegion({x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        y + GetSystemMetrics(SM_CYVIRTUALSCREEN)});
}

}
