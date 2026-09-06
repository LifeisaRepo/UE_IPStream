# Glossary

Every term used in this project's docs and discussions, defined in plain
language. **This file grows as we go** — any jargon introduced in a session gets
added here in the same session.

Format: **term** — what it is — *why it matters here* (only where that isn't
obvious).

Seeded 2026-08-22 with everything used during the Session 1 architecture
discussion.

---

## 1. Video coding

**Codec** — coder/decoder. The algorithm that compresses raw video into a
bitstream and decompresses it back. H.264 and H.265 are codecs. Distinct from a
*container* (which packages streams) and a *protocol* (which moves them).

**H.264 / AVC** — the dominant video codec since ~2004. Universally supported.

**H.265 / HEVC** — H.264's successor, roughly 50% better compression at the same
quality, at higher decode cost and with messier patent licensing. *Your camera
emits this.*

**I-frame (keyframe / IDR)** — a frame compressed entirely on its own, with no
reference to any other frame. Decodable standalone. Large.

**P-frame** — compressed as a *difference* from a previous frame. Small.
Undecodable without the frames it references.

**B-frame** — compressed as a difference from frames both before *and after* it
in display order. Smallest, but forces the decoder to buffer and reorder, which
adds latency. Live low-latency encoders often disable them for this reason.

**GOP (Group of Pictures)** — the repeating pattern from one I-frame to the next,
e.g. `I P P P P I P P P P`. **Keyframe interval** is the same idea expressed as
time or frame count.
*Why it matters here:* a decoder joining a live stream mid-flight **can produce
nothing until the next I-frame arrives**, because P-frames reference data it
never received. If your camera's GOP is 4 seconds, first-frame latency has a
4-second floor no matter how fast your code is. This is why M0 must report GOP
length, and why the M1 pass criterion's "10 seconds" is provisional.

**PTS (Presentation Time Stamp)** — when a frame should be *shown*.
**DTS (Decode Time Stamp)** — when it should be *decoded*. These differ when
B-frames are present, because decode order ≠ display order.

**NAL unit (Network Abstraction Layer unit)** — the atomic packet of an
H.264/H.265 bitstream. Every frame is one or more NAL units, each tagged with a
type (slice data, parameter set, etc.).

**Annex-B** — a NAL framing convention that separates units with start codes
(`00 00 01`). Used in streaming and broadcast. The alternative, **AVCC /
length-prefixed**, puts an explicit byte count in front of each unit and is used
in MP4 files. A decoder needs to know which it's being handed.

**VPS / SPS / PPS (parameter sets)** — Video / Sequence / Picture Parameter Set.
Small NAL units carrying the settings a decoder needs before it can decode
anything at all: resolution, bit depth, profile, level. HEVC adds VPS; H.264 has
only SPS and PPS.
*Why it matters here:* some cameras send these **only once, in the SDP at session
setup**, rather than repeating them in-band. Miss them and every frame fails to
decode with no obvious reason. This is one of the things M0 checks.

---

## 2. Colour and pixel formats

**Chroma subsampling** — human vision is far more sensitive to brightness than to
colour, so codecs store colour at lower resolution than brightness. Written as
`J:a:b`.

**4:2:0** — the near-universal case: full-resolution luma (brightness), and
chroma (colour) at half resolution in *both* dimensions. Costs 1.5 bytes per
pixel instead of 3.

**Luma (Y) / Chroma (U, V)** — brightness and the two colour-difference channels.
"YUV" is the loose everyday name for this family.

**Planar / semi-planar / packed** — how the channels are laid out in memory:
- **Planar** — three separate blocks: all Y, then all U, then all V.
- **Semi-planar** — two blocks: all Y, then U and V *interleaved* together.
- **Packed** — one block, channels interleaved per pixel (e.g. BGRA).

**YUV420P** — planar 4:2:0, three separate planes. *What FFmpeg's software HEVC
decoder outputs.*

**NV12** — semi-planar 4:2:0: a Y plane, then a single interleaved UV plane.
*What GPUs and hardware decoders natively want, and what Unreal's media texture
conversion path expects.* Converting YUV420P → NV12 is just interleaving two
planes into one — cheap.

**BGRA** — packed 8-bit blue/green/red/alpha, 4 bytes per pixel. What a GPU
texture ultimately displays. Uploading BGRA costs 2.67× the bandwidth of NV12,
which is the argument for converting on the GPU rather than the CPU.

**Stride (pitch)** — bytes per row in memory, which is often **larger** than
width × bytes-per-pixel because rows get padded for alignment. Assuming
stride == width is a classic source of skewed, diagonally-sheared images.

**BT.601 / BT.709** — the two standard matrices for converting YUV to RGB. 601 is
the standard-definition heritage one, 709 is HD. Using the wrong one produces
subtly wrong colour — usually noticed as skin tones being slightly off.

**Limited vs full range** — whether luma uses values 16–235 or the full 0–255.
Getting this wrong yields washed-out blacks or crushed highlights.

---

## 3. Streaming protocols and transport

**RTSP (Real Time Streaming Protocol)** — the *control* protocol: a
text-based session negotiator (DESCRIBE, SETUP, PLAY, TEARDOWN). Notably, **RTSP
does not carry video itself.** It sets up the session and tells you where the
media will arrive.

**RTP (Real-time Transport Protocol)** — the protocol that actually carries the
media packets an RTSP session negotiates.

**SDP (Session Description Protocol)** — the text blob returned by RTSP DESCRIBE,
listing what tracks exist, their codecs, and often the parameter sets. Not a
protocol that moves anything — a description format.

**RTSP over TCP (interleaved) vs UDP** — RTP can travel over UDP (lower latency,
packets can be lost or arrive out of order) or be tunnelled inside the TCP
control connection (reliable, slightly higher latency, traverses firewalls).
*This project pins TCP* (`rtsp_transport=tcp`) because reliability during a spike
is worth more than a few milliseconds.

**Demux (demultiplex)** — splitting an incoming multi-track stream into its
separate elementary streams (video here, audio there). The step *before* decode.

**Container** — a format that packages multiple streams plus metadata (MP4, MKV,
MPEG-TS). Handled by the same FFmpeg library as protocol handling.

**Elementary stream** — the raw codec bitstream itself (NAL units, frame
sequence, parameter sets), independent of how it's delivered or stored.
*Identical whether it's packetized into RTP for live delivery or wrapped in an
MP4 container on disk* — only the demuxing step differs, never the codec-level
structure. This is the precise term for what people usually mean by "an H.264
stream" or "an H.265 stream."

**"Stream" — three different meanings, easy to conflate:**
1. **Streaming** (verb/adjective) — live, incremental network delivery, as
   opposed to downloading a complete file first.
2. **A stream** (e.g. "Stream #0:0: Video...") — one track within a session or
   file, when more than one might exist. Used identically by `ffprobe` whether
   reading a live RTSP session or a file on disk — nothing to do with live vs.
   recorded.
3. **Elementary stream** — see above.

A recorded MP4 has multiple *streams* (sense 2), each one an *elementary
stream* (sense 3), wrapped in a container — independent of whether it was ever
*streamed* (sense 1) over a network at all.

**Substream / main stream** — most IP cameras publish the same scene at two or
more qualities on different URLs: a high-res main stream and a lower-res
substream. *A substream is often the cheaper spike target.*

**ONVIF** — an interoperability standard for IP cameras covering discovery and
control. Loosely relevant: "ONVIF-compliant" usually implies a predictable RTSP
URL scheme.

**Jitter** — variation in packet arrival timing. Network packets do not arrive at
a steady rate even when they were sent at one.

**Jitter buffer** — a deliberate small queue that absorbs jitter by delaying
playback slightly, trading latency for smoothness. *The core tradeoff in §7 of
the architecture doc.*

**Glass-to-glass latency** — total delay from light hitting the camera sensor to
photons leaving the display. The honest end-to-end number, including camera
encode, network, decode, render, and display — as opposed to measuring only your
own code and quoting that.

---

## 4. FFmpeg

FFmpeg is not one library. It's a set, and this project uses four:

**libavformat** — containers and protocols. Opens the RTSP URL, speaks RTSP/RTP,
demuxes, hands you packets.
**libavcodec** — the codecs. Turns packets into frames.
**libavutil** — shared plumbing: pixel format definitions, memory, logging, error
codes.
**libswscale** — pixel format conversion and scaling (e.g. YUV420P → NV12, or →
BGRA).
**libswresample** — the audio equivalent of swscale. *Not needed here — no audio.*

**AVFormatContext** — the handle representing an open input (a file, or in our
case an RTSP session). Owns the connection.

**AVPacket** — one chunk of still-compressed data coming out of the demuxer.
**AVFrame** — one decoded, uncompressed picture.

**`av_read_frame()`** — pulls the next `AVPacket` from the input. **Blocking** —
on a dead network connection it can sit for many seconds.

**`avcodec_send_packet()` / `avcodec_receive_frame()`** — the modern FFmpeg decode
API. You push packets in and pull frames out, and the two do not correspond 1:1
because the decoder buffers and reorders internally.

**`AVIOInterruptCB`** — a callback FFmpeg invokes periodically during blocking
I/O; return non-zero and it aborts the operation. *The only way to cancel a
blocked `av_read_frame`.* Without it, closing a stream hangs whatever thread
called it — and if that's the game thread, the whole editor freezes.

**`probesize` / `analyzeduration`** — how much data FFmpeg reads before it decides
it understands the stream. Defaults are tuned for files and add noticeable
startup delay on a live stream; both get lowered here.

**`fflags=nobuffer` / `flags=low_delay`** — options telling FFmpeg to stop
buffering for smoothness and prioritise latency.

**ffprobe** — CLI tool that reports what's in a stream without playing it.
**ffplay** — minimal CLI player. *Together these are M0.*

**`ffmpeg -buildconf`** — prints the exact configure line a binary was built with.
*Needed for the licensing NOTICE file.*
**`ffmpeg -protocols`** — lists supported protocols. *Used to check for `srt`.*

---

## 5. Unreal build system

**UBT (Unreal Build Tool)** — Unreal's own build orchestrator, which sits above
MSVC and decides what compiles, with which flags, linked against what. Configured
in **C#**, not in Visual Studio project files. Project files are generated
output, not source.

**Generated project files (`.sln` / `.vcxproj` / `.vcxproj.filters`)** —
produced by UBT scanning every `.Build.cs`/`.uplugin`/`.uproject` plus the
actual source tree (triggered by "Generate Visual Studio project files", or
automatically by the editor). `.sln` is the solution you open; `.vcxproj`
holds the real compiler settings (include paths, defines, file list);
`.vcxproj.filters` is cosmetic Solution-Explorer grouping only. **UBT
overwrites these from scratch on every regeneration — it does not merge.**
Hand-editing one (e.g. adding an include path via the VS GUI) works until the
next regen, then is silently gone with no diff to show it happened. *Why it
matters here:* the fix for an unresolved FFmpeg include/lib/DLL is always in
`.Build.cs`, never in the IDE's project settings dialog.

**Module** — Unreal's unit of compilation and linkage; roughly one DLL. Every
module has a `.Build.cs` describing its dependencies.

**`PublicDependencyModuleNames` vs `PrivateDependencyModuleNames`** — a public
dependency's include paths and symbols are re-exported to anything that
depends on *your* module; a private one stops at your module's own boundary.
*Why it matters here:* it's why the runtime module (`IPStreamMedia`) and the
factory module (`IPStreamMediaFactory`) can carry different dependency lists —
FFmpeg is a private dependency of the runtime module only, so nothing that
merely links against the plugin needs to know FFmpeg exists.

**`.Build.cs`** — the C# file declaring a module: include paths, dependencies,
libraries to link, files to stage. *This is where third-party integration lives
and where it usually goes wrong.*

**`ModuleType.External`** — a module marked as "this is not our code, there is
nothing to compile here, it only describes where somebody else's binaries live."
*How FFmpeg is wrapped.*

**Import library (`.lib`) vs DLL (`.dll`)** — on Windows, the `.lib` is a small
stub the linker uses at build time to resolve symbol names; the `.dll` holds the
actual code and is loaded at runtime. You need both, and they must match.

**Delay loading / `PublicDelayLoadDLLs`** — normally Windows loads every linked
DLL at process start, and a missing one is instant death before any of your code
runs. Delay loading defers it until first use, so **you** can control when and
from where it loads — which is what lets a plugin ship DLLs in its own folder
rather than requiring them on the system PATH.

**`FPlatformProcess::PushDllDirectory` / `GetDllHandle`** — the code that
actually uses the window delay loading opens up: called from module startup,
*before* any FFmpeg function is first invoked, to point the loader at the
plugin's own `Binaries` folder. Without delay loading there is no such window —
the crash from a missing DLL happens before this code could ever run.

**`RuntimeDependencies`** — tells UBT "this file must be copied into packaged
builds." A DLL that works in the editor and is missing from a packaged build has
almost always been left out of this list.

**UFS vs NonUFS** — UFS files go inside Unreal's packed `.pak` archive; NonUFS
files stay as loose files on disk. **A DLL must be NonUFS**
(`StagedFileType.NonUFS` in `RuntimeDependencies.Add(...)`), because a native
DLL loader needs a real file handle on real disk — it has no concept of a
`.pak` archive at all.

**Loading phase** — when a module is loaded during startup.
**`PostConfigInit`** is very early, right after config files are read.
*Why it matters here:* the player factory must be registered before anything can
try to open a media URL, so the factory module loads at `PostConfigInit` while
the heavier runtime module loads later.

**`.uplugin`** — the plugin's JSON manifest: name, modules, loading phases,
platform list.

**`UnrealTargetPlatform`** — the enum `.Build.cs` checks (e.g.
`UnrealTargetPlatform.Win64`) to guard platform-specific logic. *Why it matters
here:* all FFmpeg linkage is wrapped in a Win64 check from the first commit,
even though Win64 is the only Phase 1 target — costs nothing now, turns a
future Linux port into filling in another branch rather than removing
hardcoded assumptions.

**Editor build vs packaged build** — different linking, different file layout,
different staging. **Working in one proves nothing about the other.** This is why
a packaged build is its own milestone (M5).

---

## 6. Unreal Media Framework

**Media Framework** — Unreal's abstraction layer for video playback. It defines
interfaces; concrete *player backends* implement them. Electra (HLS/DASH/file),
WmfMedia (Windows Media Foundation), ImgMedia (image sequences), and MediaIO
(SDI capture cards) are all backends. **This project adds one for IP streams.**

**`UMediaPlayer`** — the Blueprint-facing asset: `OpenUrl`, `Play`, `Close`, and
the event delegates. It does not decode anything; it delegates to a backend.

**`UMediaTexture`** — the texture asset that receives decoded frames and can be
plugged into a material.

**`IMediaPlayer`** — the core interface a backend implements. Exposes sub-interfaces
via `GetControls()`, `GetTracks()`, `GetSamples()`, `GetView()`, `GetCache()`.
Conventionally one class inherits all of them and returns `*this`.

**`IMediaPlayerFactory`** — declares which URL schemes a backend handles (`rtsp`
here) and constructs players on demand. Registered with `IMediaModule` at startup.

**`IMediaEventSink` / `EMediaEvent`** — how a backend reports state changes
upward: `MediaOpened`, `MediaOpenFailed`, `TracksChanged`, `MediaClosed`.
*Critical:* `UMediaPlayer`'s Blueprint delegates fire **only** in response to
these. Decode perfectly and never fire `MediaOpened`, and the whole Blueprint side
appears dead.

**`IMediaSamples` / `FMediaSamples`** — the queue holding decoded samples between
the decode thread and the render side. `FMediaSamples` is the engine's ready-made
implementation in the `MediaUtils` module. *Use it; do not hand-roll one in
Phase 1.*

**`IMediaTextureSample`** — one decoded frame handed to the engine, describing its
dimensions, pixel format, stride, timestamp, and colour conversion needs.

**`IMediaPoolable` / `TMediaObjectPool`** — an engine pattern for recycling sample
objects so a 25 fps stream doesn't allocate and free memory 25 times a second.

**`IMediaTextureSampleConverter`** — an optional hook letting a backend perform
its own GPU conversion pass instead of using the engine's built-in shaders.
*Phase 2 territory.*

**`FMediaTextureResource` / `MediaShaders`** — the engine-side machinery that
takes a sample and converts it to a renderable RGB texture, including built-in
YUV→RGB shaders. *Reporting NV12 correctly means this does the colour conversion
on the GPU for free.*

**`TickFetch` / `TickInput`** — per-frame callbacks on a media player, running on
the game thread. *Bookkeeping only — never do work here.*

**Presentation clock** — the notion that playback has a current time, and frames
are fetched by matching that time. Natural for a file with a known duration;
awkward for a live stream that has no start, no end, and no seekable timeline.
*The central design tension in this project.*

---

## 7. Unreal general

**PIE (Play In Editor)** — running the game inside the editor process. Convenient,
and dangerous for native code: a leaked thread or a hung handle survives PIE
stopping and corrupts the editor session, where in a standalone build the process
would simply exit and clean up.

**`FRunnable`** — Unreal's interface for a worker thread.

**Game thread / render thread / worker thread** — Unreal's main threads. The game
thread runs gameplay and must never block; the render thread issues GPU commands.
*Decode happens on a dedicated worker so a blocking network read cannot stall
either.*

**RHI (Render Hardware Interface)** — Unreal's abstraction over D3D11/D3D12/Vulkan/
Metal. Writing "at the RHI level" means writing graphics code once against all of
them.

**Unreal Insights** — Unreal's profiling and tracing tool, showing a timeline of
instrumented events across threads.
**`TRACE_CPUPROFILER_EVENT_SCOPE`** — the macro that marks a code region so it
appears in Insights. *How the latency breakdown in M5 gets measured.*

---

## 8. Licensing

**Copyright licence vs patent** — two entirely separate things that get conflated.
A licence (LGPL, MIT) governs *the code*. A patent (HEVC) governs *the technique*.
Complying with one says nothing about the other.

**MIT** — permissive. Do what you like, keep the notice. *This plugin's own code.*

**GPL** — if your code touches GPL code at all, your *whole program* has to
become GPL too: full source given away, free to modify and redistribute. It
spreads to whatever it touches. Fatal for an Unreal plugin, which is why FFmpeg
must never be built with `--enable-gpl`.

**LGPL** — same family, but with one exception carved out for linking: you can
link an LGPL library into your own program and your program stays under
whatever licence you want. The only real condition is that if you ship the
LGPL library as a separate file, the user has to be able to swap that file for
their own version. *A `.dll` is already a separate, swappable file — so
shipping FFmpeg as dynamic DLLs satisfies that condition automatically, at zero
extra effort. Static linking bakes the library into your binary with nothing
left to swap, so it would require extra work — or fail the condition outright.
This is the entire reasoning behind D3.*

**MPL 2.0** — file-level copyleft. Changes to MPL files must be shared; your own
files are unaffected. *libsrt is MPL, which is friendly for Phase 2.*

**Static vs dynamic linking** — static bakes library code into your binary at
build time; dynamic keeps it in a separate `.dll` loaded at runtime. *The
distinction is the whole basis of LGPL compliance here.*

**`--enable-gpl` / `--enable-nonfree`** — FFmpeg configure flags that pull in
GPL-licensed components (mainly the **encoders** libx264/libx265) and change the
licence of the whole build. *This project decodes only, so neither is ever
needed.*

---

## 9. Hardware decode — Phase 2 preview

**DXVA2 / D3D11VA** — Windows APIs for GPU-accelerated video decode.
**NVDEC** — NVIDIA's dedicated decode hardware, a separate silicon block from the
shader cores.

**Zero-copy** — keeping a decoded frame in GPU memory the entire time, never
copying it to system RAM and back. Hardware decoders output NV12 directly into a
GPU texture, so the ideal path never touches the CPU at all.
*The endpoint the colour-conversion staging in §7 of the architecture doc is
building toward.*
