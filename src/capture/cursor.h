#pragma once
#include "capture/frame.h"

namespace lumashot {

class FrozenCursor {
public:
    FrozenCursor() = default;
    ~FrozenCursor();
    FrozenCursor(FrozenCursor&& other) noexcept;
    FrozenCursor& operator=(FrozenCursor&& other) noexcept;
    FrozenCursor(const FrozenCursor&) = delete;
    FrozenCursor& operator=(const FrozenCursor&) = delete;
    static FrozenCursor Snapshot();
    static FrozenCursor Copy(HCURSOR cursor, POINT position);
    void Composite(Frame& frame) const;
    bool Visible() const noexcept { return icon_ != nullptr; }
    RECT Bounds() const noexcept;
private:
    HICON icon_{};
    POINT position_{};
    POINT hotspot_{};
    SIZE size_{};
};

}
