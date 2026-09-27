#include "capture/cursor.h"
#include <algorithm>
#include <utility>

namespace lumashot {

FrozenCursor::~FrozenCursor() {
    if (icon_) DestroyIcon(icon_);
}

FrozenCursor::FrozenCursor(FrozenCursor&& other) noexcept {
    *this = std::move(other);
}

FrozenCursor& FrozenCursor::operator=(FrozenCursor&& other) noexcept {
    if (this != &other) {
        if (icon_) DestroyIcon(icon_);
        icon_ = std::exchange(other.icon_, nullptr);
        position_ = other.position_;
        hotspot_ = other.hotspot_;
        size_ = other.size_;
    }
    return *this;
}

FrozenCursor FrozenCursor::Snapshot() {
    CURSORINFO info{sizeof(CURSORINFO)};
    CheckWin32(GetCursorInfo(&info) != FALSE, "GetCursorInfo");
    if (!(info.flags & CURSOR_SHOWING) || !info.hCursor) return {};
    return Copy(info.hCursor, info.ptScreenPos);
}

FrozenCursor FrozenCursor::Copy(HCURSOR cursor, POINT position) {
    FrozenCursor result;
    result.icon_ = CopyIcon(cursor);
    CheckWin32(result.icon_ != nullptr, "CopyIcon");
    ICONINFO info{};
    CheckWin32(GetIconInfo(result.icon_, &info) != FALSE, "GetIconInfo");
    BITMAP bitmap{};
    const bool valid = GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask,
        sizeof(bitmap), &bitmap) != 0;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    CheckWin32(valid, "GetObject cursor bitmap");
    result.position_ = position;
    result.hotspot_ = {static_cast<LONG>(info.xHotspot), static_cast<LONG>(info.yHotspot)};
    result.size_ = {bitmap.bmWidth, info.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2};
    return result;
}

RECT FrozenCursor::Bounds() const noexcept {
    const LONG x = position_.x - hotspot_.x;
    const LONG y = position_.y - hotspot_.y;
    return {x, y, x + size_.cx, y + size_.cy};
}

void FrozenCursor::Composite(Frame& frame) const {
    if (!icon_) return;
    const RECT bounds = Bounds();
    RECT intersection{};
    if (!IntersectRect(&intersection, &bounds, &frame.bounds)) return;
    // Draw against the captured background, not transparent pixels: monochrome
    // cursors use AND/XOR masks and can invert the pixels beneath their shape.
    const int width=intersection.right-intersection.left,height=intersection.bottom-intersection.top;
    DibSurface surface(width,height);
    const size_t offset=static_cast<size_t>(intersection.top-frame.bounds.top)*frame.Width()+intersection.left-frame.bounds.left;
    for(int row=0;row<height;++row)
        std::copy_n(frame.pixels.data()+offset+static_cast<size_t>(row)*frame.Width(),width,
            surface.Pixels()+static_cast<size_t>(row)*width);
    CheckWin32(DrawIconEx(surface.Dc(), bounds.left - intersection.left,
        bounds.top - intersection.top, icon_, size_.cx, size_.cy,
        0, nullptr, DI_NORMAL | DI_NOMIRROR) != FALSE, "DrawIconEx");
    GdiFlush();
    // GDI does not preserve the alpha byte for every mask operation.
    // CaptureDesktop already makes the untouched background opaque.
    for(int row=0;row<height;++row) {
        const auto source=surface.Pixels()+static_cast<size_t>(row)*width;
        std::transform(source,source+width,frame.pixels.data()+offset+static_cast<size_t>(row)*frame.Width(),
            [](uint32_t pixel){return pixel|0xff000000;});
    }
}

}
