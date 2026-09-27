# Clipboard history and edge panel

## Entry points and behavior

- Settings now includes **开启剪贴板**, off by default. Its value round-trips through the INI transaction and version-6 isolated settings IPC.
- Enabling starts event-driven `AddClipboardFormatListener` monitoring, without sampling existing clipboard content. There is no polling/render loop while idle.
- A right-edge collapsed tab displays the clipboard icon, current history count and expansion arrow. Click it, use the tray entry, or press Ctrl+Shift+V to expand. If the shortcut is occupied, the tab and tray remain available.
- The reference-inspired panel contains search, All/Text/Image/Files/Favorites tabs, count/order control, selectable cards, favorite/copy/paste/delete actions, and a preview. Basic code previews have line numbers and keyword/string colors. It is a native Win32/Direct2D/DirectWrite implementation, not HTML.
- Esc or the bottom arrow collapses; the title pin keeps the expanded panel open on focus loss. The gear opens application settings. The more menu offers clear-unfavorited and confirmed clear-all.
- Ctrl+F focuses search, Up/Down selects, Left/Right switches categories, Ctrl+C copies, Enter pastes to the preceding window, Delete removes the selected history entry. Mouse wheel scrolls.
- Unicode text, PNG/DIB images and CF_HDROP file lists are supported. Clipboard formats restore text/images/file references, not arbitrary rich-text/OLE payloads. File history retains paths, not copies of file contents; deleted or moved source files cannot subsequently be pasted from those paths.

## Custom groups (2026-09-25)

- Named groups follow 收藏 in the tab row after a separator; each group tab has a colored dot. The strip scrolls horizontally (wheel over the tab row, Alt+←/→, and auto-reveal of the active tab); soft fades mark clipped edges. A pinned “＋” beside the sort button creates a group.
- At most 20 groups, names up to 12 characters, unique. An entry belongs to at most one group and may also be a favorite. Copying while a group tab is active does not auto-assign. Search is scoped to the current tab.
- Cards show a group chip (hidden inside group tabs) and a folder button. The folder button, right-click on a card, or Ctrl+G opens the group list (✓ marks the current group; 移出分组; 新建分组并移入…). Right-click a group tab for 重命名 / 更换颜色 (cycles 8 colors, menu stays open) / 删除分组.
- Naming happens inline in the self-painted search box (IME supported): Enter confirms, Esc cancels and restores the previous search. Duplicate or empty names are rejected in place.
- Shortcuts: Ctrl+1…9 moves the selection into group N (the same group again removes it); Ctrl+0 removes; Ctrl+G opens the move menu; Ctrl+Shift+N creates a group and moves the selection into it; Alt+←/→ (and ←/→ when the list has focus) cycles all tabs. Held keys do not repeat toggles.
- Deleting a group keeps its entries in 全部. Nothing is evicted when entries leave a group; the ordinary 100-entry budget is re-applied on the next copy.
- The management dialog from the mockup is implemented as right-click tab menus plus inline naming, because the layered panel cannot host native child dialogs/edits.

## Privacy and resource limits

- By default, history and favorites last only for the current run: payloads are DPAPI-encrypted in a per-session folder that is deleted when the feature is disabled or the app exits. The opt-in setting "退出后保留剪贴板历史" (off by default) keeps encrypted history across restarts; turning it off deletes the saved history. Nothing is uploaded. See [session storage](mcp-clipboard-session-storage.md).
- Limits (2026-09-25): ordinary entries roll over at 100 (oldest first). Favorites and grouped entries are *protected*: never evicted, counted separately, at most 200. All entries together are bounded to 128 MiB of model data, 16 MiB per entry. A full protected area only refuses 收藏 / 移入分组 with a prompt; ordinary copies continue. A capture is rejected only if protected data plus the new item would exceed 128 MiB.
- Recognized clipboard exclusion/privacy formats are respected. Unmarked sensitive text cannot be automatically identified; do not enable this feature when collecting sensitive clipboard data is undesirable.
- Clipboard contention uses at most five short retries. Writes allocate buffers before clearing the system clipboard. Self-published changes are not re-recorded.
- Automatic paste checks the destination window and waits for modifiers to be released. Elevated applications or Windows foreground restrictions may require manual Ctrl+V.
- Image previews use bounded WIC decoding and downscaled cached bitmaps. The panel now permits desktop capture (`WDA_NONE`); see `docs/mcp-clipboard-icons-capture.md`. Real mixed-monitor DPI, capture exclusion and cross-application paste still require interactive verification.

## Implementation

- `src/clipboard/history.h`: bounded model, stable deduplication identities, filters and favorites.
- `src/clipboard/native.cpp`: clipboard access, privacy handling, owned-format restoration and WIC thumbnails.
- `src/clipboard/panel.cpp`: native expanded/collapsed UI, search, keyboard input, event-driven listener and paste actions.
- `src/app/preferences.*`, `src/app/settings_process.cpp`, `src/app/settings_dialog.cpp`, `src/app/resources.rc`: persistent enable switch, IPC and settings layout.
- `src/app/application.*`: lifecycle, tray entry and posted settings callback.

## Verification on 2026-09-18

- Full `build.bat` build passed with the existing C++20 /W4 /WX configuration. See `clipboard-build-final.log`.
- `clipboard_history`: passed. Synthetic tests cover deduplication, stable favorites, filters/search, sorting, count eviction, oversized rejection, all-favorite capacity and clearing.
- `clipboard_panel`: passed. Actual hidden native window, synthetic entries, actual search edit notifications, favorite/category/delete actions, WIC DIB thumbnail, disabling and native surface rendering. The fixture deliberately disables clipboard monitoring, global shortcut registration, clipboard writes and SendInput.
- `clipboard_file`: passed. Existing file clipboard regression.
- `tool_preferences`: passed in the first focused run.
- `settings_dialog`: first focused run passed, including the new switch, INI/IPC round-trip and expanded layout. A later run timed out at 30 seconds. Do not count that rerun as a pass.
- `settings_capture_coexist`: failed twice at the foreground acquisition check; subsequent scope-completion check also failed after abort. This run does not establish interactive settings/capture coexistence. The test was not weakened or changed to suppress failure.
- Editor diagnostics returned no errors or warnings.
- Logs: `clipboard-tests.log`, `clipboard-focused-final.log`, `clipboard-coexist-recheck.log`.
- Actual production rendering with synthetic data: `build/clipboard-expanded-light.png`, `build/clipboard-expanded-dark-150.png`, `build/clipboard-folded.png`, `build/settings-light.png`. These were visually reviewed; they are not screenshots of personal clipboard contents or the user's desktop.

## Delivery

Build the existing full payload and `dist/LumaShot-Setup.exe` using the scripts in `docs/installer.md`. Do not run the installer or replace the user's running installation. The installation package path and checksum are reported after packaging.
### Packaging result

- Full payload and Inno Setup compilation succeeded. Installer: `dist/LumaShot-Setup.exe` (37,398,186 bytes).
- Installer SHA-256: `9b5fc73353b4ea9d96168b697ae47cda787bc80821cdbad93229735e3ef37f68`.
- Built and packaged `LumaShot.exe` match: `814078b2fe87cb7a5b5b469fb52cf96498768b75a09e89b4af374f6bbf646994`. Required worker executables and `lumatext.dll` were present.
- Logs: `clipboard-package.log`, `clipboard-installer.log`. The installer was not executed and the installed application was not replaced.