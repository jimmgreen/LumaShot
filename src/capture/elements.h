#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace lumashot {
struct ElementWindow { HWND window{}; RECT bounds{}; };
struct ElementRegion { size_t window_index{}; RECT bounds{}; };
struct ElementScanResult { uint64_t generation{}; std::vector<ElementRegion> regions; };
// Window geometry is frozen when a capture starts and re-checked after the scan so
// regions from a window that moved are discarded. Fullscreen/kiosk windows (e.g.
// Chromium) nudge their frame by a pixel once the topmost capture overlay covers
// them; treat that jitter as the same position instead of dropping every region.
inline constexpr LONG ElementGeometryTolerance = 2;
inline bool SameElementGeometry(const RECT& frozen, const RECT& current) {
    auto within = [](LONG a, LONG b) { return (a > b ? a - b : b - a) <= ElementGeometryTolerance; };
    return within(frozen.left, current.left) && within(frozen.top, current.top) &&
           within(frozen.right, current.right) && within(frozen.bottom, current.bottom);
}
std::optional<RECT> HitTestElements(POINT point, std::span<const ElementWindow> windows,
                                   std::span<const ElementRegion> regions);
class ElementScanner {
public:
    ElementScanner();
    ~ElementScanner();
    ElementScanner(const ElementScanner&) = delete;
    ElementScanner& operator=(const ElementScanner&) = delete;
    void Start(HWND notify, UINT message, uint64_t generation,
               std::vector<ElementWindow> windows, POINT pointer);
    void Cancel();
    std::optional<ElementScanResult> Take();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
