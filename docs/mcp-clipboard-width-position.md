# Narrow clipboard panel and persistent position

## Changes

- Expanded width is 480 DIP instead of 560 DIP. Height and text sizes are unchanged. Search, categories, toolbar, cards, action clusters and preview coordinates have been reflowed; this is not horizontal bitmap/text scaling.
- Favorite/delete graphics use 0.72 scale (previously 0.86), the footer collapse chevron uses 0.65, and other actions use 0.80. All action hit areas remain 32 x 32 DIP.
- Drag-end saves the common right/top anchor to `%LOCALAPPDATA%/LumaShot/clipboard-layout.ini`. Enabling after launch restores it. The history/privacy setting and clipboard contents are not stored in that file.
- Layout writes use a same-directory temporary INI and replace-on-success; only a completed drag writes the layout. Missing, invalid and out-of-range coordinates fall back to the default location. Negative monitor coordinates are supported. Existing monitor-work-area clamping keeps restored windows visible when display topology changes.
- Existing `WDA_NONE`, drag interaction, folded centering and clipboard history behavior remain unchanged.

## Verification

- Full build passed under /W4 /WX: `clipboard-width-build.log`.
- `clipboard_history` and `clipboard_panel` passed: `clipboard-width-tests.log`.
- Panel fixture checks 480-DIP expanded width, preserved 32-DIP hit areas, drag bounds, anchor retention, WDA_NONE and icon transform restoration.
- Position storage tests use a unique temporary INI (not the user's settings) and verify missing-file fallback, atomic overwrite, negative coordinates and restoration through independent reads.
- Native synthetic-data rendering reviewed: `build/clipboard-expanded-light.png`. No personal clipboard contents were read by tests. Real monitor topology changes and full application restart remain interactive acceptance checks; persistence functions were tested directly.

## Delivery

Rebuilt full payload and `dist/LumaShot-Setup.exe`, without running the installer. Packaging logs: `clipboard-width-package.log` and `clipboard-width-installer.log`.
### Verified installer

- Installer compilation succeeded. SHA-256: `5f22a6b8caab9aa8fd670beb122eca8d10aa1eadc367e74c34a1527d3f41b6df`.
- Build and packaged executable match: `dfd514310a62ef8cf61a9c9b7acec0233df845ba9d488bfa537a10dc914b4dd8`.
- No installation was performed.
