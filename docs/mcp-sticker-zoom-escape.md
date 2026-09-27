# Sticker zoom and Escape close

## Changes

- src/pin/pin.cpp previously capped wheel zoom at 4x and approximately 4096 pixels on the image long edge. Both policy caps are removed.
- src/pin/zoom.h computes a size-dependent safety limit for the composited surface, including frame, shadow and DPI. It is not unbounded zoom: maximum surface edge is 16384 pixels and maximum area is 64M pixels (256 MiB for one BGRA surface, not total process memory).
- Log-space wheel accumulation prevents overflow from extreme deltas. Ordinary 10% minimum zoom, cursor-anchored easing, reverse wheel behavior, image pixels and OCR state are preserved.
- Plain Escape closes the focused sticker through its normal WM_CLOSE path rather than only clearing its text selection. The selection tool window forwards Escape to its owner.
- Explicit Escape also closes a locked sticker. Lock continues to prevent wheel zoom and dragging.
- Repeated keydown events are ignored to prevent holding Escape from closing another sticker after focus changes. Other stickers are not iterated or closed.
- WM_DESTROY stops zoom/badge timers and clears active interaction state; existing destruction cancels OCR and destroys the owned selection tools.

## Verification

- build.bat succeeded under /W4 /WX after fixing a test variable name that conflicted with the Windows small macro.
- Four focused tests passed: pin_zoom_close, pin_zoom_performance, pin_styles and paper_appearance; total 17.82 seconds. Log: pin-zoom-close-tests.log.
- The new test exercises the real wheel and layered-window rendering path beyond both former caps, cursor anchoring, reverse zoom and unchanged source pixels.
- Synthetic OCR selections exercise Escape on the sticker and its actual tools window, a locked sticker, active zoom, preservation of other stickers and key-repeat suppression.
- Pure geometry checks cover all five styles, 96/144/192 DPI, small/4K/long-thin sources and extreme wheel input without allocating giant test images.
- Source diagnostics returned no errors or warnings. Tests use synthetic images and OCR data; no personal files or clipboard fixtures.

## Delivery

- Installer successfully built: dist/LumaShot-Setup.exe.
- SHA-256: 5cc0c93df63a2b4b5c44a7612037b22d611b1a34438e41c875558854b98cec0f.
- Packaged application and recording worker were byte-compared with the current build binaries and match. Previous completed dialog and last-tool persistence changes remain included.
- No installer was run and the installed application was not replaced.
