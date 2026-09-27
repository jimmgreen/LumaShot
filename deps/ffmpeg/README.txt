LumaShot MP4-only FFmpeg runtime for Windows x64
===============================================

Built 2026-09-17 from the same FFmpeg, SVT-AV1, x264 and dav1d source
revisions as the former gyan.dev 2024-12-19-git-494c961379 full runtime.
This is a dedicated build, not a repack or an x264-only replacement.

ffmpeg.exe: 36,424,704 bytes
SHA-256: f0ae86e685f9167f171b0561f7909588d3f2e7e21e4cbf03ae30a51828471059
License: GPL version 3 or later; see LICENSE and THIRD-PARTY-NOTICES.txt.
Exact source archive URLs, revisions and hashes: source-lock.json.
Build instructions and scope: BUILD-README.txt.

Preserved production contract (application settings are not changed):
- SVT-AV1 preset 6 / CRF 36 / tune=0:lp=2:lookahead=16; CRF 28 retry.
- x264 slow / CRF 18 / 4 threads as the compatible fallback.
- Native image dimensions, yuv420p, demuxer time base and passthrough FPS.
- MP4 faststart and compressed AAC audio packet copy.
- H.264/AV1 decoding and frame-aligned settb/setpts/SSIM validation.
- Null muxer plus wrapped_avframe: required for actual quality validation.
- CPU SIMD optimizations and existing resource/cancellation limits.

Removed: unused external codecs, network protocols, capture devices,
GPU encoder backends, subtitle stacks and general-purpose media support.
Media Foundation capture/preview in LumaShot is independent of these
removed FFmpeg devices and GPU backends. This is not a general-purpose
FFmpeg replacement. ffplay and ffprobe are not shipped.
The executable imports only Windows system DLLs and works offline.

Source availability / redistribution:
The release includes a separate LumaShot-ffmpeg-source.zip containing the
locked upstream source archives and build/control scripts. Distribute
that source archive alongside the installer with equivalent access; do
not ship only this binary while omitting corresponding-source access.
It is a developer/source artifact, not an installation dependency and
is intentionally not embedded in the application installer.
