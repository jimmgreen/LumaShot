# GIF quality-first export (2026-09-17)

Supersedes the lossy defaults described in gif-optimization.md and gif-temporal-reuse.md.

## Changes
- Full 8-bit weighted median-cut palette built from existing bounded cross-frame raw samples. Up to 255 opaque colors; transparent index remains reserved.
- Exact weighted nearest-color mapping with a balanced search tree. Removes 5-bit lookup rounding and unconditional Bayer noise. A flat source color maps consistently across positions and frames.
- Production temporal color tolerance is zero: only identical indexed pixels are reused.
- Production postprocessing runs only lossless gifsicle -O3. Existing process memory/time/cancel limits, smaller-file selection and transactional save remain.
- Resolution, selected frame rate, loop and timing remain unchanged. GIF palette quantization and the existing recording codec can still lose source detail; this is not a claim of lossless RGB recording.

## Validation
build.bat passed with /W4 /WX. Six focused CTest tests passed (70.43 s): cursor_capture, media_recording, recording_geometry, recording_pixels, gif_quality, gif_pipeline.
New checks compare accelerated mapping against exhaustive nearest-color search, preserve all 255 grayscale levels, ensure flat colors acquire no spatial noise, and verify that production postprocessing preserves all displayed pixels including near-color detail.

Synthetic gradient: previous source-relative RGB MSE 30.1023, new 17.1455 (43.0% lower). Previous final 20,475 bytes; new raw 33,572, lossless final 29,344 bytes (12.6% postprocessing saving). The increased size is accepted to restore detail.
Synthetic 1080p, 30 frames: raw 1,098,218 bytes; final 1,094,088 bytes; encoding 12.453 s, optimization 2.156 s; parent peak working set 151.711 MiB. Prior documented final was 1,295,731 bytes. This is content-specific, not a universal reduction guarantee.
Visual inspections: build/gif-quality-comparison.png (gradient before/after), build/gif-quality-text-comparison.png (synthetic source/text export). No personal media used as regression fixtures.

Synthetic 4K, 30 frames: raw 2,072,014 bytes; final 2,061,217 bytes; encoding 38.344 s, optimization 5.125 s; parent peak working set 519.086 MiB. Prior documented final was 2,533,267 bytes. Parent memory is separate from the 256 MiB subprocess limit.

Both 1080p and 4K lossless benchmark outputs were independently decoded with Pillow: all 25 composited frames, 2,000 ms duration, loop and dimensions matched their pre-optimizer input exactly. Installer SHA256: F595F9CCBBB6C80B835CB9A078CC120D43C98953FE6AF6791F92E3A66E2B6CE3.
