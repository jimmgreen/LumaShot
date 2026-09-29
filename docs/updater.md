# In-app updater

LumaShot checks GitHub Releases for a newer version, downloads the installer and,
after the user confirms, installs it silently and restarts. It is designed for
networks where `github.com` is slow or unreachable (mainland China): the same
files are fetched through public GitHub file proxies, and integrity never depends
on those proxies.

Code: `src/update/` (`manifest.*`, `http.*`, `updater.*`), wired into
`src/app/application.cpp`; tests: `tests/update_test.cpp` (`ctest -R update_manifest`).

## User-facing behavior

- **Automatic check**: 90 s after startup and then every 6 h the app re-evaluates;
  it checks at most once per 24 h (`UpdateLastCheck` in `settings.ini`). It can be
  turned off in Settings → 自动检查更新. Automatic checks fail silently.
- **Manual check**: tray → 检查更新… or Settings → 检查更新. Reports "up to date" and
  errors.
- **New version found**: themed prompt (下载并安装 / 稍后). If a capture, recording,
  long capture or settings dialog is active, an automatic prompt waits until idle.
- **Download**: background worker; tray tooltip shows progress, the tray menu offers
  取消下载更新. The installer is stored as
  `%LOCALAPPDATA%\LumaShot\Updates\LumaShot-Setup-<version>.exe`.
- **Install**: second prompt (立即安装 / 稍后). The file is re-verified, then launched
  with `/SILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCANCEL`. The installer closes
  LumaShot through `WM_CLOSE` (pins are preserved), keeps the previous folder and
  components, and restarts `LumaShot.exe --background`. Installing is refused while
  a recording is running. The next start shows a "已更新到 x.y.z" balloon and prunes
  old installers.

## Sources

For every check the manifest is requested **in parallel** from:

1. `https://github.com/jimmgreen/LumaShot/releases/latest/download/lumashot-update.txt`
2. the same URL behind each prefix proxy: `gh-proxy.com`, `ghfast.top`,
   `ghproxy.net`, `gh.llkk.cc`, `gh.zwy.one` (`DefaultMirrors()`), plus any extra
   prefixes remembered from the last verified manifest (`UpdateMirrors`).

The check ends when every source has answered, 3 s after the first valid manifest,
or after 15 s. The newest valid version wins. The installer is then downloaded
**sequentially**: sources that served a valid manifest (fastest first), then the
signed mirror list, the built-in list, and the direct URL. A source is abandoned on
error, HTTP ≠ 200, a hash/size mismatch, or throughput below 24 KB/s over 20 s
(except the last source). WinHTTP uses the system/WPAD proxy, HTTPS only, and never
follows https → http redirects.

Public proxies change often. Every release manifest carries the current proxy list
(`mirror=` lines), so dead proxies can be replaced by publishing a new manifest
without shipping a new client.

## Security model

Proxies and mirrors are untrusted transport.

- `lumashot-update.txt` is signed with **ECDSA P-256 / SHA-256**. The client embeds
  the public key (`kReleaseKey` in `src/update/manifest.cpp`) and verifies it with
  CNG (`BCryptVerifySignature`) before reading any field.
- The manifest pins the installer **size and SHA-256**. The download is hashed while
  streaming, rejected on mismatch, and verified again right before launch.
- The installer URL is built from the signed `tag` + `file`, restricted to
  `https://github.com/jimmgreen/LumaShot/releases/download/…`; names allow only
  `[A-Za-z0-9._-]`.
- Only versions strictly newer than the running build are offered (no downgrades).
- The private key never enters the repository.

## Manifest format

UTF-8, LF line endings, one `key=value` per line. The last line signs every byte
before it:

```
LumaShot-Update 1
version=0.2.0
tag=v0.2.0
file=LumaShot-Setup.exe
size=37644912
sha256=<64 lowercase hex>
published=2026-09-29T12:00:00Z
notes=<UTF-8, "\n" = newline, "\\" = backslash>
mirror=https://gh-proxy.com/
…
signature=<base64 of the 64-byte r||s signature>
```

Unknown keys are ignored (forward compatible); duplicate keys, CR characters, bad
versions or missing `version/tag/file/size/sha256/notes` are rejected.

## Release procedure

The version comes from `project(LumaShot VERSION x.y.z)` in `CMakeLists.txt`
(`scripts/build-installer.ps1` reads it; `scripts/installer.iss` has a matching
default).

1. Bump `CMakeLists.txt` (and the `installer.iss` default), build, test, and build
   `dist\LumaShot-Setup.exe`.
2. Sign the manifest for that exact installer:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\update-signing.ps1 -Action Sign `
     -Version 0.2.0 -Installer dist\LumaShot-Setup.exe -NotesFile build\notes.txt `
     -Output dist\lumashot-update.txt
   powershell -ExecutionPolicy Bypass -File scripts\update-signing.ps1 -Action Verify `
     -Manifest dist\lumashot-update.txt -Installer dist\LumaShot-Setup.exe
   ```

   `-Mirror` overrides the proxy list written into the manifest.
3. Upload `LumaShot-Setup.exe` and `lumashot-update.txt` (plus `SHA256SUMS.txt`) to
   the GitHub release and mark it **Latest**. The `latest/download/` URL must
   resolve to the new manifest.
4. After publishing, `build\lumashot_update_test.exe --live` fetches the manifest
   from GitHub and every proxy and verifies it with the embedded key.

## Signing key

- Stored at `%USERPROFILE%\.lumashot\release-signing-key`, encrypted with DPAPI
  for the current Windows user, in a folder restricted to that user.
- **Back it up**: `-Action ExportBackup -BackupPath <file>` writes the raw key
  (base64). Keep that file offline (USB drive or password manager), then delete the
  local copy. Restore on another machine with `-Action ImportBackup`.
- If the key is lost, existing installs can no longer be updated in-app: a new key
  requires shipping a client with the new public key, which users must install
  manually once.
- `-Action PublicKey` prints the C++ array for `kReleaseKey`.
