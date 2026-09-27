#pragma once
#include "model/document.h"
#include <span>
namespace lumashot {
enum class LineSnapKind { None, Endpoint, Midpoint, Nearest, Horizontal, Vertical, Diagonal };
struct LineSnap {
    Point point{},origin{};
    LineSnapKind kind{LineSnapKind::None};
};
// Physical coordinates; the magnetic radius is eight DIP. Priority: endpoints, midpoints, segment projections, then directions.
LineSnap SnapLinePoint(Point pointer,std::optional<Point> anchor,const Document& document,
    std::span<const Point> vertices,Box bounds,float scale,bool enabled);
}
