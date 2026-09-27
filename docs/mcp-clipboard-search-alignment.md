# Clipboard search text vertical alignment

## Fix

The rounded search field uses a centered custom background, but the borderless single-line Win32 EDIT used a fixed top coordinate and height. Native text is aligned to its formatting rectangle rather than automatically centered in the child control. The parent also drew a second copy of the query/placeholder behind the real edit.

`src/clipboard/panel.cpp` now lays out the native edit using its actual font TEXTMETRIC and EM_GETRECT top inset. The text line center is placed at the field's 95-DIP vertical center. Margins are explicit, and font replacement no longer deletes the font before switching the edit to its replacement. In normal use the parent no longer renders duplicate query/placeholder text; Windows owns cue, input, caret and selection rendering. Synthetic parent-only previews retain their fallback text.

## Validation

- Full `build.bat` passed: `clipboard-search-build.log`.
- Native `clipboard_panel` test passed: `clipboard-search-tests.log`.
- New geometry assertions at 100%, 125%, 150% and 200% scale measure the actual child window, actual native font metrics and actual EM_GETRECT inset. Text centers are within 0.51 physical pixels of the field center, and the child remains within the rounded field.
- Existing panel search, hit area, persistence and drag tests remain in that same passing test executable. No personal clipboard or desktop fixtures are used.
- Editor diagnostics for `src/clipboard` reported no errors or warnings.
- Geometry assertions verify the native line box, not every input method's candidate-window appearance. Interactive IME rendering remains an acceptance check.

## Delivery

Rebuild full payload and `dist/LumaShot-Setup.exe` without executing the installer. Logs: `clipboard-search-package.log` and `clipboard-search-installer.log`.
### Verified delivery

- Installer compilation succeeded. SHA-256: `e41934597ca0800ccb2a32311d21b3b6570b7c0a517512c0aefe8f494cbf83db`.
- Build/payload executable hashes match: `5e13310120ba4c11850f56e0d86b1f9689ce0a669cc46cf472b68efb5189d608`.
- Installer not executed.
