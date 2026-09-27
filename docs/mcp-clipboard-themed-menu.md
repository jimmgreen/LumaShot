# Clipboard themed more menu

## Implementation

The clipboard more button no longer calls TrackPopupMenu or displays a system HMENU. Its menu is painted as an event-driven in-panel overlay below the title bar with the panel's native Direct2D/DirectWrite renderer. It shares the light/dark colors, 12-DIP rounded shape, fine border, restrained shadow, small icons, 13-DIP labels and blue hover/focus treatment. Clear-all uses a muted destructive red; the memory limit is a separate subdued footer.

The overlay stays inside the 480-DIP panel, avoiding monitor-edge overflow. While open the overlapping native EDIT is temporarily hidden and a parent-rendered search label is shown, preventing the child window from painting over the menu. Dismissal restores the native edit. The panel keeps its existing WDA_NONE capture permission.

Clear-all now opens a themed confirmation step in the same overlay rather than a system MessageBox. Cancel is selected by default; only the explicit second action clears favorites and other records. Clear-unfavorited keeps favorites. Clearing history never empties the system clipboard.

Mouse capture dismisses on outside clicks without activating a control beneath. Esc closes only the menu, arrows/Tab navigate, Enter/Space activate. Focus or capture loss closes it. Empty history disables both destructive actions; when all records are favorites, clear-unfavorited is disabled. No persistent render loop or nested menu message loop was added.

## Verification

- Full build passed under /W4 /WX: `clipboard-menu-build.log`.
- `clipboard_panel` passed: `clipboard-menu-tests.log`.
- New checks cover native-search suppression, Esc without folding, wrapped keyboard selection, safe default confirmation, outside-click dismissal without click-through, clear-unfavorited preservation, explicit clear-all and empty-history disabled actions.
- Existing search alignment, position persistence, drag bounds, capture affinity and icon hit-area regressions remain in the same executable.
- Native synthetic-data previews: `build/clipboard-menu-light.png`, `build/clipboard-menu-dark-150.png`, `build/clipboard-menu-confirm.png`. No personal clipboard fixtures were used.
- Custom menu accessibility-provider support was not added; keyboard input is supported, but screen-reader semantics need separate validation.

## Delivery

Build the full payload and `dist/LumaShot-Setup.exe` without installing. Logs: `clipboard-menu-package.log` and `clipboard-menu-installer.log`.
### Verified installer

- Installer compilation succeeded; SHA-256: `7a861dbb9c060fe405c3db5b71c86a1b2137716e6deaad045ec27f17964eda26`.
- Built and packaged executable hashes match: `0b76e8766d100b0b800199a37cdec8bf8e79ef9090d28062fe9d0d71b640566d`.
- Installer not executed; editor diagnostics reported no errors or warnings in `src/clipboard`.
