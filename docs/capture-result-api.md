# Chat capture result API

LumaShot --capture-result <absolute-new-file.png>

Exit 0 means PNG export completed, 2 means cancelled, 1 means failure. Existing targets and malformed arguments fail before capture starts. PNG export is atomic and does not use the clipboard. Annotation and crop share the normal LumaShot renderer.

This mode bypasses the resident singleton and uses a distinct LumaShot.CaptureClient host; no tray, global hotkeys, clipboard listening, startup writes, pin restoration, or preference writes. Existing preferences are read in real mode. Pin/OCR/recording actions are excluded from the one-shot chat session. The caller should enforce a timeout and clean cancelled/failed output.

--capture-result-demo uses generated pixels only. scripts/test-capture-result.ps1 checks complete/cancel/repeat, unchanged clipboard sequence (never clipboard contents), malformed arguments, and refusing existing files. All fixture images stay under build/.

Fantai bundles the full independent installer and a matching runtime for this interface, preserving already-installed older LumaShot versions without probing their unknown CLI flags.
