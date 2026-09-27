# Clipboard system corners

The clipboard uses a normal HWND with WS_THICKFRAME, full client nonclient handling, and a DirectComposition premultiplied swap chain. DWM owns the only visible outline. Do not add WS_EX_LAYERED, UpdateLayeredWindow, or SetWindowRgn: Microsoft excludes per-pixel layered and region windows from system rounding.

DWMWA_SYSTEMBACKDROP_TYPE / DWMSBT_TRANSIENTWINDOW requests Desktop Acrylic on Windows 11 build 22621+. Unsupported backdrop calls fall back to opaque content. Content is submitted only when invalidated; disable releases the composition device and surfaces. LumaText rendering remains unchanged.

Official references:
- https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners
- https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type
- https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-createtargetforhwnd

Verification: clipboard appearance captures only synthetic windows, checks all four exterior corners against black and checks multiple intermediate coverage levels on every corner, in both themes and both sizes. Layout is also rendered at 100/150/200 percent. Related panel, pin, features, preview, disable-memory and resource-audit tests pass. Installer is built separately; no installation is performed by tests.
