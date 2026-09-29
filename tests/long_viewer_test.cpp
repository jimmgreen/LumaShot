// Long-image preview window: creation, tiled rendering, navigation keys and
// close notification, in light and dark themes. A synthetic document is used;
// nothing touches the clipboard or the save folder.
// Usage: lumashot_long_viewer_test [preview-folder]  (PNG previews for review)
#include "longshot/viewer.h"
#include "capture/desktop.h"
#include "export/png.h"
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

uint32_t Hash(uint32_t v) {
    v ^= v >> 16; v *= 0x7feb352d; v ^= v >> 15; v *= 0x846ca68b; v ^= v >> 16;
    return v;
}

std::unique_ptr<CaptureResult> MakeDocument(int width, int height) {
    auto result = std::make_unique<CaptureResult>();
    result->image = MakeFrame({0, 0, width, height}, 0xfff4f6fa);
    auto put = [&](int x0, int y0, int x1, int y1, uint32_t color) {
        for (int y = std::max(0, y0); y < std::min(height, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(width, x1); ++x) result->image.pixels[static_cast<size_t>(y) * width + x] = color;
    };
    put(0, 0, width, 64, 0xff1f2937);                       // sticky header
    put(24, 20, 140, 44, 0xff60a5fa);
    for (int card = 0; 96 + card * 260 < height - 120; ++card) {
        const int top = 96 + card * 260;
        put(40, top, width - 60, top + 230, 0xffffffff);
        put(64, top + 24, 64 + 120 + static_cast<int>(Hash(static_cast<uint32_t>(card)) % 160), top + 44, 0xff111827);
        for (int line = 0; line < 7; ++line) {
            const int length = 200 + static_cast<int>(Hash(static_cast<uint32_t>(card * 31 + line)) % 380);
            put(64, top + 64 + line * 22, 64 + length, top + 74 + line * 22, 0xff9ca3af);
        }
        if (card % 3 == 1) put(width - 260, top + 24, width - 84, top + 140, 0xff34d399);
    }
    put(0, height - 56, width, height, 0xffe5e7eb);          // footer
    put(width - 12, 0, width, height, 0xffd1d5db);           // scrollbar track
    put(width - 10, 300, width - 2, 520, 0xff9ca3af);        // thumb
    for (int y = 700; y < height; y += 700) result->seams.push_back(y);
    result->header = 64;
    result->footer = 56;
    result->scrollbar = 12;
    result->region = {100, 100, 100 + width, 700};
    return result;
}

void Pump(int milliseconds) {
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(milliseconds);
    MSG msg{};
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 15, QS_ALLINPUT);
    }
}

bool Varied(const Frame& shot) {
    if (shot.pixels.empty()) return false;
    const uint32_t first = shot.pixels[shot.pixels.size() / 2];
    int different = 0;
    for (size_t i = 0; i < shot.pixels.size(); i += 97) different += (shot.pixels[i] & 0xffffff) != (first & 0xffffff);
    return different > 50;
}
}

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::filesystem::path folder = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path();
    if (!folder.empty()) std::filesystem::create_directories(folder);
    for (bool dark : {false, true}) {
        const std::string theme = dark ? "dark" : "light";
        Host host;
        host.dark = [dark] { return dark; };
        host.clipboard_format = [] { return -1; };
        host.save_directory = [] { return std::filesystem::path(); };
        host.remember_directory = [](const std::filesystem::path&) {};
        host.pin = [](Frame, POINT, bool) {};
        host.ocr_available = [] { return true; };
        host.recapture = [] {};
        bool closed = false;
        std::unique_ptr<Viewer> viewer;
        try {
            viewer = std::make_unique<Viewer>(MakeDocument(1180, 9000), host, [&](Viewer*) { closed = true; });
        } catch (const std::exception& e) {
            std::cout << "  " << e.what() << '\n';
        }
        Expect(viewer && IsWindow(viewer->Window()) && IsWindowVisible(viewer->Window()), theme + ": viewer window opens");
        if (!viewer) continue;
        HWND window = viewer->Window();
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Pump(900);  // overview worker + first paint
        RECT rect{};
        GetWindowRect(window, &rect);
        Expect(rect.right - rect.left >= 600 && rect.bottom - rect.top >= 400, theme + ": window has a usable size");
        Frame top = CaptureRegion(rect);
        Expect(Varied(top), theme + ": image, minimap and actions render");
        if (!folder.empty()) SavePng(top, folder / ("viewer-" + theme + "-top.png"));

        SendMessageW(window, WM_KEYDOWN, VK_END, 0);
        Pump(300);
        Frame bottom = CaptureRegion(rect);
        Expect(bottom.pixels != top.pixels, theme + ": End scrolls to the bottom of the long image");
        if (!folder.empty()) SavePng(bottom, folder / ("viewer-" + theme + "-bottom.png"));

        SendMessageW(window, WM_KEYDOWN, '1', 0);  // plain 1 is ignored; Ctrl is not held
        SendMessageW(window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rect.right - rect.left, rect.bottom - rect.top));
        Pump(100);
        Expect(!closed, theme + ": stays open until closed");
        SendMessageW(window, WM_CLOSE, 0, 0);
        Pump(100);
        Expect(closed && !IsWindow(window), theme + ": WM_CLOSE destroys the window and notifies the owner");
        viewer.reset();
    }
    if (SUCCEEDED(com)) CoUninitialize();
    std::cout << (failures ? "FAILED " : "OK ") << failures << '\n';
    return failures ? 1 : 0;
}
