#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
B="$ROOT/build"; SRC="$B/src"; PREFIX="$B/prefix"
JOBS="${JOBS:-2}"
python3 "$ROOT/prepare-sources.py"
mkdir -p "$PREFIX" "$ROOT/artifacts" "$B/logs"
cat > "$B/mingw.cmake" <<'EOF'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
cat > "$B/mingw.ini" <<'EOF'
[binaries]
c = 'x86_64-w64-mingw32-gcc'
cpp = 'x86_64-w64-mingw32-g++'
ar = 'x86_64-w64-mingw32-ar'
strip = 'x86_64-w64-mingw32-strip'
windres = 'x86_64-w64-mingw32-windres'
pkg-config = 'pkg-config'
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
[properties]
needs_exe_wrapper = true
EOF
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
echo '=== Building baseline-revision x264 ==='
(cd "$SRC/x264"; ./configure --prefix="$PREFIX" --host=x86_64-w64-mingw32 --cross-prefix=x86_64-w64-mingw32- --enable-static --disable-cli --disable-opencl --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-lsmash --enable-strip --extra-cflags='-O3 -ffunction-sections -fdata-sections' > "$B/logs/x264-configure.log" 2>&1
# configure embeds archive version metadata in x264_config.h, not version.h.
# Restore the verified r3198 commit's display strings without changing codec code.
sed -i -E 's/^#define X264_VERSION .*/#define X264_VERSION " r3198 da14df5"/; s/^#define X264_POINTVER .*/#define X264_POINTVER "0.164.3198 da14df5"/' x264_config.h
make -j"$JOBS" > "$B/logs/x264-build.log" 2>&1; make install > "$B/logs/x264-install.log" 2>&1)
echo '=== Building baseline-revision SVT-AV1 (SIMD retained) ==='
cmake -S "$SRC/svt-av1" -B "$B/svt" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$B/mingw.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_APPS=OFF -DBUILD_TESTING=OFF -DNATIVE=OFF -DSVT_AV1_LTO=OFF -DCMAKE_C_FLAGS='-ffunction-sections -fdata-sections' > "$B/logs/svt-configure.log" 2>&1
cmake --build "$B/svt" -j "$JOBS" > "$B/logs/svt-build.log" 2>&1
cmake --install "$B/svt" > "$B/logs/svt-install.log" 2>&1
echo '=== Building baseline-revision dav1d (SIMD retained) ==='
meson setup "$B/dav1d" "$SRC/dav1d" --cross-file "$B/mingw.ini" --prefix "$PREFIX" --libdir lib --buildtype release --default-library static -Denable_tools=false -Denable_tests=false -Denable_examples=false -Denable_docs=false -Dc_args='-ffunction-sections -fdata-sections' > "$B/logs/dav1d-configure.log" 2>&1
ninja -C "$B/dav1d" -j "$JOBS" > "$B/logs/dav1d-build.log" 2>&1
ninja -C "$B/dav1d" install > "$B/logs/dav1d-install.log" 2>&1
echo '=== Building white-listed FFmpeg ==='
# The pinned revision omits this shared H.264 SEI cleanup dependency when the
# native AV1 decoder is disabled. This only fixes the object list, not codec code.
if ! grep -Fq 'OBJS-$(CONFIG_H264_DECODER) += aom_film_grain.o' "$SRC/ffmpeg/libavcodec/Makefile"; then
    printf '\n# Minimal H.264 shared SEI cleanup dependency.\nOBJS-$(CONFIG_H264_DECODER) += aom_film_grain.o\n' >> "$SRC/ffmpeg/libavcodec/Makefile"
fi
mkdir -p "$B/ffmpeg"
(cd "$B/ffmpeg"; "$SRC/ffmpeg/configure" --prefix="$PREFIX" --arch=x86_64 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32- --pkg-config=pkg-config --pkg-config-flags=--static --enable-gpl --enable-version3 --enable-static --disable-shared --disable-debug --disable-doc --disable-everything --disable-autodetect --disable-network --disable-ffplay --disable-ffprobe --enable-ffmpeg --disable-avdevice --disable-postproc --enable-swscale --enable-swresample --enable-avfilter --enable-libsvtav1 --enable-libx264 --enable-libdav1d --enable-encoder=libsvtav1,libx264,wrapped_avframe --enable-decoder=h264,libdav1d,aac --enable-parser=h264,av1,aac --enable-demuxer=mov --enable-muxer=mp4,null --enable-protocol=file,pipe --enable-filter=ssim,scale,format,null,anull,aformat,aresample,setpts,settb --enable-bsf=h264_mp4toannexb,aac_adtstoasc,extract_extradata,av1_metadata --extra-cflags="-I$PREFIX/include -ffunction-sections -fdata-sections" --extra-ldflags="-L$PREFIX/lib -static -Wl,--gc-sections -s" --extra-libs='-lpthread -lm' > "$B/logs/ffmpeg-configure.log" 2>&1
make -j"$JOBS" > "$B/logs/ffmpeg-build.log" 2>&1)
cp "$B/ffmpeg/ffmpeg.exe" "$ROOT/artifacts/ffmpeg.exe"
x86_64-w64-mingw32-strip --strip-unneeded "$ROOT/artifacts/ffmpeg.exe"
x86_64-w64-mingw32-objdump -p "$ROOT/artifacts/ffmpeg.exe" > "$ROOT/artifacts/pe-imports.txt"
sha256sum "$ROOT/artifacts/ffmpeg.exe"
wc -c "$ROOT/artifacts/ffmpeg.exe"
echo '=== DONE ==='
