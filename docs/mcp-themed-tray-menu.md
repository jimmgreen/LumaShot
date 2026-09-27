# Unified themed tray menu

## Request and design

Replace the native white tray context menu with the application's visual language.
Preserve every existing command, cursor checkmark, clipboard entry and both new
hotkey-policy toggles. No settings migration or command behavior change.

The menu uses a 300-DIP rounded panel plus 6-DIP shadow margins, 34-DIP rows,
13-DIP Microsoft YaHei UI text, muted vector icons, blue hover, independent
right-aligned checkboxes, thin separators and a secondary shortcut column.
Colors follow light/dark Preferences::Dark(), matching the existing clipboard
and settings palette. Native rendering (D2D/DWrite/TextRenderer), not an image
mockup or web view. It remains capturable with WDA_NONE.

## Changes

- `src/ui/tray_menu.h` / `src/ui/tray_menu.cpp`: import the existing HMENU command
  model, preserving IDs, checked/disabled flags and optional clipboard entry;
  render and track a layered popup without a native menu border.
- `src/app/application.cpp`: replace only TrackPopupMenu with TrackTrayMenu.
  The existing handlers, settings persistence, recording launch and shortcut
  policies remain unchanged.
- `CMakeLists.txt`: add the focused renderer to lumashot_ui and its regression
  test target. No sibling project touched.

The popup is DPI-scaled for the anchor monitor and clamped to its work area,
including negative origins. Rendering is invalidation-driven; no idle timer or
capture loop. A retained text renderer avoids rebuilding its context each hover.

Mouse hover/click and Up/Down/Home/End/Enter/Space are supported. Separators and
disabled rows are skipped. Escape, outside click, cancel mode and activation loss
dismiss without dispatch. A drag released on a different row does not select.
The loop preserves WM_QUIT; destruction releases mouse capture and the popup.
Clicking a divider is inert rather than selecting or closing the menu.

## Verification

- Full Windows /W4 /WX build passed after adding an explicit stdexcept include.
- Initial CTest run: tray_menu, hotkey_policy and clipboard_panel all passed
  (2.63 seconds). Final expanded regression also passed: all three tests, 3.30 seconds.
- Synthetic production-rendered PNGs generated for light/dark at 100/150/200%.
  Light 100% and dark 200% visually reviewed: labels, shortcut column, checked
  and unchecked states, separators, icons and corners are aligned and unclipped.
- The menu test uses only generated command data and native test windows, never
  user screenshots, clipboard contents or preference files.
- Live popup tests cover command dispatch, keyboard selection, automatic-game
  toggle ID, outside click, Escape, cancel mode and focus loss in both themes.
  Expanded cases cover divider click and press/release across different rows.
- Source diagnostics report no errors/warnings. No unrelated legacy suite run.

## Delivery

Native side-by-side preview uses synthetic toggle states, not the user's saved
settings. Installer build uses scripts/package.ps1 and scripts/build-installer.ps1.
Final build and all three focused tests passed. Source/test diagnostics are clear.
Final build/test/package pipeline completed with exit code 0. Installer was not
run and the installed app was not replaced. SHA256 verification:

- `dist/LumaShot-Setup.exe`:
  `918bd2dc4760a3b0d3164ca72b3609061e26c7b7443c81077b2f5a3820bbbbb3`
- `build/LumaShot.exe` and `dist/LumaShot-setup-payload/LumaShot.exe` match:
  `f97bec7ed53f2b57e4898f942bffea274dbaefce57995d69ce4e20ae6f21a9ae`
