#include "quick_input.h"
#include <utility>

namespace lumashot::clipboard {
struct QuickInput::Impl {
    inline static thread_local Impl* owner{};
    HHOOK keyboard{};
    HHOOK mouse{};
    HWND notify{};
    HWND target{};
    RECT bounds{};
    std::function<bool()> allowed;
    QuickKeyRouter router;

    ~Impl() { Unhook(mouse); Unhook(keyboard); if (owner == this) owner = nullptr; }
    static void Unhook(HHOOK& hook) { if (hook) UnhookWindowsHookEx(hook); hook = nullptr; }
    void Drain() {
        if (!router.Active() && !router.Pending()) {
            Unhook(keyboard);
            if (owner == this) owner = nullptr;
        }
    }
    void Stop() { router.Stop(); Unhook(mouse); Drain(); }
    void Dismiss() {
        if (router.Active()) PostMessageW(notify, QuickDismissMessage, 0, 0);
        Stop();
    }
    bool Guard() const { return GetForegroundWindow() == target && (!allowed || allowed()); }
    static LRESULT CALLBACK Keyboard(int code, WPARAM message, LPARAM data) {
        auto* self = owner;
        if (code < 0 || !self) return CallNextHookEx(nullptr, code, message, data);
        const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if ((key.flags & LLKHF_INJECTED) != 0) return CallNextHookEx(nullptr, code, message, data);
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (!down && !up) return CallNextHookEx(nullptr, code, message, data);
        if (self->router.Active() && !self->Guard()) self->Dismiss();
        const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool modified = ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) |
            GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
        const auto result = self->router.Route(key.vkCode, down, false, shift, modified);
        if (result.notify) PostMessageW(self->notify, QuickKeyMessage, key.vkCode, result.shift ? 1 : 0);
        if (result.dismiss) { PostMessageW(self->notify, QuickDismissMessage, 0, 0); self->Stop(); }
        self->Drain();
        // Unhooking does not destroy Impl, and no callback state is accessed after this point.
        return result.consume ? 1 : CallNextHookEx(nullptr, code, message, data);
    }
    static LRESULT CALLBACK Mouse(int code, WPARAM message, LPARAM data) {
        auto* self = owner;
        if (code >= 0 && self) {
            const auto& mouseData = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
            if ((mouseData.flags & LLMHF_INJECTED) == 0 && self->router.Active()) {
                if (!self->Guard()) self->Dismiss();
                else if (message == WM_MOUSEWHEEL && PtInRect(&self->bounds, mouseData.pt)) {
                    // Route hovered scrolling without activating the popup or scrolling the input app.
                    if (PostMessageW(self->notify, QuickWheelMessage, mouseData.mouseData,
                        MAKELPARAM(mouseData.pt.x, mouseData.pt.y))) return 1;
                }
                else if ((message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                    message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) &&
                    !PtInRect(&self->bounds, mouseData.pt)) self->Dismiss();
            }
        }
        return CallNextHookEx(nullptr, code, message, data);
    }
};

QuickInput::QuickInput() : impl_(std::make_unique<Impl>()) {}
QuickInput::~QuickInput() = default;
bool QuickInput::Start(HWND notify, HWND target, RECT popup_bounds, std::function<bool()> allowed) {
    auto& self = *impl_;
    self.Stop();
    if (self.router.Pending() || (Impl::owner && Impl::owner != &self) || !IsWindow(notify) || !IsWindow(target)) return false;
    self.notify = notify; self.target = target; self.bounds = popup_bounds; self.allowed = std::move(allowed);
    if (!self.Guard()) return false;
    self.keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, Impl::Keyboard, GetModuleHandleW(nullptr), 0);
    if (!self.keyboard) return false;
    self.mouse = SetWindowsHookExW(WH_MOUSE_LL, Impl::Mouse, GetModuleHandleW(nullptr), 0);
    if (!self.mouse) { Impl::Unhook(self.keyboard); return false; }
    Impl::owner = &self;
    self.router.Start();
    return true;
}
void QuickInput::SetBounds(RECT bounds) { impl_->bounds = bounds; }
void QuickInput::Stop() { impl_->Stop(); }
bool QuickInput::Active() const { return impl_->router.Active(); }
}
