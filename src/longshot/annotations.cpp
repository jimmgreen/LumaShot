#include "longshot/annotations.h"
#include <algorithm>
#include <cmath>

namespace lumashot::longshot {
namespace {
void Grow(Box& box, Point p) {
    box.left = std::min(box.left, p.x);
    box.top = std::min(box.top, p.y);
    box.right = std::max(box.right, p.x);
    box.bottom = std::max(box.bottom, p.y);
}
}

Box PaintBounds(const Mark& mark) {
    Box box = Bounds(mark);
    if (mark.tool == Tool::Number) {
        Grow(box, mark.number_target);
        if (mark.number_detail) {
            Grow(box, {mark.number_detail->left, mark.number_detail->top});
            Grow(box, {mark.number_detail->right, mark.number_detail->bottom});
        }
        if (mark.number_leader) for (const Point& p : *mark.number_leader) Grow(box, p);
    }
    const float pad = std::max({32.f, mark.width * 3, mark.arrow_size * 2, mark.number_size, mark.mosaic_brush});
    return {box.left - pad, box.top - pad, box.right + pad, box.bottom + pad};
}

bool Touches(const Mark& mark, RECT region) {
    const Box b = PaintBounds(mark);
    return b.right > static_cast<float>(region.left) && b.left < static_cast<float>(region.right) &&
        b.bottom > static_cast<float>(region.top) && b.top < static_cast<float>(region.bottom);
}

Slice TakeMarks(const std::vector<Mark>& marks, RECT slice) {
    Slice result;
    const Point offset{-static_cast<float>(slice.left), -static_cast<float>(slice.top)};
    for (size_t i = 0; i < marks.size(); ++i) {
        // Only marks whose own shape overlaps the slice are handed to the
        // editor; the padding above is for painting, not for picking.
        const Box own = Bounds(marks[i]);
        Box pick = own;
        if (marks[i].tool == Tool::Number) {
            Grow(pick, marks[i].number_target);
            if (marks[i].number_detail) {
                Grow(pick, {marks[i].number_detail->left, marks[i].number_detail->top});
                Grow(pick, {marks[i].number_detail->right, marks[i].number_detail->bottom});
            }
        }
        if (pick.right < static_cast<float>(slice.left) || pick.left > static_cast<float>(slice.right) ||
            pick.bottom < static_cast<float>(slice.top) || pick.top > static_cast<float>(slice.bottom)) continue;
        Mark local = marks[i];
        Translate(local, offset);
        result.local.marks.push_back(std::move(local));
        result.taken.push_back(i);
    }
    return result;
}

std::vector<Mark> MergeMarks(const std::vector<Mark>& marks, const std::vector<size_t>& taken, const Document& edited, RECT slice) {
    std::vector<Mark> result;
    result.reserve(marks.size() + edited.marks.size());
    for (size_t i = 0; i < marks.size(); ++i)
        if (std::find(taken.begin(), taken.end(), i) == taken.end()) result.push_back(marks[i]);
    const Point offset{static_cast<float>(slice.left), static_cast<float>(slice.top)};
    for (Mark mark : edited.marks) {
        Translate(mark, offset);
        result.push_back(std::move(mark));
    }
    return result;
}

Frame FlattenRegion(Renderer& renderer, const Frame& image, const std::vector<Mark>& marks, RECT region, int band_rows) {
    RECT bounds{0, 0, image.Width(), image.Height()};
    IntersectRect(&region, &region, &bounds);
    Frame output = Crop(image, region);
    output.bounds = {0, 0, output.Width(), output.Height()};
    if (marks.empty() || output.pixels.empty()) return output;
    const int width = output.Width();
    band_rows = std::max(256, band_rows);
    for (LONG top = region.top; top < region.bottom; top += band_rows) {
        const RECT band{region.left, top, region.right, std::min<LONG>(region.bottom, top + band_rows)};
        Document subset;
        for (const Mark& mark : marks) if (Touches(mark, band)) subset.marks.push_back(mark);
        if (subset.marks.empty()) continue;
        const Frame rendered = renderer.Flatten(image, subset, band);
        for (int y = 0; y < rendered.Height(); ++y)
            std::copy_n(rendered.pixels.data() + static_cast<size_t>(y) * rendered.Width(), std::min(width, rendered.Width()),
                output.pixels.data() + static_cast<size_t>(band.top - region.top + y) * width);
    }
    return output;
}
}
