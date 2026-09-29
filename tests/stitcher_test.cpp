// Long-capture stitcher tests. Every image is synthetic: a generated "page"
// is viewed through a scrolling viewport with a sticky header, a sticky
// footer and a scrollbar whose thumb moves against the content.
#include "capture/stitcher.h"
#include <cstdint>
#include <iostream>
#include <vector>

using namespace lumashot;
static int failures{};

static void Expect(bool condition, const char* name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!condition) ++failures;
}

namespace {
struct Random {
    uint32_t state;
    uint32_t Next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    int Range(int n) { return static_cast<int>(Next() % static_cast<uint32_t>(n)); }
};

constexpr uint32_t Paper = 0xffffffff;

struct Page {
    int width{}, height{};
    std::vector<uint32_t> pixels;
    uint32_t& At(int x, int y) { return pixels[static_cast<size_t>(y) * width + x]; }
    uint32_t At(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
};

// Text-like lines, paragraph gaps, a gradient "photo" and an optional block of
// identical table rows (repetitive content that makes naive matching ambiguous).
// degenerate: table cells carry no text, so the rows repeat exactly.
Page MakePage(int width, int height, uint32_t seed, bool table = false, int blank_tail = 0, bool degenerate = false) {
    Page page{width, height, std::vector<uint32_t>(static_cast<size_t>(width) * height, Paper)};
    Random random{seed};
    int y = 12;
    const int content_end = height - blank_tail;
    while (y + 20 < content_end) {
        const int kind = random.Range(10);
        if (kind == 0 && y + 120 < content_end) {
            for (int r = 0; r < 110; ++r)
                for (int x = 20; x < width - 20; ++x)
                    page.At(x, y + r) = 0xff000000 | static_cast<uint32_t>(((x * 255 / width) << 16) | ((r * 2) << 8) | ((y / 7) & 255));
            y += 126;
        } else if (table && kind == 1 && y + 200 < content_end) {
            for (int r = 0; r < 190; ++r) {
                const bool rule = r % 19 == 0;
                for (int x = 16; x < width - 16; ++x)
                    page.At(x, y + r) = rule ? 0xffd0d4dc : ((x / 40) % 2 ? 0xfff4f6fa : Paper);
            }
            for (int cell = 0; cell < 10 && !degenerate; ++cell) {
                const int x0 = 24 + random.Range(width / 2), len = 10 + random.Range(60);
                for (int r = 6; r < 14; ++r)
                    for (int c = 0; c < len; ++c)
                        if ((c + r + static_cast<int>(random.Next() & 1)) % 3) page.At(x0 + c, y + cell * 19 + r) = 0xff303640;
            }
            y += 200;
        } else {
            int x = 20 + random.Range(12);
            const int line_end = width - 24 - random.Range(width / 3);
            while (x < line_end) {
                const int word = 6 + random.Range(38);
                for (int r = 3; r < 14; ++r)
                    for (int c = 0; c < word && x + c < line_end; ++c)
                        if ((c * 7 + r * 3 + static_cast<int>(random.Next() & 3)) % 5)
                            page.At(x + c, y + r) = 0xff000000 | (0x202020u + static_cast<uint32_t>(random.Range(0x30)) * 0x010101u);
                x += word + 6;
            }
            y += kind == 9 ? 40 : 20;
        }
    }
    return page;
}

struct View {
    int width{420}, header{48}, footer{36}, viewport{500}, scrollbar{14};
    int Height() const { return header + viewport + footer; }
};

Frame Shot(const Page& page, const View& view, int offset) {
    Frame frame = MakeFrame({0, 0, view.width, view.Height()}, Paper);
    auto at = [&](int x, int y) -> uint32_t& { return frame.pixels[static_cast<size_t>(y) * view.width + x]; };
    const int content = view.width - view.scrollbar;
    for (int y = 0; y < view.header; ++y)
        for (int x = 0; x < content; ++x)
            at(x, y) = (y == view.header - 1) ? 0xffc8ccd4 : ((x / 30 + y / 12) % 3 ? 0xff2d3a4f : 0xff3d6fd6);
    for (int y = 0; y < view.viewport; ++y)
        for (int x = 0; x < content; ++x)
            at(x, view.header + y) = page.At(x, offset + y);
    for (int y = 0; y < view.footer; ++y)
        for (int x = 0; x < content; ++x)
            at(x, view.header + view.viewport + y) = y == 0 ? 0xffc8ccd4 : ((x / 50) % 2 ? 0xffeef1f6 : 0xffe0e5ee);
    const int track = view.Height();
    const int thumb = std::max(24, track * view.viewport / page.height);
    const int max_offset = page.height - view.viewport;
    const int thumb_top = max_offset > 0 ? (track - thumb) * offset / max_offset : 0;
    for (int y = 0; y < track; ++y)
        for (int x = content; x < view.width; ++x)
            at(x, y) = (y >= thumb_top && y < thumb_top + thumb) ? 0xff9aa3b2 : 0xfff0f1f3;
    return frame;
}

// Header + page[0, last + viewport) + footer, content columns only.
bool MatchesPage(const Frame& composed, const Page& page, const View& view, int last_offset, int* first_bad = nullptr) {
    const int content = view.width - view.scrollbar;
    const int expected_height = view.header + last_offset + view.viewport + view.footer;
    if (composed.Height() != expected_height || composed.Width() < content) {
        if (first_bad) *first_bad = -1;
        return false;
    }
    const Frame first = Shot(page, view, 0);
    const Frame last = Shot(page, view, last_offset);
    for (int y = 0; y < expected_height; ++y) {
        for (int x = 0; x < content; ++x) {
            uint32_t want;
            if (y < view.header) want = first.pixels[static_cast<size_t>(y) * view.width + x];
            else if (y >= expected_height - view.footer)
                want = last.pixels[static_cast<size_t>(y - expected_height + view.Height()) * view.width + x];
            else want = page.At(x, y - view.header);
            if (composed.pixels[static_cast<size_t>(y) * composed.Width() + x] != want) {
                if (first_bad) *first_bad = y;
                return false;
            }
        }
    }
    return true;
}

int RunScroll(Stitcher& stitcher, const Page& page, const View& view, const std::vector<int>& steps, bool* all_appended) {
    int offset = 0;
    *all_appended = stitcher.Add(Shot(page, view, 0)).status == StitchStatus::First;
    const int max_offset = page.height - view.viewport;
    for (size_t i = 0; offset < max_offset; ++i) {
        const int next = std::min(max_offset, offset + steps[i % steps.size()]);
        const auto result = stitcher.Add(Shot(page, view, next));
        if (result.status != StitchStatus::Appended || result.shift != next - offset) {
            std::cout << "  step " << i << " status=" << static_cast<int>(result.status) << " shift=" << result.shift
                      << " want=" << next - offset << " match=" << result.match << '\n';
            *all_appended = false;
        }
        offset = next;
    }
    return offset;
}
}

static void CheckSteadyScroll() {
    const View view;
    const Page page = MakePage(view.width, 4200, 7);
    Stitcher stitcher;
    bool appended = false;
    const int last = RunScroll(stitcher, page, view, {137, 251, 311, 90, 400, 283, 17, 350}, &appended);
    Expect(appended, "every scroll step is detected with its exact shift");
    Expect(stitcher.Add(Shot(page, view, last)).status == StitchStatus::Unchanged, "bottom of page reports Unchanged");
    Expect(stitcher.ScrollbarColumns() == view.scrollbar, "moving scrollbar thumb is detected and measured");
    Expect(stitcher.FooterRows() >= view.footer && stitcher.HeaderRows() >= view.header, "sticky header and footer are recognised");
    int bad = 0;
    const Frame composed = stitcher.Compose(true);
    Expect(composed.Width() == view.width - view.scrollbar, "compose crops the scrollbar");
    Expect(MatchesPage(composed, page, view, last, &bad), "composed image equals header + full page + footer");
    if (bad) std::cout << "  first mismatching row " << bad << '\n';
    Expect(stitcher.Height() == view.header + last + view.viewport + view.footer, "height counts header and footer once");
    Expect(static_cast<int>(stitcher.Seams().size()) == stitcher.Segments() - 1, "one seam per appended segment");
}

static void CheckRepetitiveContent() {
    const View view;
    const Page page = MakePage(view.width, 3600, 11, true);
    Stitcher stitcher;
    bool appended = false;
    const int last = RunScroll(stitcher, page, view, {190, 95, 380, 19, 228}, &appended);
    Expect(appended, "table rows with a 19px rhythm do not confuse the shift");
    Expect(MatchesPage(stitcher.Compose(true), page, view, last), "table page composes exactly");
    // Rows that repeat exactly are ambiguous; a steady scroll step resolves it.
    const Page periodic = MakePage(view.width, 3600, 11, true, 0, true);
    Stitcher steady;
    const int periodic_last = RunScroll(steady, periodic, view, {228}, &appended);
    Expect(appended, "identical repeating rows follow the steady scroll step");
    Expect(MatchesPage(steady.Compose(true), periodic, view, periodic_last), "identical repeating rows compose exactly");
}

static void CheckGapAndUndo() {
    const View view;
    const Page page = MakePage(view.width, 3000, 23);
    Stitcher stitcher;
    stitcher.Add(Shot(page, view, 0));
    Expect(stitcher.Add(Shot(page, view, 300)).status == StitchStatus::Appended, "first scroll appends");
    const int height = stitcher.Height();
    const Frame before = stitcher.Compose(false);
    Expect(stitcher.Add(Shot(page, view, 300 + view.viewport + 40)).status == StitchStatus::NoOverlap,
        "a jump larger than the viewport is refused");
    Expect(stitcher.Height() == height, "refused frame leaves the image untouched");
    Expect(stitcher.Add(Shot(page, view, 520)).status == StitchStatus::Appended, "stitching resumes from the last good frame");
    Expect(stitcher.Undo() && stitcher.Height() == height, "undo removes the last segment");
    Expect(stitcher.Compose(false).pixels == before.pixels, "undo restores the previous pixels exactly");
    Expect(stitcher.Add(Shot(page, view, 520)).status == StitchStatus::Appended, "after undo the same frame appends again");
    Expect(stitcher.Undo() && stitcher.Undo() && stitcher.Height() == view.Height() && !stitcher.CanUndo(),
        "undo back to the first frame");
    Expect(stitcher.Compose(false).pixels == Shot(page, view, 0).pixels, "first frame is restored intact");
}

static void CheckAnimatedRegion() {
    const View view;
    Page page = MakePage(view.width, 2600, 31);
    Stitcher stitcher;
    Random random{99};
    auto shot = [&](int offset) {
        // An animated image on the page (GIF/video) changes between captures.
        for (int y = 400; y < 430; ++y)
            for (int x = 40; x < 240; ++x) page.At(x, y) = 0xff000000 | (random.Next() & 0xffffff);
        return Shot(page, view, offset);
    };
    stitcher.Add(shot(0));
    const auto a = stitcher.Add(shot(260));
    const auto b = stitcher.Add(shot(560));
    Expect(a.status == StitchStatus::Appended && a.shift == 260 && b.status == StitchStatus::Appended && b.shift == 300,
        "an animated image on the page does not break matching");
}

static void CheckRenderingNoise() {
    const View view;
    const Page page = MakePage(view.width, 2400, 41);
    Stitcher stitcher;
    Random random{5};
    auto noisy = [&](int offset) {
        Frame frame = Shot(page, view, offset);
        for (auto& p : frame.pixels) {
            if (random.Range(3)) continue;
            const uint32_t b = p & 255;
            p = (p & 0xffffff00) | (b > 0 ? b - 1 : 1);
        }
        return frame;
    };
    stitcher.Add(noisy(0));
    const auto result = stitcher.Add(noisy(333));
    Expect(result.status == StitchStatus::Appended && result.shift == 333,
        "per-pixel rendering noise falls back to profile matching");
}

static void CheckLowMatchAndForce() {
    const View view;
    const Page page = MakePage(view.width, 2400, 51);
    Stitcher stitcher;
    stitcher.Add(Shot(page, view, 0));
    Frame changed = Shot(page, view, 200);
    // Re-layout of most rows (e.g. lazy images popping in) after the scroll.
    for (int y = view.header; y < view.header + 260; ++y)
        for (int x = 0; x < 180; ++x)
            changed.pixels[static_cast<size_t>(y) * view.width + x] ^= 0x00302010;
    const auto result = stitcher.Add(changed);
    Expect(result.status == StitchStatus::LowMatch && stitcher.HasPending(), "heavily changed overlap asks for confirmation");
    Expect(result.shift == 200, "low-match candidate still carries the best shift");
    const int height = stitcher.Height();
    const auto forced = stitcher.AcceptPending();
    Expect(forced.status == StitchStatus::Appended && stitcher.Height() == height + 200 - stitcher.FooterRows() + stitcher.FooterRows(),
        "accepting the candidate appends it");
}

static void CheckHeightLimit() {
    const View view;
    const Page page = MakePage(view.width, 3000, 61);
    StitchOptions options;
    options.max_height = 1500;
    Stitcher stitcher(options);
    stitcher.Add(Shot(page, view, 0));
    StitchStatus status{};
    for (int offset = 300; offset < 2400; offset += 300) {
        status = stitcher.Add(Shot(page, view, offset)).status;
        if (status != StitchStatus::Appended) break;
    }
    Expect(status == StitchStatus::Limit && stitcher.Height() == 1500, "stitching stops exactly at the height limit");
    Expect(stitcher.Add(Shot(page, view, 2400)).status == StitchStatus::Limit, "frames after the limit are refused");
}

static void CheckBlankTail() {
    const View view;
    const Page page = MakePage(view.width, 2200, 71, false, 700);
    Stitcher stitcher;
    bool appended = false;
    const int last = RunScroll(stitcher, page, view, {300}, &appended);
    (void)last;
    // Scrolling within blank paper cannot be told apart from not scrolling, so
    // the capture ends at the last frame that still showed content (1500);
    // everything up to there must be exact.
    Expect(!appended, "scrolling inside blank paper is reported as unchanged");
    Expect(MatchesPage(stitcher.Compose(true), page, view, 1500), "content before a long blank tail is exact");
}

static void CheckMismatchedFrame() {
    const View view;
    const Page page = MakePage(view.width, 1200, 81);
    Stitcher stitcher;
    stitcher.Add(Shot(page, view, 0));
    Expect(stitcher.Add(MakeFrame({0, 0, 100, 100})).status == StitchStatus::Mismatch, "frames of a different size are rejected");
}

static uint64_t Hash(const Frame& frame) {
    uint64_t h = 1469598103934665603ull ^ static_cast<uint64_t>(frame.Width()) ^ (static_cast<uint64_t>(frame.Height()) << 32);
    for (uint32_t p : frame.pixels) h = (h ^ p) * 1099511628211ull;
    return h;
}

// Undo history stores deltas against the content: every undo must restore
// the exact previous state (image and matching frame), across content block
// boundaries and after in-place repaints that differ from the content.
static void CheckDeltaUndo() {
    const View view;
    Page page = MakePage(view.width, 12000, 91, true);
    Stitcher stitcher;
    Random random{17};
    uint32_t badge = 0;
    auto shot = [&](int offset, bool repaint) {
        // Late repaint without scrolling: a header badge and the footer text
        // change (and stay changed), an animated area in the content flickers.
        if (repaint) ++badge;
        Frame frame = Shot(page, view, offset);
        for (int y = 10; y < 14 && badge; ++y) for (int x = 20; x < 90; ++x) frame.pixels[static_cast<size_t>(y) * view.width + x] = 0xff100000u * badge + static_cast<uint32_t>(x);
        for (int y = view.Height() - 20; y < view.Height() - 16 && badge; ++y) for (int x = 30; x < 160; ++x) frame.pixels[static_cast<size_t>(y) * view.width + x] = 0xff000000u | (badge * 0x1f3d) | static_cast<uint32_t>(y);
        if (repaint) for (int y = view.header + 100; y < view.header + 104; ++y) for (int x = 50; x < 250; ++x) frame.pixels[static_cast<size_t>(y) * view.width + x] = 0xff000000 | (random.Next() & 0xffffff);
        return frame;
    };
    std::vector<Frame> frames;
    std::vector<uint64_t> hashes;
    std::vector<int> offsets;
    const int steps[] = {173, 311, 402, 257, 199, 350};
    frames.push_back(shot(0, false));
    Expect(stitcher.Add(frames.back()).status == StitchStatus::First, "delta: first frame");
    hashes.push_back(Hash(stitcher.Compose(false)));
    int offset = 0;
    bool ok = true;
    for (int i = 0; offset + 450 < page.height - view.viewport; ++i) {
        offset += steps[i % 6];
        frames.push_back(shot(offset, false));
        const auto added = stitcher.Add(frames.back());
        if (added.status != StitchStatus::Appended) { std::cout << "  step " << i << " status " << static_cast<int>(added.status) << '\n'; ok = false; }
        if (i % 3 == 1) {
            frames.push_back(shot(offset, true));
            const auto same = stitcher.Add(frames.back());
            if (same.status != StitchStatus::Unchanged) { std::cout << "  repaint " << i << " status " << static_cast<int>(same.status) << '\n'; ok = false; }
        }
        hashes.push_back(Hash(stitcher.Compose(false)));
        offsets.push_back(static_cast<int>(frames.size()));
    }
    const int appended = static_cast<int>(offsets.size());
    Expect(ok && stitcher.Height() > 3 * 512, "delta: long run crosses several content blocks");
    const size_t full = static_cast<size_t>(std::min(appended, 32)) * view.width * view.Height() * sizeof(uint32_t);
    std::cout << "  history " << stitcher.HistoryBytes() << " bytes vs " << full << " for full frames\n";
    Expect(stitcher.HistoryBytes() * 4 < full, "delta: history is a fraction of full frames");
    const Frame copy = stitcher.Compose(false);
    int undone = 0;
    bool exact = true;
    while (stitcher.Undo()) {
        ++undone;
        exact = exact && Hash(stitcher.Compose(false)) == hashes[static_cast<size_t>(appended - undone)];
    }
    Expect(appended > 32 && undone == 32 && exact, "delta: every undo restores the exact previous image");
    // Re-adding the same frames (including repaints) must follow the same path.
    bool replay = true;
    size_t next = appended > undone ? static_cast<size_t>(offsets[static_cast<size_t>(appended - undone - 1)]) : 1;
    for (int k = appended - undone; k < appended; ++k) {
        const size_t end = static_cast<size_t>(offsets[static_cast<size_t>(k)]);
        for (; next < end; ++next) stitcher.Add(frames[next]);
        replay = replay && Hash(stitcher.Compose(false)) == hashes[static_cast<size_t>(k + 1)];
    }
    Expect(replay && Hash(stitcher.Compose(false)) == Hash(copy), "delta: redo after undo reproduces every step exactly");
    const Frame consumed = std::move(stitcher).Compose(false);
    Expect(consumed.pixels == copy.pixels && consumed.Width() == copy.Width(), "consuming compose equals the copying compose");
}

int main() {
    CheckSteadyScroll();
    CheckRepetitiveContent();
    CheckGapAndUndo();
    CheckAnimatedRegion();
    CheckRenderingNoise();
    CheckLowMatchAndForce();
    CheckHeightLimit();
    CheckBlankTail();
    CheckMismatchedFrame();
    CheckDeltaUndo();
    std::cout << (failures ? "FAILED " : "OK ") << failures << '\n';
    return failures ? 1 : 0;
}
