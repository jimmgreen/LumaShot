// End-to-end check of the scroll-capture session on the live desktop.
// A child process hosts a synthetic, borderless, top-most "page" that scrolls
// 60 px per wheel notch; the session runs in auto mode against it and the
// stitched image must reproduce the whole page row for row. No personal data:
// the page is generated from its coordinates.
#include "longshot/session.h"
#include "capture/desktop.h"
#include "export/png.h"
#include <filesystem>
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace lumashot;
using namespace lumashot::longshot;

namespace {
constexpr int kWidth = 480, kViewport = 360, kPage = 2400, kNotch = 60;
constexpr wchar_t kPageClass[] = L"LumaShot.LongCaptureTestPage";
int failures = 0;

void Expect(bool condition, const char* name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!condition) ++failures;
}

uint32_t Hash(uint32_t v) {
    v ^= v >> 16; v *= 0x7feb352d; v ^= v >> 15; v *= 0x846ca68b; v ^= v >> 16;
    return v;
}

// White paper, a gradient margin bar (every row unique) and "text" lines on a
// 26 px pitch so no scroll step aligns with the line grid.
uint32_t PagePixel(int x, int y) {
    if (x >= 36 && x < 52) return 0xff000000u | (static_cast<uint32_t>(y % 256) << 16) | (static_cast<uint32_t>((y / 256) * 23 % 256) << 8) | 0x60;
    const int line = y / 26, row = y % 26;
    if (x < 72 || x > kWidth - 40 || row < 6 || row > 19) return 0xffffffff;
    const int length = 180 + static_cast<int>(Hash(static_cast<uint32_t>(line)) % 200);
    if (x - 72 > length) return 0xffffffff;
    const bool ink = (Hash(static_cast<uint32_t>(line * 7919 + (x - 72) / 7 * 31 + row)) & 3) != 0 && (x - 72) % 7 < 5;
    return ink ? 0xff202833u : 0xffffffffu;
}

struct Page {
    int offset = 0;
    std::vector<uint32_t> pixels;
};

LRESULT CALLBACK PageProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* page = reinterpret_cast<Page*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_MOUSEWHEEL:
        if (page) {
            page->offset = std::clamp(page->offset - GET_WHEEL_DELTA_WPARAM(wp) * kNotch / WHEEL_DELTA, 0, kPage - kViewport);
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        if (page) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = kWidth;
            info.bmiHeader.biHeight = -kViewport;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, 0, 0, kWidth, kViewport, 0, 0, kWidth, kViewport,
                page->pixels.data() + static_cast<size_t>(page->offset) * kWidth, &info, DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wp, lp);
    }
}

int RunPage(int x, int y) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Page page;
    page.pixels.resize(static_cast<size_t>(kWidth) * kPage);
    for (int row = 0; row < kPage; ++row)
        for (int col = 0; col < kWidth; ++col) page.pixels[static_cast<size_t>(row) * kWidth + col] = PagePixel(col, row);
    WNDCLASSW wc{};
    wc.lpfnWndProc = PageProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kPageClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kPageClass, L"Long capture test page", WS_POPUP,
        x, y, kWidth, kViewport, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 2;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&page));
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// --preview <folder>: the session windows are excluded from capture in the
// product; for design review this process lifts the exclusion on its own
// windows and saves the control bar and live panel as PNGs.
struct Found { std::vector<HWND> windows; };
BOOL CALLBACK Collect(HWND window, LPARAM lp) {
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (std::wstring(name).rfind(L"LumaShot.LongCapture", 0) == 0) reinterpret_cast<Found*>(lp)->windows.push_back(window);
    return TRUE;
}
void SavePreview(const std::filesystem::path& folder, const std::string& tag) {
    Found found;
    EnumThreadWindows(GetCurrentThreadId(), Collect, reinterpret_cast<LPARAM>(&found));
    for (HWND w : found.windows) SetWindowDisplayAffinity(w, WDA_NONE);
    MSG msg{};
    const ULONGLONG until = GetTickCount64() + 250;
    while (GetTickCount64() < until) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(10); }
    for (HWND w : found.windows) {
        wchar_t name[64]{};
        GetClassNameW(w, name, 64);
        const std::wstring cls = name;
        if (cls.find(L"Bar") == std::wstring::npos && cls.find(L"Panel") == std::wstring::npos) continue;
        RECT r{};
        GetWindowRect(w, &r);
        const int pad = 12;
        const RECT shot{r.left - pad, r.top - pad, r.right + pad, r.bottom + pad};
        SavePng(CaptureRegion(shot), folder / (tag + (cls.find(L"Bar") != std::wstring::npos ? "-bar.png" : "-panel.png")));
    }
    for (HWND w : found.windows) SetWindowDisplayAffinity(w, WDA_EXCLUDEFROMCAPTURE);
}
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--page") return RunPage(std::atoi(argv[2]), std::atoi(argv[3]));
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::filesystem::path preview;
    bool dark = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--preview" && i + 1 < argc) preview = argv[++i];
        if (std::string(argv[i]) == "--dark") dark = true;
    }
    if (!preview.empty()) std::filesystem::create_directories(preview);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT work = monitor.rcWork;
    // Leave room on the right for the live panel and below for the control bar.
    const int x = work.left + std::max(0, static_cast<int>(work.right - work.left - kWidth) / 2 - 200);
    const int y = work.top + std::max(0, static_cast<int>(work.bottom - work.top - kViewport) / 2 - 80);

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(path) + L"\" --page " + std::to_wstring(x) + L" " + std::to_wstring(y);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool spawned = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE;
    Expect(spawned, "synthetic page process starts");
    if (!spawned) return 1;
    CloseHandle(process.hThread);

    HWND page = nullptr;
    for (int i = 0; i < 100 && !page; ++i) {
        Sleep(50);
        page = FindWindowW(kPageClass, nullptr);
    }
    Expect(page != nullptr, "synthetic page window appears");
    Sleep(400);

    std::unique_ptr<CaptureResult> result;
    bool closed = false, timed_out = false;
    const ULONGLONG started = GetTickCount64();
    if (page) {
        const RECT region{x, y, x + kWidth, y + kViewport};
        auto session = std::make_unique<Session>(region, dark,
            [&](std::unique_ptr<CaptureResult> r) { result = std::move(r); PostQuitMessage(0); },
            [&] { closed = true; PostQuitMessage(0); });
        const UINT_PTR timer = SetTimer(nullptr, 0, 60000, nullptr);
        const UINT_PTR shot = preview.empty() ? 0 : SetTimer(nullptr, 0, 1600, nullptr);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_TIMER && msg.hwnd == nullptr && msg.wParam == timer) { timed_out = true; break; }
            if (shot && msg.message == WM_TIMER && msg.hwnd == nullptr && msg.wParam == shot) {
                KillTimer(nullptr, shot);
                SavePreview(preview, dark ? "session-dark" : "session-light");
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        KillTimer(nullptr, timer);
        session.reset();
    }
    const ULONGLONG elapsed = GetTickCount64() - started;
    if (page) PostMessageW(page, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 0);
    CloseHandle(process.hProcess);

    if (!result) std::cout << "  closed " << closed << " timed_out " << timed_out << '\n';
    Expect(!timed_out && !closed && result != nullptr, "auto scroll reaches the bottom and finishes by itself");
    std::cout << "  elapsed " << elapsed << " ms\n";
    if (result) {
        const Frame& image = result->image;
        std::cout << "  result " << image.Width() << "x" << image.Height() << " seams " << result->seams.size()
                  << " header " << result->header << " footer " << result->footer << " scrollbar " << result->scrollbar << '\n';
        Expect(image.Width() == kWidth, "result keeps the region width");
        Expect(image.Height() == kPage, "result height equals the page height");
        int bad_rows = 0, first_bad = -1;
        const int rows = std::min(image.Height(), kPage);
        for (int row = 0; row < rows; ++row) {
            bool same = true;
            for (int col = 0; col < std::min(kWidth, image.Width()) && same; ++col)
                same = (image.pixels[static_cast<size_t>(row) * image.Width() + col] & 0xffffff) == (PagePixel(col, row) & 0xffffff);
            if (!same) { ++bad_rows; if (first_bad < 0) first_bad = row; }
        }
        if (bad_rows) std::cout << "  mismatching rows " << bad_rows << " first " << first_bad << '\n';
        // Preview mode lifts the capture exclusion for a moment, so our own
        // windows can legitimately appear in those frames.
        if (preview.empty()) Expect(bad_rows == 0, "stitched image reproduces every page row");
        Expect(!result->seams.empty(), "segments are recorded as seams");
        Expect(elapsed < 30000, "capture of a 2400 px page completes within 30 s");
    }
    if (SUCCEEDED(com)) CoUninitialize();
    std::cout << (failures ? "FAILED " : "OK ") << failures << '\n';
    return failures ? 1 : 0;
}
