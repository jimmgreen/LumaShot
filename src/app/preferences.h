#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include "model/tool_properties.h"
#include "model/pin_style.h"

namespace lumashot {
struct Preferences {
    ToolProperties tools=[] {ToolProperties value;value.width=2;return value;}();
    bool include_cursor{true};
    bool start_with_windows{true};
    bool paste_as_file{true};
    bool clipboard_enabled{false};
    // Off by default: history lives only in this session and saved payloads are purged.
    bool clipboard_persist{false};
    // The folded edge strip; hidden by dragging it onto the close target, restored
    // from the tray or settings. The hint about restoring it is shown only once.
    bool clipboard_strip_visible{true};
    bool clipboard_strip_hint_shown{false};
    bool hotkeys_disabled{false};
    bool disable_hotkeys_in_game{false};
    int paste_file_format{}; // 0: PNG, 1: JPEG, 2: BMP
    int theme{};
    PinStyle pin_style{DefaultPinStyle};
    UINT modifiers{MOD_CONTROL | MOD_ALT};
    UINT key{'A'};
    UINT gif_modifiers{MOD_CONTROL | MOD_ALT},gif_key{'G'};
    UINT video_modifiers{MOD_CONTROL | MOD_ALT},video_key{'R'};
    UINT clipboard_modifiers{MOD_WIN},clipboard_key{'V'};
    UINT translate_modifiers{MOD_CONTROL|MOD_ALT},translate_key{'Y'};
    std::filesystem::path save_directory;
    // Updater: daily automatic check (after startup), last successful check in
    // Unix seconds, proxy prefixes from the last verified manifest ('|'-separated),
    // and the version that last ran (for the post-update notice).
    bool update_auto_check{true};
    long long update_last_check{};
    std::wstring update_mirrors;
    std::wstring last_run_version;
    static Preferences Load();
    void Save() const;
    static Preferences LoadFrom(const std::filesystem::path& path);
    void SaveTo(const std::filesystem::path& path) const;
    bool Dark() const;
};
bool EditPreferences(HWND owner, Preferences& preferences, const std::function<bool(const Preferences&)>& accept = {});
}