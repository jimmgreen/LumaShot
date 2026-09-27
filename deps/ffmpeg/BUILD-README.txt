LumaShot slim FFmpeg build and corresponding source
==================================================

Host used: Debian 13 Linux, MinGW-w64 Windows x64 cross compilation.
Compiler: GCC 14.2.0 POSIX; see toolchain-versions.txt for package versions.
All build tools were installed in the Linux build sandbox, not on the
Windows product machine. The result is a statically linked Windows EXE;
its only DLL imports are bcrypt, KERNEL32, msvcrt and SHELL32.

Exact upstream source revisions:
FFmpeg  494c96137916e0e61f17c439f8f5be13b27fc592
SVT-AV1 8f1f1b0dc52b063264838b51bc2299a5e44e31d5
x264    da14df5535fd46776fb1c9da3130973295c87aca
dav1d   d242c47b437c950b545e96e7872aa914edc50be5

These match the former full runtime's FFmpeg revision, SVT-AV1
v2.3.0-72-g8f1f1b0d, x264 core164 r3198 da14df5 and dav1d
1.5.0-46-gd242c47. Upstream archives lack .git; some runtime version
banners therefore omit git suffixes. The source lock and binary manifest,
not those abbreviated banners, identify the build. Codec algorithms
and production encoding/quality parameters were not changed.

Source archive: LumaShot-ffmpeg-source.zip (delivered next to the installer).
Unzip into a fresh directory on Debian 13 or a compatible Linux/WSL host.
The sources/ directory contains all four upstream archives. Missing
archives can also be fetched from the official URLs in source-lock.json.
Every archive is SHA-256 checked before safe extraction by prepare-sources.py.

Release packaging note (2026-09-27): SVT-AV1's web archive endpoint returned
HTTP 403. The source lock now uses GitLab's official archive API for the exact
same commit. That endpoint adds the commit twice to the top-level directory,
so archive bytes and SHA-256 differ; the original archive metadata is retained
in source-lock.json as originalArchive. No source revision or binary changed.

Prerequisites (Debian package names):
  sudo apt-get install gcc-mingw-w64-x86-64-posix \
    g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 \
    mingw-w64-x86-64-dev cmake ninja-build nasm meson pkg-config make python3

Build from the directory containing build-runtime.sh:
  JOBS=2 bash build-runtime.sh

The script writes disposable compilation files under build/ and the
result under artifacts/ffmpeg.exe. It contains all configure arguments
and generates the CMake and Meson cross files. Review the separate
upstream licenses and THIRD-PARTY-NOTICES.txt.
Use a fresh directory for an independent clean rebuild. This recipe is
rebuildable, but binary hash identity across builds is not promised:
PE timestamps, source paths, compiler package updates and build metadata
can change the executable. The delivered binary hash is in manifest.json.

Build choices:
- Static x264, no CLI, OpenCL, external input/demux helpers.
- Static SVT-AV1, Release, no app/tests, no native-machine-only target,
  CPU SIMD retained, library LTO disabled for conservative portability.
- Static dav1d, Release, assembly retained, no tools/tests/examples/docs.
- FFmpeg disable-everything/disable-autodetect, then explicitly enable
  libsvtav1/libx264/wrapped_avframe encoding; H.264/libdav1d/AAC decoding;
  H.264/AV1/AAC parsers; MOV demux, MP4/null mux; file/pipe protocols;
  SSIM/time-base/PTS/format/scaling filters and their automatic dependencies.
- No network, capture devices, ffplay, ffprobe, postproc, or unused external
  codec/subtitle/GPU libraries. Function/data sections and dead-section
  linking reduce unused code without changing encoder quality options.

Two source-archive/build integration adjustments, both in the script:
1. Restore x264 generated x264_config.h metadata for the verified r3198 da14df5 archive.
   No encoding code is changed.
2. Add aom_film_grain.o to the FFmpeg H.264 object list. At this exact
   revision h2645_sei cleanup references ff_aom_uninit_film_grain_params,
   but a minimal build without the native AV1 decoder misses the object.
   This fixes linkage to the existing shared cleanup helper; it neither
   introduces an alternate encoder nor changes a codec algorithm.

Windows acceptance before replacing product dependencies:
- scripts/validate-ffmpeg-runtime.py compares full and slim through the
  production export entry, including native 1080p/AAC, forced H.264,
  Media Foundation VFR/gap capture, and short upscaled 4K60/AAC fixtures.
- Independent full-runtime metrics inspect every frame, video/audio timing,
  each compressed AAC packet hash, and full-frame/text-region SSIM.
- Existing MP4 export regression verifies adoption rather than merely an
  exit code, plus failure, memory, timeout, cancellation and cleanup.
- Packaging verifies capabilities, binary/source manifest identity, and
  all app-local runtime imports. Full dev tools are not packaged.

Redistribution:
Publish LumaShot-ffmpeg-source.zip alongside LumaShot-Setup.exe with the
same availability. The zip contains the corresponding FFmpeg/library
sources and build/control instructions, not LumaShot application sources.
Upstream URLs are provenance, not a substitute for keeping this source
archive available when distributing the binary.
