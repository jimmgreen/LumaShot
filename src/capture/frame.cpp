#include "capture/frame.h"
#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace lumashot {

void CheckWin32(bool success, const char* operation) {
    if (!success) {
        const DWORD code = GetLastError();
        throw std::system_error(static_cast<int>(code ? code : ERROR_GEN_FAILURE),
            std::system_category(), operation);
    }
}

Frame MakeFrame(RECT bounds, uint32_t color) {
    const auto width = static_cast<int64_t>(bounds.right) - bounds.left;
    const auto height = static_cast<int64_t>(bounds.bottom) - bounds.top;
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768 ||
        width * height > 128 * 1024 * 1024) {
        throw std::invalid_argument("Invalid or excessively large capture bounds");
    }
    return {bounds, std::vector<uint32_t>(static_cast<size_t>(width * height), color)};
}

Frame Crop(const Frame& frame, RECT desktop_rect) {
    RECT clipped{};
    if (!IntersectRect(&clipped, &frame.bounds, &desktop_rect)) {
        throw std::invalid_argument("Selection does not intersect the captured desktop");
    }
    Frame result = MakeFrame(clipped);
    for (int y = 0; y < result.Height(); ++y) {
        const size_t source = static_cast<size_t>(clipped.top - frame.bounds.top + y) *
            frame.Width() + clipped.left - frame.bounds.left;
        std::copy_n(frame.pixels.data() + source, result.Width(),
            result.pixels.data() + static_cast<size_t>(y) * result.Width());
    }
    return result;
}

DibSurface::DibSurface(int width, int height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc_ = CreateCompatibleDC(nullptr);
    CheckWin32(dc_ != nullptr, "CreateCompatibleDC");
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS,
        reinterpret_cast<void**>(&pixels_), nullptr, 0);
    if (!bitmap_) {
        const DWORD error = GetLastError();
        DeleteDC(dc_);
        dc_ = nullptr;
        SetLastError(error);
        CheckWin32(false, "CreateDIBSection");
    }
    old_ = SelectObject(dc_, bitmap_);
    if (!old_ || old_ == HGDI_ERROR) {
        DeleteObject(bitmap_);
        DeleteDC(dc_);
        bitmap_ = nullptr;
        dc_ = nullptr;
        CheckWin32(false, "SelectObject");
    }
}

DibSurface::~DibSurface() {
    if (dc_ && old_) SelectObject(dc_, old_);
    if (bitmap_) DeleteObject(bitmap_);
    if (dc_) DeleteDC(dc_);
}

}
