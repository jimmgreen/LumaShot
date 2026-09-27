#include "clipboard/quick_window.h"
#include "export/png.h"
#include <objbase.h>
#include <dwmapi.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace lumashot;
using namespace lumashot::clipboard;
namespace {
int failures{};
void Expect(bool result, const char* label) {
    std::cout << (result ? "PASS " : "FAIL ") << label << '\n';
    failures += !result;
}
LPARAM Point(int x, int y) { return MAKELPARAM(x, y); }
void Click(HWND window, LPARAM position) {
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, position);
    SendMessageW(window, WM_LBUTTONUP, 0, position);
}
std::vector<QuickRow> Rows() {
    return {
        {1, Kind::Text, L"一段合成文本，用于测试紧凑剪贴板窗口", true},
        {2, Kind::Image, L"截图 · 1280 × 720", false},
        {3, Kind::Files, L"示例文件夹 / synthetic-report.pdf", false},
        {4, Kind::Text, L"Hello world — synthetic test content", true},
        {5, Kind::Text, L"第一行\r\n第二行\t末尾", false},
        {6, Kind::Text, L"非常长的测试内容应当在右侧显示省略号，不换行也不覆盖收藏星标。Very long synthetic content.", true},
        {7, Kind::Image, L"合成图像 · 640 × 480", false},
        {8, Kind::Text, L"最后一条合成记录", false}
    };
}
bool Inside(RECT bounds, RECT work) {
    return bounds.left >= work.left && bounds.top >= work.top && bounds.right <= work.right && bounds.bottom <= work.bottom;
}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) { std::cerr << "COM initialization failed\n"; return 1; }
    try {
        size_t selected = (std::numeric_limits<size_t>::max)(), pasted = selected;
        int pasteCount{}, category{-1}, movement{}, closed{};
        QuickWindow popup({[&](size_t value) { selected = value; },
            [&](size_t value) { pasted = value; ++pasteCount; },
            [&](int value) { category = value; },
            [&](int value) { movement += value; }, [&] { ++closed; }});
        const auto rows = Rows();
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) throw std::runtime_error("monitor unavailable");
        const RECT work = monitor.rcWork;
        const POINT anchor{work.left + (work.right - work.left) / 2, work.bottom - 20};
        const HWND foreground = GetForegroundWindow(), focus = GetFocus(), active = GetActiveWindow();
        popup.Update(rows, 0, 0, false, false);
        popup.Show(nullptr, anchor, 1.f, false);
        const HWND window = popup.Window();
        Expect(window && popup.Visible(), "popup is visible");
        const LONG_PTR ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
        Expect((ex & (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST)) == (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST), "popup carries all no-activation styles");
        Expect(SendMessageW(window, WM_MOUSEACTIVATE, 0, MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE, "mouse activation is rejected");
        UpdateWindow(window);
        Expect(GetForegroundWindow() == foreground && GetFocus() == focus && GetActiveWindow() == active, "show and paint preserve foreground focus and activation");
        Expect(Inside(popup.Bounds(), work), "initial popup stays in monitor work area");
        if (work.bottom - work.top >= 300) Expect(popup.Bounds().bottom < anchor.y, "popup prefers position above anchor");

        SendMessageW(window, WM_MOUSEMOVE, 0, Point(70, 89));
        Expect(selected == 1 && pasteCount == 0, "hover selects without insertion");
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, Point(70, 89));
        Expect(pasteCount == 0, "press does not insert");
        SendMessageW(window, WM_LBUTTONUP, 0, Point(70, 89));
        Expect(pasteCount == 1 && pasted == 1, "matching release inserts selected row");
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, Point(70, 55));
        SendMessageW(window, WM_LBUTTONUP, 0, Point(70, 89));
        Expect(pasteCount == 1, "drag release onto another row does not insert");
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, Point(70, 55));
        auto replacement = rows; replacement[0].id = 99;
        popup.Update(replacement, 0, 0, false, false);
        SendMessageW(window, WM_LBUTTONUP, 0, Point(70, 55));
        Expect(pasteCount == 1, "updated row identity invalidates pending click");
        popup.Update(rows, 0, 0, false, true);
        Click(window, Point(70, 55));
        Expect(pasteCount == 1, "busy popup rejects insertion");
        popup.Update(rows, 0, 0, false, false);
        for (int i = 0; i < 5; ++i) { Click(window, Point(i * 72 + 36, 19)); Expect(category == i, "category mouse callback follows visible order"); }
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        Expect(movement == 0, "wheel at top does not wrap or move selection");
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-60)), 0);
        Expect(movement == 0, "fractional wheel delta is accumulated without a full step");
        UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        const int step = lines == WHEEL_PAGESCROLL ? 2 : static_cast<int>(std::min(lines, 2u));
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-60)), 0);
        Expect(movement == step, "wheel scrolls viewport using system row preference");
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(WHEEL_DELTA)), 0);
        Expect(movement == 0, "reverse wheel returns viewport to top");
        popup.Update(rows, 0, 0, false, true);
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        Expect(movement == 0, "busy list does not scroll pending insertion away");
        popup.Update(rows, 7, 0, false, false);
        Click(window, Point(70, 225));
        Expect(pasted == 7 && pasteCount == 2, "selected last row is scrolled into sixth visible slot");
        Expect(GetForegroundWindow() == foreground && GetFocus() == focus && GetActiveWindow() == active, "mouse interactions preserve activation");

        popup.Update(rows, 0, 0, false, false);
        const auto normal = popup.Snapshot();
        DWORD backdrop{};
        if (SUCCEEDED(DwmGetWindowAttribute(window, 38, &backdrop, sizeof(backdrop)))) {
            Expect(backdrop == 3, "popup requests transient acrylic backdrop");
            const auto alpha = normal.pixels[40 * normal.Width() + 2] >> 24;
            Expect(alpha == 0, "system acrylic is not covered by a tint layer");
        }
        auto singleLine = rows; singleLine[4].text = L"第一行  第二行 末尾";
        popup.Update(singleLine, 0, 0, false, false);
        Expect(popup.Snapshot().pixels == normal.pixels, "CR LF and tabs render as single-line spaces");
        popup.Update(rows, 0, 0, false, false, L"合成错误：插入失败");
        Expect(popup.Snapshot().pixels != normal.pixels, "error status is visible even with populated rows");
        popup.Update(rows, 0, 0, false, true);
        Expect(popup.Snapshot().pixels != normal.pixels, "loading status is visible even with populated rows");
        popup.Update(rows, 0, 0, true, false);
        Expect(popup.Snapshot().pixels != normal.pixels, "continuous mode changes visible footer");

        for (bool dark : {false, true}) for (int dpi : {96, 144, 192}) {
            const float scale = static_cast<float>(dpi) / 96.f;
            popup.Update(rows, 1, 0, dark, false);
            popup.Show(nullptr, anchor, scale, dark);
            const auto snapshot = popup.Snapshot();
            Expect(!snapshot.pixels.empty() && snapshot.Width() == popup.Bounds().right - popup.Bounds().left && snapshot.Height() == popup.Bounds().bottom - popup.Bounds().top, "snapshot matches physical window size");
            Expect(Inside(popup.Bounds(), work), "scaled popup stays within work area");
            const auto name = std::wstring(L"clipboard-quick-") + (dark ? L"dark-" : L"light-") + std::to_wstring(dpi) + L".png";
            SavePng(snapshot, std::filesystem::path(name));
            popup.Update({}, 0, 1, false, false);
            const auto emptyName = std::wstring(L"clipboard-quick-empty-") + (dark ? L"dark-" : L"light-") + std::to_wstring(dpi) + L".png";
            SavePng(popup.Snapshot(), std::filesystem::path(emptyName));
        }
        for (POINT edge : {POINT{work.left, work.top}, POINT{work.right - 1, work.top}, POINT{work.left, work.bottom - 1}, POINT{work.right - 1, work.bottom - 1}}) {
            popup.Show(nullptr, edge, 2.f, false); Expect(Inside(popup.Bounds(), work), "edge anchored popup clamps to monitor work area");
        }
        popup.Show(nullptr, anchor, 1.f, false);
        popup.Update({}, 0, 0, false, false, L"测试空状态");
        Click(window, Point(70, 55)); Expect(pasteCount == 2, "empty content cannot insert");
        Expect(!popup.Snapshot().pixels.empty(), "empty state renders");
        SendMessageW(window, WM_CLOSE, 0, 0);
        Expect(closed == 1 && !popup.Visible(), "close hides popup and invokes callback");
        popup.Show(nullptr, anchor, 1.f, false); popup.Hide();
        Expect(!popup.Visible() && GetCapture() != window, "hide releases capture and preserves reusable window");
        QuickWindow throwing({[](size_t) { throw std::runtime_error("synthetic callback failure"); }, {}, {}, {}, {}});
        throwing.Update(rows, 0, 0, false, false); throwing.Show(nullptr, anchor, 1.f, false);
        SendMessageW(throwing.Window(), WM_MOUSEMOVE, 0, Point(70, 89));
        Expect(throwing.Visible(), "callback exceptions do not escape window procedure");
        throwing.Hide();
    } catch (const std::exception& error) { std::cerr << "FAIL exception: " << error.what() << '\n'; ++failures; }
    CoUninitialize();
    std::cout << "Quick popup: " << (failures ? "FAILED" : "PASS") << '\n';
    return failures ? 1 : 0;
}
