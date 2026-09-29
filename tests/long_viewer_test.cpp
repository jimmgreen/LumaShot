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
#include <atomic>
#include <cstring>
#include <thread>

using namespace lumashot;
using namespace lumashot::longshot;

namespace {
constexpr uint32_t kRed = 0xffe5484d;
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

// Keys with Ctrl/Shift held, through the thread keyboard state the viewer reads.
void Chord(HWND window, WPARAM key, bool shift = false) {
    BYTE state[256]{}, saved[256]{};
    GetKeyboardState(state);
    std::memcpy(saved, state, sizeof(state));
    state[VK_CONTROL] = 0x80;
    if (shift) state[VK_SHIFT] = 0x80;
    SetKeyboardState(state);
    SendMessageW(window, WM_KEYDOWN, key, 0);
    SetKeyboardState(saved);
}

HWND OwnDialog() {
    struct Search { HWND found{}; } search;
    EnumWindows([](HWND w, LPARAM lp) -> BOOL {
        DWORD pid{};
        GetWindowThreadProcessId(w, &pid);
        wchar_t name[64]{};
        GetClassNameW(w, name, 64);
        if (pid == GetCurrentProcessId() && std::wstring(name) == L"LumaShot.ThemedMessage" && IsWindowVisible(w)) {
            reinterpret_cast<Search*>(lp)->found = w;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

// Answers the discard prompt from another thread (the prompt runs a modal loop).
std::jthread AnswerPrompt(std::atomic_bool& shown, bool accept) {
    return std::jthread([&shown, accept](std::stop_token stop) {
        while (!stop.stop_requested()) {
            if (HWND dialog = OwnDialog()) {
                shown = true;
                Sleep(150);
                if (accept) PostMessageW(dialog, WM_KEYDOWN, VK_TAB, 0);
                PostMessageW(dialog, WM_KEYDOWN, accept ? VK_RETURN : VK_ESCAPE, 0);
                return;
            }
            Sleep(20);
        }
    });
}

size_t CountNear(const Frame& shot, uint32_t color) {
    size_t count = 0;
    for (uint32_t p : shot.pixels) {
        const int dr = static_cast<int>((p >> 16) & 255) - static_cast<int>((color >> 16) & 255);
        const int dg = static_cast<int>((p >> 8) & 255) - static_cast<int>((color >> 8) & 255);
        const int db = static_cast<int>(p & 255) - static_cast<int>(color & 255);
        count += std::abs(dr) < 40 && std::abs(dg) < 40 && std::abs(db) < 40;
    }
    return count;
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
        Frame pinned, pinned_ocr;
        int pins = 0;
        host.pin = [&](Frame image, Frame ocr, POINT, bool) { pinned = std::move(image); pinned_ocr = std::move(ocr); ++pins; };
        // Editor stand-in: records what the viewer hands over and answers at once.
        struct Stub {
            int calls = 0, follow = 0;
            RECT bounds{};
            int slice_w = 0, slice_h = 0;
            size_t received = 0;
            std::function<std::optional<Document>(Document)> respond;
        } stub;
        host.annotate = [&stub](std::shared_ptr<const Frame> slice, Document marks, RECT bounds, Host::AnnotateDone done) {
            ++stub.calls;
            stub.bounds = bounds;
            stub.slice_w = slice->Width();
            stub.slice_h = slice->Height();
            stub.received = marks.marks.size();
            done(stub.respond(std::move(marks)), stub.follow);
            return true;
        };
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

        // ---- Annotation (v2): visible slice goes to the editor, marks come back.
        SendMessageW(window, WM_KEYDOWN, VK_HOME, 0);
        Pump(200);
        const Frame clean = CaptureRegion(rect);
        stub.respond = [](Document) {
            Document edited;
            Mark mark;
            mark.tool = Tool::Rectangle;
            mark.a = {60, 120};
            mark.b = {420, 300};
            mark.width = 8;
            mark.color = kRed;
            edited.marks.push_back(mark);
            return std::optional<Document>(std::move(edited));
        };
        SendMessageW(window, WM_KEYDOWN, 'E', 0);
        Pump(300);
        const RECT b = stub.bounds;
        const bool inside = b.left >= rect.left && b.top >= rect.top && b.right <= rect.right && b.bottom <= rect.bottom;
        Expect(stub.calls == 1 && stub.received == 0 && inside, theme + ": E hands the visible slice to the editor over the viewer");
        const double slice_ratio = static_cast<double>(stub.slice_w) / std::max(1, stub.slice_h);
        const double bounds_ratio = static_cast<double>(b.right - b.left) / std::max<LONG>(1, b.bottom - b.top);
        Expect(std::abs(slice_ratio / bounds_ratio - 1) < .03, theme + ": editor bounds keep the slice aspect ratio");
        Expect(viewer->Marks().size() == 1 && !viewer->Annotating() && IsWindowEnabled(window), theme + ": edited marks are merged and the viewer is re-enabled");
        const Frame annotated = CaptureRegion(rect);
        Expect(CountNear(annotated, kRed) > CountNear(clean, kRed) + 400, theme + ": annotation is composited into the visible tiles");
        if (!folder.empty()) SavePng(annotated, folder / ("viewer-" + theme + "-annotated.png"));
        const Mark placed = viewer->Marks().front();
        Expect(placed.b.x - placed.a.x == 360 && placed.b.y - placed.a.y == 180, theme + ": marks keep their size in long-image pixels");

        stub.respond = [](Document marks) { marks.marks.clear(); return std::optional<Document>(std::move(marks)); };
        SendMessageW(window, WM_KEYDOWN, 'E', 0);
        Pump(200);
        Expect(stub.received == 1 && viewer->Marks().empty(), theme + ": existing marks re-open for editing (and can be deleted)");
        Chord(window, 'Z');
        Expect(viewer->Marks().size() == 1 && viewer->Marks().front() == placed, theme + ": Ctrl+Z restores the deleted mark");
        Chord(window, 'Y');
        Expect(viewer->Marks().empty(), theme + ": Ctrl+Y redoes");
        Chord(window, 'Z', true);
        Chord(window, 'Z');
        Expect(viewer->Marks().size() == 1, theme + ": undo after redo round-trips");

        stub.respond = [](Document) { return std::optional<Document>(); };
        SendMessageW(window, WM_KEYDOWN, 'E', 0);
        Pump(100);
        Expect(viewer->Marks().size() == 1 && IsWindowEnabled(window), theme + ": canceling the editor keeps the marks");

        {
            std::atomic_bool shown{};
            auto answer = AnswerPrompt(shown, false);
            SendMessageW(window, WM_CLOSE, 0, 0);
            Pump(100);
            Expect(shown && !closed && IsWindow(window), theme + ": closing with unexported marks asks first; Esc keeps the window");
        }

        stub.respond = [](Document marks) { return std::optional<Document>(std::move(marks)); };
        stub.follow = 13;  // pin chosen in the editor: the whole long image is pinned
        SendMessageW(window, WM_KEYDOWN, 'E', 0);
        for (int i = 0; i < 100 && pins == 0; ++i) Pump(50);
        bool composed = pins == 1 && pinned.Width() == pinned_ocr.Width() && pinned.Height() == pinned_ocr.Height() && pinned.Height() == 9000;
        size_t differing = 0;
        if (composed) for (size_t i = 0; i < pinned.pixels.size(); ++i) differing += pinned.pixels[i] != pinned_ocr.pixels[i];
        Expect(composed && differing > 1000, theme + ": pin exports the annotated long image with clean OCR pixels");
        stub.follow = 0;
        {
            std::atomic_bool shown{};
            auto answer = AnswerPrompt(shown, true);
            SendMessageW(window, WM_CLOSE, 0, 0);
            Pump(100);
            answer.request_stop();
            Expect(!shown && closed && !IsWindow(window), theme + ": after export the window closes without asking");
        }
        viewer.reset();
    }
    if (SUCCEEDED(com)) CoUninitialize();
    std::cout << (failures ? "FAILED " : "OK ") << failures << '\n';
    return failures ? 1 : 0;
}
