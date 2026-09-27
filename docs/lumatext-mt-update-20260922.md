# LumaText Release SDK update (2026-09-22)

Updated deps/lumatext from the sibling out/sdk/Release package, including its manifest and all eight declared SDK files. The DLL uses x64 Release /MT, is 1,648,128 bytes, and has SHA256 1EAE7113B138FB26C9D47234D954CB53DABFD1FE0824EB10B41F831781D10535. The historical deps/lumatext-source snapshot is unchanged; CMake imports only the binary package. Application text settings and the required MSVC runtime DLLs are retained.

Focused Release build succeeded for LumaShot, recording/OCR/elements workers, lumashot_render_test and lumashot_startup_render_perf_test. text_context_memory and text_export_memory passed. The annotation_render executable passed all 12 direct DLL rasterization, visible pixel, target recreation and clipping assertions at 96/144/192 DPI, but its overall exit code was 1 due to three existing note assertions:

- all seven note presets meet 4.5 contrast on light dark and filled backgrounds
- mixed backgrounds get readable backing without changing custom ink
- label text color changes exported pixels

The same newly built executable was rerun in build/lumatext-mt-old-dll-check with the previous SDK DLL (SHA256 6FD80E3BD7BEFF1B2FC044DB0098F61598B0AA8750F6D8B6873566124F7F3D32). It reproduced exactly the same three failures. These are not reported as passing or fixed. Logs are build/lumatext-mt-update-build.log, build/lumatext-mt-update-tests.log and build/lumatext-mt-old-dll-check/old-dll-render.log.

No installed application or user process was modified. Real multi-monitor visual appearance was not manually verified. Packaging and installer hashes are recorded in build/lumatext-mt-update-verification.json.

Concurrent packaging follow-up: build/preview-installer.log records a later root installer replacement at 18:42:07 (SHA256 3185D7015F9D6904F1EA3D4669C1F1A4938C3CCDF50777A7D80D6CB8BE3DBCA3). It was preserved. Current payload still matched the new SDK DLL and runtime manifest; a frozen copy was recompiled successfully into dist/lumatext-mt-20260922-1850/LumaShot-Setup.exe, SHA256 3185D7015F9D6904F1EA3D4669C1F1A4938C3CCDF50777A7D80D6CB8BE3DBCA3, 37,569,441 bytes. The recording worker in build was modified concurrently after packaging, so this frozen payload represents the successful package rather than subsequent build edits. No tests rerun or installation performed.
