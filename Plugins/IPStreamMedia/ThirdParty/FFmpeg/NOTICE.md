# FFmpeg — Third-Party Notice

This plugin dynamically links against FFmpeg's shared libraries
(`avutil`, `avcodec`, `avformat`, `swscale`). **FFmpeg itself is not
modified** — every `.dll` in `bin/Win64/` is byte-for-byte the unmodified
build described below.

One qualification, for completeness: the MSVC import libraries (`.lib`) in
`lib/Win64/` **were regenerated** from this same build's own `.def` files —
see [Import libraries regenerated](#import-libraries-regenerated) below.
Import libraries are link-time scaffolding that contain no FFmpeg code; no
FFmpeg binary, object, or source was altered.

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

## Import libraries regenerated

**If you re-download this FFmpeg build, you must repeat this step, or the
plugin will build cleanly and then fail to load at editor startup.**

The `.lib` files BtbN ships are **GNU-format import libraries** — these
builds are cross-compiled with MinGW/GCC (visible in the configure line
above: `--cross-prefix=x86_64-w64-mingw32-`, `--cc=x86_64-w64-mingw32-gcc`),
so `dlltool` produces import libraries built from full COFF objects with
explicit jump thunks, rather than the MSVC **short-import-record** format.

MSVC's `/DELAYLOAD` can only transform short-import records. Given
GNU-format libraries it links the imports as ordinary load-time imports and
**silently ignores the flag** — no warning, no error. This plugin depends on
delay loading (the DLLs live in this folder rather than on the system path,
and are loaded explicitly at module startup), so the result was a plugin
that compiled and linked perfectly and then failed at editor startup with
`Missing import: avutil-61.dll`. Full diagnosis:
[`M1DelayLoadRCA.md`](../../../`M1DelayLoadRCA.md`).

The four import libraries this plugin links were therefore regenerated from
the `.def` files shipped in `lib/Win64/`, using MSVC's `lib.exe` (run from a
Developer Command Prompt, in `lib/Win64/`):

```
lib /DEF:avutil-61.def   /OUT:avutil.lib   /MACHINE:X64 /NAME:avutil-61.dll
lib /DEF:avcodec-63.def  /OUT:avcodec.lib  /MACHINE:X64 /NAME:avcodec-63.dll
lib /DEF:avformat-63.def /OUT:avformat.lib /MACHINE:X64 /NAME:avformat-63.dll
lib /DEF:swscale-10.def  /OUT:swscale.lib  /MACHINE:X64 /NAME:swscale-10.dll
del *.exp
```

`/NAME:` is required, not cosmetic: the shipped `.def` files carry a bare
`EXPORTS` list with no `LIBRARY` statement, so without it `lib.exe` derives
the DLL name from the `/OUT:` filename and records `avutil.dll` — a DLL that
does not exist in this build.

Verify with `dumpbin /DEPENDENTS` on the built module binary: the four FFmpeg
DLLs should appear under *"Image has the following delay load dependencies"*,
not under plain dependencies.

Only the four libraries this plugin actually links were regenerated;
`avdevice`, `avfilter`, and `swresample` are left exactly as shipped.

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
