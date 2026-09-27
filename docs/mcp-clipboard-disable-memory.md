# Fully release clipboard-specific resources on disable

## Request and confirmed cause

User clarified that “close clipboard” means turning OFF clipboard history in settings, not folding or clearing history. The previous Enable(false) stopped the session store and cleared bitmaps but retained the hidden window/edit, full-size render target/surface, text formats/factories/GDI font/brush and the idle Shell icon worker. This was a real ownership omission, not merely a Windows working-set interpretation issue.

The running installed executable was read-only verified as the preceding session-storage build: SHA256 `9ddb367ce7ec1e3e242729fd5c973bed20c326a2ab43b38fec943fb8d0edf4e4`, PID 42372. An uncontrolled live snapshot had private working set 39,669,760 bytes, private commit 47,808,512 and total working set 92,594,176; these metrics are different, include the entire app, and cannot isolate clipboard memory or establish a leak. No changes to the user's app, preferences or actual clipboard were performed.

## Implementation

`src/clipboard/panel.cpp`:

- Unified disable/destructor cleanup. Mark disabled before teardown; unregister listener/hotkey/timers, release capture, join both workers before destroying their notification HWND.
- Destroy native parent/edit child and release image/file bitmaps, D2D brush/target, bound DIB surface, text formats, code format, DirectWrite/D2D factories, GDI font and background brush.
- Release history/index/search/hit-test vector capacities and query/status strings; clear pending copy/selection/window-target state. Preserve only small placement and policy state for recreation.
- Lazy file-icon cache creation only on enable. Null-safe cache clearing and painting guards prevent disabled callbacks from rebuilding resources.
- Recreate window/fonts/render resources on next enable; clean up partial creation/listener-registration failures as well. Repeated disable is idempotent.
- Clipboard uses an isolated DirectWrite factory for ownership separation. This did not show a material additional numerical saving versus complete teardown with the shared factory; no extra performance claim is made for that flag.
- Folding is still distinct: history stays active. Session-only disk semantics and encryption are unchanged. No new preview interface enabled.

## Controlled comparison

New `clipboard_disable_memory` test uses its own hidden window, isolated temporary encrypted store, two valid synthetic 512px DIBs plus file/text/code rows. It paints a 960x1840 surface at 200% scale, then disables directly while expanded. This exercises the largest retained-surface path; normal user settings flows may first fold the panel, so savings are not guaranteed identical.

Baseline was run BEFORE modifying production cleanup, using the same test with --baseline to collect metrics without asserting the new cleanup contract. Final test runs without that flag, asserting all resource owners are empty, windows destroyed, session files removed, anchor retained, and re-enable succeeds for 30 cycles.

| After disabling | Before: private commit | After: private commit | Before: total working set | After: total working set |
| --- | ---: | ---: | ---: | ---: |
| Cycle 1 | 27,963,392 | 9,170,944 | 46,694,400 | 24,002,560 |
| Cycle 10 | 30,507,008 | 11,149,312 | 48,336,896 | 26,476,544 |
| Cycle 20 | 28,741,632 | 13,012,992 | 48,480,256 | 27,574,272 |
| Cycle 30 | 29,151,232 | 12,689,408 | 48,635,904 | 27,430,912 |

Cycle-30 private commit: 27.8 -> 12.1 MiB. Total working set: 46.4 -> 26.2 MiB. Process handles: 324 -> 240. After-change disabled GDI objects stabilize at 42 after warmup; clipboard-owned handles/COM fields are individually checked empty. Remaining library/runtime/system font caches are not all clipboard-owned. Cold and warm whole-process numbers are not expected to match exactly.

These are isolated test-process measurements, NOT the user's full LumaShot memory or an end-to-end promise that the app will drop below 30 MiB. Other app features, loaded code/shared DLL pages and allocator/runtime caches remain. No EmptyWorkingSet, SetProcessWorkingSetSize or artificial trimming was used.

## Validation and delivery

- Full /W4 /WX build succeeded; source diagnostics returned zero warnings/errors before the isolated-factory refinement; the subsequent full build also passed.
- Initial focused regressions: 5/5 passed in 27.12s (history, panel, icons, session store, disable lifecycle).
- Final isolated-factory disable test passed all 30 recreation/cleanup cycles.
- Tests never open/read/write/empty the system clipboard or send paste input. The existing idle_memory test was deliberately NOT run because its global FindWindow shutdown could target an already-running user instance.
- Existing settings_dialog intermittent failure and pin_interaction baseline failure remain outside this task. Settings test is not included in this task's focused regression; no full-suite-success claim.
- Final 8-test regression passed in 27.56s: clipboard_history, clipboard_panel, hotkey_policy, tray_menu, pin_session, clipboard_file_icons, clipboard_session_store, clipboard_disable_memory. Final source diagnostics also returned zero warnings/errors.
- Final pipeline cmd_de046a25cc4adfe295e709ac8d82615f4930efd1d1118108 completed exit 0 in 70.106s; Inno compilation succeeded in 18.515s.
- Installer: `C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`
- Installer SHA256: `0fb8d5d3c7a8aef5427c844298d40d42af70eb76b0a322b4d67483a77116490e`
- Build/payload exe SHA256: `ad18e955a8f6c15a4115ae525f9c7a5ca5efa2f44c2a1329349f2f41f035bc15`; equality verified by matching hashes and cmp.
- Installer generated only, never executed. Running app was not closed, restarted or updated. User must install this build before observing changed disable behavior.
- Earlier package pipeline cmd_6a8ed43edf066356666048bbcff8805021f15dd439878dbe is superseded; do not use its intermediate hashes.

Logs: clipboard-disable-baseline-build.log, clipboard-disable-baseline.log, clipboard-disable-build.log, clipboard-disable-tests.log, clipboard-disable-isolated-build.log, clipboard-disable-isolated-test.log, clipboard-disable-final-tests.log, clipboard-disable-final-package.log, clipboard-disable-final-installer.log, clipboard-disable-final-hashes.log.
