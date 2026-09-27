# Clipboard capture permission and smaller icons

## Changes

- Removed `WDA_EXCLUDEFROMCAPTURE`; the native clipboard window now uses `WDA_NONE`. Both expanded and folded views use this same capturable window.
- Header/list action graphics are scaled to 86% around the center of their unchanged 32 x 32 DIP hit targets. Copy, paste, delete, favorite, settings, sorting and collapse remain easy to click.
- Selected favorite stroke reduced from 2.8 to 1.9 DIP before the shared scale, reducing the heavy blue appearance.
- Header clipboard mark reduced from 34 to 30 DIP; folded clipboard mark reduced from 17 to 15 DIP, preserving horizontal centering.
- Text/code/link type marks reduced from 28/22/24 to 25/19/21 DIP. Cards, search, text, count alignment and dragging are unchanged.

## Verification

- Full build passed with /W4 /WX (`clipboard-icons-build.log`).
- `clipboard_history` and `clipboard_panel` passed (`clipboard-icons-tests.log`). New assertions check `GetWindowDisplayAffinity == WDA_NONE`, preserved 32 DIP action hit targets, and restoration of the render transform after drawing smaller icons.
- Editor diagnostics for `src/clipboard` reported no errors or warnings.
- Actual native rendering with synthetic data was inspected: `build/clipboard-expanded-light.png` and `build/clipboard-folded.png`.
- No personal clipboard content or desktop pixels were used as test fixtures. The capture restriction was verified through the Windows affinity API; an end-to-end screenshot with every external screenshot utility was not run.
- The existing automatic fold-on-focus-loss behavior remains: use the header pin to keep the panel visible when switching applications. This is independent of capture permission.

## Delivery

Full payload and installer are rebuilt by the existing packaging scripts. Logs: `clipboard-icons-package.log` and `clipboard-icons-installer.log`. The installer is not executed automatically.
### Package verification

- Installer compilation succeeded. SHA-256: `f302f20e2b221f7f6824d24742d5e3be1fe04cfa3be4ec9a33e9fd2b5cfb175f`.
- Build/payload executable hashes match: `2c9ff596c53a19ab0cb2b2c92c459251066320167aa24e743a3b83490ccd5562`.
- The installer was not run.
