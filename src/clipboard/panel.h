#pragma once
#include <windows.h>
#include <functional>
#include <memory>
namespace lumashot {
class ClipboardPanel {
public:
    ClipboardPanel(HWND owner,std::function<void()> settings,std::function<bool()> hotkey_allowed = {});
    ~ClipboardPanel();
    ClipboardPanel(const ClipboardPanel&)=delete;
    ClipboardPanel& operator=(const ClipboardPanel&)=delete;
    bool SetShortcut(UINT modifiers,UINT key);
    bool Enable(bool enabled,bool dark);
    // false: history is session-only and previously saved history is deleted.
    bool SetPersistent(bool persistent);
    void Show();
    void SetHotkeysSuspended(bool suspended);
private:
    friend struct ClipboardPanelTest;
    struct Impl;std::unique_ptr<Impl> impl_;
};
}