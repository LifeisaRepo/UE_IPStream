# FFmpeg — Third-Party Notice

This plugin dynamically links against FFmpeg's shared libraries
(`avutil`, `avcodec`, `avformat`, `swscale`). FFmpeg is **not** modified —
the DLLs shipped here are the unmodified build described below.

## Exact build pinned

- **Version:** `N-126455-gecc7eb519e-20260907`
- **Source:** [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases),
  asset `ffmpeg-N-126455-gecc7eb519e-win64-lgpl-shared`
- **Git commit:** `ecc7eb519e` (FFmpeg upstream)
- **Build date:** 2026-09-07

This is a dev-snapshot build (BtbN publishes on every FFmpeg master push,
not periodic version tags), so the git commit hash and build date above,
not a semantic version number, are what make this pin reproducible.

## License

**LGPL version 3**, not the FFmpeg default of LGPL 2.1 — this build was
configured with `--enable-version3` (`--enable-gpl` is **not** set, so this
stays in the LGPL family rather than becoming GPL). Confirmed directly
against this build's own `LICENSE.txt`, not assumed from the flag name.
Full text: [`COPYING.LGPLv3`](COPYING.LGPLv3) in this folder, copied
verbatim from the downloaded build.

Per this plugin's own `LICENSE.md`: this notice, and the LGPLv3 terms,
apply to FFmpeg only. The plugin's own source code is MIT.

## Full configure line (`ffmpeg -buildconf`)

```
--prefix=/ffbuild/prefix --pkg-config-flags=--static --pkg-config=pkg-config --cross-prefix=x86_64-w64-mingw32- --arch=x86_64 --target-os=mingw32 --enable-version3 --disable-debug --enable-shared --disable-static --disable-w32threads --enable-pthreads --enable-iconv --enable-zlib --enable-libxml2 --enable-libvmaf --enable-fontconfig --enable-libharfbuzz --enable-libfreetype --enable-libfribidi --enable-vulkan --enable-libvorbis --disable-libxcb --disable-xlib --disable-libpulse --enable-gmp --enable-lzma --enable-liblcevc-dec --enable-opencl --enable-amf --enable-libaom --enable-libaribb24 --disable-avisynth --enable-chromaprint --enable-libdav1d --disable-libdavs2 --disable-libdvdread --disable-libdvdnav --disable-libfdk-aac --enable-ffnvcodec --enable-cuda-llvm --disable-frei0r --enable-libgme --enable-libkvazaar --enable-libaribcaption --enable-libass --enable-libbluray --enable-libjxl --enable-libmp3lame --enable-libopus --enable-libplacebo --enable-librist --enable-libssh --enable-libtheora --enable-libvpx --enable-libwebp --enable-libzmq --enable-lv2 --enable-libvpl --enable-openal --enable-liboapv --enable-libopencore-amrnb --enable-libopencore-amrwb --enable-libopenh264 --enable-libopenjpeg --enable-libopenmpt --enable-librav1e --disable-librubberband --enable-schannel --enable-sdl2 --enable-libsnappy --enable-libsoxr --enable-libsrt --enable-libsvtav1 --enable-libtwolame --enable-libuavs3d --disable-libdrm --enable-vaapi --disable-libvidstab --enable-libvvenc --disable-whisper --disable-libx264 --disable-libx265 --disable-libxavs2 --disable-libxvid --enable-libzimg --enable-libzvbi --extra-cflags=-DLIBTWOLAME_STATIC --extra-cxxflags= --extra-libs=-lgomp --extra-ldflags=-pthread --extra-ldexeflags= --cc=x86_64-w64-mingw32-gcc --cxx=x86_64-w64-mingw32-g++ --ar=x86_64-w64-mingw32-gcc-ar --ranlib=x86_64-w64-mingw32-gcc-ranlib --nm=x86_64-w64-mingw32-gcc-nm --extra-version=20260907
```

Confirms, relevant to this project's D3: no `--enable-gpl`, no
`--enable-nonfree`.

## Not yet audited: other third-party libraries statically built into these DLLs

The configure line above enables roughly forty other third-party libraries —
`libsrt`, `gmp`, `libssh`, `sdl2`, `libaom`, `libdav1d`, `libvpx`, and more —
compiled **statically into** `avformat`/`avcodec`/etc., not shipped as
separate DLLs. Only the FFmpeg libraries' own LGPL status has been reviewed
so far; the individual licenses of these other libraries (mostly permissive —
BSD/MIT/zlib — but not yet checked one by one) have not been audited.
**Open item, not blocking Phase 1** (this plugin's own code never calls any
of them directly — only FFmpeg's own public API), but a real gap to close
before treating this project's licensing story as fully audited. Logged in
Architecture §13.

## Library versions in this build

```
libavutil      61.  7.100
libavcodec     63. 11.100
libavformat    63.  6.100
libavdevice    63.  2.100
libavfilter    12.  4.100
libswscale     10.  2.100
libswresample   7.  3.100
```

Only `avutil`, `avcodec`, `avformat`, and `swscale` are linked by this
plugin (see Architecture §4 — Phase 1 has no audio, no filtering, no
device capture).
