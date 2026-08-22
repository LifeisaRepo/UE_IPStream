# IPStreamMedia — Architecture & Phase 1 Plan

**Status:** Design agreed, implementation not started
**Engine:** Unreal Engine 5.3
**Platform:** Win64 only (Phase 1)
**Last updated:** 2026-08-22

---

## 1. What this is

A Media Framework player backend that ingests live IP video streams into Unreal
Engine. Unreal ships no first-party IP-stream ingest: Electra covers HLS/DASH and
local files, MediaIO covers SDI through vendor plugins, and there is a gap in the
middle. This plugin fills it.

It is implemented as a **proper Media Framework player module** — `IMediaPlayer`,
a registered player factory, and the sample interfaces — so that a stock
`UMediaPlayer` and `UMediaTexture` work against an `rtsp://` URL with no bespoke
API. That constraint is deliberate: it is the difference between an engine
integration and a wrapper.

### Phased scope

| Phase | Protocol | Status |
|---|---|---|
| 1 | RTSP (pull) | This document |
| 2 | SRT | Deferred |
| 3 | RTMP (push-to-server; architecturally the odd one out) | Deferred |

---

## 2. Phase 1 target and non-goals

**Target:** one RTSP stream to one texture, on Windows, in UE 5.3, with a
documented end-to-end latency measurement and a reproducible measurement method.

### Non-goals — Phase 1 will not do these

These are pre-refused. They are listed here so the decision is already made and
does not have to be re-made at 11pm on a Saturday.

- **Audio.** The test camera carries PCM A-law. Phase 1 reports no audio track.
  Media Framework's audio sample interfaces are right there and are the single
  most likely thing to consume a weekend for zero portfolio value.
- **Multiple simultaneous streams.**
- **Hardware decode.** Phase 2. Software HEVC at 1080p25 costs roughly 10–15% of
  one modern core, which is not a Phase 1 problem.
- **Editor UI, custom asset types, custom detail panels.**
- **Linux / Android / Quest.**
- **Recording, DVR, seeking back through a live buffer.**
- **Working with arbitrary cameras.** Phase 1 targets *the* test camera.
  Generalisation happens after it ships. "It should probably handle cameras that
  do X" is how a six-week project becomes a six-month one, and no reviewer will
  ever test it against hardware they do not own.

---

## 3. Test source

| Property | Value |
|---|---|
| Transport | RTSP over LAN |
| Video codec | HEVC / H.265 (MPEG-H Part 2) |
| Resolution | 1920×1080 |
| Frame rate | 25 fps |
| Audio codec | PCM A-law (ignored — see non-goals) |

### HEVC consequences

Two, neither fatal:

1. **FFmpeg's software `hevc` decoder outputs `AV_PIX_FMT_YUV420P`** — three
   separate planes — not NV12. The GPU conversion path (§7) wants NV12. The gap
   is a cheap chroma interleave, but it is a decision rather than an accident.
2. **Some HEVC cameras deliver VPS/SPS/PPS only in the SDP**, out of band. M0
   surfaces this before any Unreal code is involved.

Patent note: HEVC decode is patent-encumbered technology. FFmpeg's licence says
nothing about patents, and every open-source media project (VLC, mpv, OBS,
Chromium) ships HEVC decode regardless. For a public portfolio repository
distributing no commercial product this is not a practical concern. The README
carries one line stating that codec patent licensing is the integrator's
responsibility.

---

## 4. Third-party dependency and licensing

### The rule

**FFmpeg, LGPL 2.1, shared DLLs, dynamically linked. Never static. Never
`--enable-gpl`.**

### Why, in plain terms

FFmpeg ships under **LGPL 2.1** by default. Passing `--enable-gpl` at configure
time makes the entire build **GPL**.

- **GPL is viral.** Linking GPL code into this plugin would force the plugin to
  be GPL, and arguably anything linking the plugin too. For an Unreal plugin that
  is fatal — no studio will touch it, and it collides with the Unreal EULA in
  practice.
- **LGPL is the workable one**, with a single condition: a user must be able to
  substitute their own build of the library. **Dynamic linking satisfies this
  automatically.** Static linking technically obliges you to also distribute
  object files so a third party could relink — a real compliance headache for no
  gain.

The fact that keeps this simple: `--enable-gpl` exists mainly to pull in
**libx264 / libx265**, which are **encoders**. This plugin only decodes. FFmpeg's
native `h264` and `hevc` decoders are LGPL. The GPL flag is never needed.

### Compliance checklist

1. Use a build configured **without** `--enable-gpl` and **without**
   `--enable-nonfree`. [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds)
   publishes `win64-lgpl-shared` releases with MSVC-usable import libraries.
   **Pin an exact release tag.**
2. Ship the DLLs **unmodified**.
3. `ThirdParty/FFmpeg/COPYING.LGPLv2.1` — the licence text, verbatim.
4. `ThirdParty/FFmpeg/NOTICE.md` — exact version, the full configure line (from
   `ffmpeg -buildconf`), and the source URL.
5. **This plugin's own code is MIT.** LGPL does not infect it, because the
   linkage is dynamic.

### Do this at pin time — two minutes, saves a rebuild in Phase 2

Run `ffmpeg -protocols` on the chosen build and check whether `srt` is listed.

FFmpeg can be built against libsrt (MPL-2.0, LGPL-compatible). If the pinned
build has it, **Phase 2 SRT becomes largely a URL-scheme-and-options change
through the same avformat path** instead of a from-scratch integration. If it
does not, that means rebuilding FFmpeg in Phase 2 — much better to know that now.

Counterpoint, recorded so it is not a surprise later: FFmpeg's libsrt wrapper
hides most of SRT's statistics and tuning surface (RTT, loss, buffer levels,
caller/listener configuration). Phase 2 may want to link libsrt directly to
expose that telemetry. **Not decided now.** The FFmpeg path is the cheap version
and there is a legitimate reason to reject it later.

### Why binaries are committed rather than fetched by a script

Git LFS is present in this repository regardless, because it is an Unreal
project. That removes the repository-size argument for a setup script, and
`git clone` into `Plugins/` working immediately is worth real money when the
reader is a hiring manager with fifteen minutes.

---

## 5. Module structure

Plugin name **`IPStreamMedia`**, following engine convention (`WmfMedia`,
`AvfMedia`, `ImgMedia`, `ElectraPlayer`).

```
Plugins/IPStreamMedia/
  IPStreamMedia.uplugin
  Source/
    IPStreamMedia/                    # Runtime module: the player
      Private/
        IPStreamMediaModule.cpp
        Player/   IPStreamPlayer          # IMediaPlayer + Controls/Tracks/View/Cache
        Player/   IPStreamPlayerSession   # decode thread + connection state machine
        Decoder/  FFmpegDemuxer           # avformat: open, read, reconnect, interrupt
        Decoder/  FFmpegVideoDecoder      # avcodec: packets -> AVFrame
        Decoder/  FFmpegLogBridge         # av_log -> UE_LOG
        Samples/  IPStreamTextureSample   # IMediaTextureSample + IMediaPoolable
      Public/
        IIPStreamMediaModule.h
      IPStreamMedia.Build.cs
    IPStreamMediaFactory/             # Factory module: registration + URL schemes
      Private/
        IPStreamMediaFactoryModule.cpp
      IPStreamMediaFactory.Build.cs
  ThirdParty/
    FFmpeg/
      include/          # libavcodec/ libavformat/ libavutil/ libswscale/
      lib/Win64/        # import libraries
      bin/Win64/        # runtime DLLs
      COPYING.LGPLv2.1
      NOTICE.md
      FFmpeg.Build.cs   # ModuleType.External
  Content/              # demo material + MediaTexture. Minimal.
  Resources/Icon128.png
  Docs/
```

### Why two modules

The factory module loads at **`PostConfigInit`** so the player is registered with
`IMediaModule` before anything can attempt to open a URL; the heavier runtime
module loads later. This mirrors `WmfMedia` / `WmfMediaFactory` exactly. It is
also the difference between "works in the editor, mysteriously does not in a
packaged build" and not.

### Why FFmpeg is an External module

All platform binary logic — include paths, import libraries,
`PublicDelayLoadDLLs`, `RuntimeDependencies` staging — lives in one file.
Third-party binary integration is the most commonly botched part of Unreal plugin
work; isolating it and getting it right is a legitimate thing to point at.

### Platform guarding

Wrap platform-specific `.Build.cs` logic in
`if (Target.Platform == UnrealTargetPlatform.Win64)` **from the first commit**,
despite Windows being the only Phase 1 target. It costs nothing now, makes a
future Linux port a fill-in-the-blank, and reads as intent rather than
limitation.

---

## 6. Media Framework contract

| Interface | Implementation | Notes |
|---|---|---|
| `IMediaPlayer` (+ `IMediaControls`, `IMediaTracks`, `IMediaView`, `IMediaCache`) | `FIPStreamPlayer`, multiple inheritance on one class | Standard engine pattern; `GetControls()` and friends return `*this` |
| `IMediaEventSink` | injected by the factory at construction | See warning below |
| `IMediaSamples` | `FMediaSamples` from the `MediaUtils` module | **Do not hand-roll a queue in Phase 1.** Saves days. |
| `IMediaTextureSample` + `IMediaPoolable` | `FIPStreamTextureSample` in a `TMediaObjectPool` | Pooling gives zero per-frame allocation nearly free |
| `IMediaPlayerFactory` | factory module | Declares the `rtsp` scheme, feature flags, `CreatePlayer()` |

### Event sink — the gotcha that costs an evening

`UMediaPlayer`'s Blueprint delegates — `OnMediaOpened`, `OnPlaybackResumed`,
`OnTracksChanged` — are driven **entirely** by events pushed to
`IMediaEventSink`. Decode can be working perfectly and frames flowing, and if
`EMediaEvent::MediaOpened` never fires, the entire Blueprint surface looks dead.

**Fire events from day one of M2.**

### Live-stream control semantics

Getting these wrong makes `UMediaPlayer` either refuse to start or immediately
report `EndReached`:

- `GetDuration()` — zero or `FTimespan::MaxValue`
- `IsLooping()` — false
- Seek — unsupported; report it as such rather than failing silently
- State — `EMediaState::Playing` once frames are flowing

---

## 7. Threading, timing, and colour

### Threading model

- **One `FRunnable` decode worker per session.** Blocking `av_read_frame` →
  `avcodec_send_packet` / `avcodec_receive_frame` → convert → pooled sample →
  `FMediaSamples`.
- **`TickFetch` / `TickInput` run on the game thread and do bookkeeping only** —
  draining events, queue maintenance. Never work.

### The interrupt callback is not optional

`av_read_frame` on a dead RTSP connection blocks for **seconds**. An
`AVIOInterruptCB` wired to an atomic abort flag *and* a deadline goes in from the
first line of the demuxer.

Without it, `Close()` hangs and stopping PIE freezes the entire editor. This is
the single most common reason a media plugin feels broken.

### Sample-queue timing — the crux

Unreal's media pipeline is built around a **presentation clock**:
`FetchVideo(TimeRange, Sample)` pulls samples matching the player's current time.
That is correct for a file. A live stream has no seekable timeline and no
meaningful start time.

| Strategy | Behaviour | Cost |
|---|---|---|
| **Honest timestamps** | Map PTS onto a monotonic timeline, advance a clock, report it from `GetTime()` | Handles jitter properly, buys latency |
| **Latest-frame-wins** | Every sample's duration covers "now"; drop anything older than newest | Minimum latency, judders under jitter |

**Decision: build latest-frame-wins first**, because it is simpler and it is what
a surveillance monitor actually wants — but **put it behind a policy interface**
so Phase 2 can add a real jitter buffer and publish a measured side-by-side
comparison. The improvement is then demonstrable rather than asserted.

### Colour conversion — YUV420P to NV12 to RGB

Staged deliberately across milestones:

| | Approach | Where |
|---|---|---|
| **(a)** | CPU `swscale` to BGRA, report `CharBGRA` | **Spike only.** ~2–4 ms/frame at 1080p; uploads 4 bytes/px instead of 1.5 |
| **(b)** | YUV420P to NV12 (chroma interleave), report `EMediaTextureSampleFormat::CharNV12`, let `FMediaTextureResource` + `MediaShaders` convert on GPU | **Phase 1 target (M3).** Still requires correct strides, `GetDim` vs `GetOutputDim`, and the YUV to RGB matrix — which *is* the sample-interface contract |
| **(c)** | Custom `IMediaTextureSampleConverter`, owning the RHI pass | **Phase 2.** Natural home for a zero-copy D3D11 shared-texture path from a hardware decoder |

(a) then (b) then (c) is also a three-post devlog arc with measurable numbers at
each step.

---

## 8. Connection lifecycle

**Decision: reconnect lives inside the session, transparently, with backoff.**
Not "report failure and let Blueprint retry."

```
Idle -> Connecting -> Playing -> Reconnecting -> Failed
                          ^___________|
```

Surfaced through `EMediaState` and events. Two reasons this is worth doing in
Phase 1: it is what lets a portfolio capture survive a network hiccup, and it is
the clearest single differentiator from the naive FFmpeg-in-Unreal repositories
already on GitHub.

Open-time FFmpeg options to plan for: `rtsp_transport=tcp`, socket timeout,
`fflags=nobuffer`, `flags=low_delay`, small `probesize` / `analyzeduration`.

---

## 9. Packaging

### The `.gitignore` trap — fix before M0

Engine convention places third-party runtime binaries in
`Plugins/IPStreamMedia/Binaries/ThirdParty/...`. The repository's current
`.gitignore` contains:

```
*.dll
*.lib
Plugins/**/Binaries/*
```

**All three silently swallow the FFmpeg binaries.** Required:

- negation rules for the ThirdParty binary paths in `.gitignore`
- `*.dll` and `*.lib` tracked by LFS in `.gitattributes`

The failure is silent and only shows up when somebody else clones the
repository — which is the one moment that matters.

### DLL loading and staging are different mechanisms

- `PublicDelayLoadDLLs` plus an explicit `FPlatformProcess::GetDllHandle` at
  module startup, with the DLL directory pushed
- `RuntimeDependencies.Add(..., StagedFileType.NonUFS)` for packaged builds

Editor-side DLL loading working proves **nothing** about packaged-build staging.
This is why a packaged build is a milestone (M5) rather than a final step.

---

## 10. Milestones

Eight weekends, evenings and weekends only. One milestone is explicitly marked
cuttable as schedule insurance.

### M0 — one evening, before week 1: prove the stream outside Unreal

**Definition of done:** `ffprobe` and `ffplay`, using **the exact FFmpeg build
that will ship**, open the camera URL and display video. Output committed to
`Docs/TestSource.md`.

Required outputs from this milestone:

1. Full `ffprobe` dump of the main stream.
2. **Whether the camera exposes a substream.** If a lower-resolution or H.264
   profile exists, record its URL — the spike targets *that*, and HEVC becomes an
   M2 problem. Two unknowns at once is how a spike fails ambiguously.
3. **GOP length / keyframe interval.** Some cameras default to 4+ seconds between
   keyframes, which directly bounds first-frame latency and therefore the "10
   seconds" figure in the M1 pass criterion. If the GOP is long, adjust that
   number *before* committing to it — otherwise a pass reads as a fail.
4. Whether parameter sets arrive in-band or SDP-only.

Thirty minutes that permanently removes "is it the camera or my code?" from every
subsequent debugging session.

### M1 — Week 1: THE SPIKE (go / no-go)

> **From a fresh clone, in a UE 5.3 editor, one Blueprint node given the camera's
> RTSP URL puts a recognisable live image from that camera onto a plane in the
> level within 10 seconds — and stopping PIE returns control to the editor
> immediately, with no hang and no crash.**

Clean shutdown is folded into the pass criterion deliberately. A spike that
renders a frame but hangs the editor on exit has not proven viability — it has
proven that the hardest remaining problem, lifecycle against a blocking network
API, is untouched. Including it costs an afternoon and is the difference between
a real go/no-go and a false positive.

**In scope:** FFmpeg External module, `.Build.cs`, DLL loading, demux, decode,
CPU swscale to BGRA, `UTexture2D` update, one Blueprint node.

**Explicitly out:** `IMediaPlayer`, the factory, packaging, audio, reconnect, GPU
conversion, continuous playback. One frame passes.

**This code is deleted in week 2.** That is intended — the spike buys
information, not assets.

### M2 — Weeks 2–3: real player module

**Definition of done:** `UMediaPlayer::OpenUrl("rtsp://...")` driving a
`UMediaTexture` shows live video on a material in PIE. Factory registered,
`MediaOpened` fires, video track reported, `FMediaSamples` in use, decode on a
worker thread, clean `Close()`. CPU BGRA still acceptable. No reconnect yet.

The "it's real" milestone — nothing about the API is bespoke from here on.

### M3 — Week 4: GPU colour conversion — **CUTTABLE**

**Definition of done:** samples reported as NV12, engine-side GPU conversion,
visually identical to the CPU path, with before/after CPU-time measurements on
both the game thread and the worker.

**This is the only cuttable milestone, and it is named now so the decision is
pre-made rather than panicked.** If week 6 arrives and this is not done, ship
with CPU conversion — the plugin is still complete and working. This is the
insurance against the 70%-finished outcome.

### M4 — Week 5: lifecycle hardening

**Definition of done:**

- Camera network cable pulled mid-stream → reconnects without an editor restart
- PIE killed mid-stream → no hang
- Bad URL → clean failure event, no crash
- Open/close 50× in a loop → no leak

Not skippable. This is what separates the project from a toy.

### M5 — Week 6: measurement and packaged build

**Definition of done (a):** a documented, reproducible latency figure.
**Definition of done (b):** a **packaged Windows build** — not just editor — runs
and streams.

(b) sits here rather than at the end on purpose. DLL staging bugs appear only in
packaged builds, and they should surface in week 6 with slack remaining, not in
week 8.

### M6 — Weeks 7–8: demo and write-up

**Definition of done:** a room with a wall-mounted screen showing the live feed;
Blueprint controls for open, close, and forced reconnect; primitives and free
marketplace assets only. README with architecture diagram, latency figure and
method, licensing notes, and clone-and-run instructions. Two to three minute
screen capture.

---

## 11. Latency measurement method

Phase 1 ships a **method and a number**, not a target. A credible methodology is
the portfolio artifact; a number without one is worth nothing. Optimisation is
Phase 2, where the improvement can be shown against this baseline.

### Glass-to-glass

Point the camera at a monitor displaying a millisecond stopwatch. Display the
live feed on a plane in Unreal. Capture a single frame containing **both** the
real stopwatch and Unreal's render of that stopwatch. The difference is
end-to-end latency, including camera encode, network, decode, render, and
display.

- Twenty samples; report **median and spread**, not a best case.
- **State the confounders** — monitor refresh interval and camera exposure each
  contribute a frame or two. Say so rather than implying precision that is not
  there.

### Instrumented breakdown

Instrument the pipeline with `TRACE_CPUPROFILER_EVENT_SCOPE` and read it in
Unreal Insights:

```
packet arrival -> decode complete -> sample queued -> rendered
```

This is the half that reads as engine depth, and it is what makes Phase 2
improvement measurable at a specific stage rather than asserted in aggregate.

---

## 12. Decision log

Settled. Reopen only on new technical evidence.

| # | Decision | Rationale |
|---|---|---|
| D1 | RTSP first; SRT Phase 2; RTMP Phase 3 | Pull model, easiest to test, highest everyday utility |
| D2 | Proper Media Framework player, not a bespoke API | The integration *is* the portfolio point |
| D3 | FFmpeg, LGPL, shared DLLs, dynamic linking | §4 |
| D4 | Binaries committed via LFS, not fetched by script | LFS present anyway; clone-and-run is the point |
| D5 | Plugin code MIT | Dynamic linking keeps LGPL out of it |
| D6 | Two modules: runtime + factory | Engine convention; `PostConfigInit` registration ordering |
| D7 | FFmpeg as `ModuleType.External` | Isolates the most commonly botched integration surface |
| D8 | Spike is throwaway, no Media Framework | Isolates the genuinely unknown risks |
| D9 | Latest-frame-wins first, behind a policy interface | Minimum latency now; measurable improvement in Phase 2 |
| D10 | Reconnect is transparent, inside the session | Survives network hiccups on camera; key differentiator |
| D11 | No latency target, only a documented method | Improvement over time beats a single unverifiable number |
| D12 | M3 (GPU conversion) is the designated cuttable milestone | Schedule insurance against shipping nothing |

---

## 13. Open items

All three resolved by M0 — none blocks starting.

- [ ] **Does the camera expose a substream?** If yes, M1 targets it and HEVC
      moves to M2.
- [ ] **Camera GOP length.** Bounds first-frame latency, and therefore the "10
      seconds" in the M1 pass criterion. Adjust that number before committing.
- [ ] **Does the pinned FFmpeg build include libsrt?** (`ffmpeg -protocols`)
      Determines whether Phase 2 needs an FFmpeg rebuild.
