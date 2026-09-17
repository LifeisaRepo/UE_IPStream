# UE_IPStream — Session Log

Running discussion record for this project. Chronological, oldest first.
The **Current state** block below is the fast path — read it first, then the most
recent entries for detail.

## How to maintain this file

- Append an entry at the end of any session that produces a **decision**, a
  **milestone change**, a **measurement**, or a **change of direction**.
- Update the **Current state** block in the same edit. A stale header is worse
  than no header.
- Record decisions *and the reasoning behind them*. A future session needs to
  know why, otherwise it will re-litigate.
- Record what was **rejected** and why. That is usually the more valuable half.
- Keep raw measurements here even when they also go elsewhere — this file is the
  timeline.

---

## Current state

**As of:** 2026-09-18 (Session 12)
**Phase:** 1 — RTSP ingest

**READ FIRST — how to explain, changed 2026-09-17.** `CLAUDE.md` now carries six
hard rules under **"How to explain — hard rules, from 2026-09-17"**, and they
outrank ordinary style guidance: no jargon explaining jargon; one idea per
chunk; concept before code; check with a restatement not "make sense?"; go down
a level rather than re-explaining longer; fewest and simplest words. The older
bullet "explanation length is not a cost" has been corrected to **"coverage is
not a cost; repetition is"**. Session 11 below has the context.

**M2 step 2, item 1 is CODE-COMPLETE.** The FFmpeg session object
(`FIPStreamFFmpegSession`) has both `.h` and `.cpp` written, typed in and
reviewed — **but not yet compiled, and that is the next action.** Nothing calls
it yet, so it builds as dead code, which is exactly the check wanted before the
worker goes on top.

Decisions along the way: `std::atomic` over `TAtomic` (`Atomic.h:13`),
`UE_NONCOPYABLE`, a five-value `EIPStreamDecodeResult`, the stop flag declared
on the session rather than the `FRunnable` (the interrupt callback gets one
`void*`, so that pointer is `this`), `relaxed` memory ordering on the flag,
`DecodeNext()` asking the decoder before reading a packet, and 6.0 s connect /
2.0 s read timeouts. `Close()` deliberately does **not** reset the stop flag.
Two bugs caught in review — a missing `return` after a decode error, and an
omitted `avcodec_parameters_to_context()`. Full detail in Session 12;
call-by-call reasoning in `M2ThreadingWalkthrough.md` §1-7.

**Item 2 (the stop flag in the interrupt callback) was absorbed into item 1.**
Next is item 3, the `FRunnable` worker.

**Status: M2 STEP 1 PASSES — the Media Framework chain is proven end to end,
with no FFmpeg in it.** Factory registers at `PostEngineInit`; `IP Stream Media`
is selectable as the player override on a `UMediaSource`; our factory beats
WmfMedia for `rtsp`; our player is constructed via
`IIPStreamMediaModule::CreatePlayer`; `MediaOpened` reaches a Blueprint
`Print String` in PIE; `Player Closed` on PIE stop. All five files written by
Sanjyot from chat. Call-by-call reasoning in
`M2PlayerWalkthrough.md`. Full detail in
Session 9 below.

**Committed and pushed** — `8d73e99` ("IPStreamMedia Player working skeleton"),
`main` in sync with `origin/main`, working tree clean, full credential scan of
HEAD clean.

**Q4 (the threading model) is DERIVED AND SETTLED as D15 — Session 10.**
The decode loop runs on an owned `FRunnable` worker; `Close()` **joins** it; and
M1's interrupt callback gains a stop flag so that join is bounded rather than a
hang. Electra's detached async teardown was considered and rejected as
disproportionate, with five recorded conditions that reopen it. Full record in
`M2DesignDerivation.md`; summary in Session 10 below.

**Next action: M2 step 2 — FFmpeg demux/decode on the worker thread**, behind
the `Open()` that already works. D15 commits us to **measuring the game-thread
pause at PIE stop** (M1's `Tick`-gap technique): single-digit ms confirms the
decision, beyond ~100 ms invalidates its premise and reopens the alternative.

**Local study notes now live in the Obsidian vault**
(`Q:\Obsidian\LIAR_Learns\UE_IPStream\IssuesAndWalkthroughs`, a private git
repo) and `Docs/IssuesAndWalkthroughs` is a **junction** to it, verified working
from both VS Code and Obsidian. They were deleted on 2026-09-14 and recovered
from the recycle bin on 2026-09-16 — this closed that gap. Still to do:
`git add` and push them in the vault repo.

**Credential hazard, hit and resolved — read before creating any test asset.**
A `UStreamMediaSource` stores its URL as a default property **inside the binary
`.uasset`**, and `Content/` is tracked — so a committed test asset leaks the
camera password with *nothing readable in the diff*, and the `*.txt` ignore rule
does not apply. **The remedy is to remove the secret, not to hide the file:** the
URL field is cleared to a dummy and the asset re-saved, which keeps
`Content/MediaAssets/` committable (the player-override setting is a separate
field and is worth keeping for M6). **On any future credential finding: alert
Sanjyot and stop — he chooses the remedy.** Also: the camera password
**contains an `@`**, so any check or redaction cutting at the *first* `@` is
wrong — search the authority section (before the first `/`) and take the *last*
`@`, as `RedactUrlCredentials` does.

**M1 (the spike) passed, committed, and pushed** — commit `81714c5`
("First Spike Successful"), confirmed on `main` matching `origin/main`, not
assumed. All three pass criteria met: recognisable image on a plane, timing
measured (0.999s success / 6.039s failure), clean PIE stop with no hang or
crash across multiple good and bad runs. Full detail in Session 5 below.

**M1's throwaway code is deliberately still in the project.** Per D8 it's
meant to be deleted, but that's been explicitly deferred this session:
Sanjyot doesn't want it gone until M2 has something working and testable to
replace it, not just because M1 technically finished. Still present:
`IPStreamSpike.h`/`.cpp`, `Content/BP_SpikePlane.uasset` (placed inside
`Content/Maps/Main.umap` — deleting it outside the editor would leave a
dangling reference in the map), `M_SpikeTest`/`MPC_SpikeTest`, and
`WBP_Timer` (the Tick-gap timer widget used for the M1 timing measurement).
None of these are to be touched until M2 has a replacement.

**Block C (Media Framework concept prep) is COMPLETE — all 5 chunks, across
Sessions 6–7.** Covered: the big picture (why `IMediaPlayer`,
`IMediaPlayerFactory`, `IMediaEventSink`, `IMediaSamples`, and
`IMediaTextureSample` each exist and how they wire together at runtime),
`IMediaPlayer`'s full contract (mandatory vs optional virtuals, the two
`Open()` overloads, why `GetPlayerPluginGUID` exists on two interfaces),
`IMediaEventSink` (the public/internal event split and why events are
pushed from a worker thread and queued rather than polled — traced through
the actual facade code, which is *why* the "fire events from day one"
gotcha in `Architecture.md` §6 is real), `IMediaPlayerFactory` (checked
against a real shipped factory, `WmfMediaFactoryModule.cpp` — including a
concrete gotcha for M2 testing: WmfMedia already claims the `rtsp` scheme,
and factory selection with no explicit override is pure registration
order), and `IMediaSamples`/`IMediaTextureSample` together (the thread-safe
queue, the per-sample data contract, and `TMediaObjectPool`'s
reuse-via-custom-deleter trick). Full detail in Sessions 6–7 below.

**Chunk 5 needed a full remedial pass — recorded as a standing lesson, not
just a one-off fix.** The first attempt at explaining `FMediaSamples`
thread-safety and `TMediaObjectPool` by naming mechanisms
(`FScopeLock`/`FCriticalSection`, `TSharedPtr` custom deleters) and citing
engine source didn't land — Sanjyot has *written* `FRunnable`/`FScopeLock`
code before, but didn't have deep conceptual grounding in *why* a lock is
needed (race conditions) or how `TSharedPtr` reference counting works
underneath, and hadn't read Media Framework source firsthand. Rebuilt from
true fundamentals (a concrete race-condition example using this project's
own `AddVideo`/`FetchVideo` calls, then the lock as "only one key" — then
separately, raw-pointer ownership ambiguity → refcounting → custom
deleters as recycling instead of deleting) before reconnecting to the named
UE mechanisms, plus two more rounds of Sanjyot restating the model in his
own words and me correcting small precise inaccuracies (pool size isn't
the queue's `MaxNumberOfQueuedVideoSamples = 4`, it floats to match actual
peak concurrent usage; recycling triggers on refcount-zero, not
specifically "after the render thread reads it" — relevant since D9 drops
stale samples that are never rendered at all). Saved to memory
(`feedback_unfamiliar_capi_teaching`, broadened beyond just C-APIs): "has
used this API before" and "understands the concept it's built on" are
different claims, confirmed now for UE's own threading/smart-pointer
internals too, not just third-party C libraries — verify explicitly rather
than inferring one from the other, and lead with fundamentals before naming
the UE mechanism, pre-emptively, for threading/smart-pointer explanations
on this project going forward.

**D13 decided this session** (see `Architecture.md` §7 and the decision log
in §12 for full evidence): M2's `FIPStreamPlayer` will implement D9's
latest-frame-wins on `IMediaPlayer`'s **V2 timing model**
(`FMediaTimeStamp`/`SequenceIndex`), not the older V1 model. Grounded in
reading `MediaPlayerFacade.cpp` directly plus three shipped 5.3 players
(`RivermaxMediaPlayer` and `SharedMemoryMediaPlayer` stay on V1;
`ElectraPlayerPlugin` uses V2 unconditionally), and in the discovery that
`FMediaSamples` — already committed to in §6 — implements V2's sample-
selection logic for free. The deciding factor: `SequenceIndex` exists
specifically to handle non-monotonic timeline jumps ("seek or loop," per the
engine header), and D10's transparent reconnect is exactly that kind of
event — V1 would require hand-rolled PTS rebasing on every reconnect; V2
just increments one integer.

**M2 DESIGN IS BEING DERIVED, NOT DELIVERED — working-method change, Session
8, and it governs everything from here.** Sanjyot stopped M2's first code
delivery because reading engine source myself and reporting conclusions
transmits **precedent, not reasoning** — "Electra does it this way" is the
answer that collapses when an interviewer asks *why Electra does it that way*.
M1 was throwaway per D8 so this was survivable; **M2 onward is permanent**, so
the standard changes here. New method, in memory as
`feedback_design_derivation.md`: state the question with no answer, hand over
exact file paths and line ranges, **he reads and answers first**, then correct
and settle together, then record question + evidence + decision + the
alternative that lost in `M2DesignDerivation.md`.
Deliberately **not** applied to stub values or one-line-reversible choices.
"I don't know why" beats a confident rationalisation of engine convention.

**D14 decided this session** (`M2DesignDerivation.md` Q1): `FIPStreamPlayer`
**owns** a `TUniquePtr<FMediaSamples>` and implements `IMediaCache`/
`IMediaControls`/`IMediaTracks`/`IMediaView` directly. Derived from the
`FImgMediaPlayer`-inherits vs `FElectraPlayerPlugin`-composes contradiction:
nothing in the engine inherits `FMediaSamples` (grepped — zero hits), and
ImgMedia implements `IMediaSamples` itself only because its storage is
`TLruCache<int32, ...>` keyed by frame number, which a FIFO queue physically
cannot serve. Ours is sequential-arrival, so FIFO is correct.

**D6's rationale was found to be WRONG and has been corrected**
(`M2DesignDerivation.md` Q2, `Architecture.md` §5 and §12). It claimed
"`PostConfigInit` registration ordering"; in fact **every shipped media
backend loads its factory at `PostEngineInit`, after its player** — there is
no registration race, and `PostConfigInit` would silently break editor-facing
factory startup code. The real reason for the two-module split: **a factory is
metadata about a player, queryable where the player itself can't load** (a
Windows editor cooking for iOS must enumerate `AvfMediaFactory`). This also
resolves the Block C open thread about the explicit Windows player override —
it writes into `PlatformPlayerNames`, read at `BaseMediaSource.cpp:50`.
**`.uplugin` change agreed; Sanjyot is making that edit himself.**

**Q3 also settled** (`M2DesignDerivation.md` Q3): the `.Build.cs` dependency
model. `Media` is `DynamicallyLoadedModuleNames` + `PrivateIncludePathModuleNames`
because its 22 public headers hold **one** export site — a debug string helper —
so there is nothing to link against; `MediaUtils` (164 export sites, concrete
`FMediaSamples`) is a normal `PrivateDependencyModuleNames` entry. Key
correction recorded: `DynamicallyLoadedModuleNames` **loads nothing**, it
declares staging + build order without an import-table entry, which is what
actually keeps the factory decoupled from the player.

**`.uplugin` and both `.Build.cs` files are now updated and correct**, comments
included. Two bugs were caught by verifying rather than trusting: a `//` comment
in the `.uplugin` (JSON has none; would have stopped the plugin loading) and
three `.Build.cs` comments asserting wrong mechanisms.

**Step 1 is DONE and tested** (Session 9). GUID in use, generated by Sanjyot:
`FGuid(0x6bd90b53, 0xbe5340cd, 0xab90c371, 0x7f1b284a)` — identical in factory
and player, which is mandatory (`MediaPlayerFacade.cpp:404` dereferences the
lookup with no null check).

**Next action:** M2 **step 2** — FFmpeg demux/decode on a worker thread, behind
the `Open()` that already works. Already flagged for it: the destructor stops
being empty (a thread must be shut down there), and `CreatePlayer` may want an
`#if WITH_FFMPEG` guard so a failed DLL load returns `nullptr` rather than a
player that can never decode. Then step 3 (samples into `FMediaSamples`, turn on
`AlwaysPullNewestVideoFrame`) and step 4 (`UMediaTexture` in PIE). Still-open
thread from Block C: which `EMediaEvent`s fire around a reconnect — an M4
concern, doesn't block step 2.

**Standing gotcha, still live:** if the FFmpeg pin is ever re-downloaded,
the MSVC import libraries must be regenerated (see
`ThirdParty/FFmpeg/NOTICE.md`) or the plugin will build cleanly and fail to
load.

**Standing practice, in `CLAUDE.md`:** per-file "code walkthrough" docs (see
`M1FFmpegWalkthrough.md`, the template) —
chunked, call-by-call, checked interactively — for any code touching an
unfamiliar API surface. See Session 4 below for why this became a rule
rather than a one-off.

**RESOLVED 2026-09-05: credential exposure on the public GitHub repo.**
`output.txt`, committed in `1713393` ("M0 Testing", tip of `main`) and live on
`origin/main` (confirmed public) since 2026-08-27, contained the camera's RTSP
URL with plaintext credentials (`rtsp://admin:***@192.168.0.131:...` — real
password redacted here deliberately; see below for why that redaction matters
even after rotation). **Password has been rotated, and git history has been
cleaned** — full write-up below.

**M0 findings — see [Docs/TestSource.md](Docs/TestSource.md) for full detail:**
- Main stream: HEVC Main, 1920×1080, 25fps, `yuv420p(tv)`. Substream: HEVC,
  1280×720, 25fps — deprioritized as an M1 target (see below).
- 0 B-frames across 746 captured frames — confirmed low-latency encoder config.
- I-frame vs P-frame size: 260–269 KB vs 400 B–1 KB, a measured 270–670×.
- **GOP measured at ~8.0–8.1 seconds (~200 frames)** — a hard floor on
  worst-case join latency, independent of any Unreal-side code.
- **The camera's own admin UI claims a 2-second I-frame interval, locked/
  uneditable, on both streams. This is measurably wrong** — real GOP is ~4×
  that. Root cause undetermined (leading hypothesis: NVR-managed channel
  override, given `channel=2` in the URL); deliberately not investigated
  further — logged as a time-boxed non-blocking open item, not chased, per the
  project's own scope-discipline rule.
- **M1 pass-criterion timeout revised 10s → 15s** in `Architecture.md`, to keep
  real margin over the measured (not guessed) worst case.
- Substream: same codec family, lower res, identical locked "2s" UI value, no
  independent GOP measurement taken. Not pursued as an M1 target — resolution
  alone isn't a meaningful risk-reducer, and codec choice never was (see
  Session 1's H.264 correction, below).

**Open items remaining:**
1. Parameter set delivery (in-band vs SDP-only) — not directly tested; soft
   signal (clean decode with no special flags) suggests it's a non-issue.
   Revisit only on a mysterious decode failure.
2. Does the pinned FFmpeg build include libsrt? (`ffmpeg -protocols`) — still
   open, applies at FFmpeg-pin time, not yet reached.

**Milestone position:** M1 done (spike passed, committed, pushed). Block C
done (5/5 chunks). M2 not started.

---

## Session 1 — 2026-08-22

**Type:** Architecture. No code, by explicit request.

### Context established

Sanjyot: Unreal developer, ~6 years C++ and Blueprint, background in games, XR,
and real-time production. Two IPL seasons architecting a live 360° stadium-to-CDN
broadcast pipeline; chose SRT over RTMP in production because RTMP was not stable
enough to stake a live broadcast on. Knows these protocols from the **operations**
side.

Genuinely new territory, and therefore where the effort and the explaining goes:
**Media Framework player backends** and **shipping a plugin with a third-party
native dependency**.

Project is a portfolio and job-search asset for Unreal Gameplay / Systems /
Tools-Pipeline IC roles, targeting applications Sept–Nov 2026.

### Test source identified

RTSP over LAN. **HEVC / H.265, 1920×1080, 25 fps**, PCM A-law audio.

Two consequences recorded:
- FFmpeg's software `hevc` decoder emits `AV_PIX_FMT_YUV420P` (3-plane), **not
  NV12**, which is what the GPU conversion path wants. Cheap chroma interleave,
  but a decision rather than an accident.
- Some HEVC cameras deliver VPS/SPS/PPS only in the SDP. M0 surfaces this.

Audio ruled permanently out of scope for Phase 1 — Media Framework's audio sample
interfaces are the most likely thing to eat a weekend for zero portfolio value.

### Decisions made

D1–D12, recorded in full with rationale in
[Docs/Architecture.md §12](Docs/Architecture.md). The ones with the most
reasoning behind them:

**D3 — FFmpeg, LGPL, shared DLLs, dynamic linking.** FFmpeg is LGPL 2.1 by
default; `--enable-gpl` makes the whole build GPL, which would be fatal for an
Unreal plugin. The flag exists mainly to pull in libx264/libx265, which are
*encoders* — this plugin only decodes, and FFmpeg's native `h264` and `hevc`
decoders are LGPL. So the GPL flag is never needed. LGPL's one real condition is
that a user can substitute their own library build; **dynamic linking satisfies
that automatically**, static linking does not.

**D4 — commit binaries via LFS rather than a setup script.** LFS is present
regardless because this is an Unreal project, which kills the repo-size argument.
Clone-and-run matters when the reader is a hiring manager with fifteen minutes.

**D8 — the week-1 spike is throwaway, with no Media Framework in it.** Isolates
the three genuinely unknown risks (ThirdParty `.Build.cs` integration, FFmpeg
inside Unreal, the camera itself) from Media Framework's surface area. Debugging
FFmpeg linkage and sample plumbing simultaneously is how a spike fails
ambiguously.

**D11 — no latency target.** Sanjyot's stated preference: showing improvement
beats presenting a best-first-attempt. Phase 1 ships a documented, reproducible
method and whatever number falls out; Phase 2 improves against that baseline.
Also serves the devlog.

**D12 — M3 (GPU colour conversion) is the designated cuttable milestone.** Named
in advance so the decision is pre-made rather than panicked in week 6. If it
slips, ship with CPU conversion — the plugin is still complete.

### Rejected, and why

- **Media Foundation / D3D11 hardware decode instead of FFmpeg for Phase 1.**
  Strongest engine-internals story, but would require hand-parsing HEVC parameter
  sets out of SDP. Deferred to Phase 2, where it has a working baseline to be
  measured against.
- **A setup script that fetches FFmpeg.** Cleaner repo, but adds friction between
  a reviewer and a working build — friction on the one thing the repo exists to do.
- **Building the real `IMediaPlayer` in week 1.** See D8.
- **A hard latency target.** See D11.

### Findings

- **`.gitignore` trap.** The repo's current `.gitignore` contains `*.dll`,
  `*.lib`, and `Plugins/**/Binaries/*`. Engine convention puts third-party runtime
  binaries in `Plugins/<Plugin>/Binaries/ThirdParty/`, so **all three silently
  swallow the FFmpeg binaries**. Needs negation rules plus LFS tracking for
  `*.dll` / `*.lib`. The failure is silent and only appears when somebody else
  clones the repo. **Not yet fixed.**
- **libsrt check is worth two minutes at FFmpeg-pin time.** If the pinned build
  has libsrt, Phase 2 SRT is largely a URL-scheme-and-options change through the
  same avformat path. If not, it means an FFmpeg rebuild in Phase 2. Counterpoint
  recorded: FFmpeg's libsrt wrapper hides most of SRT's stats and tuning surface
  (RTT, loss, buffer levels, caller/listener config), so Phase 2 may want to link
  libsrt directly anyway. **Not decided.**

### M1 pass/fail criterion agreed

> From a fresh clone, in a UE 5.3 editor, one Blueprint node given the camera's
> RTSP URL puts a recognisable live image from that camera onto a plane in the
> level within 10 seconds — and stopping PIE returns control to the editor
> immediately, with no hang and no crash.

Clean shutdown is inside the criterion deliberately: a spike that renders a frame
but hangs the editor has not proven viability, it has proven the hardest
remaining problem (lifecycle against a blocking network API) is untouched.

The "10 seconds" is provisional and **must be checked against the camera's GOP
length from M0** before being committed to — a long keyframe interval could make
a legitimate pass read as a fail.

### Artifacts produced

- `Docs/Architecture.md` — full design, 13 sections.
- `CLAUDE.md` — project instructions.
- `UE_IPStream_Session_Log.md` — this file.

### Ended with

Sanjyot to run M0. Offered in parallel: the `.gitignore` fix and a
no-FFmpeg-calls plugin skeleton, so that any breakage in M1 is unambiguously
FFmpeg's fault. Not yet started.

### Working agreement revised — same session, after CLAUDE.md was first written

Sanjyot rejected the initial *Working style* brief. The correction, and it is a
real one: **operational experience with RTSP/SRT/RTMP from broadcast work does
not imply knowledge of codec internals, Media Framework, Unreal's build system,
or licensing mechanics.** Those are separate bodies of knowledge, and the first
draft of the brief collapsed them — telling future sessions *not* to explain
protocol fundamentals, on an inference rather than a statement.

Specifically: GOP was not a familiar concept, and much of the jargon used in the
module-structure and milestone discussion was new.

**New standing directives, now in `CLAUDE.md` under *How we work together*:**

1. **This is a learning project, not only a build project.** The target is a
   plugin Sanjyot can defend line by line in an interview — a harder goal than a
   plugin that works.
2. **Explanation length is not a cost.** Do not compress to save time. He will
   say when something is already known. Err toward explaining.
3. **Define jargon on first use**, and add it to `Docs/Glossary.md`.
4. **Every non-obvious line of code needs a stated reason** — why this and not the
   alternative, not merely what it does.
5. **Concept prep precedes each milestone.** Ideas before code.
6. **Hard rule — Claude never writes code into project files without asking.**
   Each time code is due, ask: (a) posted in chat for Sanjyot to type himself, or
   (b) written directly by Claude. Offer (a) as default, since typing forces
   reading. Free rein on `.md` and `Docs/` only. Everything else — `.cpp`, `.h`,
   `.cs`, `.uplugin`, `.ini`, scripts, `Content/` — requires asking every time.

**Created:** `Docs/Glossary.md`, seeded with ~90 terms already used in Session 1,
across nine categories: video coding, colour/pixel formats, streaming protocols,
FFmpeg, Unreal build system, Unreal Media Framework, Unreal general, licensing,
and a hardware-decode preview. Each entry carries a *why it matters here* note
where the relevance isn't self-evident.

**Note for future sessions:** the glossary is the retroactive payment of jargon
debt from Session 1. Keep it current — the rule is that no unexplained acronym
survives a message.

### Block A — video coding fundamentals (concept prep)

Covered, in order: why video compression exists (uncompressed 1080p25 ≈ 1.24
Gbps vs. camera's actual ~2-8 Mbps); spatial vs. temporal redundancy; I/P/B
frame types and why B-frames force decode/display reorder (PTS vs DTS); GOP as
the direct consequence of P-frame chaining, and why GOP length is a hard floor
on stream-join latency; NAL units as the actual on-wire unit; SPS/PPS/VPS
parameter sets and why a decoder is completely non-functional without them, not
merely degraded; codec vs. container vs. protocol as three genuinely separate
concerns; RTSP (control-only, carries no video) vs. RTP (carries media) vs. SDP
(session description, sometimes carries parameter sets) vs. IDR/non-IDR/CRA as
the axis answering "is this a safe join point" — separate from the I/P/B axis
answering "how is this predicted."

Sanjyot asked a follow-up on IDR vs. non-IDR slices specifically — answered in
full, including the HEVC-specific CRA/RASL wrinkle (a plausible cause of a
single corrupted frame right at stream-join time). This produced a genuine
correction to the glossary's earlier phrasing, noted there.

### M0 executed live, alongside Block A

Sanjyot ran `ffprobe` twice against the real camera (5s and 30s captures) and
shared the output. Findings summarized in the Current State block above and
written up fully in `Docs/TestSource.md`. Two console-encoding artifacts hit
along the way, both worked around rather than blocking: PowerShell console
padding stripped real newlines from the first capture (parsed via pattern
extraction instead of line-based reading); the second capture came out as
UTF-16 (converted via `Get-Content -Encoding Unicode | Set-Content -Encoding
utf8` before parsing). Neither is a project concern — just Windows console
redirection behavior worth remembering if it recurs.

**Correction made mid-session:** Session 1's original architecture discussion
suggested the substream might "materially de-risk" M1 by being H.264 rather
than HEVC. Sanjyot correctly pushed back — asked what the actual benefit was.
On inspection, the claim didn't hold: FFmpeg's decode API (`avcodec_send_packet`
/ `avcodec_receive_frame`) is codec-agnostic, so H.264 vs. HEVC makes zero
difference to the spike's code path. The real, corrected reasoning: a substream
only helps if it has a **shorter GOP or lower resolution**, and codec identity
was never the actual variable. Recorded here so it isn't silently re-asserted
later — this is exactly the kind of "why this and not the alternative"
challenge the working agreement asks for, and it worked as intended.

**The headline finding:** measured GOP (~8.0–8.1s, three consistent I-frame
gaps) directly contradicts the camera admin UI's claimed 2-second I-frame
interval — a 4× mismatch, confirmed not a measurement artifact (a true 2s GOP
over 30s would show ~15 I-frames; only 4 were observed). The UI field is
greyed out on both main and substream, showing the identical wrong value —
weak evidence the override is device-wide (most likely an NVR-managed channel,
given `channel=2` in the URL) rather than a per-stream display bug, though the
root cause was deliberately left uninvestigated past a quick mental check, in
line with the project's own scope-discipline rule.

**Decisions made from this data:**
- M1 pass-criterion timeout revised **10s → 15s** in `Architecture.md`, now
  backed by a real measurement instead of a guess.
- Substream deprioritized as an M1 target — no evidence it differs meaningfully
  from the main stream now that codec identity is known not to matter.
- GOP-mismatch root cause **not pursued further** — logged as a non-blocking
  open item rather than chased, an explicit scope-discipline call.
- **New design note for M4** (not an M0/M1 action): reconnect logic will hit
  this same ~8s wait on every reconnection. Whether to freeze the last good
  frame during that wait instead of showing black is a real UX decision for the
  "live monitor" demo, not just a spike-timing detail. Logged now so it isn't
  rediscovered from scratch at M4.

**Files updated this session (beyond the working-agreement changes above):**
`Docs/TestSource.md` (new), `Docs/Architecture.md` (M1 criterion revised, with
the reasoning inline), this file.

### The GOP mystery resolves — root cause found, stream decision made

Immediate correction to the above: the "camera admin UI" checked for the
8s-vs-2s discrepancy was actually the **NVR's** UI, not the camera's own —
Sanjyot flagged this mistake himself. Its "2 seconds, locked" reading turned
out to be a red herring regardless of source.

**Checking the camera's own UI directly found the real cause.** The primary
stream (`subtype=0`) runs a proprietary feature the firmware labels
**"InstaStream"** — likely this vendor's branded name for the adaptive/smart-
codec mode hypothesized earlier. With it active, "I Frame Interval" is disabled
entirely; GOP is managed internally, measured at the earlier ~8.0–8.1s, not
admin-adjustable. **The secondary stream (`subtype=1`) does not run
InstaStream** and exposes a normal editable field defaulting to **`50`**
(frames).

Sanjyot ran a 30-second `ffprobe` capture against the secondary stream and
reported 15 I-frames; verified independently rather than taken on trust — 15×I,
734×P, 0×B across 749 frames, 14 consecutive I-frame gaps averaging exactly
50.1 frames (2.007s), matching the UI field almost exactly. Genuinely the
cleanest measurement this project has produced — effectively zero drift.
1280×720, HEVC, no InstaStream. I-frame ~143–154 KB vs. P-frame ~1–3.5 KB,
confirming the intra/inter size gap at a second, independent resolution.

**Decision: Phase 1 targets the secondary stream, not the primary.** Sanjyot
confirmed. Reasoning, all recorded in `Architecture.md` §3:

1. Worst-case join latency drops from the primary's ~8s to ~2s — a materially
   better number for the deliverable's own latency section, and it directly
   softens the M4 reconnect-freeze concern.
2. The I-frame interval is a real, admin-editable knob on this stream —
   something the primary stream can never offer, since InstaStream owns that
   decision. Gives a concrete axis for a later measured-improvement comparison
   (try 50 vs. 25 vs. 12 frames, measure join latency at each) — directly
   serves the devlog narrative Sanjyot asked for in the original brief.
3. 720p plausibly fits the game-context framing better than crisp 1080p would
   — reads more like an in-universe security feed than broadcast video.

**M1 pass criterion revised again: 15s → 8s.** The 15s figure was calibrated
against the primary stream's ~8s worst case; now that Phase 1 targets the
secondary stream's ~2s worst case, 8s gives roughly 4× margin — tight enough to
still mean something as a bar, loose enough not to fail on ordinary jitter.

**New standing constraint, added to `CLAUDE.md`'s Active Constraints:** this
camera is shared via an NVR with at least one other active user. **No
camera-side reconfiguration — resolution, bitrate, GOP, disabling
InstaStream — without explicitly raising it first, in any future milestone.**
Sanjyot was explicit: testing the primary/1080p stream later is possible by
disabling InstaStream, but deliberately not done now because of this. This is a
durable project constraint, not a one-off note — a future session must not
casually suggest touching camera config to "just check something."

**Primary stream status: deferred, not abandoned.** Documented as a real,
available option in `Architecture.md` §3 for whenever the shared-access
constraint is explicitly renegotiated.

**Files updated:** `Docs/TestSource.md`, `Docs/Architecture.md` (§3 rewritten
as a two-stream comparison; M1 criterion revised again), `CLAUDE.md` (new
shared-camera constraint), this file.

### Repo and workflow decisions — end of Session 1

**Git flow: straight to `main`.** Solo repo, no collaborators, no CI. Branch-per-
milestone and PR-per-milestone were both considered and rejected as ceremony that
buys nothing here; a portfolio reviewer reads commit history, not the branch
graph.

**Commit `995b216`** — the four documentation files. `main` is ahead of
`origin/main` by one; **not pushed yet**, pending a decision on whether the design
doc should be public before M1's go/no-go resolves.

**Model per phase.** Recorded as a table in `CLAUDE.md`. Short version: Sonnet for
concept prep, docs, and routine implementation; Opus for M1 debugging, M4
lifecycle work, and any reopening of architecture or licensing. The
documentation-first setup is what makes switching free — state is on disk, not in
a conversation.

Attached caveat, applying to every model: **UE 5.3 Media Framework signatures must
be verified against engine headers on disk, not recalled.** That API surface is
obscure enough to invite confident confabulation. Start at
`Engine/Source/Runtime/Media/Public/` and
`Engine/Source/Runtime/MediaUtils/Public/`.

**Future task logged:** git reported LF→CRLF conversion on all four files.
Harmless for Markdown, but before any `.cpp`/`.h` lands, `.gitattributes` needs
line-ending normalisation for source files — otherwise a later Linux port
produces diffs in which every line appears changed. Not done, because
`.gitattributes` is not a `.md` file and the code-delivery rule requires asking
first.

---

## Session 2 — 2026-09-05

**Type:** Concept prep (Block B, part 1). No code.

### Block B started: FFmpeg's architecture

Covered: FFmpeg as project vs CLI tools vs libraries, and why M0's `ffprobe`/
`ffplay` results prove the stream is decodable but not that our own code will
decode it correctly; the six-library dependency graph (avutil at the base;
avcodec, avformat, swscale used, with avfilter/avdevice/postproc explicitly
excluded and why); the packet→frame data flow mapped onto our actual pipeline
and onto the threading seam already decided (avformat's blocking
`av_read_frame` vs avcodec's CPU-bound decode); the "shared" vs "dev" Windows
distribution split; ABI version pinning and why the version suffix in
`avcodec-60.dll` is load-bearing; and a recap of the LGPL compliance argument
(D3) grounded in which specific libraries we link.

Follow-up: GPL vs LGPL re-explained in plain language on request (no legal
phrasing) and the glossary entries in
[Docs/Glossary.md §8](Docs/Glossary.md) rewritten to match — same content, the
"why it matters here" note tied explicitly to D3.

**Not yet covered — still pending before M1:** Unreal's build system and
module model (UBT, modules, `.Build.cs`, DLL loading and staging). This is
still part of Block B, not a separate block — corrected below.

### Curriculum recovered from an unpersisted prior conversation

Sanjyot surfaced a screenshot from an earlier chat containing a five-block
concept curriculum (Blocks A–E, each mapped to the milestone it precedes) that
had never been written to any file in this repo. This is exactly the failure
mode the project's documentation-first model exists to prevent — a decision
made in conversation but not persisted is invisible to the next session, this
one included. It produced a real error worth recording: this session initially
treated "FFmpeg's library layout" and "Unreal's build system" as two separate
blocks (B and C) when starting Block B teaching, guessing at boundaries CLAUDE.md's
prose only implied. The recovered table shows they are **one block (B)**, and
Block C is actually Media Framework (before M2) — not reached for a while yet.

**Corrected and now the durable record:** [Architecture.md §10](Docs/Architecture.md),
new "Concept curriculum" subsection — the full A–E table, Block A's exit-test
definition (read your own `ffprobe` output and account for every line,
including open items — satisfied by Session 1/M0), and current status (A done,
B in progress).

**`CLAUDE.md`'s Current status section updated to match** — points at the
Architecture.md table as canonical rather than re-stating the curriculum
inline, to avoid a second copy drifting out of sync.

### Loose ends from that prior conversation — one resolved, two still open

The recovered screenshot also flagged two loose ends from that same
unpersisted discussion, checked against current repo state this session:

1. **"Those five files are still untracked in git" — checked, already
   resolved.** `git status --ignored` confirms `output.txt`, `output2.txt`,
   `output2_utf8.txt`, `output3.txt`, `output3_utf8.txt` are all correctly
   matched by the `output*.txt` rule added to `.gitignore` during the
   2026-09-05 credential-exposure remediation (commit `74e8df5`). Not at risk
   of a repeat commit. No action needed.
2. **"Item 1 from your earlier numbered list is still unstated"** — reference
   to a numbered list from that same prior, unpersisted conversation. **Closed,
   no action.** Sanjyot confirmed this was a mistaken reference on his end;
   nothing to recover.
3. **Whether the never-write-code-without-asking rule should move to the
   global `~/.claude/CLAUDE.md`** so it applies to all projects, not just this
   one. **Closed, no action.** Sanjyot declined — rule stays scoped to this
   project's `CLAUDE.md` only.

**Separately, still not fixed (Session 1 finding, unrelated to the above):**
the `.gitignore` trap — `*.dll`, `*.lib`, `Plugins/**/Binaries/*` would
silently swallow FFmpeg's ThirdParty binaries once they're added. Confirmed
still present in `.gitignore` this session. Not urgent yet — no FFmpeg
binaries exist in the repo — but must be fixed as part of repo-prep, before
the plugin skeleton is scaffolded.

### Block B finished: Unreal's build system and module model

Covered: why UBT exists as a layer above MSVC at all (C#-configured,
cross-platform, generated project files); the module as unit of
compilation/linkage and `.Build.cs` as its manifest; `PublicDependencyModuleNames`
vs `PrivateDependencyModuleNames` and why it's what lets the runtime and
factory modules carry different dependency lists; `ModuleType.External` and
why FFmpeg's binary-integration logic is isolated in its own `.Build.cs`
rather than folded into the runtime module's; the two-phase import-lib/DLL
story and why delay loading is load-bearing, not a nicety, given DLLs ship
inside the plugin's own folder rather than requiring a system install (D4)
— including the alternative that lost, hand-rolled `LoadLibrary`/
`GetProcAddress` shimming, rejected as strictly more code for no benefit;
`RuntimeDependencies` / UFS vs `StagedFileType.NonUFS` and why editor-mode DLL
loading working proves nothing about a packaged build (the `.pak` UFS
archive has no concept of a loadable DLL file) — grounding why M5 is a real
milestone, not a formality; `.uplugin` loading phases and why the factory
module loads at `PostConfigInit` while the runtime module loads later; and
platform guarding via `UnrealTargetPlatform.Win64` from the first commit.

**Block B is now complete** (both halves: FFmpeg's architecture, and Unreal's
build system/module model). Per the curriculum, Block C (Media Framework —
what each interface is for, before M2) is not yet due; the next concept-prep
item is actually the repo-prep step that sits between Block B and M1 — the
`.gitignore`/`.gitattributes` fix and a no-FFmpeg-calls plugin skeleton.

### Repo-prep executed and reviewed

Sanjyot completed the three repo-prep items from Block B's follow-on, content
delivered in chat (option (a)) for him to type/apply directly:

1. **`.gitignore` fix** — applied with one deliberate change from what was
   given: generalized `!Plugins/IPStreamMedia/ThirdParty/**/*.{dll,lib}` to
   `!Plugins/**/ThirdParty/**/*.{dll,lib}`, removing the hardcoded plugin name
   from infrastructure that doesn't need it. Reviewed — correct, no conflict
   with the existing `Plugins/**/Binaries/*` rule (different, unrelated path).
2. **`.gitattributes` fix** — `*.dll`/`*.lib` added to LFS tracking, exactly as
   specified.
3. **UE project creation.** Hit the expected wizard limitation — Unreal's New
   Project flow always nests under `<Location>/<ProjectName>/` and won't
   target an already-populated folder directly. Worked around by creating in a
   scratch folder (Blank, C++, no starter content) named **`IPStreamMediaDemo`**
   — chosen to name the demo harness after the plugin it hosts, read as
   engineering rather than broadcast-flavored, and stay short given FFmpeg's
   own deep header tree plus Windows' path-length limit — then moving
   `.uproject`/`Source/`/`Config`/`Content/` into the existing repo root and
   letting `Binaries`/`Intermediate`/`Saved` regenerate in place. No nested
   `.git` created (source control was left unchecked in the wizard).

Both `.gitignore`/`.gitattributes` changes were committed by Sanjyot
independently (`d0fd929`, "Git updates before Project Init") — outside this
session's own edit flow, confirmed via `git log`/`git show --stat` afterward
rather than assumed.

**Plugin skeleton created, built, and verified.** Both module startup log
lines appeared in the Output Log in the correct order, confirming the
`PostConfigInit`/`Default` loading-phase split actually works, not merely that
the `.uplugin` JSON parses. Files reviewed line-by-line against what was
specified in chat:

- Structurally exact match: two-module split, loading phases, dependency
  scoping (public vs. private) all correct. No functional issues.
- **One real, good deviation:** dedicated log categories from the start
  instead of the suggested `LogTemp` placeholder — and via two different,
  both-individually-correct mechanisms. `IPStreamMedia` uses
  `DECLARE_LOG_CATEGORY_EXTERN` (header) + `DEFINE_LOG_CATEGORY` (`.cpp`),
  giving external linkage appropriate for a module that Architecture §5 says
  will grow multiple `.cpp` files (`Decoder/`, `Player/`, `Samples/`) all
  logging to the same category. `IPStreamMediaFactory` uses
  `DEFINE_LOG_CATEGORY_STATIC` — internal linkage, correct because that module
  is genuinely one file. **Confirmed deliberate.** Sanjyot chose `_STATIC` for the factory
  specifically because it's a one-file module — drawing on his own 2025
  devlog research into custom Unreal log categories — and reasoned each
  module's log-category linkage independently rather than copying one pattern
  everywhere. Real, stated "why this and not the alternative," exactly per the
  working agreement.
- Cosmetic-only, left as-is by agreement: Epic's copyright boilerplate present
  on two of six files but not the other four; tabs vs. spaces between the two
  `.Build.cs` files; a trailing semicolon after one `IMPLEMENT_MODULE(...)`
  call but not the other. None affect correctness; not worth a special trip
  back into these files.

**Repo-prep is now fully complete.** Per the curriculum, next is M1 itself —
the throwaway spike (D8) — not further concept prep; Block C (Media Framework)
isn't due until M2.

### Licensing gap closed: `LICENSE.md` added, copyright headers applied

Reviewing the Epic boilerplate comment surfaced a real gap: D5 has always
stated the plugin's own code is MIT, but no `LICENSE.md` existed anywhere in
the repo — nothing on disk backed the claim for a public-repo reviewer.
Closed this session:

- **`LICENSE.md`** created at the repo root — standard MIT text, copyright
  Sanjyot Dahale, with a closing note pointing at FFmpeg's own
  `COPYING.LGPLv2.1`/`NOTICE.md` for third-party terms once M1 adds them.
- **Copyright header applied to all five actual source files** (`.Build.cs`
  ×2, `.h`, `.cpp` ×2) — `// Copyright (c) 2026 Sanjyot Dahale. Licensed
  under the MIT License — see LICENSE.md.`, replacing Epic's leftover
  boilerplate on the two files that had it. This also resolves the earlier
  cosmetic inconsistency (only 2 of 6 files carrying any header).
- **`IPStreamMedia.uplugin` deliberately left untouched.** JSON has no native
  comment syntax; a `"_comment"` pseudo-key was tried and reverted rather than
  risk introducing an unrecognized field into a manifest UBT actually parses
  strictly enough to matter for plugin loading. Not worth the risk for a
  cosmetic addition — the root `LICENSE.md` already covers this file too.
  Written by Claude directly, with Sanjyot's explicit authorization (option
  (b)) for this specific change.

**Follow-up, same session:** Sanjyot asked whether the note appended after the
`---` divider in `LICENSE.md` (scoping the MIT grant to the plugin's own code,
pointing at FFmpeg's separate LGPL terms) was intentional. Confirmed yes — but
flagged a real trade-off not raised at write time: GitHub's automatic license
detection wants a close match against the standard MIT text, and appended
prose risks the repo not auto-displaying as "MIT licensed," a real cost for a
portfolio repo. Offered to move the note into a `## License` section in
`README.md` instead (which M6 already designates as the home for licensing
notes) and strip `LICENSE.md` back to pure standard text. **Sanjyot declined —
build succeeds, files look correct, leaving `LICENSE.md` as-is for now.**
Recorded so this isn't silently "fixed" by a future session without knowing
it was a deliberate call, not an oversight.

**Final follow-up, same session:** Sanjyot also applied the same copyright
header to the demo project's own `Source/IPStreamMediaDemo/` files (the
wizard-generated primary game module — `.Build.cs`, two `.Target.cs`, `.h`,
`.cpp`). Confirmed fine — comment-only, zero build risk, and normal practice
to license your own copy of project scaffolding the same as the rest of the
repo. This did make `LICENSE.md`'s scoping note stale (it said "this plugin's
own source code," narrower than reality now that the header spans plugin and
demo project both) — fixed by changing "plugin's" to "repository's" in that
note.

### Files updated this session

`Docs/Glossary.md` (§8 GPL/LGPL entries rewritten in plain language; §5
Unreal build system expanded with public/private dependency names, the
explicit delay-load window mechanism, `StagedFileType.NonUFS`,
`UnrealTargetPlatform`, and generated-project-files regeneration behavior),
`Docs/Architecture.md` (§10 concept curriculum table added), `CLAUDE.md`
(Current status corrected, then updated again to reflect repo-prep completion
and M1 as next action), `LICENSE.md` (new), five plugin source files
(copyright headers), this file.

---

## Session 3 — 2026-09-08

**Type:** M1 prerequisite — pinning the FFmpeg build. Short session (~30 min
Pomodoro), no code. No camera involved.

### FFmpeg build pinned

Sanjyot downloaded `ffmpeg-N-126455-gecc7eb519e-win64-lgpl-shared` from
[BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases) (the
`win64-lgpl-shared` variant, per Architecture §4's compliance checklist) and
laid it out at `Plugins/IPStreamMedia/ThirdParty/FFmpeg/` matching the
`include/` / `lib/Win64/` / `bin/Win64/` structure from Architecture §5.
Ran `ffmpeg -buildconf` and `ffmpeg -protocols` from the build's `bin/`
folder and shared the output for review.

### Correction: the pinned build is LGPL v3, not LGPL 2.1

Every doc in this repo (`CLAUDE.md` D3, Architecture §4, `LICENSE.md`) stated
"LGPL 2.1" — a reasonable assumption since that's FFmpeg's un-configured
default, but wrong for this specific build. The configure line shows
`--enable-version3` set (a flag independent of, and easy to conflate with,
`--enable-gpl` — the name doesn't mention GPL at all) with `--enable-gpl`
absent. Confirmed with hard evidence, not just the flag name: read the
build's own bundled `LICENSE.txt` directly — its header reads "GNU LESSER
GENERAL PUBLIC LICENSE, Version 3, 29 June 2007."

**Does not change D3's actual reasoning** — dynamic linking satisfies
LGPL's substitution condition identically under v2.1 or v3. What changes is
paperwork only: the exact license text to bundle, and every doc reference to
"2.1." Fixed this session:

- `Plugins/IPStreamMedia/ThirdParty/FFmpeg/COPYING.LGPLv3` — the build's
  actual `LICENSE.txt`, copied verbatim (byte-for-byte file copy, not
  retyped, to avoid any risk of transcription error in a legal text).
- `CLAUDE.md` (D3 table row), `Docs/Architecture.md` (§4 rule statement, "why
  in plain terms" section expanded to explain the `--enable-version3` axis,
  compliance checklist item 3, §5 tree diagram), `LICENSE.md` (FFmpeg
  reference) — all changed from "LGPL 2.1" / `COPYING.LGPLv2.1` to reflect
  v3. Session 1's original entry recording D3 (2026-08-22, above) was
  deliberately left as-is rather than rewritten — it's a historical record
  of the reasoning at the time, not a live spec; this entry is the
  correction on record instead.

**Also resolved — Architecture §4's "do this at pin time" open item:**
`ffmpeg -protocols` lists `srt` under both Input and Output. **libsrt is
present in this build.** Per the doc's own conditional, Phase 2 SRT can
likely reuse the same `avformat` path rather than needing a from-scratch
integration or an FFmpeg rebuild — which approach Phase 2 actually takes is
still not decided (FFmpeg's libsrt wrapper hides most of SRT's telemetry
surface, a real reason to reject it later), but the option is now confirmed
available rather than assumed.

### `NOTICE.md` written

`Plugins/IPStreamMedia/ThirdParty/FFmpeg/NOTICE.md` created per Architecture
§4 compliance checklist item 4 — exact version (`N-126455-gecc7eb519e-20260907`,
git commit `ecc7eb519e`, built 2026-09-07), full `ffmpeg -buildconf` output,
the corrected LGPL v3 finding, the libsrt confirmation, and the linked
library version list. Noted that BtbN publishes on every FFmpeg master push
rather than periodic version tags, so the git commit hash and build date —
not a semantic version number — are what make this pin reproducible.

**Open, low priority:** the exact BtbN release-page URL/tag for this build
wasn't captured, only the asset filename (which does uniquely identify the
git commit and date). Fine for now; revisit only if exact reproducibility is
ever actually needed (e.g. rebuilding from source later).

### Follow-up: `NOTICE.md`'s audience, and a real gap it surfaced

Sanjyot asked who `NOTICE.md` is actually for — a good question that exposed
scope creep in how it was first written. Answer: two external audiences,
per Architecture §4's original design — (1) someone exercising LGPL
relinking rights, who needs to identify exactly which FFmpeg build is
linked, and (2) a reviewer/hiring manager auditing the licensing story,
given the project's "public repo, licensing is a hard constraint" framing.
Neither audience needs Phase 2 SRT planning commentary, which had been
included — moved that reasoning to Architecture §4's own "do this at pin
time" section (marked the libsrt open item resolved, §13 checkbox ticked),
leaving `NOTICE.md` to state only the compliance-relevant fact (libsrt is
present in the build).

Re-reading the file with the audience question in mind surfaced a real,
previously-unstated gap: the pinned build's configure line statically
compiles roughly forty other third-party libraries into the same DLLs
(`libsrt`, `gmp`, `libssh`, `sdl2`, `libaom`, etc.) — only FFmpeg's own LGPL
status had been reviewed. Not a blocker (this plugin's code only calls
FFmpeg's own public API, never these libraries directly), but a genuine gap
on a project where licensing is a hard constraint. Stated plainly in
`NOTICE.md` itself and logged as a new open item in Architecture §13, rather
than left implicit.

### Files updated this session

`Plugins/IPStreamMedia/ThirdParty/FFmpeg/COPYING.LGPLv3` (new, verbatim copy),
`Plugins/IPStreamMedia/ThirdParty/FFmpeg/NOTICE.md` (new, then refocused),
`CLAUDE.md`, `Docs/Architecture.md` (LGPL version corrected throughout;
libsrt finding relocated to §4 and §13; new third-party-audit open item),
`LICENSE.md`, this file.

---

## Session 4 — 2026-09-08 to 2026-09-09

**Type:** M1 — the spike. First actual code written on this project.
Delivered in chat (option (a)) throughout, Sanjyot typing everything in
himself and building it.

### `FFmpeg.Build.cs` written — and a real bug caught in the architecture doc before it caused a build failure

Before writing anything, verified the planned module layout against UBT's
actual source (`Engine/Source/Programs/UnrealBuildTool/System/RulesCompiler.cs`)
rather than trusting `Architecture.md §5`'s tree diagram. Found a genuine
error: UBT only discovers plugin modules — `ModuleType.External` ones
included — under a plugin's `Source/` folder. The diagram had
`FFmpeg.Build.cs` living directly under `Plugins/IPStreamMedia/ThirdParty/`,
a sibling of `Source/`, never a plugin-module folder UBT scans. Following
that plan as written would have produced a module that silently never
existed.

**Fixed by precedent, not invented:** Epic's own `OpenCV` plugin
(`Engine/Plugins/Runtime/OpenCV/Source/ThirdParty/OpenCV/OpenCV.Build.cs`)
splits exactly this way — the `.Build.cs` lives under `Source/ThirdParty/
FFmpeg/`, a near-empty module folder holding only build rules, while the
actual binaries (already LFS-committed) stay at
`Plugins/IPStreamMedia/ThirdParty/FFmpeg/{include,lib,bin}`, referenced from
the Build.cs via the `PluginDirectory` property. `Architecture.md §5`
corrected with a note explaining the mistake and the fix, rather than
silently rewritten.

`FFmpeg.Build.cs` links only the four libraries M1 actually touches
(`avformat`, `avcodec`, `avutil`, `swscale` — `avdevice`/`avfilter`/
`swresample` deliberately left unlinked, audio being permanently out of
scope), using `PublicSystemIncludePaths`, `PublicAdditionalLibraries`,
`PublicDelayLoadDLLs`, and `RuntimeDependencies`, gated behind
`Target.Platform == UnrealTargetPlatform.Win64` with a `WITH_FFMPEG`
define for the (currently inert, deliberately future-proofed) non-Win64
branch. Every UBT API used (`ModuleType.External`, the `RuntimeDependencies`
overloads, `PluginDirectory`) was checked against
`ModuleRules.cs`/`OpenCV.Build.cs` on disk before being written, not
recalled.

### `IPStreamMedia.Build.cs` and `IPStreamMediaModule.cpp` — wiring the dependency, loading the DLLs

`IPStreamMedia.Build.cs` updated to add `"FFmpeg"` (private — nothing
outside this module should ever see FFmpeg's types) and later `"Projects"`
(for `IPluginManager.h`).

`IPStreamMediaModule.cpp` now does the actual DLL resolution Architecture
§9 called for: at `StartupModule`, resolves the plugin's own base directory
via `IPluginManager::Get().FindPlugin("IPStreamMedia")->GetBaseDir()`,
pushes `ThirdParty/FFmpeg/bin/Win64` onto the DLL search path, and calls
`FPlatformProcess::GetDllHandle` on each of the four delay-loaded DLLs
explicitly, logging (not crashing) on a per-DLL failure — a real, expected
failure mode given this project's own documented LFS/`.gitignore` risk, not
speculative defensiveness. `ShutdownModule` frees the handles in reverse
order. All four `FPlatformProcess`/`IPluginManager` signatures verified
against `GenericPlatformProcess.h`/`Interfaces/IPluginManager.h` on disk.

### `IPStreamSpike.h`/`.cpp` written — the actual throwaway spike

One `UBlueprintFunctionLibrary`, one function:
`GrabOneFrame(const FString& RtspUrl)`, synchronous and blocking by design
(not async/latent — that complexity belongs to M2+, and a blocking call
sidesteps M4's harder shutdown-race problem entirely, since nothing is left
running on another thread for "stop PIE" to hang on). Full pipeline: a
pre-allocated `AVFormatContext` with an `AVIOInterruptCB` wired to a
6-second wall-clock deadline (needed even for a "throwaway" spike, since
without it a dead URL freezes the whole editor, not just PIE, given this
runs on the game thread) → `avformat_open_input` with `rtsp_transport=tcp`/
trimmed `probesize`/`analyzeduration` → `avformat_find_stream_info` +
scan for the video stream (skipping the camera's PCM A-law audio track,
confirmed present in the same session as a second stream in the array) →
`avcodec_find_decoder`/`avcodec_alloc_context3`/`avcodec_parameters_to_context`/
`avcodec_open2` → a `av_read_frame`/`avcodec_send_packet`/
`avcodec_receive_frame` loop stopping at the first successfully decoded
frame → `sws_scale` YUV420P→BGRA → `UTexture2D::CreateTransient` +
direct `BulkData` write + `UpdateResource()`. Every FFmpeg and UE call
verified against the actual headers on disk (`ThirdParty/FFmpeg/include/`
and the UE 5.3 engine source) before being written — several signatures
(`avformat_open_input`'s `ps`-may-be-preallocated behavior, `AVFrame::format`
being a plain `int` not the enum, `CreateTransient` not calling
`UpdateResource` internally) came back different from what recall alone
would have produced. **Build succeeded. Not yet run in the editor or tested
against the camera** — M1's actual 8-second pass/fail criterion is still
unmeasured.

### Real course-correction: code was compiling but not understood — new standing practice adopted

After the code was written and built, Sanjyot flagged a genuine problem
rather than proceeding: he could follow the surrounding C++ mechanics
(pointers, linkage, `extern "C"` — all explained at length) and the video-
coding theory, but not the actual FFmpeg call sequence's own logic — unable
to defend the lines he'd just typed and compiled. Correctly identified as a
teaching-depth problem, not a capability one — build succeeded, nothing was
actually wrong, and FFmpeg's C API isn't the kind of obscure/version-
specific surface the model table's confabulation warning targets (that's
specifically about UE Media Framework). Considered and explicitly rejected
switching to Opus for this reason; stayed on Sonnet.

**Fix, agreed and executed:** a dedicated walkthrough doc,
`M1FFmpegWalkthrough.md`, covering the
entire `IPStreamSpike.cpp` FFmpeg sequence in eight chunks (setup/
interrupt-callback, opening the connection, finding the video stream,
opening the decoder, the decode loop, cleanup, the swscale conversion, the
texture upload), each explaining what the call's job is, why this call and
not an alternative, and what it hands back and why the next line needs
that — chunked and confirmed one piece at a time in conversation, not
delivered as one long pass. Explicitly not a duplicate of `Architecture.md`
(system design) or `Glossary.md` (term definitions) — this is call-by-call
code reasoning specifically, the piece that had been missing.

**Promoted to a standing rule**, not treated as a one-off: `CLAUDE.md` gained
a new "Code walkthrough docs" subsection under *How we work together*,
codifying the format (one file per unfamiliar-API code unit, chunked and
checked, code's own logic explained before surrounding mechanics/theory,
updated live rather than written up after the fact) for any future code
touching an unfamiliar API surface, not just FFmpeg. `CLAUDE.md`'s Current
status section also corrected — it still read "No code written. Nothing
scaffolded," stale since before this session.

Also surfaced during the walkthrough: a small but real structural
preference for the *actual* (non-spike) codebase going forward — Sanjyot
wants real M2+ functions broken into smaller, clearly-named pieces (unlike
the single-function shape of the throwaway `GrabOneFrame`), specifically for
six-months-later readability. Not applied to M1 itself — deliberately, since
it's deleted regardless per D8 — but worth carrying into M2.

### Glossary and other doc updates this session

`Docs/Glossary.md`: new `IPluginManager`/`GetBaseDir()` entry (§5); new
"Translation unit" and "Internal vs external linkage" entries (§5), added
after a from-scratch explanation of `void*`, function pointers, and
anonymous vs. named namespaces that took several iterations to land at the
right level of simplicity — worth remembering for future teaching in this
project: start plainer than feels necessary for foundational C/C++
mechanics the user hasn't hit before, even when he's an experienced UE C++
programmer generally.

### Files updated this session

`Plugins/IPStreamMedia/Source/ThirdParty/FFmpeg/FFmpeg.Build.cs` (new),
`Plugins/IPStreamMedia/Source/IPStreamMedia/IPStreamMedia.Build.cs`,
`Plugins/IPStreamMedia/Source/IPStreamMedia/Private/IPStreamMediaModule.cpp`,
`Plugins/IPStreamMedia/Source/IPStreamMedia/Private/IPStreamSpike.h` (new),
`Plugins/IPStreamMedia/Source/IPStreamMedia/Private/IPStreamSpike.cpp`
(new), `M1FFmpegWalkthrough.md` (new), `Docs/Architecture.md` (§5
module-tree correction, Block B status line corrected), `Docs/Glossary.md`
(`IPluginManager`, translation unit, linkage entries), `CLAUDE.md` (new
Code walkthrough docs subsection, Current status corrected), this file.

---

## Session 5 — 2026-09-09

**Type:** M1 debugging — plugin failed to load at editor startup. Continued
directly from Session 4 in the same conversation; **switched Sonnet → Opus
partway through**, per `CLAUDE.md`'s model table, which names "M1 debugging —
delay-load failures, unresolved externals against FFmpeg import libs" as an
explicit Opus trigger. The rule worked as designed: Sonnet's hypotheses were
reasonable but wrong, and the switch happened on evidence (two failed fixes),
not vibes.

### The failure

The project compiled and linked with zero errors, then the editor refused to
load the plugin. `DebugGame Editor` log:

```
LogWindows: Failed to load '...UnrealEditor-IPStreamMedia-Win64-DebugGame.dll' (GetLastError=126)
LogWindows:   Missing import: avutil-61.dll   (+ avcodec-63, avformat-63, swscale-10)
LogPluginManager: Error: Plugin 'IPStreamMedia' failed to load because module
                  'IPStreamMedia' could not be loaded.
```

### Root cause

**BtbN's FFmpeg Windows builds are cross-compiled with MinGW/GCC, so the
shipped `.lib` files are GNU-format import libraries — full COFF objects with
explicit `dlltool` jump thunks (`_t.o`/`_h.o`/`_s#####.o`), containing zero
MSVC short-import records. MSVC's `/DELAYLOAD` can only transform
short-import records; given GNU-format libraries it accepts the flag, links
`delayimp.lib`, emits no warning, and produces ordinary load-time imports
anyway.**

The consequence inverted the entire load-order design: the FFmpeg DLLs became
load-time dependencies → Windows demanded them before loading the module
binary → the module binary failed to load → so `StartupModule()`, containing
the `PushDllDirectory`/`GetDllHandle` code written precisely to solve that
problem, never ran. The delay-load architecture (decided in Block B concept
prep, weeks earlier) was correct and had simply never engaged.

Full diagnosis, including the diagnostic path and the dead ends, in
`M1DelayLoadRCA.md` — written as a base document
for a portfolio devlog.

### How it was found (condensed)

1. **Stale-binary theory — refuted** by file mtimes: the captured log
   predated the clean rebuild by ~30 min; the binary was fresh.
2. **Two experiments that couldn't have helped** — changing the factory's
   `LoadingPhase`, adding `AdditionalDependencies` to the `.uplugin`. Both
   operate at plugin-orchestration level; the failure was in the OS loader
   resolving a PE import table. Both reverted.
3. **`.Build.cs` logging** proved the build script ran with correct values —
   eliminated a hypothesis class without finding the bug.
4. **Read the engine's own diagnostic source**
   (`WindowsPlatformProcess.cpp`, `ReadLibraryImportsFromMemory`): it reads
   only `IMAGE_DIRECTORY_ENTRY_IMPORT`, never the delay-import directory.
   **This was the pivot** — it meant the four DLLs were provably *regular*
   imports, not a generic "can't find DLL" message.
5. **Linker response file** showed `/DELAYLOAD:` was correctly emitted for all
   four, plus `delayimp.lib` → the bug is below the build system.
6. **`dumpbin /DEPENDENTS`** on the built binary: no delay-load section at
   all → the linker ignored the flags.
7. **`dumpbin /IMPORTS`**: functions only, no data imports (data imports
   legitimately can't be delay-loaded — ruled that out). No `LNK4199`.
8. **`dumpbin /ARCHIVEMEMBERS` + `/HEADERS` on `avutil.lib`**: GNU `dlltool`
   objects, **0 short-import records**. Root cause.
9. **Verified the fix before proposing it**: regenerating from the same
   `.def` with `lib.exe` produced **639** short-import records, each
   correctly recording `avutil-61.dll`.

### Fix, and the alternatives rejected

Regenerated the four linked import libraries from the `.def` files shipped in
`lib/Win64/`, with `lib /DEF:... /MACHINE:X64 /NAME:<versioned>.dll`.
`/NAME:` is mandatory — the `.def` files have no `LIBRARY` statement, so
without it `lib.exe` records `avutil.dll`, which doesn't exist. **No source
changes were needed**; `FFmpeg.Build.cs`, the startup code, and the
descriptor were all already correct. Confirmed by Sanjyot: delay-load section
present in `dumpbin`, plugin loads.

**Rejected — B: drop delay loading, copy DLLs next to the module binary.**
Would have worked (UE searches the plugin's own `Binaries/Win64`, visible in
the failure log's search paths) and needed only a `RuntimeDependencies`
change with vendor files untouched. Rejected because it works *around* a
deliberate decision rather than fixing it: `PublicDelayLoadDLLs` becomes a
no-op, the explicit startup-loading code becomes dead, and Architecture §9 +
the Glossary would need rewriting to describe a weaker mechanism — all to
avoid a four-command fix.

**Rejected — C: runtime-only `LoadLibrary`/`GetProcAddress` linking.**
Already rejected in Block B as more code for no benefit; that rejection had
assumed delay loading worked, but the conclusion survives the assumption
changing (~21 function pointers for the spike alone, growing).

### Documentation added

- `M1DelayLoadRCA.md` (new) — full RCA, written to be rewritten as a
  devlog. Deliberately keeps the wrong turns and marks which claims are
  empirically demonstrated here vs. mechanism explanation.
- `ThirdParty/FFmpeg/NOTICE.md` — new "Import libraries regenerated" section
  with the exact commands and a **"repeat this after any FFmpeg
  re-download"** warning; opening "FFmpeg is not modified" claim qualified
  (DLLs are byte-for-byte unmodified; the `.lib` files were regenerated —
  link-time scaffolding containing no FFmpeg code, so LGPL-immaterial, but a
  compliance reader deserves the full picture).
- `Docs/Architecture.md §9` — new subsection recording that delay loading has
  an unstated prerequisite (import library format), plus a standing check to
  run `dumpbin /DEPENDENTS` whenever the FFmpeg pin changes.

### The lesson worth carrying forward

**Configuration is not behaviour.** Every layer reported success — build
script correct, UBT flags correct, linker accepted them, build succeeded —
and the mechanism still never engaged. Verifying a setting was *applied* is
not the same as verifying it *took effect*. The standing `dumpbin` check now
in Architecture §9 exists because of this.

### First frame — the spike works

**`GrabOneFrame` was run against the real camera over RTSP and returned a
frame.** End to end: connect → demux → decode → CPU swscale → `UTexture2D`.
Sanjyot's impression was "within a second," **explicitly not a measurement** —
eyeballed, not instrumented, and recorded here as an impression so it doesn't
later get quoted as a number. If it holds up under actual measurement it is
comfortably inside the 8-second criterion (which was calibrated against the
secondary stream's ~2s GOP).

This answers the spike's actual question — D8's whole purpose was buying
information about the three unknowns (ThirdParty `.Build.cs` integration,
FFmpeg inside Unreal, the camera itself). All three are now known-good.

**M1 is not yet passed.** The formal criterion also requires: a *recognisable
image on a plane in the level* (not just a returned texture object), the
8-second bound actually measured rather than eyeballed, and **stopping PIE
returning control to the editor immediately with no hang and no crash** —
that last one is half the criterion and is entirely untested. Deferred to the
next session by agreement.

### Files updated this session

`Plugins/IPStreamMedia/ThirdParty/FFmpeg/lib/Win64/{avutil,avcodec,avformat,swscale}.lib`
(regenerated), `Plugins/IPStreamMedia/ThirdParty/FFmpeg/NOTICE.md`,
`Plugins/IPStreamMedia/IPStreamMedia.uplugin` (experiments reverted),
`Plugins/IPStreamMedia/Source/ThirdParty/FFmpeg/FFmpeg.Build.cs` (debug
scaffolding removed), `M1DelayLoadRCA.md` (new),
`Docs/Architecture.md` (§9), `CLAUDE.md` (Current status), this file.

### M1 pass criterion closed out — measured, not eyeballed

Continuing the same day. Two things done: a Blueprint wired `GrabOneFrame`'s
returned texture into a dynamic material instance's texture parameter on a
plane (`BP_SpikePlane`), and a `Tick`-based timestamp print gave a free,
zero-extra-instrumentation way to measure the game-thread freeze — `Tick`
physically cannot fire while the thread is blocked inside a synchronous
call, so the gap between consecutive `Tick` log lines *is* the blocking
duration.

**Good stream** (the real camera): last tick before the call to first tick
after —

```
37:58.868  →  37:59.867   (delta: 0.999s)
```

Full pipeline — connect, demux, decode, swscale, `UTexture2D` creation, and
the material parameter update — in **0.999 seconds**, measured, not the
earlier eyeballed "about a second." Comfortably inside the 8-second
criterion.

**Bad stream** (deliberately unreachable — `freja.hiof.no:1935`, a public
RTSP test endpoint chosen specifically so the URL itself carries no
credentials and is safe to reference): three ticks at normal ~8ms cadence,
then silence, then the logged open failure, then the next tick —

```
last normal tick: 34:51.370
error logged:      "Failed to open: rtsp://freja.hiof.no:..."
next tick:         34:57.409   (delta: 6.039s)
```

**6.039 seconds** — against a coded interrupt-callback deadline of exactly
`+6.0`. 39ms of overhead on top of a 6-second bound is about as tight a
real-world confirmation as this mechanism could produce: the `AVIOInterruptCB`
from `M1FFmpegWalkthrough.md` §1 is doing precisely what it was designed to
do, empirically, not just in theory.

**Both numbers kept as the devlog's before/after-style result** (D11: no
latency target, a documented reproducible method and whatever number falls
out) — 0.999s success case, 6.039s worst-case-bounded failure case.

**PIE-stop tested explicitly, multiple times, both scenarios** (distinct
from `Tick` resuming, which only proves the game thread was unblocked —
this is the actual "click Stop and observe" action the criterion asks for).
No hangs, no crashes, in either the good-stream or bad-stream case, across
multiple repetitions.

**M1 is PASSED.** All three criteria from the Session 1 pass/fail definition
are met. Per D8, this code is throwaway — next up is deleting the spike,
Block C (Media Framework concept prep), then M2's real `IMediaPlayer`
module. Not started this session, a deliberate stop, not a gap.

**Housekeeping flagged, not yet actioned:** `GoodStream.txt`/`BadStream.txt`
(the raw capture files these numbers came from) sit at the repo root,
untracked, and are **not** caught by the existing `output*.txt` `.gitignore`
rule — a different filename shape than the one added after the 2026-09-05
incident. Content in both is confirmed safe (no camera credentials — the bad
stream deliberately used a public test URL for exactly this reason), but per
the project's own rule, raw captures are disposable once their numbers are
extracted into `Docs`/the session log, which they now are. Recommended:
delete both rather than leave them sitting unprotected.

### Files updated, continued

`GoodStream.txt`, `BadStream.txt` (new, local, flagged for deletion —
see above), this file.

### Pre-push safety pass, and a genuine near-miss caught by it

Before committing, Sanjyot removed the RTSP URL from `BP_SpikePlane` and
asked to confirm push-readiness without re-checking the Blueprint
specifically. Re-ran the credential-pattern sweep anyway, across the full
changeset — the hard rule says "actively check... not just trust that
redaction happened," and it's cheap enough to always run regardless of what's
assumed already fixed.

**First pass came back clean and was wrong.** Ripgrep silently skips files
it detects as binary by default; a second pass forcing binary-as-text
(`grep -a`) found the real camera URL, **with live current credentials**,
baked into `Content/BP_SpikePlane.uasset` as a literal string node feeding
`GrabOneFrame` — the exact failure mode the 2026-09-05 incident was, just in
a `.uasset` instead of a `.txt` file. Caught before any `git add`, so a
near-miss, not a repeat — but the false-clean first pass is the real lesson:
**a tool reporting no matches is not the same as a file actually being
scanned.** `Main.umap` and the two test materials came back genuinely clean
on the same forced-text check.

Simplest fix considered — excluding `Content/` from the commit entirely,
since it's throwaway per D8 anyway — was superseded when Sanjyot removed the
literal from the Blueprint directly and re-verified clean; `Content/` ended
up committed after all, which is fine since it was independently confirmed
credential-free before that happened.

**Separately, while gitignoring `*.txt`:** Sanjyot's own addition put the
new rule *after* the existing `!Build/*/PakBlacklist*.txt` whitelist
negation. `.gitignore` is last-match-wins, so the broad rule would have
silently re-ignored that whitelisted file once `Build/` actually exists at
M5 — not a live bug (no `Build/` yet), but the same class of silent trap as
the original FFmpeg-binaries `.gitignore` issue from Session 1. Fixed by
moving `*.txt` earlier in the file, before the `Build/` section, matching
the file's own established convention elsewhere (broad ignore immediately
followed by its own specific negation, as already done for `*.dll`/`*.lib`).
Verified both directions with `git check-ignore` rather than just reasoning
about it — `*.txt` files ignored, a simulated `PakBlacklist-Shipping.txt`
still correctly un-ignored.

Also reviewed before push: both `Config/*.ini` diffs (benign — an
auto-generated empty preview-scene section, and `Main.umap` set as the
startup/default map), and LFS filter routing for the four regenerated
`.lib` files (`git check-attr` confirms they still route through LFS by
pattern, unaffected by being regenerated).

### M1 committed and pushed

Commit `81714c5`, "First Spike Successful," on `main`. Confirmed
`HEAD` matches `origin/main` via `git log`, not assumed from a successful
`git push` message alone.

### Files updated, continued (2)

`.gitignore` (`*.txt` rule reordered), `Content/` (credential removed from
`BP_SpikePlane`, then committed), this file, `CLAUDE.md` (push confirmed,
next-action reframed toward Block C / M2).

---

## Session 6 — 2026-09-10

### Spike deletion deferred

Picked up at the documented next action: delete M1's throwaway code, run
Block C, start M2. Before touching anything, flagged that `Content/
BP_SpikePlane.uasset` is placed inside `Content/Maps/Main.umap` — deleting
the `.uasset` on disk (or via `git rm`) without going through the editor's
Content Browser first would leave the map with a dangling reference, since
raw file deletion doesn't do Unreal's reference fixup. Sanjyot's call:
**don't delete any of it yet** — not `IPStreamSpike.h`/`.cpp`, not
`BP_SpikePlane`/`M_SpikeTest`/`MPC_SpikeTest`, not `WBP_Timer`, not
`Main.umap` — until M2 has something working and testable to replace it.
D8 (spike is throwaway) isn't being reversed, just resequenced: cleanup
happens once there's a working replacement, not the moment the old thing is
declared done.

### Block C, Chunks 1–3

Before teaching anything, read the actual 5.3 engine headers rather than
relying on recall, per the project's standing caveat about Media Framework
being an obscure, confabulation-prone surface: `IMediaPlayer.h`,
`IMediaPlayerFactory.h`, `IMediaEventSink.h`, `IMediaSamples.h`,
`MediaSamples.h`, `IMediaTextureSample.h`, all under
`F:\EpicGames\UE_5.3\Engine\Source\Runtime\{Media,MediaUtils}\Public`.

**Chunk 1 — the big picture.** Media Framework exists because Unreal ships
no built-in codec support at all — every real decoder is a plugin. Walked
through why that requires exactly four things: `IMediaPlayerFactory` (a
plugin declaring "I can play this URL"), `IMediaPlayer` (the common shape
every plugin's player implements), `IMediaEventSink` (how a player pushes
state changes to Blueprint without being polled), and
`IMediaSamples`/`IMediaTextureSample` (the common shape for "here is one
decoded frame"). Traced the runtime wiring end to end: factory registration
at startup → `UMediaPlayer::OpenUrl` → `CanPlayUrl` → `CreatePlayer(EventSink)`
→ per-frame sample pulls. Flagged one fact with real M2 consequences: the
factory *receives* the `IMediaEventSink&` from the facade, it never
constructs one — there's no "add event firing later," the sink exists
before the player does.

**Chunk 2 — `IMediaPlayer`'s full contract.** The interface splits into 12
mandatory pure virtuals and a long tail of optional virtuals with default
bodies — extensibility without breaking every existing player plugin when
new hooks get added. Covered why `GetCache`/`GetControls`/`GetTracks`/
`GetView` conventionally return `*this` (one class, multiple inheritance,
matching `Architecture.md` §6) while `GetSamples` returns a genuinely
separate `FMediaSamples` member instead. Covered why two `Open()` overloads
exist (URL vs `FArchive` — the archive path is dead code for an
inherently address-based protocol like RTSP; `CanPlayUrl` routing means it's
never called anyway) and why `GetPlayerPluginGUID` exists on both
`IMediaPlayer` and `IMediaPlayerFactory` (confirmed via
`BaseMediaSource.cpp`: a `UMediaSource` asset pins a specific player
per-platform by GUID, not by name string, since GUIDs stay stable across
display-name changes).

This chunk surfaced a real architectural fork: `IMediaPlayer::EFeatureFlag::
AlwaysPullNewestVideoFrame` looked like a built-in mechanism for D9's
latest-frame-wins policy, but only applies under a second, newer timing
model (`UsePlaybackTimingV2` / `FMediaTimeStamp`) that sits alongside the
original `FTimespan`-based one. Sanjyot pushed back on solving only "now" —
explicitly asked which was better *in the long run*, given possible future
audio, other protocols (SRT/RTMP), and future engine versions — so this
became its own investigation rather than a quick pick:

- Read `MediaPlayerFacade.cpp` directly, plus `GetPlayerFeatureFlag`
  overrides in three shipped 5.3 players: `RivermaxMediaPlayer` (SMPTE ST
  2110 broadcast ingest — the closest existing engine analog to this
  project) and `SharedMemoryMediaPlayer` (nDisplay live frame source) both
  stay on the old model, unmodified; `ElectraPlayerPlugin` (Epic's
  flagship, full AV sync) uses the new one unconditionally.
- Checked whether V2 actually costs more hand-written code — it doesn't:
  `FMediaSamples`/`FMediaTextureSampleQueue`, already committed to in
  `Architecture.md` §6, already implements V2's sample-selection algorithm
  (`FetchBestSampleForTimeRange`). Using the engine's own queue either way
  makes the two models' real cost difference much smaller than it first
  looked.
- The deciding fact: `FMediaTimeStamp::SequenceIndex` exists, per its own
  header comment, to mark "an event that causes time to no longer be
  monotonic — e.g. seek or loop." D10's transparent reconnect is exactly
  that class of event — a fresh RTSP session means PTS resets near zero
  while the player session stays open. V1 has no field for this; a player
  would have to detect the reconnect and manually rebase every subsequent
  timestamp by an accumulating offset, with a fresh chance to get it wrong
  on every reconnect. V2 replaces that with incrementing one integer, and
  the comparison operator (sequence index compared before time) does the
  rest correctly, automatically.
- Answered Sanjyot's three long-run questions directly: audio sync is what
  V2 is actually built for (V1 isn't audio-incapable, but it's not where
  Epic's new investment is going); the timing-model choice is orthogonal to
  transport protocol, so it doesn't need re-deciding when SRT/RTMP arrive;
  and while no explicit V1 deprecation was found (said so plainly rather
  than overclaiming), every piece of new 5.3 work found was V2.
- Walked the reconnect scenario with concrete numbers (last pre-reconnect
  frame at `Seq=0, Time=47.20s`; first post-reconnect frame at `Seq=1,
  Time=0.04s`) against the actual `FMediaTimeStamp::operator<` from the
  header, to make the "lap counter, not a wall clock" framing concrete
  rather than just asserted.

**Decision: D13** — M2 builds D9's latest-frame-wins on V2 timing, not V1.
Recorded in `Architecture.md` §7 (full evidence and consequences for M2)
and §12 (decision log), `CLAUDE.md`'s decision table, and new Glossary §6
entries (`Playback timing V1 / V2`, `FMediaTimeStamp`/`SequenceIndex`,
`AlwaysPullNewestVideoFrame`, `FMediaPlayerFacade`).

**Chunk 3 — `IMediaEventSink`.** Covered the enum's public/internal split
(`Internal_Start` partitions events Blueprint never sees, like
`Internal_PurgeVideoSamplesHint`, from the ones that drive `UMediaPlayer`'s
delegates). Traced *why* events are pushed rather than polled all the way
through the real code, not just asserted: `FMediaPlayerFacade::
ReceiveMediaEvent` does almost nothing on the calling thread — it just
enqueues onto a thread-safe `TQueue` — and the actual
`MediaEvent.Broadcast()` that fires Blueprint delegates only happens later,
on the game thread, inside `TickInput`. This is the concrete mechanism
behind the "fire events from day one" gotcha already in `Architecture.md`
§6: `OnMediaOpened` firing is entirely downstream of the player calling
`ReceiveMediaEvent(MediaOpened)` on the decode worker thread at the right
moment — nothing else drives it, so frames can be visibly flowing into the
texture while every Blueprint-facing signal still reports the media as
never having opened. Also drew the line between `EMediaEvent` (one-shot,
"something just happened") and `IMediaControls::GetState()` (queryable,
"what's true right now") — noted as easy to conflate. Left open, on
purpose, rather than guessed at: exactly which events fire around a
reconnect (reuse `MediaConnecting`, or something else?) — an M2 design
question, not a Block C one.

**Block C remaining:** `IMediaPlayerFactory`, then `IMediaSamples`/
`IMediaTextureSample` together (grouped since D13 already covered the
timing half of `IMediaSamples`) — next session.

### Files updated

`CLAUDE.md` (D13 added to the decision table), `Docs/Architecture.md` (§7
new D13 subsection with full evidence and M2 consequences; §12 decision log
row), `Docs/Glossary.md` (`FMediaPlayerFacade`, `Playback timing V1 / V2`,
`FMediaTimeStamp`/`SequenceIndex`, `AlwaysPullNewestVideoFrame`), this file.

---

## Session 7 — 2026-09-11

### Block C, Chunk 4 — `IMediaPlayerFactory`

Checked against a real, complete shipped implementation rather than the
header alone: `WmfMediaFactoryModule.cpp`. Confirmed one class implements
both `IMediaPlayerFactory` and `IModuleInterface` — `StartupModule()`
populates supported schemes/extensions/platforms then calls
`MediaModule->RegisterPlayerFactory(*this)`; `ShutdownModule()` mirrors it.
`CreatePlayer()` is a one-line hop into the separate runtime module
(`FModuleManager::LoadModulePtr<IWmfMediaModule>("WmfMedia")->CreatePlayer(...)`)
— D6's two-module split, validated against a real example rather than just
planned. `CanPlayUrl`'s out-params (`OutWarnings`/`OutErrors`) exist because
`MediaPlayerFacade.cpp` logs a specific per-factory refusal reason when no
factory can play a URL — not just a bare failure.

**Concrete gotcha found, not hypothetical:** `WmfMediaFactoryModule.cpp`
registers `rtsp`/`rtspt`/`rtspu` as supported schemes — **WmfMedia already
claims RTSP** on any stock Win64 project where it's enabled. Checked
`MediaModule.cpp`: factory registration is `PlayerFactories.AddUnique(&Factory)`
on a plain array, no priority. Selection with no explicit override is pure
registration order — first match in `GetPlayerFactories()` wins
(`MediaPlayerFacade.cpp` ~line 1784). So with both plugins enabled and no
override configured, whichever module loads first silently wins, and if
that's WmfMedia, `IPStreamMediaFactory::CreatePlayer` never gets called —
symptom would look like "the plugin doesn't work" rather than "wrong player
got picked." Fix already exists in the engine and ties back to Chunk 2's
`GetPlayerPluginGUID` finding: a `UMediaSource` asset's per-platform player
override is checked *first*, before the auto-select loop
(`GetPlayerFactory(PlayerName)`, ~line 1755). Held for M2 test-scene setup
rather than fixed now, at Sanjyot's call.

### Block C, Chunk 5 — `IMediaSamples`/`FMediaSamples` + `IMediaTextureSample`

First pass covered: `FMediaSamples`'s locking (`FScopeLock`/`FCriticalSection`
in `MediaSampleQueue.h`) as the producer/consumer thread-safety boundary
between the decode worker and the game thread; `IMediaTextureSample`'s
`GetBuffer()` vs `GetTexture()` split (only one needs implementing — maps
onto `Architecture.md` §7's staged colour-conversion plan), `GetDim()` vs
`GetOutputDim()` (padding — your 1280×720 secondary stream is already
16-aligned so unlikely to matter in practice), and `IsCacheable()` as a
second, sample-level lever for D9/D13's latest-frame-wins. Checked
`MediaObjectPool.h` directly for the pooling claim in `Architecture.md`
§6: `TMediaObjectPool::AcquireShared()` hands back a `TSharedPtr` built
with a custom deleter, so when the refcount hits zero the object is
recycled via `Release()` instead of freed — `InitializePoolable()`/
`ShutdownPoolable()` are the reset-without-realloc hooks. Deliberately
deferred `GetYUVToRGBMatrix()`/`GetSampleToRGBMatrix()`/`GetFullRange()` to
Block D (M3's colour-conversion prep), rather than duplicating that block's
job early.

**This didn't land — full remedial pass, recorded as a standing lesson.**
Sanjyot named the actual gap precisely: has written `FRunnable`/
`FScopeLock` code before, but not with deep conceptual grounding in *why*
a lock is needed, hasn't worked with `TSharedPtr` reference counting, and
hadn't read Media Framework source firsthand — naming mechanisms and
citing engine source doesn't teach the concept those mechanisms implement.
Rebuilt in two pieces:

1. **Threading, from scratch, anchored in the M1 spike itself** (a real,
   already-lived example rather than an abstract one): why `GrabOneFrame`
   running on the game thread was *why* it froze measurably; what two
   threads touching `AddVideo`/`FetchVideo` "at the same time" concretely
   breaks (a race condition, walked step-by-step through a hypothetical
   partial-write); what `FScopeLock`/`FCriticalSection` mechanically do
   about it (one key, RAII release on scope exit) — *then* reconnected to
   "this is what `FMediaSamples` already does for you."
2. **`TSharedPtr`, from scratch, separately**: the raw-pointer multi-owner
   problem (worker thread creates a sample, queue holds it, render thread
   reads it — no single right place for `delete`); refcounting as the fix
   (a shared counter, object dies only when it provably hits zero); custom
   deleters as "swap out what happens at zero" — *then* reconnected to
   `TMediaObjectPool::AcquireShared()` as exactly that trick, recycling
   instead of freeing.

Sanjyot then restated the model twice in his own words, refined across two
more rounds — first correctly grasping "reuse instead of delete" but
implying one dedicated slot cycling immediately back to the next frame
(corrected: `FMediaSamples`'s queue depth, `MaxNumberOfQueuedVideoSamples =
4`, is a different number from the pool's actual size, which floats to
match real peak concurrent usage) and "recycled once the render thread
reads it" (corrected: the actual trigger is refcount-zero, not specifically
render-thread completion — relevant because D9 drops stale samples that
are never rendered at all, and those still get recycled correctly). Both
corrections were precise refinements to an already-correct core model, not
fundamental misunderstandings.

**Saved to memory**, broadening `feedback_unfamiliar_capi_teaching` beyond
just C-APIs: "has used this API before" and "understands the concept it's
built on" are different claims, now confirmed for UE's own threading and
smart-pointer internals too — lead with fundamentals grounded in this
project's own code before naming the UE mechanism, pre-emptively, for any
future threading/smart-pointer explanation here.

### Block C — COMPLETE

All 5 chunks done across Sessions 6–7. Two open items folded into "next
action" for M2 rather than resolved now: which `EMediaEvent`s fire around a
reconnect, and setting the explicit Windows player override on the M2 test
`UMediaSource` to sidestep the WmfMedia registration-order gotcha.

**Next action:** M2 — the real `IMediaPlayer` module. Sanjyot is stepping
away and will resume in about an hour; nothing else outstanding from this
session.

### Files updated

`Docs/Glossary.md` §7 (`Race condition`, `Critical section`/
`FCriticalSection`/`FScopeLock`, `TSharedPtr`/reference counting, `Custom
deleter`), this file. Memory: `feedback_unfamiliar_capi_teaching.md`
broadened beyond C-APIs to cover UE's own threading/smart-pointer
internals, and re-titled in the index.

---

## Session 8 — 2026-09-11 to 2026-09-12

### Working-method change — the most important thing in this session

M2 started the way M1 ended: I read engine source, compressed ~15 file reads
into ~6 sentences of conclusion, and posted code. **Sanjyot stopped it**, and
the objection is the correct one — what that transmits is **precedent, not
reasoning**. "Electra composes `FMediaSamples`, so we compose `FMediaSamples`"
is precisely the answer that collapses under the interview follow-up *"why does
Electra do it that way?"* He'd be defending a choice he didn't make.

His framing, which is right and worth preserving: **M1 was throwaway per D8, so
silver-platter delivery was survivable there. Everything from M2 on is
permanent.** The ownership standard steps up exactly at this milestone.

A second failure in the same episode, admitted on the record: precedent was used
to paper over a **contradiction I had seen and not resolved** —
`FImgMediaPlayer` inherits `IMediaSamples` while `FElectraPlayerPlugin` composes
`FMediaSamples`. I reported Electra's choice and moved on. Copying a convention
is not making a decision.

**New method, agreed and now in memory (`feedback_design_derivation.md`):**
for any decision that locks in architecture — (1) state the question with no
answer attached; (2) hand over **exact file paths and line ranges** plus the
contrast to look for, not a summary; (3) **he reads and goes first**; (4) correct
and fill gaps; (5) question + evidence + decision + alternative-that-lost go into
a derivation doc. Explicitly **not** applied to stub return values or anything
one-line reversible — that would burn the schedule. He reads in his own editor
(engine source at `F:\EpicGames\UE_5.3`) rather than from pasted excerpts, so he
sees full context rather than what I chose to show.

"I don't know why" is now an acceptable and preferred answer over a confident
post-hoc rationalisation of engine convention.

### D14 — where the sample queue lives

**Decision: `FIPStreamPlayer` owns a `TUniquePtr<FMediaSamples>`** and
implements `IMediaCache`/`IMediaControls`/`IMediaTracks`/`IMediaView` directly
(returning `*this`). Derived, not copied — Sanjyot read
`IMediaSamples.h`, `MediaSamples.h`, both player headers, `ImgMediaPlayer.cpp`,
and `ImgMediaLoader.h`, and answered first.

Three findings, in his words then sharpened:

1. **`IMediaSamples` is a container; the other four are behaviour.** He got this
   unprompted. The other four are accessors over state the player already keeps
   (`GetState()` returns a member); `IMediaSamples` means owning frames,
   lifetimes, thread-safety, eviction. That asymmetry is why `MediaUtils` ships a
   concrete `FMediaSamples` and no concrete `FMediaControls`.
2. **`FMediaSamples` is a component, not a base class** — he guessed base class;
   corrected by grepping the whole engine: **nothing inherits it**, two things
   own one (`ElectraPlayerPlugin.h:178`, `TextureMediaPlayer.h:174`). Framing
   that landed: *`IMediaSamples` is what you must **be**; `FMediaSamples` is
   something you may **have**.*
3. **ImgMedia inherits because `FMediaSamples` physically cannot do its job.**
   `ImgMediaLoader.h:550` is `TLruCache<int32, ...> Frames` — random access keyed
   by frame number, over files that all already exist. A FIFO queue cannot express
   "frame 12 after frame 500." Not a style choice.

Ours is Electra-shaped: frames arrive sequentially off the network, no end, no
seek, no reverse; D9's latest-frame-wins *is* a queue policy; `FMediaSamples`
already implements the V2 selection path D13 committed to. **Reopens only if a
Phase 2 D9 policy can't be expressed as queue configuration** — note queue depth
alone doesn't force that, since `MaxNumberOfQueuedVideoSamples` is a constructor
parameter.

### D6's rationale was wrong — corrected

Surveying every shipped media backend's `.uplugin` produced a result that
**contradicted what I had told him earlier in the same session**, and what D6
recorded:

| Plugin | Player | Factory |
|---|---|---|
| WmfMedia | `PostConfigInit` | **`PostEngineInit`** |
| ElectraPlayer | `PreLoadingScreen` | **`PostEngineInit`** |
| AvfMedia | `PreLoadingScreen` | **`PostEngineInit`** |
| AndroidMedia | `PreLoadingScreen` | **`PostEngineInit`** |
| **Ours** | `Default` | **`PostConfigInit`** |

Every shipped factory loads *after* its player; ours loaded before. D6 said
*"Engine convention; `PostConfigInit` registration ordering"* — **wrong on both
halves.**

- **No race exists.** `FMediaPlayerFacade::CanPlayUrl` is reachable only from a
  `UMediaPlayer` (a `UObject`, driven from gameplay), so the earliest possible
  consultation is vastly later than `PostEngineInit`.
- **Early costs something.** It drags `Media` up the startup order, and
  editor-facing calls in a factory `StartupModule` silently no-op —
  `WmfMediaFactoryModule.cpp:186-195` guards `ISettingsModule` with
  `if (... != nullptr)`, so at `PostConfigInit` its settings page would simply
  never appear, with no error. **WmfMedia could not do what it does at
  `PostConfigInit`.**
- **The real reason for the split:** `UBaseMediaSource::PreSave`
  (`BaseMediaSource.cpp:43-53`) runs in the editor while saving a `UMediaSource`
  **for a target platform the editor isn't running on**. For the details panel to
  offer "on iOS, use AvfMedia", a Windows editor must enumerate a factory for a
  player that can never load on Windows — which is why `AvfMediaFactory` compiles
  for Win64 and `AvfMedia` doesn't. Corroborated by
  `MediaPlayerFacade.cpp:214`'s `SupportsPlatform(RunningPlatformName)` filter,
  meaningful only in a system expecting factories for platforms you're not on.
  **A factory is metadata about a player, queryable where the player can't
  load.** This also resolves the Block C open thread — the explicit Windows
  override writes into `PlatformPlayerNames`, read at `BaseMediaSource.cpp:50`.

Also learned: shipped factories are declared **twice** (`Editor` +
`RuntimeNoCommandlet`) purely because one module entry carries one `Type` and one
`PlatformAllowList`, and those are two different rules. Win64-only projects need
only one entry.

**`.uplugin` change agreed** — player `RuntimeNoCommandlet`/`Default`, factory
`RuntimeNoCommandlet`/`PostEngineInit`, both Win64. Player deliberately stays at
`Default` rather than copying WmfMedia's `PostConfigInit`: WmfMedia earns early
by initializing a platform subsystem, we only load our own DLLs, and after the
M1 delay-load RCA, resolving DLLs with more of the engine up is the safer side.
**Sanjyot is making this edit himself.**

### Mechanics covered along the way

- **`MEDIAUTILS_API` on some members and not others** — his question. The pattern
  in `MediaSamples.h` is exact: a member carries the macro **precisely when its
  body lives in the `.cpp`**. Inline bodies need no export (the consuming
  compiler generates that code into your own module — nothing crosses a DLL
  boundary). **Consequence for our code:** `IIPStreamMediaModule::CreatePlayer`
  needs **no** `IPSTREAMMEDIA_API` despite a cross-module call, because a pure
  virtual resolves through the vtable at runtime rather than needing a link-time
  symbol address — confirmed against `IWmfMediaModule.h`, which has no export
  macro anywhere.
- **`ImgMediaPlayer.cpp:830`** — `Scheduler->TickInput(Zero, MinValue)` **ignores
  both parameters** (`ImgMediaScheduler.cpp:147-165`); it's a pump that
  dispatches queued disk-load jobs, not a time-based tick.
- **`ImgMediaPlayer.cpp:839`** — the reverse/forward ternary, proving ImgMedia has
  a definite end and plays backwards. Neither is true for a live camera.

### Where M2 code stands

Step-1 design is fully settled (factory as one class implementing both
`IModuleInterface` and `IMediaPlayerFactory`; `FIPStreamPlayer` shape per D14;
`GetSupportedRates` empty and `CanControl`/`SetRate` all false at the stub
stage). Code was posted in chat for the `.Build.cs` files only and **typed in by
Sanjyot** — but the Build.cs dependency reasoning was delivered the old way and
still needs re-deriving under the new method, since what you link against follows
from what you use. **Nothing else has been written.**

Fresh GUID generated for `GetPlayerPluginGUID`, to be used by both factory and
player (WMF uses the same literal on both):
`FGuid(0x7f1035ec, 0xbb724295, 0x8a3f6b76, 0xf5666420)`.

### Design question 3 — the `.Build.cs` dependency model

Derived under the new method, and Sanjyot got 4 of 5 unaided. The measurement
that settles it: **`Media`'s 22 public headers contain exactly ONE export site**
(`MEDIA_API const TCHAR* MediaTextureSampleFormat::EnumToString`, a debug string
helper, `IMediaTextureSample.h:128`), against **164** across `MediaUtils`. So the
rule is: *a module whose public surface is pure-virtual interfaces, enums and
header-defined types has nothing to link against* — the module-scale form of the
`MEDIA_API` reasoning from earlier in the session. Reopens only if we ever want
`EnumToString` for logging.

**The one he got wrong, and it's worth keeping:** he read
`DynamicallyLoadedModuleNames` as "dynamically loads modules at run-time when
needed." **It loads nothing** — it's a declaration, and our own
`FModuleManager::LoadModulePtr` call does the loading. What it actually buys is
(1) **staging**, so a packaged build doesn't omit a module nothing links against
— the M5 failure class — and (2) **build ordering with no import-table entry**,
which is what keeps the factory decoupled from the player. Linking
`IPStreamMedia` would put it in the factory DLL's import table and the Windows
loader would pull it in regardless of loading phase, quietly undoing D6.

**Deliberate departure from engine precedent, recorded:** `WmfMediaFactory`
reaches its runtime module's header with a raw relative path
(`#include "../../WmfMedia/Public/IWmfMediaModule.h"`,
`WmfMediaFactoryModule.cpp:23`). We use `PrivateIncludePathModuleNames` instead —
the relative path bypasses UBT include resolution, hardcodes on-disk layout,
ignores the Public/Private boundary, and is invisible to UBT so nothing
validates it. Sanjyot called this one correctly before being told.

### Two bugs caught by verifying rather than trusting

1. **`.uplugin` had a `//` comment.** Module entries were correct, but JSON has
   no comment syntax and UE's reader adds none — `JsonReader.h:544-546` ends its
   token switch with `SetErrorMessage(TEXT("Invalid Json Token."))`. The
   descriptor would have failed to parse and the plugin would not have loaded.
   Removed; the rationale lives in `Architecture.md` §5 and the derivation doc.
2. **Three wrong `.Build.cs` comments**, all since fixed by Sanjyot. One claimed
   `PrivateIncludePathModuleNames` prevents *linker errors* (it avoids the
   linker entirely). One still said the factory loads at `PostConfigInit` —
   stale within the hour — and asserted modules "will not be ready to link" at a
   given loading phase, which confuses linkage with load order. One said the
   platform player override is set in `UMediaPlayer` when it's `UMediaSource`
   (`PlatformPlayerNames`, `BaseMediaSource.cpp:50`). Worth logging because a
   comment asserting a wrong mechanism is worse than no comment — it's the
   sentence that gets repeated in an interview.

**Next action:** M2 step-1 code — factory registration plus a do-nothing
`FIPStreamPlayer` that fires `MediaOpened`/`MediaClosed`, no FFmpeg yet. Design
for it is fully settled; delivery mode is (a), posted in chat for Sanjyot to
type. Nothing is blocking it.

### Files updated

`M2DesignDerivation.md` (**new** — Q1, Q2 and Q3 in full, the template for
every M2 design decision), `Docs/Architecture.md` (§5 "Why two modules"
rewritten; D6 rationale corrected in §12; **D14 added**), `Docs/Glossary.md`
(`IMediaPlayer` entry **corrected** — it claimed one class conventionally
inherits all five sub-interfaces, it's four; `IMediaSamples`/`FMediaSamples`
rewritten; `Loading phase` corrected; **new** `Module API macro`, `EHostType`,
`PrivateIncludePathModuleNames` and `DynamicallyLoadedModuleNames` entries),
`CLAUDE.md` (D6 and D14 rows; **Current status block rewritten** — it still
claimed Block C hadn't started; **new "Design derivation — hard rule" section**
under "How we work together"), this file. Memory: **new**
`feedback_design_derivation.md`.

Source files touched by Sanjyot this session: `IPStreamMedia.uplugin` (module
types/phases per Q2), `IPStreamMedia.Build.cs` and `IPStreamMediaFactory.Build.cs`
(dependency lists per Q3, plus comment corrections). **No `.cpp`/`.h` written
yet** — step-1 code is the next thing.

---

## Session 9 — 2026-09-12

### M2 STEP 1 PASSES — the Media Framework chain is proven end to end

All five files written (delivery mode (a) — posted in chat, **typed in by
Sanjyot**), built clean, and tested in PIE. Every link confirmed:

| Check | Result |
|---|---|
| `LogIPStreamMediaFactory: IPStreamMedia player factory registered` at startup | ✅ |
| `IP Stream Media` listed in the `UMediaSource` player-override dropdown | ✅ |
| `LogIPStreamMedia: Player opened rtsp://***@…` (redacted) | ✅ |
| `MediaOpened` reaching a Blueprint `Print String` in PIE | ✅ |

That sequence proves: the factory module loads at `PostEngineInit` and
registers; the editor can enumerate the factory (the `PlatformPlayerNames` path
from Q2); **our factory beat WmfMedia** for the `rtsp` scheme with the explicit
override set; our player was constructed through
`IIPStreamMediaModule::CreatePlayer`; and events travel player → facade →
`UMediaPlayer` → Blueprint. **No FFmpeg is involved anywhere in this** — exactly
the D8 isolation instinct, one unknown at a time.

The five files: `IIPStreamMediaModule.h` (gains `CreatePlayer`),
`IPStreamMediaFactoryModule.cpp` (full factory), `IPStreamPlayer.h`/`.cpp` (new),
and a two-line edit to `IPStreamMediaModule.cpp`. Sanjyot generated his **own**
GUID rather than using the one drafted for him —
`FGuid(0x6bd90b53, 0xbe5340cd, 0xab90c371, 0x7f1b284a)`, identical in factory and
player as required.

Full call-by-call reasoning captured in **`M2PlayerWalkthrough.md`**
(new — the `M1FFmpegWalkthrough.md` template applied to M2). Covers: why two
dispatch mechanisms exist at all, the `"Windows"`-not-`"Win64"` trap,
`LoadModulePtr` vs `GetModulePtr`, the destructor/incomplete-type gotcha,
`public` vs `protected` on the five sub-interfaces, what `Open()` actually
promises, why the destructor must not fire events (Electra's evidence), and
`GetPlayerFeatureFlag` as the entirety of D13.

### CREDENTIAL NEAR-MISS — caught before commit, and it exposed a bad detector

**`Content/MediaAssets/TestLinkSource.uasset` contained the full credentialed
camera URL three times.** A `UStreamMediaSource` stores its URL as a default
property *inside the `.uasset`*, `Content/` is tracked, and a `.uasset` is
**binary** — so `git diff` would have shown nothing readable and the `*.txt`
ignore rule (added after the 2026-09-05 incident) does not apply at all. One
`git add .` from repeating that incident through a different door.

**How it was fixed — and a correction I needed.** My first move was to add
`Content/MediaAssets/` to `.gitignore`, unilaterally. **Sanjyot rejected that,
correctly: "ignore everything is not the solution."** Ignoring *conceals* — the
password stayed on disk untouched, only git's visibility changed, and one
removed line would bring it back. It was also an oversized repo decision for a
one-field problem: that folder also holds a URL-free Media Player asset, and a
Media Source *with the player override set* is exactly what M6's demo wants
committed. Only the URL field was ever sensitive.

**Actual fix, his:** the URL field was cleared to a dummy
(`rtsp://YourStreamHere`) and the asset re-saved; the original
`TestLinkSource.uasset` no longer exists. The `.gitignore` entry was reverted.
Secret *removed*, not hidden, and `Content/MediaAssets/` stays committable.
Verified clean afterwards with four detectors (credential-in-authority,
literal camera IP, redacted display of every `rtsp://` string, and a loose
`word:something@` backstop — the last producing two false positives from
serialised UE enum names like `ENodeAdvancedPins::Hidden`). Nothing was
committed at any point.

**Standing rule from this, now in memory
(`feedback_credential_findings_alert_only.md`): on a credential finding, ALERT
and STOP.** Report what was found and where, state the options with tradeoffs,
and let Sanjyot choose the remedy. Never remediate unilaterally, and never by
gitignoring.

**Two lessons worth keeping:**

1. **My first credential-detection regex produced a FALSE NEGATIVE on this exact
   file** — it reported `credentials=0` while credentials were present, because
   the pattern assumed no `@` inside the password. The corrected detector looks
   for *any* `@` between `://` and the next `/`. A clean scan result is only as
   trustworthy as the pattern behind it; state the pattern, not just the verdict.
2. **The password contains an `@`.** A quick `sed` cutting at the *first* `@`
   leaked a fragment of it into the chat transcript (not into any file). Raised
   with Sanjyot rather than quietly moved past.

**The silver lining is a genuine validation:** `RedactUrlCredentials` handles
this correctly, because it searches the **authority section only** (before the
first `/`) and takes the **last** `@`. Against a URL shaped like
`rtsp://<user>:<password-containing-an-@>@<host>:554/...` it yields
`rtsp://***@<host>:554/...` — full redaction, `@`-in-password and all. The naive
first-`@` approach leaves the tail of the password exposed, which is exactly
what happened when a throwaway `sed` was used for the scan. That edge case was
written for on principle and has now been proven against the actual camera.

### Content reorganisation by Sanjyot

`BP_SpikePlane` moved to `Content/Blueprints/`, `WBP_Timer` deleted,
`Main.umap` modified accordingly (done in-editor, so no dangling references).
M1's timing numbers survive in the docs, so losing the timer widget costs
nothing. **`IPStreamSpike.h`/`.cpp` deliberately still present** — step 1 renders
no picture, so M2 has not yet replaced what the spike demonstrates. D8's deletion
still waits on step 3/4.

**Next action — agreed at session end: derive Q4 BEFORE writing step-2 code.**
Sanjyot was offered "derive first" vs "code first and derive if something bites"
and chose to derive, on the grounds that this is the expensive-to-unwind kind of
decision.

**Q4 — what runs the decode loop, and how is it shut down?** `FRunnable` +
`FRunnableThread`, a task, or something else? Where does the FFmpeg session state
live — on the player, or in its own object? And what exact ordering guarantees
`Close()` cannot return while the worker still holds an FFmpeg handle? M1 already
proved the cancellation mechanism (`AVIOInterruptCB`, measured 6.039s against an
unreachable URL) but it has never been combined with a thread somebody is waiting
to join. `CLAUDE.md` singles this class out — *"shutdown deadlocks against
blocking network reads... easy to 'fix' in a way that only relocates them"* — and
it fails as an editor hang, not a compile error.

**Then M2 step 2** — FFmpeg demux/decode on the worker thread, behind the
`Open()` that now demonstrably works. The FFmpeg call sequence itself is **not
new**: it is M1's, documented call-by-call in `M1FFmpegWalkthrough.md`. Two
things already flagged: `~FIPStreamPlayer` stops being empty (a thread must be
shut down there), and `CreatePlayer` may want an `#if WITH_FFMPEG` guard so a
failed DLL load returns `nullptr` rather than a player that can never decode.
Step 3 then pushes samples into `FMediaSamples` and turns on
`AlwaysPullNewestVideoFrame`; step 4 points a `UMediaTexture` at it.

**Committed and pushed** as `8d73e99` ("IPStreamMedia Player working skeleton"),
`main` confirmed in sync with `origin/main`, working tree empty. A full
credential scan of every tracked file at HEAD came back clean — the only hits
are documented placeholders (`rtsp://admin:***@…`, `rtsp://user:pass@host/…`,
the `rtsp://YourStreamHere` dummy, a public `freja.hiof.no` test stream), the
`TEXT("://***@")` format string inside `RedactUrlCredentials` itself, and binary
noise within the FFmpeg DLLs.

**Flagged to Sanjyot, his call, left as-is:** the camera's LAN IP
`192.168.0.131` appears in plaintext in `CLAUDE.md`, `Docs/TestSource.md` and
this file. RFC 1918 private space, not routable from the internet, and it is the
project's own established convention (`TestSource.md` redacts credentials but
keeps the host).

### Files updated

**New:** `M2PlayerWalkthrough.md`. **Modified:** `.gitignore`
(`Content/MediaAssets/`), `Docs/Glossary.md` (§6 `GetPlayerPluginGUID`, `Facade`;
§7 `FString`/`FName`/`FText`, `LOCTEXT`/`LOCTEXT_NAMESPACE`), `CLAUDE.md`, this
file. Memory: **new** `feedback_kiss_answers.md` — "KISS" in a question means
answer short and simple, no assumed knowledge, glossary terms only with line
numbers.

---

## Session 10 — 2026-09-16

**Q4 derived and settled as D15. No code written this session.**

### Data-loss incident, recovered before any other work

`Docs/IssuesAndWalkthroughs/` was found **empty** at session start. All four
local study notes — `M1FFmpegWalkthrough.md`, `M1DelayLoadRCA.md`,
`M2DesignDerivation.md`, `M2PlayerWalkthrough.md` — had been deleted on
**2026-09-14 20:53:49**, all four within 30 ms of each other, i.e. a
folder-level delete. Two days *after* the last commit (`afa1a5d`, 2026-09-12
23:05), so unrelated to that commit's docs-hygiene pass.

They are gitignored (`.gitignore:88`) and have never been tracked, so git held
no copy — exactly the risk `CLAUDE.md` names when it says to treat them as the
only copy. All four were recovered intact from `F:\$RECYCLE.BIN` and restored by
Sanjyot at their Session 9 content (mtimes 2026-09-12 22:56, sizes and line
counts verified: 592 / 526 / 419 / 367 lines).

**Worth acting on:** these files are one accidental folder delete away from
being gone permanently, and the only reason this was caught is that a session
happened to open them. A backup outside the repo is not yet in place.

### Q4 — what runs the decode loop, and how is it shut down?

Derived properly per the Session 8 rule: question stated, exact engine file and
line ranges handed over, Sanjyot read and answered first, corrections and gaps
filled afterwards. Full record — evidence, findings A1–A5 and B1–B6, decision,
the alternative that lost, and the reopen conditions — in
`M2DesignDerivation.md`.

**Reading covered:** the primitives (`Runnable.h:30-75`,
`RunnableThread.h:36-90`, `Event.h:20-90`, plus `WindowsRunnableThread.h/.cpp`
for what `Kill` actually does); worked example A,
`FImgMediaSchedulerThread` (both files in full); worked example B,
`FElectraPlayer::CloseInternal` (`ElectraPlayer.cpp:383-493`) and
`DoCloseAsync` (`:495-540`).

**Facts established that the header comments do not state plainly:**

- `Init`, `Run` and `Exit` all run on the **worker** thread; `Stop()` runs on
  whoever calls `Kill()` (`WindowsRunnableThread.cpp:134-165`). That asymmetry
  is the reason shared state is unavoidable. "The aggregating thread" in
  `FRunnable`'s comments means the worker, not the creating thread.
- `Kill(true)` = `Runnable->Stop()` + an **infinite** wait + `CloseHandle`, so it
  contains `WaitForCompletion()` rather than complementing it
  (`WindowsRunnableThread.h:79-119`).
- `FImgMediaSchedulerThread` **never overrides `Stop()`** — so `Kill`'s call to
  it is a no-op and the destructor does all the stopping by hand. Its `Run()`
  loop never exits on its own; the thread's lifetime *is* the object's lifetime.
- Electra's `CloseInternal` **never joins a worker.** It severs every callback
  path first, then hands teardown to a thread-pool task that captures a
  `TSharedPtr` **by value** — ownership in place of waiting. Non-shipping builds
  add a 3-second watchdog that logs *"Player may be dead and dangling!"*.
- `bKillAfterClose` does **not** mean "wait for shutdown" — the Session 9 guess
  recorded in the derivation doc was wrong. It reaches
  `GetPlayerFeatureFlag(EFeatureFlag::AllowShutdownOnClose)` via
  `ElectraPlayer.h:96` → `ElectraPlayerPlugin.cpp:785`, and
  `MediaPlayerFacade.cpp:1896` uses it to destroy the player the instant
  `MediaClosed` arrives. It means the opposite of waiting.

**Decision — D15: a hard join, bought with an interruptible worker.**
An owned `FRunnable` worker (not a task — D10 needs a worker that outlives any
one connection); FFmpeg session state in its own object behind a
worker-thread-only boundary; and ImgMedia's four-step shutdown with **step 2
replaced by the interrupt callback returning `1`**, because that callback is the
only thing that can reach a thread parked in `av_read_frame`. Electra's one idea
kept: sever callback paths into the player before tearing down.

**The change that makes it work:** M1's callback
(`IPStreamSpike.cpp:30-33`) knows a wall-clock deadline and nothing else, so
after a `Close()` it keeps answering "keep going" until the deadline expires. It
must read a stop flag as well as the clock.

**Rejected — B, Electra's detached async teardown — as *disproportionate*.** It
buys freedom from a wait we can bound to milliseconds and charges:
nondeterministic teardown; a possible second RTSP session against a camera
shared through an NVR with another user; and a crash risk neither reference
plugin has — we `FreeDllHandle` the FFmpeg DLLs by hand
(`IPStreamMediaModule.cpp:85-89`), and a detached worker still inside
`avformat-*.dll` when that runs is executing unmapped memory. Electra's
machinery is proportionate to Electra's scale (many uninterruptible threads,
teardown that genuinely takes seconds); we have one thread and one blocking call.

**Reopens if:** the join measures beyond ~100 ms; teardown stops being boundable
(M3 or Phase 2 hardware decode); multi-stream arrives; any blocking path turns
out not to poll the interrupt callback; or the worker acquires a game-thread
dependency (which would make the join a deadlock). A documented partial retreat
exists — adopt B's shared-ownership keep-alive *without* dropping the join.

### Measurement this decision commits us to

**M2 step 2 must measure the game-thread pause at PIE stop**, using M1's
`Tick`-gap technique. Single-digit milliseconds confirms D15 and yields a devlog
number; consistently beyond ~100 ms invalidates its premise. This is the
evidence that decides whether D15 survives — it is not optional polish.

### Teaching note

The derivation stalled at `DoCloseAsync` because lambdas, capture semantics,
`TFunction` and `TSharedPtr` reference counting had never been covered — Sanjyot
said so directly rather than guessing, which was the right call and is what the
rule is for. Covered from fundamentals, then the design question was re-asked in
plain English and answered. Terms added to `Docs/Glossary.md` §10.

His own words, which turned out to be the conclusion rather than a gap:
*"ElectraPlayer's complicated shutdown procedure is not justified if we still
need to do things in the ImgMedia way also."*

### Files updated

**Modified (tracked):** `Docs/Architecture.md` (D15 in §12),
`Docs/Glossary.md` (new §10, "Threading and concurrency" — ~25 entries covering
`FRunnable`/`FRunnableThread`/`FEvent`, TLS, lambdas and captures, `TFunction`,
`TSharedPtr`/`ESPMode`, `Async`/`MoveTemp`, `TAtomic` vs `volatile`,
`AVIOInterruptCB`), `CLAUDE.md`, this file.
**Modified (local-only):** `M2DesignDerivation.md` — Q4 evidence list, findings
A1–A5 and B1–B6, the D15 decision record, the alternative that lost, and five
reopen conditions. 526 → 885 lines.

**Not committed.** Nothing in `Plugins/` or `Content/` was touched; no code was
written this session.


### Study notes moved out of the repo and junctioned back in

The backup gap identified at the top of this session is now closed. The four
notes live at `Q:\Obsidian\LIAR_Learns\UE_IPStream\IssuesAndWalkthroughs` — a
folder inside Sanjyot's Obsidian vault, which is a private git repo
(`LifeisaRepo/LIAR_Learns`) on a local NTFS disk.
`Docs/IssuesAndWalkthroughs` in this project is now a **junction** to it.

Junction rather than symlink deliberately: `mklink /J` needs no elevation or
Developer Mode, and Git for Windows walks a junction as an ordinary directory.
`.gitignore:88` was tightened from `Docs/IssuesAndWalkthroughs/*` to
`Docs/IssuesAndWalkthroughs` so the rule covers the **link entry itself** as
well as its contents — with the old `/*` form, a link of that name would not
have been ignored and could have been committed, putting a path into a personal
vault in a public repo.

Verified end to end: junction resolves; all four files intact (367 / 592 / 884 /
419 lines); `git status` in this repo shows nothing from the folder;
`git check-ignore` covers both the entry and its contents; a write made through
the junction appeared on the `Q:\` side and **was picked up live by Obsidian
without a restart**, which was the one genuinely uncertain part.

**Known and accepted wart:** the notes contain eight relative links pointing out
of the folder (`../Architecture.md`, `../Glossary.md`,
`../../Plugins/…/IPStreamSpike.cpp`). They resolve when the notes are read
through the project path; they will show as unresolved in Obsidian, which
resolves relative paths against the vault root. Flipping the link direction does
not fix this — it is a property of Obsidian's path model. Accepted, because the
notes are read while working in the project, and Obsidian is serving as backup
and sync rather than as the reading surface.

**Still outstanding:** the four notes are untracked in the vault repo. They need
a `git add` and push there before the backup is real. Sanjyot is doing that
separately.

**Credential scan of the notes before the move:** one hit,
`M2PlayerWalkthrough.md:272` — `rtsp://admin:password@192.168.0.131/…`.
Reported and **confirmed by Sanjyot as a placeholder**, left as-is. Detector
pattern `rtsp://[^/\s]*:[^/\s]*@`, which reads only the authority section and
matches to the last `@`, so an `@` inside a password cannot hide it.

### Committed

`84b208a` — "Decide how the decode thread is shut down". Five tracked files
(`.gitignore`, `CLAUDE.md`, `Docs/Architecture.md`, `Docs/Glossary.md`, this
file), +326/−25. Pushed; `main` in sync with `origin/main`, working tree clean.

**Commit message convention corrected this session, and saved to memory:** no
internal shorthand — decision numbers, derivation question numbers, findings
labels — in commit messages, *especially* titles, because they are read months
later from `git log` with none of the docs open. And keep the body to a few
lines giving a basic understanding of the change; rationale, alternatives and
measurements belong in the docs. The first drafts failed on both counts.

### Agreed before clearing the chat

- **Step 2 order:** FFmpeg session object first, then the interrupt stop flag,
  then the worker, then wiring into the player, then the measurement. Recorded
  in `CLAUDE.md`.
- **Code delivery: option (a)** — posted in chat, Sanjyot types it in.
- **A threading walkthrough doc is due**, `M2ThreadingWalkthrough.md`, written
  live chunk by chunk. His reasoning: *"we are getting into core logic now."*

### Next action

**M2 step 2 — FFmpeg demux/decode on the worker thread, behind the `Open()`
that already works.** The FFmpeg call sequence is M1's, already documented
call-by-call in `M1FFmpegWalkthrough.md`; what is new is the threading, now
settled by D15. Flagged going in: `~FIPStreamPlayer` stops being empty;
`CreatePlayer` may want an `#if WITH_FFMPEG` guard so a failed DLL load returns
`nullptr`; and the interrupt callback must gain its stop flag. A walkthrough doc
for the worker/threading code is due per the standing rule, as the API surface
is unfamiliar.

---

## Session 11 — 2026-09-17

**M2 step 2, item 1: the FFmpeg session object — header designed and typed in.
And a process change that outranks it.**

### The header

`FIPStreamFFmpegSession`, at
`Plugins/IPStreamMedia/Source/IPStreamMedia/Private/IPStreamFFmpegSession.h`.
Written by Sanjyot from chat (option (a), as agreed in Session 10). Owns
`AVFormatContext`, `AVCodecContext`, a reusable `AVPacket` and `AVFrame`, the
video stream index, the interrupt deadline and the stop flag. Three verbs —
`Open()`, `DecodeNext()`, `Close()` — plus `RequestStop()` / `IsStopRequested()`
as the only methods callable from another thread.

`.cpp` not yet written. Chunk 2 (`Open()`) deliberately not started.

### Decisions taken

- **`std::atomic<bool>`, not `TAtomic<bool>`.** `Templates/Atomic.h:13` reads
  *"`TAtomic` is planned for deprecation. Please use `std::atomic`"*, read on
  disk. `TAtomic` still compiles in 5.3 behind `USE_DEPRECATED_TATOMIC`
  (defaults to `1`), so both work — Epic's own guidance decided it. The ImgMedia
  and Electra source read during Q4 uses `TAtomic`, which is age, not
  endorsement. `Docs/Glossary.md` §10 documents `TAtomic` and now needs a
  footnote.
- **`UE_NONCOPYABLE(FIPStreamFFmpegSession)`** (`CoreMiscDefines.h:325`) rather
  than two hand-written `= delete` lines. The session owns raw FFmpeg pointers
  freed by hand in its destructor; a compiler-generated copy would duplicate the
  addresses and double-free them, and there is no meaningful copy of a live
  network connection. The macro also deletes the two move operations, which we
  do not want either.
- **`EIPStreamDecodeResult`** with five values — `GotFrame`, `NoFrameYet`,
  `Aborted`, `StreamEnded`, `Error`. Accepted provisionally, to be revisited if
  `DecodeNext()` turns out to need finer distinctions.
- **The stop flag is declared on the session, not on the `FRunnable`.** The
  interrupt callback is a C function FFmpeg calls with exactly one `void*`
  (`avio.h:60`), and that pointer is the only channel into a thread blocked
  inside `av_read_frame`. Putting the flag on the session makes that pointer
  `this` — free and always valid. The alternative puts the flag on the
  `FRunnable` and gives the session a raw back-pointer to it, which needs a
  wiring step nothing enforces (forget it and shutdown hangs, cleanly compiled)
  and couples the session's correctness to another object's lifetime.

### A claim of Claude's that was wrong, and the correction

Claude asserted that `FImgMediaSchedulerThread` "literally does" the rejected
alternative, implying engine precedent against our choice. Sanjyot asked to see
it. On reading `ImgMediaSchedulerThread.h:66`, the flag *is* on the `FRunnable`
— but **ImgMedia has no second object at all**, no session and no foreign
library holding a callback, so the question "which of two classes declares the
flag" does not exist there. It is not evidence either way.

The correct general statement, now in the walkthrough: **the flag on the session
is not better in general.** It is better given a constraint we have and ImgMedia
does not — a C library reaching us through a single opaque pointer. Remove
FFmpeg and the flag belongs on the `FRunnable`, exactly where ImgMedia put it.

This is the Q3 lesson recurring: engine precedent shows what works in the
engine's situation, not what is right in ours. **It also shows the Session 8
derivation rule working in the direction it was written for** — Sanjyot asking
to see the source caught an overstated claim that would otherwise have gone into
his notes as fact.

### The process change — this is the important part of the session

Chunk 1 took three passes and Sanjyot still had to re-derive it himself. The
content was correct throughout; the delivery failed. His words: *"This was
already too much to understand and learn."*

Six hard rules are now in `CLAUDE.md` under **"How to explain — hard rules,
from 2026-09-17"**, marked as the highest-priority instructions in that file for
any teaching message:

1. **Never explain jargon with other jargon.** Failed by explaining forward
   declarations as *"a pointer to an incomplete type is itself a complete
   type"*; and by using *thread affinity, translation unit, undefined behaviour,
   memory model, member initialisation order* undefined in one message.
2. **One idea per chunk**, max ~10 lines of code attached. "Chunk 1" carried six
   concepts plus a 60-line class declaration.
3. **Concept first, code second** — never post a finished file and then explain
   its parts. Assemble the full listing only once every piece is understood, so
   it reads as a summary rather than an introduction.
4. **Check with a specific question or a restatement request**, never "does that
   make sense?"
5. **When he says he doesn't understand, go down a level — do not re-explain.**
   The second pass was longer than the first.
6. **Fewest words, simplest words.**

**Also corrected:** `CLAUDE.md`'s long-standing bullet *"explanation length is
not a cost on this project"* was being read as licence to restate one idea in
several phrasings. It now reads **"coverage is not a cost; repetition is"** —
cover every "why" once, then stop. The original never meant otherwise.

**What actually diagnosed the problem:** Sanjyot restating the whole concept in
his own words, unprompted. That one message exposed four misunderstandings after
two explanation passes had exposed none. Asking for a restatement is now the
standard check.

### Next action

**M2 step 2 chunk 2 — `Open()`**, under the new rules: one idea at a time,
concept before code, restatement as the check. Then `DecodeNext()`, then
`Close()` and the destructor, then item 2 onward from the Session 10 order.

---

## Session 12 — 2026-09-18

**M2 step 2, item 1 is code-complete: `FIPStreamFFmpegSession.h` and `.cpp` both
written and reviewed.** Not yet compiled — that is the next action.

### What was built

`FIPStreamFFmpegSession`, at
`Plugins/IPStreamMedia/Source/IPStreamMedia/Private/IPStreamFFmpegSession.{h,cpp}`.
Owns one FFmpeg connection: `AVFormatContext`, `AVCodecContext`, a reusable
`AVPacket` and `AVFrame`, the video stream index, the interrupt deadline and the
stop flag. `Open()` / `DecodeNext()` / `Close()`, plus `RequestStop()` and
`IsStopRequested()` as the only methods legal from another thread.

Typed in by Sanjyot at the assemble step (option (a)). Full call-by-call
reasoning in `M2ThreadingWalkthrough.md` §1-7.

### Decisions taken

- **`std::atomic<bool>`, not `TAtomic<bool>`** — `Templates/Atomic.h:13` says
  *"planned for deprecation. Please use `std::atomic`"*. `TAtomic` still compiles
  behind `USE_DEPRECATED_TATOMIC` (default `1`), so both work; Epic's own
  guidance decided it. ImgMedia and Electra use `TAtomic`, which is age, not
  endorsement. `Docs/Glossary.md` §10 carries the note.
- **`UE_NONCOPYABLE`** (`CoreMiscDefines.h:325`) rather than hand-written
  `= delete` lines. The session owns raw FFmpeg pointers freed by hand; a
  compiler-generated copy would duplicate the addresses and double-free them.
- **The stop flag is declared on the session, not on the `FRunnable`.** The
  interrupt callback is a C function FFmpeg calls with exactly one `void*`
  (`avio.h:60`), and that is the only channel into a thread blocked inside
  `av_read_frame`. Putting the flag on the session makes that pointer `this` —
  free and always valid. The alternative needs a raw back-pointer from the
  session into its owner, wired by a step nothing enforces; forget it and
  shutdown hangs, cleanly compiled.
- **`relaxed` memory ordering** on both flag accessors. The flag signals nothing
  but itself, and being one loop iteration late costs nothing. Reopens the
  moment any data rides along with it — **including if `DeadlineSeconds` ever
  becomes game-thread-written** (a Blueprint-editable timeout, say), which would
  also stop it being worker-thread-only.
- **`DecodeNext()` asks the decoder for a frame first**, and only reads a packet
  when the decoder reports itself empty. This makes `AVERROR(EAGAIN)` from
  `avcodec_send_packet` unreachable, which is the case M1's order can hit and
  silently drop a packet on. It also keeps one call to one blocking read, so the
  worker can check the stop flag between calls — which matters for D15.
- **Two timeouts, as named constants:** 6.0 s connect (M1's measured number),
  2.0 s read (one full GOP at GOP = 50, so a normal keyframe gap cannot trip it).
- **`Close()` deliberately does not reset `bStopRequested`.** `Close()` ends a
  connection; `RequestStop()` ends the worker. Resetting it would let the
  reconnect path clear a shutdown request that had just been made.

### A claim of Claude's that was wrong, and the correction

Claude asserted `FImgMediaSchedulerThread` "literally does" the rejected
stop-flag alternative, implying engine precedent against our choice. Sanjyot
asked to see it. `ImgMediaSchedulerThread.h:66` does put the flag on the
`FRunnable` — but **ImgMedia has no second object at all**, no session and no C
library holding a callback, so the question does not exist there. It is not
evidence either way.

The correct general statement, now in the walkthrough: the flag on the session
is not better in general. It is better *given a constraint we have and ImgMedia
does not*. Remove FFmpeg and the flag belongs on the `FRunnable`, exactly where
ImgMedia put it. Q3's lesson recurring — and the Session 8 derivation rule
working as intended, since asking to see the source is what caught it.

### `analyzeduration` was removed — the line did nothing

Sanjyot asked where the three `AVDictionary` keys came from and how 32768 was
chosen. Honest answer: carried over from M1, and M1 got them from Claude's
recall, not evidence. Checking properly turned up a real finding.

`ffmpeg -h full` reports `-analyzeduration ... (default 0)`, and
`avformat.h:1540` says *"Can be set to 0 to let avformat choose using a
heuristic."* So zero does not mean "skip analysis" — it means "use the
heuristic", which happens anyway. **The line set the value to the value it
already had**, and M1's comment was wrong on both counts. Deleted.

`probesize` = 32768 is honestly 2^15 picked to be small. The default is 5 MB
(~39 s of data at this stream's ~128 KB/s), but it is a **cap, not a target** —
`avformat_find_stream_info` stops as soon as it knows the format. So it bounds
the worst case rather than speeding up the normal one, which is a weaker
justification than M1's comment implied.

Also found, not adopted: the RTSP demuxer exposes `-buffer_size` (the socket
buffer lever) and `-timeout` (FFmpeg's own socket timeout). `timeout` is **not**
a substitute for our interrupt callback — it only knows a clock, so it could
never answer the stop flag.

*Caveat recorded:* those defaults were read from ffmpeg 8.1.1
(`libavformat 62.12.101`) on PATH, not from our pinned build
(`libavformat 63.6.100`).

### Two bugs caught reviewing the typed-in `.cpp`

1. **A missing `return` after the `avcodec_receive_frame` error log.** A genuine
   decode failure would log and fall through to read another packet, so
   `DecodeNext()` keeps returning `NoFrameYet`, the worker never sees `Error`,
   never reconnects, and the log repeats every iteration forever.
2. **`avcodec_parameters_to_context()` omitted.** `avcodec_alloc_context3` gives
   a blank context knowing only the codec type; this call copies width, height,
   pixel format and `extradata` (HEVC's VPS/SPS/PPS). **The more dangerous of
   the two, because it may appear to work** — RTSP usually carries those
   parameter sets in-band, so the decoder often recovers at the next keyframe,
   giving a picture that is merely later and less reliable, with no error to
   point at.

Both fixed.

### The teaching rules from Session 11 were used for the first time, and worked

Chunks 2 through 11 ran under the six rules: one idea per chunk, concept before
code, a specific question or restatement at the end of each, no typing until the
assemble step. No chunk needed a second pass. Sanjyot's own questions during it
produced two of this session's findings — the `analyzeduration` no-op, and the
packets/frames-are-1:1-in-count correction.

Two process points confirmed along the way:

- **He types the code once, at the assemble step, not chunk by chunk.** His
  reasoning: typing incrementally is transcription; typing the whole file once
  from a complete picture is a revision pass. Recorded in `CLAUDE.md` rule 3.
- **The walkthrough docs get the same simplification treatment as chat**, by his
  test: *"no point reading 500 words if 50 words can do the same job — but I
  said 'same job', not 'similar'."* Cut restatement, never cut a "why", an
  alternative that lost, or a line number that makes a claim checkable.

### Next action

**Compile.** Nothing calls the session yet, so it builds as dead code — which is
exactly the point: it is a real check that the includes and every signature are
right, before the worker goes on top.

Then **work-order item 3, the worker** — the `FRunnable`: the loop, and the
four-step shutdown in its destructor (flag -> interrupt -> `WaitForCompletion()`
-> `Kill`/free). Item 2 (the stop flag in the interrupt callback) was absorbed
into item 1, as planned.
