# LumaShot

Independent Windows C++20 screenshot application. Do not modify the sibling Pulse project.

- Keep capture, cursor handling, annotation, UI, export and preferences in focused modules.
- Use Win32, Direct2D, DirectWrite and WIC. No always-running capture or rendering loop.
- Render all application-owned UI text through `TextRenderer` / LumaText. DirectWrite and native EDIT may still provide layout and input semantics; do not draw visible UI glyphs directly with D2D/GDI. Windows-owned file dialogs, notifications and IME candidate windows remain system-rendered.
- Preserve the cursor shape, hotspot and position sampled before showing the capture overlay.
- Keep screen/image coordinates in physical pixels; UI dimensions in DIP, with Per-Monitor V2 awareness.
- Keep desktop capture and PNG encoding off the UI thread. Discard canceled session results.
- Use RAII for handles and COM resources; build with /W4 /WX and C++20.
- Run focused tests for changed behavior. Compilation alone is not functional or visual verification. Run only regression tests related to the current change; do not rerun unrelated legacy suites by default.
- Do not use personal files, preferences or clipboard contents as test fixtures.
- Keep one-off logs, generated previews, baseline snapshots and temporary scripts under build/, not the repository root; choose test working directories accordingly. Reusable tools belong in scripts/ and lasting documentation in docs/.
- No recursive or unrequested agent delegation.
- Testing delivery: after each requested application change, build and verify a Windows installer (dist/LumaShot-Setup.exe) for the user, not only loose build binaries. Include all completed changes, report the installer path and SHA-256, and respect any host confirmation required for packaging. Do not run the installer or replace the installed app unless the user requests installation.

Build with `build.bat`. Focused tests: `build\lumashot_capture_test.exe`.
