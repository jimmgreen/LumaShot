# Windows installer

Build the application using `build.bat`, then run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-ffmpeg-source.ps1
powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -PackageName LumaShot-setup-payload
powershell -ExecutionPolicy Bypass -File scripts/build-installer.ps1
```

Publish `LumaShot-ffmpeg-source.zip` alongside every installer, plus SHA-256
checksums. Keep the installer AppId stable and increment `-AppVersion` for
subsequent releases. Users download the new installer and install over the
existing directory; no uninstall is required. There is no in-app update check.

Testing deliveries after application changes must include a freshly built `dist/LumaShot-Setup.exe`, with its path and SHA-256 reported to the user. Do not install it automatically; the user runs it for testing.

The result is `dist/LumaShot-Setup.exe`. Inno Setup 6 must be installed. The build script searches PATH, the current user's Programs directory, and both Program Files directories. Override the compiler using `-IsccPath`. Optional `-PayloadDir`, `-OutputDir` and `-AppVersion` parameters select an existing packaged payload, output directory and version (default `0.1.0`). Compilation does not run the installer or stop any application.

The installer targets Windows 10 or later on systems supporting x64 applications. It installs for the current user without administrator privileges to `%LOCALAPPDATA%\Programs\LumaShot`, creates a Start menu shortcut, and offers an unchecked desktop shortcut option. Windows Installed apps provides the uninstaller. The stable AppId allows subsequent installers to upgrade the same installation.

The component selection page includes **Offline text recognition (OCR)**, checked by default for a new full installation. Uncheck it to omit the offline models, dictionary, OCR worker and ONNX runtime. The capture toolbar hides its OCR button and the pinned-image menu hides text recognition when those required assets are absent. Installing OCR later restores these actions. Silent installations can choose `/COMPONENTS="core"` or `/COMPONENTS="core,ocr"`.

When upgrading and deselecting OCR, the installer deletes only the distribution's known OCR assets, including their packaged license and manifest files. It removes the OCR directory only if empty; unrelated files such as a user-added note remain. Application settings are preserved. OCR models are unchanged by this option. The download still contains both components, so deselection reduces installed size rather than download size.

Upgrades automatically request a normal exit from the running LumaShot at the installation path, wait up to 15 seconds for it to finish saving/closing, and then replace its files. A timeout stops installation with a retry message rather than terminating the application. Inno Restart Manager automatically handles remaining file locks. After successful installation (including silent installation), the installer starts LumaShot with --background as the original user. This creates the tray process without opening capture or settings; an existing instance is left running without triggering capture. The installer does not register login startup. Settings in `%LOCALAPPDATA%\LumaShot\settings.ini` remain separate from installed files and are preserved during upgrades and uninstall. The installer is unsigned unless a separate signing step is added.

## Package size

The installer uses solid LZMA2 ultra64 compression. Developer CSV traces, TXT test logs and JSON comparison outputs under `docs` remain in the portable validation payload but are excluded from the installer. User documentation, screenshots and all third-party license notices remain included.

The packaged Visual C++ runtime is restricted to `msvcp140.dll`, `msvcp140_1.dll`, `vcruntime140.dll` and `vcruntime140_1.dll`. The packager inspects direct and delayed imports of every EXE and DLL with the installed Visual Studio `dumpbin`; a newly required missing CRT DLL fails packaging instead of silently producing a broken installation. This does not depend on a separately installed VC redistributable.


## Running upgrade verification

Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-installer-upgrade.ps1`.
The test derives an isolated installer from the production script, uses a synthetic hidden host with its own window class and app id plus tiny synthetic OCR assets, disables uninstall registration and shortcuts, and installs only under build/installer-upgrade-test. Four passes select core only, core with OCR, core only again, and core with OCR again. It checks OCR omission, installation, removal and restoration, core payload integrity, background startup, WM_CLOSE for each running upgrade, and background relaunch. Synthetic settings and an unknown `ocr/user-note.txt` must survive every pass. It does not install over the user's app or read personal preferences.
