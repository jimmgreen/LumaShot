// Private non-input desktop: no user clipboard, preferences, capture or input injection.
// Only the foreground bridge and low-level hook installation are substituted.
#include "clipboard_preview_probe.h"
#include "clipboard/quick_input.h"
#include "clipboard/input_focus.h"
#include <iostream>
#include <richedit.h>
static HWND FixtureForeground() { return GetActiveWindow(); }
static BOOL FixtureActivate(HWND window) { SetActiveWindow(window); return GetActiveWindow() == window; }
namespace lumashot::clipboard {
class FixtureQuickInput {
    bool active_{};
public:
    bool Start(HWND, HWND target, RECT, std::function<bool()> allowed = {}) {
        active_ = FixtureForeground() == target && (!allowed || allowed()); return active_;
    }
    void SetBounds(RECT) {}
    void Stop() { active_ = false; }
    bool Active() const { return active_; }
};
}
#define QuickInput FixtureQuickInput
#define GetForegroundWindow FixtureForeground
#define SetForegroundWindow FixtureActivate
#include "../src/clipboard/panel.cpp"
#undef QuickInput
#undef GetForegroundWindow
#undef SetForegroundWindow
struct IsolatedDesktop {
    HDESK original_desktop{GetThreadDesktop(GetCurrentThreadId())}, desktop{};
    bool Init() {
        // Keep native WS_VISIBLE semantics, but never switch the user's input desktop.
        desktop = CreateDesktopW(L"LumaShotShortcutFocusFixture", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        return desktop && SetThreadDesktop(desktop);
    }
    ~IsolatedDesktop() {
        SetThreadDesktop(original_desktop);
        if (desktop) CloseDesktop(desktop);
    }
};
namespace lumashot {
struct ClipboardPanelTest {
    static int Run() {
        int failures{}, checks{};
        const auto expect = [&](bool value, const char* name) {
            ++checks; failures += !value;
            std::cout << (value ? "PASS " : "FAIL ") << name << std::endl;
        };
        const HMODULE rich_module = LoadLibraryW(L"Msftedit.dll");
        const HWND root = CreateWindowExW(0, L"STATIC", L"Synthetic focus host", WS_POPUP | WS_VISIBLE,
            0, 0, 600, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        const auto child = [&](const wchar_t* type, DWORD style) {
            return CreateWindowExW(0, type, L"", WS_CHILD | WS_VISIBLE | style,
                10, 10, 200, 80, root, nullptr, GetModuleHandleW(nullptr), nullptr);
        };
        const HWND edit = child(L"EDIT", ES_MULTILINE);
        const HWND button = child(L"BUTTON", BS_PUSHBUTTON);
        const HWND rich = rich_module ? child(MSFTEDIT_CLASS, ES_MULTILINE) : nullptr;
        const HWND choice = child(L"COMBOBOX", CBS_DROPDOWNLIST);
        expect(root && edit && button && rich && choice, "create isolated native input and non-input fixtures");
        if (!root || !edit || !button || !rich || !choice) { if (root) DestroyWindow(root); if (rich_module) FreeLibrary(rich_module); return 1; }
        {
            bool allowed = true;
            ClipboardPanel panel(nullptr, [] {}, [&] { return allowed; });
            auto& p = *panel.impl_;
            p.test_mode = true;
            expect(p.Create(), "create production panel without history, settings or clipboard listener");
            if (p.window) {
                p.enabled = true;
                const auto reset = [&](HWND focus) {
                    p.CloseQuick(); p.expanded = false; p.hotkeys_suspended = false;
                    p.copy_pending = p.paste_inflight = false; p.shortcut_key = 'V'; allowed = true;
                    FixtureActivate(root); SetFocus(focus);
                    if (FixtureForeground() != root || GetFocus() != focus || !IsWindowVisible(root) ||
                        (focus && !IsWindowVisible(focus))) expect(false, "fixture must supply real visible controls and the requested focus");
                };
                const auto hotkey = [&] { SendMessageW(p.window, WM_HOTKEY, 1, MAKELPARAM(MOD_WIN, 'V')); };
                reset(nullptr); hotkey();
                expect(p.expanded && !p.quick_active, "Win+V with no input focus expands the full clipboard");
                reset(root); hotkey();
                expect(p.expanded && !p.quick_active, "focused ordinary window is not an input field");
                reset(button); hotkey();
                expect(p.expanded && !p.quick_active, "focused button opens the full clipboard");
                reset(edit); hotkey();
                expect(p.quick_active && !p.expanded && p.quick && p.quick->Visible(), "focused writable EDIT opens the mini clipboard");
                expect(GetFocus() == edit && FixtureForeground() == root, "mini clipboard preserves original edit focus and foreground");
                hotkey();
                expect(!p.quick_active && !p.expanded, "repeated shortcut dismisses the mini clipboard");
                reset(edit); SendMessageW(edit, EM_SETREADONLY, TRUE, 0); hotkey();
                expect(p.expanded && !p.quick_active, "read-only EDIT opens the full clipboard despite owning a caret");
                SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
                reset(rich); hotkey();
                expect(p.quick_active && !p.expanded && GetFocus() == rich, "focused writable RichEdit opens mini without taking focus");
                reset(rich); SendMessageW(rich, EM_SETREADONLY, TRUE, 0); hotkey();
                expect(p.expanded && !p.quick_active, "read-only RichEdit opens the full clipboard");
                reset(choice); hotkey();
                expect(p.expanded && !p.quick_active, "selection-only combo is not a text input");
                reset(edit); p.RememberTarget(root); SetFocus(nullptr); hotkey();
                expect(p.expanded && !p.quick_active, "remembered prior edit focus is not reused after focus clears");
                reset(edit); hotkey(); SetFocus(button); hotkey();
                expect(p.expanded && !p.quick_active, "shortcut after edit-to-button focus change replaces mini with full panel");
                reset(edit); p.hotkeys_suspended = true; hotkey();
                expect(!p.expanded && !p.quick_active, "suspended shortcut opens neither surface");
                reset(edit); allowed = false; hotkey();
                expect(!p.expanded && !p.quick_active, "host hotkey policy is preserved");
                reset(edit); p.shortcut_key = 0; hotkey();
                expect(!p.expanded && !p.quick_active, "disabled shortcut opens neither surface");
                reset(edit); p.enabled = false; hotkey();
                expect(!p.expanded && !p.quick_active, "disabled clipboard ignores a queued shortcut");
                p.enabled = true;
                reset(edit); p.Show(); SetFocus(p.search); hotkey();
                expect(p.expanded && !p.quick_active, "own search field does not reuse a previous external input target");
                reset(edit); p.copy_pending = true; hotkey();
                expect(!p.expanded && !p.quick_active, "in-flight copy is not interrupted by opening another surface");
                reset(edit); p.ime_windows.insert(button); hotkey();
                expect(!p.expanded && !p.quick_active, "active IME candidate UI retains its keyboard ownership");
                p.ime_windows.clear();
                reset(edit); p.shortcut_focus = clipboard::InputFocusSnapshot::Capture(root);
                p.shortcut_pending = true; p.shortcut_deadline = 0; SetFocus(button); p.ShortcutFocusTick();
                expect(!p.shortcut_pending && !p.expanded && !p.quick_active, "delayed focus result is discarded after focus changes");
                reset(button); p.shortcut_focus = clipboard::InputFocusSnapshot::Capture(root);
                p.shortcut_pending = true; p.shortcut_deadline = 0; p.ShortcutFocusTick();
                expect(!p.shortcut_pending && p.expanded && !p.quick_active, "unavailable or timed-out accessibility probe falls back to full panel");
                reset(edit); p.shortcut_pending = true; p.CloseQuick();
                expect(!p.shortcut_pending, "closing or disabling the clipboard cancels pending focus routing");
                reset(edit); const auto snapshot = clipboard::InputFocusSnapshot::Capture(root);
                EnableWindow(edit, FALSE);
                expect(clipboard::NativeEditableInput(snapshot) == false, "disabled editor is never a quick-paste target");
                EnableWindow(edit, TRUE);
            }
        }
        clipboard::AccessibleInputState state;
        state.enabled = state.focused = state.edit = true;
        expect(clipboard::EditableAccessibleInput(state), "focused enabled accessibility edit is an input");
        state.read_only = true;
        expect(!clipboard::EditableAccessibleInput(state), "accessibility read-only state overrides edit control type");
        state.read_only = false; state.focused = false;
        expect(!clipboard::EditableAccessibleInput(state), "unfocused accessibility element is rejected");
        state.focused = true; state.enabled = false;
        expect(!clipboard::EditableAccessibleInput(state), "disabled accessibility element is rejected");
        state.enabled = true; state.edit = false; state.document = true; state.read_only.reset();
        expect(!clipboard::EditableAccessibleInput(state), "generic document or selected page text is not an editor");
        state.read_only = false;
        expect(clipboard::EditableAccessibleInput(state), "editable accessibility document supports contenteditable editors");
        state.read_only = true; state.text_edit = true;
        expect(!clipboard::EditableAccessibleInput(state), "read-only document remains excluded even with a text-edit pattern");
        state.read_only.reset();
        expect(clipboard::EditableAccessibleInput(state), "text-edit provider identifies a custom editable control");
        DestroyWindow(root); FreeLibrary(rich_module);
        std::cout << "Shortcut focus: " << checks - failures << '/' << checks << " checks passed" << std::endl;
        return failures ? 1 : 0;
    }
};
}
int main() {
    IsolatedDesktop desktop;
    if (!desktop.Init()) { std::cout << "BLOCKED: cannot create isolated input desktop" << std::endl; return 2; }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    const int result = lumashot::ClipboardPanelTest::Run();
    CoUninitialize();
    return result;
}
