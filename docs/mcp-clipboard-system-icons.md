# Clipboard Windows system file icons

## Request and implementation

Replace the simplified file glyph and hardcoded PDF artwork with real Windows Shell association icons. Preserve panel geometry, controls, search, capture support, movement and persisted position.

- `src/clipboard/file_icons.h/.cpp`: background STA Shell loader, SHGetFileInfo + system extra-large/jumbo image lists, WIC premultiplied pixels, transparent-padding trim and stock-icon fallback. Owned HICONs are destroyed; COM pointers use RAII.
- First path determines multi-file entry artwork. Word/Excel/PDF/archive artwork follows installed file associations, not bundled/custom logos. Existing local folders and executables use their system icons.
- 32 DIP display requests 48px at 100/150% and jumbo at higher scaling. Some associations only provide low-resolution artwork; upscaling cannot invent missing detail.
- CPU cache: 128-entry bounded LRU, queued/inflight deduplication, separate DPI keys, generation invalidation. GPU bitmap cache is separate and released on render-target recreation. History deletion/clear, disable and setting changes clear icon caches.
- Worker joins before panel HWND destruction. Native shell calls cannot be canceled mid-call; an unusually slow third-party handler may delay shutdown. Loading does not occur on the painting thread.
- UNC/mapped/removable paths use type-based lookup rather than explicit file-attribute probes; missing files, reparse points and shortcuts fall back to type icons. Remote folders without a trailing separator cannot be reliably identified without probing. Shortcut-specific target artwork and overlays are not guaranteed.

## Validation

- Full Windows build with existing /W4 /WX passed.
- New `clipboard_file_icons` test: PDF/docx/xlsx/zip/unknown type, isolated temporary Unicode folder, test executable, synthetic unavailable UNC path, 48/256 resolution selection, alpha, cache reuse, deduplication, clear/stale completion and bounded eviction.
- `clipboard_panel` extended with native light 100% and dark 200% synthetic file rows and checks that real shell bitmaps populate both DPI buckets; disable clears both caches.
- Focused clipboard_file_icons + clipboard_panel + clipboard_history: 3/3 passed in 3.19 seconds.
- Reviewed native `build/clipboard-system-icons-light.png` and `build/clipboard-system-icons-dark-200.png`: Windows PDF, Word, Excel and host-associated archive icons visible, transparent backgrounds, unchanged layout.
- Clipboard source diagnostics: zero returned warnings/errors. This supplements, not substitutes for, the successful build.
- Synthetic fixtures only for new tests; no installer execution or OS reboot. Existing unrelated pin_interaction baseline failure remains outside this task; no full-suite-success claim.

## Delivery

Expanded regression: 8/8 passed in 9.82 seconds (settings_dialog, clipboard_file, clipboard_history, clipboard_panel, hotkey_policy, tray_menu, pin_session, clipboard_file_icons).

Important test side effect: the pre-existing clipboard_file integration test publishes synthetic images/text to the real Windows clipboard and empties it on exit. It was included inadvertently; the user was notified. It does not read personal files, but pre-test clipboard contents were not preserved. New icon tests and the panel fixture remain isolated from the real clipboard. Future focused icon runs should exclude clipboard_file.

Delivery complete. Packaging pipeline cmd_83dacba9d88c8a177cb4f8a77106ce2cb3c085d7c511b0dc exited 0; Inno compilation succeeded in 26.344 seconds. Installer generated only, never executed.

- Installer: `C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`
- Installer SHA256: `903d2a4fe72938c63d2b606010b945dbd78528dd34b75ee427493a4f291aba30`
- Matching build/payload LumaShot.exe SHA256: `97a1c74061e7eca1c1d6223d7720cab3ce1740eae5c169fdbd7a9e3be32f0013`
- Build/payload hash equality explicitly checked after packaging. Light native preview presented to user; dark 200% preview also reviewed.
 Logs: clipboard-icons-build.log, clipboard-icons-tests.log, clipboard-icons-regression.log, clipboard-icons-package.log, clipboard-icons-installer.log, clipboard-icons-hashes.log.
