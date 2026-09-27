# UI text rendering

All application-owned visible UI text uses `TextRenderer` in `src/ui/text_renderer.*`, which sends DirectWrite layouts to the bundled LumaText rasterizer. DirectWrite remains responsible for font selection, shaping, line layout, alignment and hit testing. This does not require changing the UI's existing font families.

The shared path covers:

- Screenshot annotations, toolbars, tooltips, magnifier and shared dropdowns.
- Settings labels, shortcut fields, segmented options and buttons.
- Tray and pinned-image menus, pinned-image selection tools and zoom percentage.
- Clipboard panel, search text, preview text and preview language menu.
- Recording controls, preview/export messages and recording-selection instructions.
- Text-entry content and its confirmation/keyboard-hint frame.
- Application-owned informational/error dialogs.

Native text editing remains responsible for input and IME semantics. GDI measurement and an offscreen native EDIT paint maintain layout and caret state; those pixels are never presented. Visible input text is painted with LumaText. Windows-owned file pickers, shell notifications and IME candidate windows remain system-rendered.

`TextRenderer` shares a bounded glyph context on each drawing thread. Render targets and frame objects do not survive a draw call. UI painting remains event-driven.

Do not introduce direct application UI glyph rendering through `ID2D1RenderTarget::DrawText`, `DrawTextLayout`, GDI `TextOut`, or native message boxes. Use the shared renderer or the existing LumaText-backed themed message helper instead. The helper named `PreviewWindow::DrawText` already delegates to `TextRenderer`.

## Verification artifacts

The 2026-09-21 migration's build logs, focused test logs and synthetic previews are under `build/lumatext-ui/`. No personal files, clipboard contents or preferences are used as fixtures.

Focused coverage includes settings light/dark snapshots, recording selection hints and pinned-image zoom labels at 100/150/200% DPI, menu and themed-dialog behavior, input selection/caret/commit/cancel behavior, and LumaText glyph statistics for live input and synthetic IME preedit. The synthetic IME check does not replace manual testing of an installed input method's candidate window.
