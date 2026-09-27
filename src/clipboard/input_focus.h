#pragma once
#include <windows.h>
#include <memory>
#include <optional>

namespace lumashot::clipboard {
struct InputFocusSnapshot {
    HWND foreground{};
    DWORD process{};
    GUITHREADINFO gui{sizeof(GUITHREADINFO)};
    bool caret_visible{};
    static InputFocusSnapshot Capture(HWND foreground);
    bool Current(HWND foreground) const;
};
// nullopt means a custom control needs accessibility metadata, not that it is editable.
std::optional<bool> NativeEditableInput(const InputFocusSnapshot& focus);
struct AccessibleInputState {
    bool enabled{}, focused{}, edit{}, document{}, text_edit{};
    std::optional<bool> read_only;
};
inline bool EditableAccessibleInput(const AccessibleInputState& state) {
    if (!state.enabled || !state.focused || state.read_only == true) return false;
    return state.edit || state.text_edit || (state.document && state.read_only == false);
}
// One metadata-only MTA query at a time. Results never call back into the UI.
// A slow provider may outlive the UI deadline, but cannot create an unbounded queue.
class InputFocusProbe {
public:
    bool Begin(const InputFocusSnapshot& focus);
    std::optional<bool> Result() const;
private:
    struct Job;
    std::shared_ptr<Job> job_;
};
}
