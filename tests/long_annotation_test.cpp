// Long-image annotations (v2): marks move in and out of an editing slice in
// long-image pixels, and band-by-band composition matches a single render.
// Synthetic images only.
#include "longshot/annotations.h"
#include <windows.h>
#include <iostream>
#include <string>

using namespace lumashot;
using namespace lumashot::longshot;

namespace {
int failures = 0;
void Expect(bool condition, const std::string& name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!condition) ++failures;
}

Mark Rect(float x0, float y0, float x1, float y1, uint32_t color) {
    Mark mark;
    mark.tool = Tool::Rectangle;
    mark.a = {x0, y0};
    mark.b = {x1, y1};
    mark.color = color;
    mark.width = 6;
    return mark;
}

Frame Pattern(int width, int height) {
    Frame frame = MakeFrame({0, 0, width, height});
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint32_t v = static_cast<uint32_t>((x * 7 + y * 3) & 255), w = static_cast<uint32_t>((x ^ y) & 255);
            frame.pixels[static_cast<size_t>(y) * width + x] = 0xff000000u | (v << 16) | (w << 8) | ((v + w) & 255);
        }
    return frame;
}

// Same pixels in `part` as in `whole` at (dx, dy).
bool Matches(const Frame& whole, const Frame& part, int dx, int dy, int* mismatches = nullptr) {
    int bad = 0;
    for (int y = 0; y < part.Height(); ++y)
        for (int x = 0; x < part.Width(); ++x)
            bad += part.pixels[static_cast<size_t>(y) * part.Width() + x] != whole.pixels[static_cast<size_t>(y + dy) * whole.Width() + x + dx];
    if (mismatches) *mismatches = bad;
    return bad == 0;
}
}

int main() {
    std::cout << std::unitbuf;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // ---- Slice in / merge out.
    const std::vector<Mark> marks{
        Rect(100, 1200, 300, 1300, 0xffff0000),   // inside the slice
        Rect(100, 5000, 300, 5100, 0xff00ff00),   // far below
        Rect(100, 1700, 300, 1900, 0xff0000ff),   // crosses the slice bottom
    };
    const RECT slice{0, 1000, 800, 1800};
    Slice taken = TakeMarks(marks, slice);
    Expect(taken.taken == std::vector<size_t>({0, 2}), "marks overlapping the slice are handed to the editor");
    Expect(taken.local.marks.size() == 2 && taken.local.marks[0].a == Point{100, 200} && taken.local.marks[1].b == Point{300, 900},
        "handed marks are in slice coordinates");

    Document edited = taken.local;
    Translate(edited.marks[0], {10, -20});               // moved
    edited.marks.erase(edited.marks.begin() + 1);         // deleted
    edited.marks.push_back(Rect(400, 50, 500, 150, 0xffffff00));  // added
    const auto merged = MergeMarks(marks, taken.taken, edited, slice);
    Expect(merged.size() == 3, "merge keeps untouched marks and applies edits");
    Expect(merged[0].color == 0xff00ff00 && merged[0].a == Point{100, 5000}, "untouched mark keeps its position");
    Expect(merged[1].a == Point{110, 1180} && merged[2].a == Point{400, 1050}, "edited marks return in long-image coordinates");
    Expect(MergeMarks(marks, taken.taken, taken.local, slice).size() == marks.size(), "an unchanged edit keeps every mark");

    // ---- Band-by-band composition (the renderer must go before CoUninitialize).
    {
    const Frame image = Pattern(640, 9000);
    Renderer renderer;
    const RECT whole{0, 0, 640, 9000};
    const Frame plain = FlattenRegion(renderer, image, {}, whole);
    Expect(plain.Width() == 640 && plain.Height() == 9000 && plain.pixels == image.pixels, "without marks the region is a plain crop");

    std::vector<Mark> crossing{Rect(80, 3990, 560, 4200, 0xffe5484d)};
    Mark mosaic;
    mosaic.tool = Tool::Mosaic;
    mosaic.mosaic_method = MosaicMethod::Rectangle;
    mosaic.a = {120, 8100};
    mosaic.b = {520, 8400};
    mosaic.mosaic_cell = 12;
    crossing.push_back(mosaic);
    Mark arrow;
    arrow.tool = Tool::Arrow;
    arrow.a = {100, 300};
    arrow.b = {500, 700};
    arrow.width = 5;
    crossing.push_back(arrow);

    const Frame banded = FlattenRegion(renderer, image, crossing, whole, 256);   // many seams
    const Frame coarse = FlattenRegion(renderer, image, crossing, whole);         // default 4096 bands
    Expect(banded.Height() == 9000 && banded.pixels != image.pixels, "marks are composited into the long image");
    int mismatches = 0;
    Expect(Matches(coarse, banded, 0, 0, &mismatches), "band size does not change the result (mismatches: " + std::to_string(mismatches) + ")");

    // A single render of a small window around the band seam must agree.
    const RECT window{0, 3900, 640, 4300};
    Document around;
    around.marks = crossing;
    const Frame direct = renderer.Flatten(image, around, window);
    Expect(Matches(banded, direct, 0, 3900, &mismatches), "seam at row 4096 is invisible (mismatches: " + std::to_string(mismatches) + ")");
    const RECT mosaic_window{0, 8000, 640, 8500};
    const Frame direct_mosaic = renderer.Flatten(image, around, mosaic_window);
    Expect(Matches(banded, direct_mosaic, 0, 8000, &mismatches), "mosaic reads the full long image across bands (mismatches: " + std::to_string(mismatches) + ")");

    // Cropped export (trim / scrollbar columns and crop rows) keeps the marks in place.
    const RECT crop{40, 3000, 600, 5000};
    const Frame cropped = FlattenRegion(renderer, image, crossing, crop);
    Expect(cropped.Width() == 560 && cropped.Height() == 2000, "cropped export has the crop size");
    int crop_bad = 0;
    for (int y = 0; y < cropped.Height(); ++y)
        for (int x = 0; x < cropped.Width(); ++x)
            crop_bad += cropped.pixels[static_cast<size_t>(y) * 560 + x] != banded.pixels[static_cast<size_t>(y + 3000) * 640 + x + 40];
    Expect(crop_bad == 0, "cropped export equals the same area of the full export (mismatches: " + std::to_string(crop_bad) + ")");
    }

    if (SUCCEEDED(com)) CoUninitialize();
    std::cout << (failures ? "FAILED " : "OK ") << failures << '\n';
    return failures ? 1 : 0;
}
