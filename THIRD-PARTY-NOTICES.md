# Third-party components

The root MIT license covers LumaShot's original code only. Third-party files
retain their original licenses and copyright notices.

- LumaText: MIT; see `deps/lumatext/licenses/` and `deps/lumatext-source/LICENSE`.
- Lexilla and Scintilla: see `deps/lexilla/License.txt` and `deps/scintilla/License.txt`.
- ONNX Runtime: MIT and third-party notices in `deps/ocr/`.
- PaddleOCR models: Apache-2.0; see `deps/ocr/LICENSE-PaddleOCR.txt`.
- FFmpeg and its linked codecs: the distributed executable is GPL-3.0-or-later.
  See `deps/ffmpeg/LICENSE`, `THIRD-PARTY-NOTICES.txt`, and `source-lock.json`.
  Every binary release must include `LumaShot-ffmpeg-source.zip` alongside the
  installer. Build it with `scripts/package-ffmpeg-source.ps1`; it contains
  verified upstream archives and the corresponding build scripts.
- Gifsicle: see `deps/gifsicle/COPYING` and source-file notices. The installer
  includes its source archive under `licenses/gifsicle/source.zip` when bundled.
- Microsoft Visual C++ runtime: redistributable under Microsoft's applicable
  terms; the installer includes `NOTICE-VC-Runtime.txt`.

FFmpeg and Gifsicle are separate command-line executables invoked by LumaShot.
The MIT license does not replace their licenses or cover Microsoft runtime files.
