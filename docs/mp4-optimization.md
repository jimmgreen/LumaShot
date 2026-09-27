# MP4 quality-first size optimization (2026-09-17)

## Implementation
The H.264 output now requests High profile, quality-based VBR at quality 85, and CABAC where supported. Quality parameters are passed through SetInputMediaType before encoder configuration; setting them only through ICodecAPI before BeginWriting did not reliably survive sink-writer initialization. The actual mode is read back after BeginWriting. Microsoft converts quality to integer QP (85 reads back as 86 on this runner).

B frames are explicitly disabled during codec configuration: experimental default High-profile output shifted the first video timestamp by one frame. The final code preserves zero-origin video timing and the existing audio clock. The hardware texture pool, throttling, AAC settings, selected frame rate, selected dimensions, capture and save/cancellation paths remain unchanged.

If the optimized input configuration fails, discard the partially configured writer and recreate the previous Baseline/bitrate path. Hardware selection still requires a real hardware encoder; no silent CPU fallback. Unsupported CABAC is optional. The unsupported-driver fallback was reviewed, but not exercised against a physical unsupported device.

This is lossy H.264 recording with a high quality target, not mathematically lossless recording or an absolute minimum-size guarantee. Complex motion can still require large files. No downscaling, frame dropping, or new UI setting was introduced. Existing NV12 even-dimension alignment remains.

## Measurements
All fixtures are synthetic; no personal video was used. Bytes are actual MP4 lengths.

| Fixture | Previous bytes | New bytes | Reduction | Quality evidence |
|---|---:|---:|---:|---|
| 1920x1080, text/gradient/cursor, 30 frames, software | 427472 | 302620 | 29.2% | RGB PSNR 34.591 -> 34.628 dB; text 42.012 -> 42.716 dB |
| 3840x2160, text/gradient/cursor, 30 frames, software | 971943 | 619818 | 36.2% | RGB PSNR 34.736 -> 34.749 dB; text 43.427 -> 43.756 dB |
| 1280x720, text/moving gradient/checkerboard, 45 frames, software | 475259 | 378392 | 20.4% | RGB MSE 3.234 -> 3.090; text 2.428 -> 1.858 |
| Same 1280x720 content, hardware | 453979 | 203768 | 55.1% | RGB MSE 2.174 -> 3.054; text 1.037 -> 1.894 |

The hardware result has a measurable quality loss; visual checks of text and moving checkerboard edges found no obvious new damage at original size. Do not extrapolate these short fixtures to every video or GPU. Full RGB metrics include the unchanged RGB/NV12 conversion.

Visual evidence: build/mp4-study/text-comparison.png and hardware-comparison.png. Data: build/mp4-study/results.json, baseline-quality.log, quality-details.log. Source-relative comparison decodes all frames. Original encoder snapshot is in build/mp4-study/encoder-before.cpp; optional CMake LUMASHOT_MP4_BASELINE_SOURCE builds the same motion test against it. This option is reset to empty after benchmarking.

## Verification
build.bat passed (/W4 /WX). Seven focused tests passed: media_recording, recording_performance, cursor_capture, gif_quality, gif_pipeline, recording_geometry and mp4_quality (90.97 seconds). A final targeted rerun covers the encoder after fallback hardening.

The new mp4_quality test checks CPU and GPU readback/encoding, native dimensions, every presentation timestamp within 1 ms of the requested 30 fps timeline, all 45 frames, source-relative text/motion error and decoded PNGs. The 20-second performance test retains all 600 video frames, bounded post-warmup memory and decodable synthetic AAC audio. Independent ffprobe check: H.264 1920x1080 at 30 fps starts at 0, duration 19.999967 seconds; AAC starts at 0, duration 20.010646 seconds (AAC packet padding).

The existing media_recording GIF test assumed every delta frame covered the full canvas. It now checks the full first-frame canvas separately and accepts bounded delta rectangles, consistent with the existing transparent-delta encoder.

References:
- https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder
- https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsinkwriter-setinputmediatype

Final targeted rerun: all 3 tests passed in 29.68 seconds after fallback hardening.
