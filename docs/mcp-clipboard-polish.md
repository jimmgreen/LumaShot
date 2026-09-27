# Clipboard visual polish and dragging

## Changes

- Collapsed tab shrinks from 36 x 132 to 28 x 96 DIP (about 43% less surface area). Clipboard icon is 17 DIP; the chevron is a 4 x 8 DIP vector. The 20 DIP count badge uses DirectWrite horizontal and vertical centering; three digits use 9 DIP text instead of 10 DIP.
- The entire collapsed tab is draggable. Clicking without crossing the system drag threshold expands it only on mouse-up. Expanded title area (left portion, excluding buttons) drags the panel. Capture loss cancels an unfinished gesture.
- A shared right/top screen-coordinate anchor retains the dragged location across expand/collapse and appearance updates during this process lifetime. No new position preference is written to disk. Each shape is clamped into the relevant monitor work area. The folded tab uses monitor DPI rather than being shrunk to fit the expanded panel height.
- Expanded view uses a subtle pale vertical/diagonal wash, soft card shadows, restrained borders, a stronger selected outline, centered category labels, consistent vector action strokes, and a separated code line-number gutter.
- Cards use content-dependent heights: more room for code, less for links. Image cards use a 96 DIP-wide thumbnail; PDF paths receive a red PDF tile. Timestamp uses Today when applicable. Metadata never substitutes invented original image sizes or filenames.

## Validation

- Full `build.bat` passed under /W4 /WX: `clipboard-polish-build.log`.
- `clipboard_history` and `clipboard_panel` passed: `clipboard-polish-tests.log`.
- Updated actual native panel fixture exercises folded and expanded dragging, no expansion during dragging, preserved anchor, monitor-work-area containment, and 28 x 96 DIP dimensions at 200% scale, in addition to existing search/favorite/delete/thumbnail checks.
- Synthetic production-rendered previews: `build/clipboard-expanded-light.png`, `build/clipboard-expanded-dark-150.png`, `build/clipboard-folded.png`, `build/clipboard-folded-one.png`, `build/clipboard-folded-hundred.png`.
- Tests do not monitor or overwrite the user's clipboard. Real mixed-DPI monitor crossing and interactive foreground/paste restrictions remain manual-verification items. Previous settings/coexistence limitations are recorded in `docs/mcp-clipboard-history.md`; unrelated suites were not rerun.

## Delivery

Build the full installation payload and `dist/LumaShot-Setup.exe`; do not run the installer or replace the installed app. Packaging logs: `clipboard-polish-package.log`, `clipboard-polish-installer.log`.
### Verified package

- Full packaging and installer compilation succeeded; the installer was not run.
- Installer SHA-256: `f771ce9a77ec999687215952d9b3c969d045126f0f364eca56085bde09d1a329`.
- Built and packaged executable hashes match: `fa4cb1b8a46205a8f19965d50fc61b7ca9eb33c6291240346e2e48e22d201b46`.
- No editor errors or warnings were reported for `src/clipboard`.
