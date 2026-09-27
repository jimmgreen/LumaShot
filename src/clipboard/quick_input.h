#pragma once
#include <windows.h>
#include <array>
#include <functional>
#include <memory>

namespace lumashot::clipboard {
inline constexpr UINT QuickKeyMessage = WM_APP + 181;
inline constexpr UINT QuickDismissMessage = WM_APP + 182;
inline constexpr UINT QuickWheelMessage = WM_APP + 183;

// Pure routing state; modifiers and injection provenance come from the caller.
class QuickKeyRouter {
public:
    struct Result { bool consume{}; bool notify{}; bool dismiss{}; bool shift{}; };
    void Start() { active_ = true; }
    void Stop() { active_ = false; }
    bool Active() const { return active_; }
    bool Pending() const {
        for (const auto& key : keys_) if (key.held) return true;
        return false;
    }
    Result Route(UINT key, bool down, bool injected, bool shift, bool ctrlAltWin) {
        if (injected || key >= keys_.size()) return {};
        auto& state = keys_[key];
        const bool releaseAction = key == VK_RETURN || key == VK_ESCAPE || key == VK_F2;
        if (state.held) {
            const bool snapshot = state.shift;
            if (!down) state = {};
            return {true, active_ && (releaseAction ? !down : down), false, snapshot};
        }
        if (!active_ || !down || ctrlAltWin) return {};
        const bool navigation = key == VK_UP || key == VK_DOWN || key == VK_LEFT ||
            key == VK_RIGHT || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT;
        if (navigation || releaseAction) {
            if (shift && key != VK_RETURN) return {};
            state = {true, shift};
            return {true, !releaseAction, false, shift};
        }
        const bool character = (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') ||
            (key >= VK_NUMPAD0 && key <= VK_DIVIDE) || key == VK_SPACE ||
            (key >= VK_OEM_1 && key <= VK_OEM_3) || (key >= VK_OEM_4 && key <= VK_OEM_8) || key == VK_OEM_102;
        if (character) { Stop(); return {false, false, true, false}; }
        return {};
    }
private:
    struct Key { bool held{}; bool shift{}; };
    std::array<Key, 256> keys_{};
    bool active_{};
};

// Install, use, and destroy on the same message-pumping UI thread.
class QuickInput {
public:
    QuickInput();
    ~QuickInput();
    QuickInput(const QuickInput&) = delete;
    QuickInput& operator=(const QuickInput&) = delete;
    bool Start(HWND notify, HWND target, RECT popup_bounds, std::function<bool()> allowed = {});
    void SetBounds(RECT bounds);
    void Stop();
    bool Active() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
