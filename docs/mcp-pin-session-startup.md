# Persistent pin session and login startup

## User decisions

The cursor report was withdrawn as normal; cursor capture behavior was not changed.
Unclosed pins should return when LumaShot restarts. The user explicitly requested
an Open at login setting, checked by default, with restoration tied to LumaShot
startup. Manual launches restore the session even if login startup is disabled.

## Implementation

- `src/pin/session.h` / `src/pin/session.cpp`: versioned local session store under
  `%LOCALAPPDATA%/LumaShot/pin-session`. Compressed, lossless PNG payloads contain
  display image, OCR source and original editable background. Identical frames
  are deduplicated within a payload. Explicit field serialization preserves all
  current Mark properties, independent leader vertices and text decorations.
- A small atomic index stores image origin, zoom, lock state, sticker appearance
  and OCR-request state. Per-content revisions avoid PNG recompression when only
  a pin moves/zooms/locks. A new random namespace per store run prevents replacing
  image data referenced by the last good index before committing the new index.
- Writes are serialized on DeferredWriter's COM-initialized background thread.
  PNG decoding also runs on a worker; startup waits for that finite load before
  creating restored windows. There is no continuous capture/render loop.
- `src/pin/pin.cpp` / `.h`: checkpoint on creation, annotation/decorations,
  completed movement/zoom and style/lock changes. Explicit close updates the
  saved index and flushes before releasing the pin. Application shutdown instead
  freezes a final snapshot; subsequent native window destruction cannot clear it.
- Both host and pin windows handle Windows query/end-session messages. A canceled
  end-session does not freeze persistence. Restored windows do not steal focus,
  and positions are clamped to available monitor work areas. Zoom is bounded by
  the existing surface resource limit.
- `src/app/startup.h`: quoted per-user Run command ending in --background; no
  administrator permissions needed. `StartWithWindows` defaults true and persists
  independently of the two hotkey-policy toggles. App startup applies the setting;
  a settings save applies it transactionally with rollback on preference failure.
- `src/app/settings_dialog.cpp`, `src/app/resources.rc`: matching owner-drawn
  startup switch and explanation; panel expanded from 872 to 934 DIP and remaining
  sections shifted together. Settings IPC version advances from 6 to 7 and carries
  the new flag. `scripts/installer.iss` removes only LumaShot's Run value on uninstall.

## Storage safety / scope

Only local cache storage; no upload, no test reads of personal screenshots or
preferences. Explicitly closed screenshots are garbage-collected only after the
new index commits. Unreadable payload entries are retained for retry, not silently
deleted. A malformed index disables writing for that run to preserve original
files and reports a restoration problem. Size/count/enum/finite-number checks
bound decoding; the same 512 MiB archive limit is checked before committing a
write, so an oversized live snapshot cannot replace a readable previous index.
Obsolete temp files cannot become active sessions.

Disk-write failure triggers a nonmodal tray warning and retains the previous
committed session. Forced power loss or immediate process termination can still
lose the most recent unfinished checkpoint; orderly exit and Windows shutdown
are explicitly flushed. Current editable annotations are preserved, but transient
selection, OCR results and undo/redo stacks are intentionally not serialized.
OCR can be rerun from its saved source when available. Per-pin stacking order is
creation order rather than a persisted foreground history.

This cannot recover unrecorded pins from the older installed version. User was
warned to save important existing pins before upgrading. No installed app was
closed, replaced or installed during this task, and the real login Run key was
not changed by tests.

## Verification

- Final full Windows /W4 /WX build passed. Pin/app/test diagnostics: zero issues.
- Seven focused CTests passed, 5.68s: pin_session, pin_styles, pin_zoom_close,
  pin_save_reentry, settings_dialog, hotkey_policy, tray_menu.
- Pin-session regression uses synthetic pixels, Unicode annotations and all Mark
  fields; verifies PNG exactness, editable base/OCR distinction, metadata-only
  saves, close cleanup, corrupt payload retention and manifest rejection.
- Live PinManager restart validates lock/zoom/annotations and visible placement.
- Separate child processes validate write -> process exit -> restore two pins,
  close one -> process exit -> restore one, and Windows shutdown messages followed
  by window destruction -> next-process restore. No actual OS reboot was performed.
- Startup on/off, Unicode/space path quoting and --background are tested in an
  isolated temporary HKCU test key, never the user's real Run key.
- Settings switch layout/toggling/saving and light/dark/DPI rendering tested. Native
  settings-light.png visually reviewed with the new checked-by-default control.

### Existing baseline failure (not concealed)

The larger pin_interaction suite fails at "double and triple click on text never
closes pin" and then dereferences the closed pin (SEGFAULT). This was reproduced
with the pre-change pin.cpp in a separate baseline executable, not merely assumed
unrelated. pin-session-baseline-pin.cpp and pin-session-baseline-test.cpp are
scratch reproduction sources; their temporary CMake target was removed. Logs:
pin-session-tests.log, pin-session-interaction-retest.log,
pin-session-baseline-build.log, pin-session-baseline-test.log. This existing
interaction logic was not modified to broaden this feature's scope.

## Delivery

Delivery build and the same seven focused tests passed again (7.40s) after the
archive-write size guard. Packaging/compiler pipeline exit code 0; Inno compile
166.079s. No installation or actual system reboot was performed.

SHA256:

- `dist/LumaShot-Setup.exe`:
  `1c87b9736fb2e95291f47c0165d05437b24b1db303cb9cf6f7f39ec6a9ba8310`
- `build/LumaShot.exe` and `dist/LumaShot-setup-payload/LumaShot.exe` match:
  `b532475ebf66c0503378efe7feb37573d48dd2f04e6ceb3b0c0cd645fd7cc2bd`
