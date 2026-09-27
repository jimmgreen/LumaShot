# Recording and media export optimization — 2026-09-21

## Delivered changes

- WGC drains pending frames and submits only the newest texture copy. Superseded frame leases are closed promptly; the newest remains alive through copy submission. Dimensions are checked for every acquired frame.
- Encoder input views are reused while the source texture is unchanged; a source change recreates the input view. Output views and constant conversion settings are initialized once. Cropping remains per-frame.
- GIF nearest-color quantization uses at most three persistent workers plus the caller, with independent row stripes and identical tie-breaking. Images below 128K pixels remain serial. Workers sleep between frames and are joined at export teardown.
- Original-size GIF export reuses its palette-sampling pixel buffer and bypasses the redundant identity WIC scaling/copy. Resized exports retain WIC Fant.
- MP4 first reads stream metadata, then scans packet timestamps and hashes compressed audio on a separate COM-initialized thread while encoding. Candidate acceptance still waits for the complete scan and all existing timing/audio/whole-clip quality checks. Cancellation and exceptional exits stop/join the scanner before Media Foundation shutdown.

AV1 concurrency/memory limits, CRF, quality thresholds, palette sampling, and output timing are unchanged. Audio-buffer reuse, staging ping-pong and a decoder/quantizer pipeline were not added.

## Rejected resize experiment

Advanced Source Reader video processing was tested only for GIF presentation frames, retaining original-resolution palette sampling. On a synthetic 1080p-to-640px clip, all-frame comparison against WIC Fant gave mean RGB MSE 46.7855 and worst-frame MSE 46.9447, exceeding the preset per-frame limit of 25. The raw GIF also grew from 48,027 to 51,326 bytes. Visual inspection showed changed text rendering. A 4K-to-640px clip passed that tolerance (worst-frame MSE 4.18708), but that does not establish quality across input sizes. The experimental resize implementation is **not shipped**.

The experiment, baseline source and comparison images/logs are retained only under `build/media-optimization/`.

## Validation

Synthetic fixtures only; no personal clipboard, files or preferences are used.

Passed focused checks: `media_recording`, `gif_quality`, `gif_pipeline`, `gif_quantizer_parallel`, `encoder_cache`, `recording_latest_frame`, `recording_pixels`, `recording_geometry`, `mp4_quality`, and `mp4_export`.

- Quantizer output matches its serial reference byte-for-byte across gradients, repeated colors, palette ties, threshold boundaries, uneven stripes and repeated frames.
- CPU and GPU encoder tests each decode 30 frames and check repeated input, changed crop, changed texture, and return to the original texture. Neutral levels isolate resource/crop identity from decoder color-matrix defaults; separate MP4 quality tests cover color/text error.
- Latest-frame tests cover empty/single/multiple frames, lease lifetime and cleanup on acquire/validation/consume errors.
- MP4 regression includes real AV1/H.264 adoption, variable frame timing, audio identity, quality rejection, fallback and cancellation. No end-to-end MP4 speedup percentage is claimed.
- WGC integration could not complete in this execution environment: sandbox support detection reported an unavailable service; an approved host retry failed at capture-window creation with `0x80070057`. Synthetic frame-draining and CPU/GPU encoding tests passed, but these do not replace an interactive WGC capture test.

Logs: `build/media-optimization/tests.log`, `final-tests.log`, `quantizer.log`, `encoder-cache.log`, and `wgc-host.log`. The initial encoder-cache fixture used saturated colors and was corrected to neutral levels after decoding confirmed the source/crop sequence was correct but decoder color conversion affected the expected RGB values.

## Measured performance

Single-machine synthetic measurements; timings are not universal speed guarantees.

| Quantization workload | Serial | Parallel | Speedup |
|---|---:|---:|---:|
| 640×360 gradient, 8 frames | 454.57 ms | 166.605 ms | 2.73× |
| 640×360 flat, 160 frames | 33.214 ms | 13.671 ms | 2.43× |
| 1920×1080 gradient, 8 frames | 4288.27 ms | 1713.35 ms | 2.50× |
| 1920×1080 flat, 160 frames | 341.561 ms | 137.833 ms | 2.48× |

For the complete two-second GIF fixture (before optional lossless optimization):

| Source → output | Before | After | Pixel/timing comparison |
|---|---:|---:|---|
| 1920×1080 → 640×360 | 9.157 s | 8.844 s | Exact, MSE 0 on every displayed frame |
| 3840×2160 → 640×360 | 33.500 s | 34.000 s | Exact, MSE 0 on every displayed frame |
| 1920×1080 → original size | 10.156 s | 8.859 s | Exact, MSE 0 on every displayed frame |

The 4K difference is approximately 1.5% slower in this run. Palette sampling and decoding dominate these short resized exports; quantization speedup must not be reported as equivalent whole-export speedup. Raw GIF byte sizes also remain unchanged (48,027 and 34,234 bytes respectively).

Original-size 1080p export took approximately 12.8% less time; process peak working set decreased from 144.004 MiB to 129.191 MiB in that run. Its raw GIF remains 269,750 bytes. Logs are under `build/media-optimization/original-size/`.

The reusable comparison tool is `tests/gif_export_benchmark.cpp`; optional `LUMASHOT_GIF_BASELINE_SOURCE` compiles the captured prior exporter. `compare-exact` composites all GIF frames and checks the complete presentation timeline, including merged/delta frames.

## Delivery

`build.bat`, `scripts/package.ps1` and `scripts/build-installer.ps1` completed successfully. The seven packaged application/worker/runtime binaries match the current build by SHA-256. The installer includes the prior clipboard F2/unpin fixes; it was not executed and the installed app was not replaced.

- Installer: `dist/LumaShot-Setup.exe`
- SHA-256: `B604D539ED8980E41BFC2935000A41B43549BF3F44BEA46C9F239EF07652E87A`
- Verification: `build/media-optimization/delivery.json`
