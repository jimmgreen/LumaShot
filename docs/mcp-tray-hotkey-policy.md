# Tray hotkey suspension policy

## Request

Add independent checked tray-menu options for disabling global shortcuts during
fullscreen games and for manually disabling all global shortcuts. User chose
best-effort game-only detection rather than all fullscreen windows.

## Implementation

- `src/app/preferences.h` / `src/app/preferences.cpp`: default-off persistent
  General/HotkeysDisabled and General/DisableHotkeysInGame fields. Existing keys,
  clipboard/history settings and other preferences are preserved by atomic saves.
- `src/app/hotkey_policy.h` / `src/app/hotkey_policy.cpp`: policy, native checked
  menu items, registration lifecycle and persistence. Manual disable has priority.
- Automatic detection uses only successful SHQueryUserNotificationState results
  equal to QUNS_RUNNING_D3D_FULL_SCREEN, with a visible, non-minimized, non-shell,
  non-LumaShot foreground window covering its monitor. Busy/fullscreen-window,
  presentation, quiet-time and Store-app states alone do not disable shortcuts.
- Detection runs every 500ms only when automatic mode is on and manual disable
  is off. Incoming hotkey messages recheck state before dispatch. No rendering,
  screen capture, process scanning, keyboard hook or network loop was added.
- Suspend unregisters screenshot/GIF/video shortcuts and clipboard Ctrl+Shift+V.
  Resume registers the latest configured combinations. A conflicting key does
  not prevent unrelated keys from restoring; a nonmodal tray warning is used,
  and manually toggling off/on retries after the conflict has cleared.
- `src/clipboard/panel.cpp` / `.h`: independently suspend its hotkey without
  disabling listening, clearing history or hiding manual access. Queued hotkey
  messages obey both current policy and the live application callback.
- `src/app/application.cpp` / `.h`: tray controls and optional policy timer;
  settings edits use effective (possibly empty) shortcut sets and preserve policy
  changes made from the tray while the isolated settings window is open.
- `src/app/main.cpp`: manual second-instance activation, tray starts, delayed
  captures and initial explicit captures use a separate launch message, so they
  remain available while keyboard activation is disabled. Annotation/editor
  local keys and manual recording controls are not global activation shortcuts
  and remain unchanged.
- CMake includes the focused policy module alongside each application target.

## Detection limits communicated to user

Windows reports an exclusive-fullscreen Direct3D application, not a universal
or authoritative game classification. Borderless/OpenGL/Vulkan games may be
missed; an exclusive-fullscreen Direct3D media player may also match. Ordinary
fullscreen browser/video windows are not disabled merely because of size.
Manual disable is the fallback. Changes to OS key ownership follow the 500ms
poll; the dispatch guard prevents launching during that transition, but the
first key arriving before unregister can still be consumed by Windows.

Reference: Microsoft QUERY_USER_NOTIFICATION_STATE documentation:
https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ne-shellapi-query_user_notification_state

## Focused verification

First full Windows /W4 /WX build passed. CTest hotkey_policy, clipboard_panel,
and tool_preferences all passed (1.51s). Tests use synthetic, message-only
windows, temporary INI files and injected game state, never user preferences,
clipboard data or real gameplay. No real-game compatibility claim is made.

`tests/hotkey_policy_test.cpp` checks independent save/load, defaults, policy
precedence, native menu checkmarks/label, shell-state filtering, actual Win32
hotkey registration/unregistration for all three launch keys, queued-message
suppression, setting changes while paused, restore, conflict handling and retry.
The clipboard test checks queued/live guards, manual access and retained history.

Final full Windows build passed. The three focused CTests passed again (0.73s)
after routing second-instance/manual startup through the separate launch message.
App, clipboard and test diagnostics report zero errors/warnings.
Final build/test/package pipeline completed with exit code 0. Installer was
built but not run; the installed application was not replaced.

SHA256:

- `dist/LumaShot-Setup.exe`:
  `b585aafa1b95fa198812ef12dbb896442786a2c5b13cb198683a618a0b68d40a`
- `build/LumaShot.exe` and `dist/LumaShot-setup-payload/LumaShot.exe` match:
  `ff688308f5c8a82d4f3305773c9045be9eedad80cbe7682e08a45b32700a0954`
