// Long capture memory and speed benchmark (synthetic page, no desktop, no
// clipboard, no files). A sampler thread records the peak private commit of
// every phase: stitching up to the height limit, undo/redo, compose, opening
// the viewer and exporting a pin (the largest export).
// Usage: lumashot_long_memory_bench [width height]   (default 1180 900)
// Registered as a test with generous budgets; the printed table is the result.
#include "capture/stitcher.h"
#include "longshot/viewer.h"
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

using namespace lumashot;

namespace {
size_t PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    return counters.PrivateUsage;
}

// Polls private commit; Phase() returns the peak above the phase baseline.
class Sampler {
public:
    Sampler() : thread_([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            const size_t now = PrivateBytes();
            size_t peak = peak_.load();
            while (now > peak && !peak_.compare_exchange_weak(peak, now)) {}
            Sleep(1);
        }
    }) {}
    void Begin() { base_ = PrivateBytes(); peak_ = base_; start_ = std::chrono::steady_clock::now(); }
    void End(const char* name) {
        Sleep(5);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
        const double peak = static_cast<double>(peak_.load() - base_) / 1048576.0;
        const double kept = (static_cast<double>(PrivateBytes()) - static_cast<double>(base_)) / 1048576.0;
        std::printf("%-26s peak +%8.1f MB   retained %+8.1f MB   %8.1f ms\n", name, peak, kept, ms);
        last_peak_ = peak;
    }
    double LastPeak() const { return last_peak_; }
private:
    std::atomic<size_t> peak_{};
    size_t base_{};
    double last_peak_{};
    std::chrono::steady_clock::time_point start_;
    std::jthread thread_;
};

uint32_t Hash(uint32_t v) {
    v ^= v >> 16; v *= 0x7feb352d; v ^= v >> 15; v *= 0x846ca68b; v ^= v >> 16;
    return v;
}

// Page row y of a document with text-like lines; generated on demand so the
// page itself costs no memory.
void PageRow(uint32_t* out, int width, int y) {
    const int line = y / 22, in = y % 22;
    const uint32_t h = Hash(static_cast<uint32_t>(line));
    const int length = in < 12 ? 80 + static_cast<int>(h % static_cast<uint32_t>(std::max(1, width - 200))) : 0;
    for (int x = 0; x < width; ++x) {
        uint32_t c = 0xfff7f8fa;
        if (x >= 40 && x < 40 + length && ((x / 7 + static_cast<int>(h)) % 5 != 0)) c = 0xff30343c + (h & 0x0f0f0f);
        if (x >= width - 14) c = 0xffd9dce2;  // scrollbar track
        out[x] = c;
    }
}

Frame Screen(int width, int height, int offset) {
    Frame frame = MakeFrame({0, 0, width, height});
    constexpr int header = 56, footer = 36;
    for (int y = 0; y < height; ++y) {
        uint32_t* row = frame.pixels.data() + static_cast<size_t>(y) * width;
        if (y < header) { std::fill_n(row, width, 0xff1f2937); for (int x = 24; x < 180; ++x) if (y > 18 && y < 38) row[x] = 0xff60a5fa; }
        else if (y >= height - footer) std::fill_n(row, width, 0xffe5e7eb);
        else PageRow(row, width, offset + y);
        // Scrollbar thumb moves against the content.
        const int thumb = header + (offset / 40) % std::max(1, height - header - footer - 120);
        if (y >= thumb && y < thumb + 120) for (int x = width - 12; x < width - 2; ++x) row[x] = 0xff9ca3af;
    }
    return frame;
}

void Pump(int milliseconds) {
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(milliseconds);
    MSG msg{};
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
}
}

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int width = argc > 2 ? std::stoi(argv[1]) : 1180, height = argc > 2 ? std::stoi(argv[2]) : 900;
    const double image_mb = static_cast<double>(width) * 30000 * 4 / 1048576.0;
    std::printf("Region %dx%d, limit 30000 rows (final image %.1f MB)\n", width, height, image_mb);
    Sampler sampler;
    int failures = 0;
    std::unique_ptr<longshot::CaptureResult> result;
    {
        Stitcher stitcher;
        sampler.Begin();
        int offset = 0, adds = 0;
        double add_ms = 0;
        for (;;) {
            Frame frame = Screen(width, height, offset);
            const auto t0 = std::chrono::steady_clock::now();
            const auto r = stitcher.Add(frame);
            add_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++adds;
            if (r.status == StitchStatus::Limit || adds > 400) break;
            if (r.status != StitchStatus::First && r.status != StitchStatus::Appended) { std::printf("unexpected status %d\n", static_cast<int>(r.status)); ++failures; break; }
            offset += 300;
        }
        sampler.End("stitch to limit");
        std::printf("  %d frames, %.2f ms per Add, height %d, segments %d, scrollbar %d\n", adds, add_ms / adds, stitcher.Height(), stitcher.Segments(), stitcher.ScrollbarColumns());
        if (stitcher.Height() != 30000) { std::printf("  height is not the limit\n"); ++failures; }

        // Undo the full history, then scroll the same frames back in.
        const Frame before = stitcher.Compose(false);
        sampler.Begin();
        int undone = 0;
        while (stitcher.Undo()) ++undone;
        int redone = 0;
        for (int i = 0; i < undone + 1; ++i) {
            offset = (adds - 1 - undone + i) * 300;
            const auto r = stitcher.Add(Screen(width, height, offset));
            if (r.status == StitchStatus::Appended || r.status == StitchStatus::Limit) ++redone;
            if (r.status == StitchStatus::Limit) break;
        }
        sampler.End("undo all + redo");
        const Frame after = stitcher.Compose(false);
        std::printf("  undone %d, re-added %d, identical %s\n", undone, redone, before.pixels == after.pixels ? "yes" : "no");
        if (before.pixels != after.pixels) ++failures;

        sampler.Begin();
        result = std::make_unique<longshot::CaptureResult>();
        result->seams = stitcher.Seams();
        result->scrollbar = stitcher.ScrollbarColumns();
        result->image = std::move(stitcher).Compose(false);
        result->region = {100, 100, 100 + width, 100 + height};
        sampler.End("compose (consuming)");
    }
    {
        // Viewer: open, render, then pin (clean + annotated copies).
        longshot::Host host;
        host.dark = [] { return false; };
        host.clipboard_format = [] { return -1; };
        size_t pinned = 0;
        host.pin = [&](Frame image, Frame ocr, POINT, bool) { pinned = image.pixels.size() + ocr.pixels.size(); };
        host.ocr_available = [] { return false; };
        int follow = 0;
        host.annotate = [&](std::shared_ptr<const Frame>, Document, RECT, longshot::Host::AnnotateDone done) {
            done(std::nullopt, follow);
            return true;
        };
        sampler.Begin();
        const size_t before_viewer = PrivateBytes();
        auto viewer = std::make_unique<longshot::Viewer>(std::move(result), host, [](longshot::Viewer*) {});
        const size_t constructed = PrivateBytes();
        const auto p0 = std::chrono::steady_clock::now();
        UpdateWindow(viewer->Window());
        const double first_paint = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count();
        const size_t painted = PrivateBytes();
        Pump(800);
        sampler.End("viewer open + paint");
        std::printf("  constructed %+.1f MB, first paint %+.1f MB (%.1f ms), after overview %+.1f MB\n",
            (static_cast<double>(constructed) - static_cast<double>(before_viewer)) / 1048576.0,
            (static_cast<double>(painted) - static_cast<double>(constructed)) / 1048576.0, first_paint,
            (static_cast<double>(PrivateBytes()) - static_cast<double>(painted)) / 1048576.0);
        // Synchronous paint time per page step (new tiles appear every other step).
        double paint_total = 0, paint_max = 0;
        int steps = 0;
        sampler.Begin();
        for (int i = 0; i < 40; ++i) {
            SendMessageW(viewer->Window(), WM_KEYDOWN, i < 30 ? VK_NEXT : VK_PRIOR, 0);
            const auto t0 = std::chrono::steady_clock::now();
            UpdateWindow(viewer->Window());
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            paint_total += ms;
            paint_max = std::max(paint_max, ms);
            ++steps;
            Pump(1);
        }
        sampler.End("viewer scrolling");
        std::printf("  paint per page step: avg %.1f ms, max %.1f ms\n", paint_total / steps, paint_max);
        // Steady repaint without scrolling (no new tiles).
        double still = 0;
        for (int i = 0; i < 10; ++i) {
            InvalidateRect(viewer->Window(), nullptr, FALSE);
            const auto t0 = std::chrono::steady_clock::now();
            UpdateWindow(viewer->Window());
            still += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        std::printf("  repaint without scrolling: avg %.1f ms\n", still / 10);
        // Zoomed far out: many rows visible at once.
        const auto control = [](bool down) {
            BYTE keys[256]{};
            GetKeyboardState(keys);
            keys[VK_CONTROL] = down ? 0x80 : 0;
            SetKeyboardState(keys);
        };
        sampler.Begin();
        control(true);
        for (int i = 0; i < 15; ++i) SendMessageW(viewer->Window(), WM_KEYDOWN, VK_OEM_MINUS, 0);
        control(false);
        double far_total = 0, far_max = 0;
        for (int i = 0; i < 12; ++i) {
            SendMessageW(viewer->Window(), WM_KEYDOWN, i < 8 ? VK_NEXT : VK_PRIOR, 0);
            const auto t0 = std::chrono::steady_clock::now();
            UpdateWindow(viewer->Window());
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            far_total += ms;
            far_max = std::max(far_max, ms);
            Pump(1);
        }
        sampler.End("viewer zoomed out");
        std::printf("  paint per page step: avg %.1f ms, max %.1f ms\n", far_total / 12, far_max);
        control(true);
        SendMessageW(viewer->Window(), WM_KEYDOWN, '0', 0);  // back to fit width
        control(false);
        UpdateWindow(viewer->Window());
        sampler.Begin();
        follow = 13;
        SendMessageW(viewer->Window(), WM_KEYDOWN, 'E', 0);
        for (int i = 0; i < 400 && pinned == 0; ++i) Pump(10);
        sampler.End("pin export");
        if (!pinned) { std::printf("  pin was not produced\n"); ++failures; }
        const double pin_peak = sampler.LastPeak();
        SendMessageW(viewer->Window(), WM_CLOSE, 0, 0);
        Pump(50);
        viewer.reset();
        std::printf("pin peak / image size: %.2fx\n", pin_peak / image_mb);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    std::printf("%s %d\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
