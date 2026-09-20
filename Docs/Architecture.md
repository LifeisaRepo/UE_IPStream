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

The camera exposes two RTSP streams. **Phase 1 targets the secondary stream.**

| Property | Secondary (Phase 1 target) | Primary (deferred) |
|---|---|---|
| Transport | RTSP over LAN | RTSP over LAN |
| Video codec | HEVC / H.265 | HEVC / H.265 + "InstaStream" |
| Resolution | 1280×720 | 1920×1080 |
| Frame rate | 25 fps | 25 fps |
| GOP | **50 frames / 2.0s, measured, admin-editable** | ~8.0–8.1s, measured, **not editable while InstaStream is active** |
| Audio codec | PCM A-law (ignored — see non-goals) | PCM A-law (ignored) |

### Why the secondary stream, not the primary

Both were measured directly against the real camera in Session 1 (full data in
[TestSource.md](TestSource.md)). The primary stream runs a proprietary
adaptive-codec feature the camera's firmware labels "InstaStream," which
manages its GOP internally, unpredictably, and outside admin control — measured
at ~8 seconds, not adjustable while the feature is active. The secondary stream
does not run it, exposes a normal editable "I Frame Interval" field, and
measured GOP matches that field's value almost exactly (50 frames / 2.0s across
14 consecutive gaps, essentially zero drift).

This is a **materially better number for the deliverable**, not just a spike
convenience: worst-case join latency drops from ~8s to ~2s, which also softens
the reconnect-freeze concern flagged for M4. It also gives the project a real,
admin-controllable GOP knob — an axis for a measured-improvement comparison
later (D11), which the primary stream cannot offer since InstaStream owns that
decision.

**The primary stream is not abandoned, only deferred.** Disabling InstaStream on
the camera restores manual control of its I-frame interval, which would make a
1080p Phase-2-or-later comparison possible. **Not done now: this camera is
shared via an NVR with at least one other active user, and reconfiguring it is
not a one-person decision.** See [CLAUDE.md](../CLAUDE.md) — this constraint
applies to any future milestone that touches camera-side configuration, not just
this decision.

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

**FFmpeg, LGPL, shared DLLs, dynamically linked. Never static. Never
`--enable-gpl`.**

### Why, in plain terms

FFmpeg ships under **LGPL 2.1** by default. Passing `--enable-gpl` at configure
time makes the entire build **GPL**, regardless of version. A third, separate
flag, `--enable-version3`, upgrades the *LGPL* (not GPL) build from 2.1 to
**LGPL v3** — this is independent of `--enable-gpl` and easy to miss because
the name doesn't mention "GPL" at all. **The pinned build for this project
(see `ThirdParty/FFmpeg/NOTICE.md`) has `--enable-version3` set and
`--enable-gpl` unset, confirmed both from its configure line and by reading
its bundled `LICENSE.txt` directly — it is LGPL v3, not the FFmpeg default of
2.1.** Doesn't change the reasoning below at all; only which exact license
text gets bundled and cited.

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
3. `ThirdParty/FFmpeg/COPYING.LGPLv3` — the licence text, verbatim, matching
   whatever version the pinned build actually reports (check its own
   `LICENSE.txt`, don't assume 2.1).
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

**Checked 2026-09-08, on the pinned build (see `ThirdParty/FFmpeg/NOTICE.md`):**
`ffmpeg -protocols` lists `srt` under both Input and Output — **libsrt is
present.** The cheap Phase 2 path above is confirmed available; the
counterpoint above still applies and the choice is still not made.

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
        FFmpeg.Build.cs   # ModuleType.External
  ThirdParty/
    FFmpeg/
      include/          # libavcodec/ libavformat/ libavutil/ libswscale/
      lib/Win64/        # import libraries
      bin/Win64/        # runtime DLLs
      COPYING.LGPLv3
      NOTICE.md
  Content/              # demo material + MediaTexture. Minimal.
  Resources/Icon128.png
  Docs/
```

**Correction (M1, verified against UBT source on disk,
`Engine/Source/Programs/UnrealBuildTool/System/RulesCompiler.cs`):** UBT only
discovers plugin modules — including `ModuleType.External` ones — under
`Source/`. A `.Build.cs` living directly under the plugin root's `ThirdParty/`
(as originally sketched here) is never scanned and the module would not exist.
Epic's own `OpenCV` plugin (`Engine/Plugins/Runtime/OpenCV/Source/ThirdParty/OpenCV/OpenCV.Build.cs`)
is the precedent this now follows: the `.Build.cs` lives at
`Source/ThirdParty/FFmpeg/FFmpeg.Build.cs`, a near-empty module folder that
only contains build rules, while the actual binaries stay where they were
already committed via LFS — `Plugins/IPStreamMedia/ThirdParty/FFmpeg/{include,lib,bin}` —
referenced from the Build.cs via `PluginDirectory`. Two real folders named
`FFmpeg` under the same plugin, one holding a build-rules file and one holding
binaries, reads oddly at first glance; the alternative (moving the binaries
under `Source/ThirdParty/FFmpeg/`) was rejected to avoid re-doing the LFS
commit for no functional gain — UBT does not care where the binaries
physically live, only where the `.Build.cs` sits.

### Why two modules

**A factory is metadata about a player — queryable from anywhere, including
platforms where the player itself can never load.** That, not load ordering, is
what the split is for.

The evidence is `UBaseMediaSource::PreSave`
(`Runtime/MediaAssets/Private/Assets/BaseMediaSource.cpp:43-53`), which runs in
the **editor** while saving a `UMediaSource` **for a target platform other than
the one the editor is running on** — cooking an iOS build from a Windows PC. For
the details panel to offer *"on iOS, use AvfMedia"*, the Windows editor must be
able to enumerate a factory for a player that cannot load on Windows. Hence
`AvfMediaFactory` is compiled for Win64 while `AvfMedia` is not.
`FMediaPlayerFacade` confirms the same expectation from the other direction: it
filters with `Factory->SupportsPlatform(RunningPlatformName)`
(`MediaPlayerFacade.cpp:214`), which is only meaningful in a system that assumes
registered factories for platforms you are not on.

So the division of labour is: the **player** is the heavy, platform-bound,
third-party-linked thing; the **factory** is a light description of it that
travels where the player can't.

**Loading phases — corrected 2026-09-12.** An earlier version of this section
claimed the factory loads at `PostConfigInit` to win a registration race, and
that this mirrored `WmfMedia`. Both halves were wrong. Every shipped media
backend does the opposite:

| Plugin | Player module | Factory module |
|---|---|---|
| WmfMedia | `PostConfigInit` | `PostEngineInit` |
| ElectraPlayer | `PreLoadingScreen` | `PostEngineInit` |
| AvfMedia | `PreLoadingScreen` | `PostEngineInit` |
| AndroidMedia | `PreLoadingScreen` | `PostEngineInit` |

There is no race to win. The only requirement is *registered before someone opens
a media URL*, and the earliest that can happen is a `UMediaPlayer` — a `UObject`
asset driven from Blueprint or gameplay — which is vastly later than
`PostEngineInit`. Loading a factory at `PostConfigInit` buys nothing and costs
something: it drags the whole `Media` module up the startup order, and
editor-facing calls in a factory's `StartupModule` silently no-op that early.
`WmfMediaFactoryModule.cpp:186-195` is the proof — its `ISettingsModule` lookup
returns `nullptr` before the engine is up, and its settings page would simply
never appear, with no error.

The early phase in that table belongs to the **player**, where it is earned:
`WmfMedia` initializes Windows Media Foundation, a platform subsystem. That is a
genuine low-level hook in the sense `ELoadingPhase` means it.

Full derivation, including the reading that settled it:
`M2DesignDerivation.md` Q2. The `.Build.cs` dependency
model that follows from this split — why `Media` is headers-only while
`MediaUtils` needs real linkage — is Q3 in the same doc.

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

### D13: implement latest-frame-wins on `IMediaPlayer`'s V2 timing model, not V1

Decided in the Block C concept-prep session (2026-09-10), after reading
`MediaPlayerFacade.cpp` and three shipped 5.3 players directly rather than
assuming. `IMediaPlayer::EFeatureFlag::UsePlaybackTimingV2` gates an entire
alternate timing model (`FMediaTimeStamp`, sequence-indexed) alongside the
original `FTimespan`-based one ("V1", the default when the flag isn't
overridden).

**The evidence, not just the theory:**

| Player | Timing model |
|---|---|
| `RivermaxMediaPlayer` (SMPTE ST 2110 broadcast ingest — closest existing engine analog to this project) | V1, unmodified |
| `SharedMemoryMediaPlayer` (nDisplay live frame source) | V1, unmodified |
| `WmfMediaPlayer` (newer internal implementation) | V2 |
| `ElectraPlayerPlugin` (Epic's flagship — HLS/DASH, full AV sync) | V2, unconditionally |

So the two existing "live frame source, no seek, no meaningful duration"
players both stay on V1 — the naive read would be "V1 is right for us too."
Three things changed that:

1. **`FMediaSamples`/`FMediaTextureSampleQueue` — the queue this project
   already committed to using (§6) — already implements V2's sample-selection
   algorithm** (`FetchBestSampleForTimeRange`, best-overlap-then-newest). V2
   doesn't cost a hand-written selection algorithm on top of the engine
   dependency already taken.
2. **`FMediaTimeStamp::SequenceIndex` exists, by its own header comment, to
   mark "an event that causes time to no longer be monotonic — e.g. seek or
   loop."** D10's transparent in-session reconnect is exactly that class of
   event: a fresh RTSP session means FFmpeg's PTS restarts near zero while
   the player session stays open. V1 has no field for this — a player has to
   detect the reconnect itself and manually rebase every subsequent
   timestamp by an accumulating offset, with a fresh chance to get the
   offset wrong on every reconnect. V2 replaces that with incrementing one
   integer per reconnect; the comparison operator (`Seq` compared before
   `Time`) then makes "the new session's frame 0.04s counts as later than
   the old session's frame 47.20s" correct automatically.
3. Epic's own *new* investment (Electra unconditionally, WmfMedia's newer
   path) is V2. No explicit deprecation of V1 was found — that claim would
   outrun the evidence — but it's a reasonable signal for where engine
   investment is headed across future UE versions.

**The alternative that lost:** V1 + hand-rolled latest-frame-wins (drop
older samples before/while adding to the queue, rebase PTS across
reconnects manually). Simpler at the surface and precedented by
`RivermaxMediaPlayer`/`SharedMemoryMediaPlayer`, but it reinvents, by hand,
a discontinuity-handling job `FMediaTimeStamp` already has a field for — and
this project has an actual discontinuity source (D10 reconnects) that V1
would hit in practice, not hypothetically.

**Consequence for M2:** `FIPStreamPlayer::GetPlayerFeatureFlag` returns true
for `UsePlaybackTimingV2` (and `PlayerUsesInternalFlushOnSeek`, since seek is
unsupported and the player should own that fact rather than let the facade
issue seek-flush calls that don't apply). `AlwaysPullNewestVideoFrame` is the
concrete mechanism for the latest-frame-wins policy interface from D9 — the
policy interface itself still exists (Phase 2's jitter buffer replaces the
feature-flag value and the sample-timestamping logic, not the facade
contract). On every detected reconnect, bump the sequence index passed to
`FIPStreamTextureSample`'s `FMediaTimeStamp` rather than rebasing PTS.

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

### Reconnect policy — D16

**Who decides when to give up: the user, per media source.** One option key,
`MaxReconnectAttempts`:

| Value | Behaviour |
|---|---|
| `0` (default) | retry forever, at the capped backoff interval |
| *N* | give up after *N* failed attempts and report `Failed` |

`0` is the default because the demo is a surveillance monitor on a camera that is
expected to come and go: an end-of-stream usually means the server closed the
session (reboot, NVR drop, network blip), not that the stream is gone for good.
Retrying forever is cheap — one connect attempt per interval — and never blocks
shutdown, because `Close()` interrupts the wait (`M2DesignDerivation.md` Q5).

**How the user sets it, with no C++ and no new asset type.** `UMediaSource`
already exposes BlueprintCallable option setters — "SetMediaOption (integer64)"
and friends, `MediaSource.h:174-186` — and the source object itself is handed to
the player as its `IMediaOptions`
(`MediaPlayer.cpp:677`: `PlayerFacade->Open(MediaSource->GetUrl(), MediaSource, PlayerOptions)`).
So two Blueprint nodes before "Open Source" configure any stock
`UStreamMediaSource`.

**The cost, accepted:** that option map is a plain `TMap`, not a `UPROPERTY`
(`MediaSource.h:190`), so values are runtime-only. They are not saved in the
asset and do not appear in the details panel.

**Deferred, and explicitly optional if it ships:** our own `UMediaSource`
subclass, which would give the same values as saved fields in the details panel.
A later version, never a requirement — the plugin must stay fully usable with the
stock media source.

**The worker never ends itself.** Every decode result except `Aborted` means
"this connection is over", not "the worker is over". Only the owner ends the
worker, through `Stop()`. Hitting `MaxReconnectAttempts` is the single exception,
and it is a user-chosen limit rather than the worker's own judgement.

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

### Delay loading has an unstated prerequisite: import library format

**Discovered the hard way during M1 (2026-09-09) — full diagnosis in
`M1DelayLoadRCA.md`.**

`PublicDelayLoadDLLs` is necessary but **not sufficient**. MSVC's
`/DELAYLOAD` can only transform imports that come from **MSVC
short-import-format** import libraries. The FFmpeg build pinned here is
cross-compiled with MinGW/GCC, so its shipped `.lib` files are GNU-format
(full COFF objects with explicit jump thunks). Handed those, the linker
accepts `/DELAYLOAD`, links `delayimp.lib`, emits **no warning**, and
produces ordinary load-time imports anyway.

The consequence is specifically nasty because it inverts the load order this
whole section depends on: the DLLs become load-time dependencies, Windows
demands them before it will load the module binary, the module binary fails
to load, and therefore `StartupModule()` — containing the
`PushDllDirectory`/`GetDllHandle` code written to solve exactly this — never
runs at all. The design is correct; it simply never executes.

Fixed by regenerating the four linked import libraries from the `.def` files
shipped alongside them, with `lib.exe /DEF: ... /NAME:<versioned>.dll` —
commands recorded in
[`ThirdParty/FFmpeg/NOTICE.md`](../Plugins/IPStreamMedia/ThirdParty/FFmpeg/NOTICE.md),
which is also where the "repeat this after any FFmpeg re-download" warning
lives.

**Standing check whenever the FFmpeg pin changes:** after rebuilding, run
`dumpbin /DEPENDENTS` on the module binary and confirm the FFmpeg DLLs sit
under *delay load dependencies*, not plain dependencies. Configuration being
present in `.Build.cs` proves nothing about whether it took effect.

Editor-side DLL loading working proves **nothing** about packaged-build staging.
This is why a packaged build is a milestone (M5) rather than a final step.

---

## 10. Milestones

Eight weekends, evenings and weekends only. One milestone is explicitly marked
cuttable as schedule insurance.

### Concept curriculum (Blocks A–E)

Each milestone is front-loaded by a concept-prep block — no code until the
underlying ideas are in place, per the *concept prep precedes each milestone*
rule in `CLAUDE.md`. This table was defined in an earlier discussion that
hadn't yet been written to disk; recovered and recorded here so it doesn't
depend on a specific conversation's memory again.

| Block | Before | Covers |
|---|---|---|
| A | M0 | Video coding fundamentals — how a stream becomes frames. Codecs, GOP, NAL units, parameter sets, RTSP vs RTP vs SDP, why demux and decode are separate steps. |
| B | M1 | FFmpeg's architecture (four libraries, the packet→frame loop) + Unreal's build system (UBT, modules, `.Build.cs`, DLL loading and staging). |
| C | M2 | Media Framework — what each interface is for, and why the engine is shaped this way. |
| D | M3 | Colour, pixel formats, and GPU conversion. |
| E | M4–M5 | Threading, lifecycle, and measurement methodology. |

**Block A's exit test:** read your own `ffprobe` output and be able to account
for every line of it, including open items — learn, then immediately apply to
real hardware. Satisfied during Session 1: [Docs/TestSource.md](TestSource.md)
records the accounted-for output, including the InstaStream/GOP investigation
that came out of it.

**Status:** Blocks A and B done — FFmpeg's architecture and Unreal's build
system (UBT, modules, `.Build.cs`, DLL loading and staging) both covered. (This
line previously lagged the session log; corrected 2026-09-08 to match
[the session log](../UE_IPStream_Session_Log.md)'s Current-state block, which
is the more current source.)

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

> **From a fresh clone, in a UE 5.3 editor, one Blueprint node given the
> secondary stream's RTSP URL puts a recognisable live image from that camera
> onto a plane in the level within 8 seconds — and stopping PIE returns control
> to the editor immediately, with no hang and no crash.**

**8 seconds, revised down from a provisional 15.** The 15-second figure was
calibrated against the *primary* stream's ~8-second GOP, before Session 1
discovered that stream runs a proprietary adaptive-codec feature
("InstaStream") with an unpredictable, non-adjustable GOP. **Phase 1 targets
the secondary stream instead** (§3) — its GOP measures a clean, admin-
controlled 50 frames / 2.0 seconds, confirmed consistent across 14 I-frame
intervals in a 30-second capture. Worst-case GOP wait is therefore ~2 seconds;
8 seconds gives roughly 4× margin over that for RTSP handshake, decode startup,
and texture upload — a meaningful bar (if it's taking 8+ seconds, something is
likely actually wrong) without being so tight that ordinary timing jitter reads
as a false failure.

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
| D6 | Two modules: runtime + factory | A factory is *metadata about a player*, queryable where the player can't load (editor cooking for another platform). **Rationale corrected 2026-09-12** — not registration ordering; factory loads `PostEngineInit`, player earlier. §5, and `M2DesignDerivation.md` Q2 |
| D7 | FFmpeg as `ModuleType.External` | Isolates the most commonly botched integration surface |
| D8 | Spike is throwaway, no Media Framework | Isolates the genuinely unknown risks |
| D9 | Latest-frame-wins first, behind a policy interface | Minimum latency now; measurable improvement in Phase 2 |
| D10 | Reconnect is transparent, inside the session | Survives network hiccups on camera; key differentiator |
| D11 | No latency target, only a documented method | Improvement over time beats a single unverifiable number |
| D12 | M3 (GPU conversion) is the designated cuttable milestone | Schedule insurance against shipping nothing |
| D13 | D9's latest-frame-wins built on `IMediaPlayer`'s V2 timing model, not V1 | §7 — `FMediaSamples` already implements V2 sample selection; `SequenceIndex` fits D10 reconnects natively; Electra's own precedent |
| D14 | `FIPStreamPlayer` **owns** a `TUniquePtr<FMediaSamples>`; implements the other four sub-interfaces directly | RTSP frames arrive sequentially, and FIFO is the right structure for sequentially-arriving data. Nothing in the engine inherits `FMediaSamples` — it is a component, not a base class. Reopens only if a Phase 2 D9 policy can't be expressed as queue configuration. `M2DesignDerivation.md` Q1 |
| D15 | The decode loop runs on an owned `FRunnable` worker; `Close()` **joins** it, and M1's interrupt callback gains a stop flag so the join is bounded | A hard join (ImgMedia's pattern) is only unsafe while the worker is unreachable inside `av_read_frame`; the interrupt callback is the one wire that reaches it, and M1 measured its granularity at 39 ms. Electra's detached async teardown lost as **disproportionate** — it buys freedom from a millisecond-scale wait and charges nondeterministic teardown, a possible second RTSP session against a shared camera, and a crash risk unique to us (we `FreeDllHandle` FFmpeg by hand). Reopens if the join measures >~100 ms, if teardown stops being boundable (M3/hardware decode), on multi-stream, or if any blocking path ignores the callback. `M2DesignDerivation.md` Q4 |

---

## 13. Open items

All three resolved by M0 — none blocks starting.

- [ ] **Does the camera expose a substream?** If yes, M1 targets it and HEVC
      moves to M2.
- [ ] **Camera GOP length.** Bounds first-frame latency, and therefore the "10
      seconds" in the M1 pass criterion. Adjust that number before committing.
- [x] **Does the pinned FFmpeg build include libsrt?** (`ffmpeg -protocols`)
      **Resolved 2026-09-08 — yes.** See §4's "do this at pin time" note and
      `ThirdParty/FFmpeg/NOTICE.md`.

**New, opened 2026-09-08:**

- [ ] **Audit the ~40 other third-party libraries statically built into the
      pinned FFmpeg DLLs** (`libsrt`, `gmp`, `libssh`, `sdl2`, `libaom`, etc.
      — full list in the pinned build's configure line, `NOTICE.md`). Only
      FFmpeg's own LGPL status has been reviewed so far. Not blocking Phase 1
      — this plugin never calls any of them directly — but a real gap before
      the licensing story is fully audited, on a project where "public repo,
      licensing is a hard constraint" is a stated active constraint.
