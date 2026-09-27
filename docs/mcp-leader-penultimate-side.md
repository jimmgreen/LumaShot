# Clipboard preview label and number leader side

## Request and interpretation

Remove the redundant 图片预览 caption only, retaining the image content preview.
Choose leader attachment side from the penultimate point in target-to-badge order,
not the distant annotated target. Storage runs in the reverse order: badge at
index 0, adjacent elbow at index 1, remote elbow at index 2, target at index 3.

## Implementation

- `src/clipboard/panel.cpp`: image-kind metadata caption is empty; text/file
  captions, preview image, controls and layout are unchanged.
- `src/model/selection.cpp`: shared `LeaderBadgeAnchor` resolves left/right from
  the adjacent elbow relative to the badge center. On the center line (0.01px
  numerical tolerance), retain the prior side instead of consulting remote points.
- Both free-vertex edits and badge reconnection use this rule. Remote vertex
  movement keeps the adjacent elbow intact. Badge movement retains its existing
  adjacent-elbow offset behavior and keeps the remote elbow/target fixed.
- Inferred initial leaders retain their existing generated geometry. There is no
  storage schema change and no change to whole-group transforms or undo history.

## Validation

- Windows full build with project /W4 /WX succeeded. A test variable originally
  collided with the Windows `near` macro; renamed before the successful build.
- `selection_edit`, `clipboard_panel`, `selected_mark_properties`,
  `selected_property_controls`, `clipboard_history`, `hand_arrow`: all passed.
- New regression cases cover both sides, 0/30/-45 degree rotations, negative
  origins, remote elbow/target crossing without flipping, adjacent elbow crossing
  with flipping, center-line stability, and exact undo/redo restoration.
- `tests/number_reconnect_cases.h` attachment oracle now independently checks
  the adjacent elbow rather than incorrectly requiring the far-target side.
  Its existing badge resize/move and property reconnection cases passed.
- Source diagnostics: zero errors or warnings reported.
- Synthetic clipboard image snapshots added for light and dark themes in
  `tests/clipboard_panel_test.cpp`; no user clipboard content is used.
- No assertion that unrelated full-suite failures previously documented in
  `docs/mcp-hand-arrow-terminal-alignment.md` were fixed by this change.

## Delivery

Package generated with `scripts/package.ps1 -PackageName LumaShot-setup-payload`
and `scripts/build-installer.ps1`. Installer is not executed.
Final combined CTest run: all six tests passed (1.34 seconds).
Light/dark native clipboard snapshots visually checked: the redundant image
caption is absent, the selected synthetic image remains visible, and the card
layout and copy action are unchanged. Test diagnostics also report zero issues.
Installer compile succeeded in 169.984 seconds; package/build pipeline exit 0.
No installer was run. SHA256:

- `dist/LumaShot-Setup.exe`: `4e230be80f8ea867f6dad560c2d389538a2cdd845d317f2dbac48c034b223f23`
- `build/LumaShot.exe` and `dist/LumaShot-setup-payload/LumaShot.exe` match:
  `eeacac2132991ffb93ff03f644afa6fa343af1934dadc74727e54d10317b7fef`
