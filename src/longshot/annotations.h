#pragma once
#include "ui/render.h"
#include <vector>

namespace lumashot::longshot {
// Long-image annotations are kept apart from the pixels (non-destructive) in
// long-image coordinates. Editing happens one visible slice at a time in the
// regular screenshot editor; these helpers move marks in and out of a slice
// and compose the final pixels band by band (D2D targets stay small).

// Area a mark can paint, generously padded for strokes, arrow heads, shadows,
// number leaders and detail boxes.
Box PaintBounds(const Mark& mark);
bool Touches(const Mark& mark, RECT region);

struct Slice {
    Document local;               // marks in slice coordinates (0,0 = slice top-left)
    std::vector<size_t> taken;    // indices into the long-image marks
};
// Marks that touch the slice, translated into slice coordinates.
Slice TakeMarks(const std::vector<Mark>& marks, RECT slice);
// Replaces the taken marks with the edited slice marks (translated back).
// Untouched marks keep their order; edited marks are drawn above them.
std::vector<Mark> MergeMarks(const std::vector<Mark>& marks, const std::vector<size_t>& taken,
    const Document& edited, RECT slice);

// Pixels of region with every mark that touches it composited, rendered in
// bands of at most band_rows rows. Without marks this is a plain crop.
Frame FlattenRegion(Renderer& renderer, const Frame& image, const std::vector<Mark>& marks, RECT region, int band_rows = 4096);
}
