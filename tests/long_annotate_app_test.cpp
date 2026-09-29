// Long-image annotation through the real screenshot editor (v2): the viewer's
// visible slice opens in the editor, a drawn rectangle returns to the viewer
// in long-image pixels, re-opening shows it again, Esc keeps it, and "pin" in
// the editor pins the whole annotated long image. Synthetic image; no
// clipboard, no files.
#include "app/application.h"
#include "capture/desktop.h"
#include "export/png.h"
#include <commctrl.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>

using namespace lumashot;

namespace lumashot {
struct LongAnnotateTest {
    static inline Application* app{};
    static void CALLBACK Open(HWND, UINT, UINT_PTR id, DWORD) {
        if (!app || !app->longshot_) return;
        KillTimer(nullptr, id);
        auto result = std::make_unique<longshot::CaptureResult>();
        result->image = MakeFrame({0, 0, 700, 6000}, 0xfff4f6fa);
        for (int y = 0; y < 6000; ++y)
            for (int x = 0; x < 700; ++x)
                if ((y / 40 + x / 60) % 5 == 0) result->image.pixels[static_cast<size_t>(y) * 700 + x] = 0xffd7dde8;
        result->region = {160, 120, 860, 720};
        app->longshot_->Open(std::move(result));
    }
    static longshot::Viewer* Viewer() { return app && app->longshot_ ? app->longshot_->ViewerAt(0) : nullptr; }
    static size_t Marks() { auto* v = Viewer(); return v ? v->Marks().size() : 0; }
    static Box First() { auto* v = Viewer(); return v && !v->Marks().empty() ? Bounds(v->Marks().front()) : Box{}; }
    static RECT EditBounds() { return app->pin_edit_bounds_; }
    static bool ImageEditing() { return app->pin_edit_id_ == Application::kImageEditId; }
    static size_t EditorMarks() { return app->document_.marks.size(); }
    static LPARAM ButtonPoint(HWND editor, int id) {
        const auto* view = reinterpret_cast<Application::View*>(GetWindowLongPtrW(editor, GWLP_USERDATA));
        const auto button = view->app->state_.toolbar.Button(id);
        return MAKELPARAM(static_cast<short>((button.left + button.right) / 2 - view->bounds.left),
            static_cast<short>((button.top + button.bottom) / 2 - view->bounds.top));
    }
};
}

static HWND OwnWindow(const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result; };
    Search search{name, nullptr};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(value);
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        wchar_t cls[128]{};
        GetClassNameW(window, cls, 128);
        if (pid == GetCurrentProcessId() && std::wstring(cls) == s.name && IsWindowVisible(window)) { s.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

template <class F> static bool WaitFor(F condition, int milliseconds) {
    const auto end = GetTickCount64() + static_cast<ULONGLONG>(milliseconds);
    while (GetTickCount64() < end) { if (condition()) return true; Sleep(15); }
    return condition();
}

static int failures = 0;
static void Expect(bool condition, const char* name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!condition) ++failures;
}

// Usage: lumashot_long_annotate_app_test [preview-folder]  (editor-over-viewer PNG)
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    const std::filesystem::path preview = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    std::atomic_bool finished{};
    const DWORD main_thread = GetCurrentThreadId();
    std::jthread driver([&] {
        HWND viewer{};
        Expect(WaitFor([&] { return (viewer = OwnWindow(L"LumaShot.LongCaptureViewer")) != nullptr; }, 10000), "viewer opens");
        if (viewer) {
            // Preview only: keep other windows out of the capture (the editor opens above).
            if (!preview.empty()) SetWindowPos(viewer, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            Sleep(600);
            RECT frame{};
            GetWindowRect(viewer, &frame);
            // 1. Draw a rectangle in the editor and confirm with Enter.
            SendMessageW(viewer, WM_KEYDOWN, 'E', 0);
            HWND editor{};
            Expect(WaitFor([&] { return (editor = OwnWindow(L"LumaShot.Overlay")) != nullptr; }, 3000) && LongAnnotateTest::ImageEditing(),
                "E opens the screenshot editor in long-image mode");
            const RECT bounds = LongAnnotateTest::EditBounds();
            Expect(bounds.left >= frame.left && bounds.top >= frame.top && bounds.right <= frame.right && bounds.bottom <= frame.bottom &&
                bounds.right - bounds.left > 300 && bounds.bottom - bounds.top > 200, "editor sits over the viewer's image area");
            Expect(!IsWindowEnabled(viewer), "viewer is disabled while editing");
            if (editor) {
                SendMessageW(editor, WM_KEYDOWN, 'R', 0);
                Sleep(250);
                POINT a{bounds.left + 80, bounds.top + 80}, b{bounds.left + 280, bounds.top + 200};
                ScreenToClient(editor, &a);
                ScreenToClient(editor, &b);
                SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(a.x, a.y));
                for (int i = 1; i <= 8; ++i)
                    SendMessageW(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(a.x + (b.x - a.x) * i / 8, a.y + (b.y - a.y) * i / 8));
                SendMessageW(editor, WM_LBUTTONUP, 0, MAKELPARAM(b.x, b.y));
                if (!preview.empty()) {
                    Sleep(400);
                    // WIC needs COM on this driver thread.
                    const HRESULT preview_com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    try {
                        std::filesystem::create_directories(preview);
                        RECT area = frame;  // own windows only: the viewer with the editor on top
                        SavePng(CaptureRegion(area), preview / "editor-over-viewer.png");
                    } catch (const std::exception& e) {
                        std::cout << "  preview failed: " << e.what() << '\n';
                    }
                    if (SUCCEEDED(preview_com)) CoUninitialize();
                }
                SendMessageW(editor, WM_KEYDOWN, VK_RETURN, 0);
                Expect(WaitFor([&] { return !IsWindow(editor) && LongAnnotateTest::Marks() == 1; }, 5000), "Enter returns the mark to the viewer");
                const Box mark = LongAnnotateTest::First();
                const float w = mark.right - mark.left, h = mark.bottom - mark.top;
                std::cout << "  mark " << mark.left << ',' << mark.top << ' ' << w << 'x' << h << '\n';
                Expect(w > 0 && h > 0 && std::abs(w / h - 200.f / 120.f) < .06f, "mark keeps its shape in long-image pixels");
                Expect(WaitFor([&] { return IsWindowEnabled(viewer) != FALSE; }, 1000), "viewer is enabled again");
            }
            // 2. Re-open: the mark is editable again; Esc keeps it.
            SendMessageW(viewer, WM_KEYDOWN, 'E', 0);
            editor = nullptr;
            WaitFor([&] { return (editor = OwnWindow(L"LumaShot.Overlay")) != nullptr; }, 3000);
            Expect(editor && LongAnnotateTest::EditorMarks() == 1, "re-opening shows the existing mark in the editor");
            if (editor) {
                SendMessageW(editor, WM_KEYDOWN, VK_ESCAPE, 0);
                Expect(WaitFor([&] { return !IsWindow(editor); }, 3000) && LongAnnotateTest::Marks() == 1, "Esc leaves the marks unchanged");
            }
            // 3. Pin from inside the editor pins the whole annotated long image.
            SendMessageW(viewer, WM_KEYDOWN, 'E', 0);
            editor = nullptr;
            WaitFor([&] { return (editor = OwnWindow(L"LumaShot.Overlay")) != nullptr; }, 3000);
            HWND pin{};
            if (editor) {
                const LPARAM point = LongAnnotateTest::ButtonPoint(editor, 13);
                SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON, point);
                SendMessageW(editor, WM_LBUTTONUP, 0, point);
                WaitFor([&] { return (pin = OwnWindow(L"LumaShot.Pin")) != nullptr; }, 10000);
            }
            Expect(pin && !OwnWindow(L"LumaShot.Overlay") && LongAnnotateTest::Marks() == 1, "editor pin button pins the long image and keeps the marks");
            if (pin) {
                RECT pr{};
                GetWindowRect(pin, &pr);
                std::cout << "  pin " << pr.right - pr.left << 'x' << pr.bottom - pr.top << '\n';
                SendMessageW(pin, WM_CLOSE, 0, 0);
            }
            // 4. Exported, so closing does not ask.
            PostMessageW(viewer, WM_CLOSE, 0, 0);
            Expect(WaitFor([&] { return !IsWindow(viewer); }, 3000), "viewer closes without a prompt after export");
        }
        finished = true;
        if (auto host = OwnWindow(L"LumaShot.Host")) PostMessageW(host, WM_CLOSE, 0, 0);
        else {
            EnumWindows([](HWND w, LPARAM) -> BOOL {
                DWORD pid{};
                GetWindowThreadProcessId(w, &pid);
                wchar_t cls[64]{};
                GetClassNameW(w, cls, 64);
                if (pid == GetCurrentProcessId() && std::wstring(cls) == L"LumaShot.Host") PostMessageW(w, WM_CLOSE, 0, 0);
                return TRUE;
            }, 0);
            PostThreadMessageW(main_thread, WM_QUIT, 0, 0);
        }
    });
    int result = 1;
    try {
        Application app;
        LongAnnotateTest::app = &app;
        SetTimer(nullptr, 0, 50, LongAnnotateTest::Open);
        // Isolated diagnostic session: default preferences, no tray, hotkeys or
        // saved settings, and no automatic capture (the app starts idle).
        app.Run(false, false, true);
        LongAnnotateTest::app = nullptr;
        result = failures == 0 && finished ? 0 : 1;
    } catch (const std::exception& e) {
        std::cout << e.what() << '\n';
    }
    driver.join();
    std::cout << (failures == 0 && result == 0 ? "PASS" : "FAIL") << " long image annotation through the screenshot editor\n";
    CoUninitialize();
    return failures == 0 ? result : 1;
}
