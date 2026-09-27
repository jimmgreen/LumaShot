# Hand-drawn arrow terminal alignment

## Reproduction and scope

A synthetic right-to-left hook with a tight turn toward the released endpoint reproduced the visible off-axis approach beneath an open/arc arrowhead. The initial new regression failed on the unchanged arrow model across 18 scale/rotation/head combinations. The local head-region deviation was about 0.80 physical pixels at 0.5 scale and 1.45 pixels at 1.0 scale (`hand-arrow-baseline-tests.log`). The reference image is a visual symptom, not the exact original mouse sample stream; this fixture represents that class of bend.

The previous implementation shared the exact tip but oriented an open head from the last small polyline segment. Solid/hollow heads had a separate base connection correction; open/arc heads had zero inset and bypassed that correction. On a tight bend, the body across the visible head footprint could lie to one side of that final-segment direction.

## Fix in src/model/arrow.cpp

- For hand-drawn open/arc heads, derive the axis from an arc-length chord covering the local head connection footprint, rather than a potentially subpixel last segment.
- Bring the last short portion onto this axis. Taper the lateral correction to zero with a quintic smoothstep along the already fitted curve, preserving axial progression and sampling.
- Do not add a new terminal cubic: a trial cubic connection produced a small local bulge in the synthetic visual check and was replaced by this limited projection blend.
- Preserve the released endpoint, raw pointer samples, distant curve geometry, undo/redo representation and existing smoothing. Correction lengths are capped by stroke extent, with separate budgets for two-ended arrows.
- No-head and circular markers bypass this open-head correction. Existing filled/hollow attachment behavior is unchanged.

## Verification

- Full build succeeded with C++20 /W4 /WX (`hand-arrow-fix-build.log`).
- Final native `lumashot_hand_arrow_test.exe --connection-preview` passed (`hand-arrow-fix-tests.log`); CTest `hand_arrow` passed (`hand-arrow-final-ctest.log`).
- Tests sample interpolated shaft segments within the connection region, rather than only checking the shared endpoint. Eighteen open/arc combinations (0.5/1/2 scale, three rotations) meet the 0.05 x scale pixel lateral tolerance.
- Additional passing cases: short/repeated/closed double-arc paths, original body sampling/prefix preservation, backward release jitter, broad bends, filled/hollow raster connection, endpoint editing, undo/redo and bounded 20,000-sample geometry.
- Real production renderer preview: `build/hand-arrow-connection-preview.png`. Synthetic data only; no personal desktop capture. The final image was visually reviewed.
- `annotation_render` was also run and failed three non-arrow checks: note-preset contrast, mixed-background readable backing and label text color. Arrow variant/raster checks in that run passed. See `hand-arrow-focused-tests.log`. This broad suite must not be reported as passing; the unrelated text modules were not modified and those failures were not suppressed.
- Editor diagnostics for the arrow model returned no errors/warnings. Real mouse/device trajectories beyond the synthetic fixtures still need user acceptance.

## Delivery

Build the full payload and `dist/LumaShot-Setup.exe`; do not execute the installer. Packaging logs: `hand-arrow-package.log`, `hand-arrow-installer.log`.
### Verified installer

- Installer compilation succeeded; SHA-256: `b6f7720b9542a70fac777886c60d2f23e1c6ce1e07cfae1d192b32c17b8439a8`.
- Built and packaged executable hashes match: `093d67963d71f6852fa1240570923079a2a15f2dd9decd339c8259eec8f1cd5b`.
- The installer was not executed.
