# Clipboard session disk storage and keyboard retrieval

## Current behavior (2026-09-25 update)

This section supersedes the path and retention statements below where they differ.

- Storage root is `%LOCALAPPDATA%/LumaShot/clipboard-history` (not `clipboard-session`).
- Setting `ClipboardPersist` (settings UI: "退出后保留剪贴板历史") defaults to **off**.
  - Off: `SessionStore` runs session-only in `clipboard-history/session-{GUID}`; the directory is removed on disable/exit, and abandoned sessions are cleaned but never restored. Initialization also deletes any `clipboard-history/history-v1` left by an earlier opt-in (flat files only; reparse points and a history held by a live lease are left alone).
  - On: encrypted payloads and index live in `clipboard-history/history-v1` and are restored after restart (the behavior the 2026-09-24 build shipped unconditionally).
  - Changing the setting while the clipboard is enabled rebuilds the store under the new policy, so the in-memory list is reloaded. Turning it off while the clipboard is disabled purges `history-v1` immediately.
- The Win+V low-level keyboard hook runs on a dedicated thread with its own message loop (`WinVShortcut`), so a busy UI thread can no longer delay keystrokes or make Windows silently drop the hook after `LowLevelHooksTimeout`. The callback touches no UI state; the panel installs the hook only while hotkeys are not suspended, and the live game/settings-recording check runs on the UI thread when the posted `WM_HOTKEY` arrives.
- Focused tests: `clipboard_persistence` (opt-out purge, live-lease refusal, restart), `clipboard_session_store`, `settings_dialog` (toggle, defaults, IPC and INI roundtrip).
- Custom groups: the DPAPI-encrypted index is now v2 (magic `0x3248434c`): each entry record gains a `u32` group id, followed by a group table (count, then id, color, name length and UTF-16 name per group). v1 indexes (`0x3148434c`) are still read; unknown group ids are dropped on restore. Session-only mode keeps groups for the current run only; persistent mode stores them with the history. Index limits: 300 entries (100 ordinary + 200 protected), 20 groups, 128 MiB logical data; tracked encrypted files capped at 192 MiB including replacement headroom.

## Scope and privacy

User selected session-only retention, not cross-restart history. Implement storage and keyboard flow first; do not enable the new hover/click preview design or change clipboard layout in this task. No installer execution or replacement of the user's running app.

Payload directory: `%LOCALAPPDATA%/LumaShot/clipboard-session/session-{GUID}`. Only short summaries/metadata remain in the panel history. Payloads are encrypted with current-user Windows DPAPI; filenames expose no clipboard text. A SHA-256 digest supports deduplication and validates decrypted content against the selected entry. This is user-level protection, not isolation from other programs running as the same user, and deletion is not secure forensic erasure.

Normal destruction/disable closes the worker and removes its session directory. Next initialization cleans abandoned session directories, but never restores them. Exclusive leases protect another live instance; cleanup is serialized with session creation, rejects reparse directories and does not traverse unexpected subdirectories. Cleanup can fail on filesystem errors/locks; the next launch retries orphan cleanup. An empty parent directory/lock file can remain.

## Storage and bounds

- Existing limits retained: 100 entries, 64 MiB logical payload total, 16 MiB maximum item. Oldest non-favorite items are evicted; all-favorite capacity rejects new items.
- New asynchronous storage worker: write/read/DPAPI/thumbnail decoding/full-text fallback search, with immutable UI results.
- At most 32 MiB of queued/in-flight capture input, at most 32 queued save/read jobs. Individual disk files checked before allocation; actual tracked encrypted files capped at 96 MiB, including replacement/deletion headroom.
- Metadata summary: at most 512 UTF-16 units plus ellipsis per entry, excluding a split surrogate. Full text and exact clipboard format bytes remain in the encrypted payload, not truncated on paste.
- Queued capture input is not a whole-process memory cap: encryption/decryption, format transfer, allocator, and decoder temporary buffers add transient overhead. Windows system clipboard also has its own copy after actual publication.
- Shared file leases defer deletion until an outstanding reader releases its reference. Deletes run on the worker. Clear cancels both pending reads and captures so late saves cannot repopulate a cleared history.
- No SQLite/FTS database or plaintext full-text index in this version. Summary search is immediate; for the bounded 100-entry history, an asynchronous search checks full text when necessary, one entry per worker turn so confirmed reads can take priority. Stale queries are rejected. Very large matching text histories can take longer; no universal millisecond guarantee.

## Keyboard and rendering

- Ctrl+Shift+V opens with empty query/recent selection and typing focus in search.
- Up and Down route from the native search edit to list selection without a full payload read. Enter queues only the selected payload. Typing remains available after navigating.
- Esc/fold cancels pending paste. Selection/query changes invalidate old copy requests. Actual publication checks panel foreground and original target window/process before writing; automatic input still requires foreground restoration and released modifier keys. Existing Windows/UIPI limits apply.
- Tests use a synthetic paste sink, not actual clipboard publication/SendInput. Real third-party input-app integration is not claimed as tested.
- Image decode is no longer in production painting. At most six currently needed 128px thumbnails are retained (five visible rows plus current existing preview): at most 384 KiB of bitmap pixels, excluding API bookkeeping.
- Decoder source pixel budget: 4,194,304 pixels. Above-budget/invalid images use a placeholder rather than unconstrained decoding. The underlying original clipboard bytes can still be restored. Existing preview uses the same small thumbnail, not a new high-resolution viewer.
- File-icon CPU/GPU caches lowered from 128 to 16 entries each. Worst-case 256px pixels amount to 4 MiB per cache, often much less after transparent-padding trim. Fold clears both caches, image bitmaps, large DIB surface and render target.
- Existing capture support, position persistence, layout, themes, pin restoration/startup and hotkey policies preserved. New preview UI remains design-only.

## Validation status

Full /W4 /WX Windows build passed. Clipboard source diagnostics returned zero warnings/errors.

8/8 focused regressions passed in 14.34s before the extended cycle test: settings_dialog, clipboard_history, clipboard_panel, hotkey_policy, tray_menu, pin_session, clipboard_file_icons, clipboard_session_store. No clipboard_file integration test was run; it would modify the real clipboard and is deliberately excluded. No personal data fixtures.

Storage tests cover 100 encrypted synthetic payloads, metadata residency, selected byte-exact load, full Unicode text beyond summary, case-insensitive background search, digest deduplication/favorites, oversized rejection, cancellation/stale completion, corrupt payload fail-closed, deletion, normal cleanup, parallel live-session protection, and a separate child process exiting without destructors followed by real orphan cleanup without history restoration.

Panel tests cover search-edit Up/Down navigation and scrolling, no payload reads for arrow dispatch, Enter to a synthetic sink, delayed-paste cancellation, full-text tail matches, stale search rejection, actual synthetic DIB decoding, thumbnail pixel limits, and repeated open/fold resource checks.

Preliminary isolated storage measurements (bytes; not whole LumaShot): baseline private 1,302,528; 10 items 1,937,408; 50 items 3,043,328; 100 items 3,072,000. 100 x 512 KiB synthetic payloads amount to 52,431,180 logical bytes, with only 3,000 bytes of index text capacity in that fixture. 100 repeated selected-text loads settled at 1,916,928 private bytes. These payloads test storage, not large-image decode peaks.

Initial 100-cycle real-DIB panel test: closed-state working set 42,504,192 / 42,561,536 / 42,680,320 bytes at cycles 10/50/100; private bytes 23,990,272 / 24,309,760 / 25,157,632. Dedicated resources returned to zero, but private memory rose modestly, so an extended 250-cycle run is being checked rather than claiming zero growth. Keyboard dispatch-only measurement: 1,000 navigation events in 1,180 microseconds; excludes paint, disk load and real OS paste, not end-to-end latency.

## Final memory run

`clipboard-session-cycles-final.log`: 250 open/fold cycles passed in 119.65s with the thread message queue pumped, rather than accumulating unprocessed test notifications. Normal regression defaults to 10 cycles; set `LUMASHOT_CLIPBOARD_MEMORY_CYCLES=250` for this extended run. Each close checks image/file bitmap caches and queued plaintext result bytes are empty.

| Closed cycle | Private bytes | Working set bytes | GDI objects |
| --- | ---: | ---: | ---: |
| 10 | 25,509,888 | 42,573,824 | 50 |
| 50 | 25,939,968 | 42,156,032 | 50 |
| 100 | 25,899,008 | 42,274,816 | 50 |
| 150 | 25,194,496 | 42,323,968 | 50 |
| 200 | 24,752,128 | 42,762,240 | 50 |
| 250 | 26,415,104 | 42,893,312 | 50 |

This is a bounded synthetic UI process, not the user's running LumaShot. Private memory fluctuated rather than growing monotonically; the ending value is not identical to the starting value. No forced working-set trimming was used. This does not prove zero leaks for every input/driver or provide a whole-process hard memory cap.

After final checksum validation changes, isolated storage metrics: baseline private 1,314,816 bytes; 10/50/100 entries private 1,945,600 / 3,047,424 / 3,092,480 bytes. Index text capacity 3,000 bytes; 52,431,180 logical payload bytes stored on disk. Repeated selected-text loads at 10/50/100: 1,654,784 / 2,043,904 / 2,023,424 private bytes. Valid encrypted item substitution under another key is explicitly rejected by digest verification.

Final delivery build succeeded (52 steps), clipboard diagnostics zero returned warnings/errors. In the first delivery regression, 7/8 passed; settings_dialog exited with 0xc0000409 after many passing assertions. A standalone retry passed in 1.80s. This intermittent failure is not claimed fixed or proven caused by this change. Full regression retry passed 8/8 in 18.05s (`clipboard-session-delivery-retest.log`). The earlier settings_dialog failure remains recorded, not silently discarded.

## Verified delivery

Packaging pipeline `cmd_826344c84f9844ea192931593f4adc7f779aac987b69e06e` completed with exit 0. Inno compilation succeeded in 17.781s. Installer was generated only, not executed; running user app was not replaced.

- Installer: `C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`
- SHA256: `4dc8574c402beec3b0c5f29e3a04a7a7d12124c63de66327e7efff2151964aff`
- Build and payload executable SHA256: `9ddb367ce7ec1e3e242729fd5c973bed20c326a2ab43b38fec943fb8d0edf4e4`
- Build/payload equality checked with both matching SHA256 and `cmp`.
- Final logs: clipboard-session-delivery-build.log, clipboard-session-cycles-final.log, clipboard-session-delivery-tests.log (initial settings failure), clipboard-session-settings-retest.log, clipboard-session-delivery-retest.log, clipboard-session-package.log, clipboard-session-installer.log, clipboard-session-hashes.log.

No test in this task published to or emptied the real Windows clipboard. No real third-party target paste or OS reboot was performed; synthetic panel tests replace publication with a test sink. New preview design remains unimplemented/unenabled as scoped.

Existing previously reproduced pin_interaction baseline failure is also outside this task; no full-suite-success claim.