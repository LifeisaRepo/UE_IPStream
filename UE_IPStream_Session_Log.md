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

**As of:** 2026-09-05 (Session 2)
**Phase:** 1 — RTSP ingest
**Status:** M0 complete. Blocks A and B complete. No code written. Nothing
scaffolded.

**RESOLVED 2026-09-05: credential exposure on the public GitHub repo.**
`output.txt`, committed in `1713393` ("M0 Testing", tip of `main`) and live on
`origin/main` (confirmed public) since 2026-08-27, contained the camera's RTSP
URL with plaintext credentials (`rtsp://admin:***@192.168.0.131:...` — real
password redacted here deliberately; see below for why that redaction matters
even after rotation). **Password has been rotated, and git history has been
cleaned** — full write-up below.

**Next action:** Blocks A and B both complete (Session 2). Concept curriculum
(Blocks A–E, mapped to milestones) is recorded at
[Architecture.md §10](Docs/Architecture.md); Block C (Media Framework) isn't
due until M2. Immediate next step is the repo-prep work that sits between
Block B and M1 — `.gitignore` / `.gitattributes` fix
([Architecture §9](Docs/Architecture.md)) plus a plugin skeleton that compiles
and loads with no FFmpeg calls in it. This touches `.gitignore`/
`.gitattributes`/`.uplugin`/`.Build.cs` — code-delivery files, so the (a)
chat-vs-(b)-direct-write question applies before any of it is written.

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

**Milestone position:** M0 done, including a real stream-selection decision.
M1 not started.

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

### Files updated this session

`Docs/Glossary.md` (§8 GPL/LGPL entries rewritten in plain language; §5
Unreal build system expanded with public/private dependency names, the
explicit delay-load window mechanism, `StagedFileType.NonUFS`, and
`UnrealTargetPlatform`), `Docs/Architecture.md` (§10 concept curriculum table
added), `CLAUDE.md` (Current status corrected), this file.
