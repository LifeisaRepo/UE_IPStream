# UE_IPStream — Project Instructions

## What this project is

An Unreal Engine 5.3 plugin that ingests live IP video streams into Unreal as a
proper Media Framework player backend — `IMediaPlayer`, a registered player
factory, and the sample interfaces — so a stock `UMediaPlayer` and
`UMediaTexture` work against an `rtsp://` URL with no bespoke API. Unreal has no
first-party IP-stream ingest: Electra covers HLS/DASH and files, MediaIO covers
SDI via vendor plugins, and this fills the gap between them.

It is a **portfolio and job-search asset**, targeting Unreal Gameplay / Systems /
Tools-Pipeline IC roles. Its purpose is to demonstrate engine-internals depth and
third-party native integration skill.

## Current status

**M1 — THE SPIKE — PASSED, committed, and pushed.** Commit `81714c5`
("First Spike Successful"), on `main`, matching `origin/main`. Recognisable
image on a plane, timing measured (0.999s success case), clean PIE stop
confirmed with no hang or crash across multiple runs, both success and
failure paths.

- Full design lives in [Docs/Architecture.md](Docs/Architecture.md) — module
  layout, Media Framework contract, threading, licensing, milestones M0–M6.
- **Concept prep delivered as five blocks (A–E)**, each front-loading the
  milestone it precedes. Canonical table: [Docs/Architecture.md §10](Docs/Architecture.md).
  **Blocks A, B and C all done** — video coding fundamentals; FFmpeg's
  architecture + Unreal's build system and module model; and the full Media
  Framework interface set (`IMediaPlayer`, `IMediaPlayerFactory`,
  `IMediaEventSink`, `IMediaSamples`/`FMediaSamples`, `IMediaTextureSample`)
  across Sessions 6–7. Block D isn't due until M3.
- **Repo-prep complete.** `.gitignore`/`.gitattributes` fixed (committed
  `d0fd929`). UE project `IPStreamMediaDemo` created at the repo root; the
  `IPStreamMedia`/`IPStreamMediaFactory` plugin skeleton exists and compiles.
- **M1 — the spike — code complete, plugin loads in the editor.**
  `FFmpeg.Build.cs` (External module), `IPStreamMedia.Build.cs` wired to
  depend on it, `IPStreamMediaModule.cpp` doing delay-load DLL resolution at
  startup, and `IPStreamSpike.cpp`/`.h` (the throwaway
  `UBlueprintFunctionLibrary`, `GrabOneFrame`) are all written and working.
  Every FFmpeg and UE call in the spike was verified against the actual
  headers on disk rather than recalled — see
  `M1FFmpegWalkthrough.md` for the full
  call-by-call reasoning.
- **A delay-load failure blocked editor startup entirely and was root-caused
  and fixed** — BtbN's FFmpeg `.lib` files are GNU-format import libraries,
  which MSVC's `/DELAYLOAD` silently ignores; regenerated from the shipped
  `.def` files with `lib.exe`. Full write-up:
  `M1DelayLoadRCA.md`. **If the FFmpeg pin is
  ever re-downloaded, this must be repeated** — see
  `ThirdParty/FFmpeg/NOTICE.md`.
- **All three M1 pass criteria met, measured not eyeballed.** A `Tick`-based
  timestamp trick (the game thread can't tick while blocked inside
  `GrabOneFrame`, so the gap between `Tick` logs directly measures the
  freeze) gave real numbers: **0.999s** for the full pipeline against the
  real camera, **6.039s** for a deliberately unreachable URL — matching the
  coded interrupt-callback deadline of `+6.0` almost exactly, empirically
  confirming that mechanism works as designed. PIE stopped cleanly, no hang,
  no crash, across multiple runs of both the success and failure paths.
- **M1's throwaway code is still in the project, deliberately.** Per D8 it
  gets deleted, but that's deferred until M2 has something working to replace
  it. Still present: `IPStreamSpike.h`/`.cpp`, `Content/BP_SpikePlane.uasset`
  (placed inside `Content/Maps/Main.umap` — deleting it outside the editor
  leaves a dangling map reference), `M_SpikeTest`/`MPC_SpikeTest`, and
  `WBP_Timer`. **Do not touch any of these until M2 has a replacement.**

- **M2 IS IN PROGRESS, AND ITS DESIGN IS BEING *DERIVED*, NOT DELIVERED.**
  This changed mid-M2 (Session 8) and governs everything from here — see
  "How we work together" below and
  `M2DesignDerivation.md`. Three design
  questions settled so far, each by Sanjyot reading engine source himself:
  **Q1 → D14** (player *owns* a `TUniquePtr<FMediaSamples>`; implements the
  other four sub-interfaces directly), **Q2 → D6's rationale corrected**
  (a factory is metadata about a player, queryable where the player can't
  load; factory belongs at `PostEngineInit`, not `PostConfigInit`), and
  **Q3** (the `.Build.cs` dependency model — `Media` is headers-only because
  its public surface is pure-virtual; `MediaUtils` needs real linkage).
  `.uplugin` and both `.Build.cs` files are updated accordingly.

- **M2 STEP 1 PASSES — the Media Framework chain is proven end to end, with no
  FFmpeg in it.** Factory registers at `PostEngineInit`; `IP Stream Media` is
  selectable as the player override on a `UMediaSource`; our factory beats
  WmfMedia for `rtsp`; the player is constructed via
  `IIPStreamMediaModule::CreatePlayer`; `MediaOpened` reaches Blueprint in PIE;
  `Player Closed` on stop. Five files, all typed in by Sanjyot. Call-by-call
  reasoning in `M2PlayerWalkthrough.md`.
  GUID in use (Sanjyot's own, identical in factory and player as required):
  `FGuid(0x6bd90b53, 0xbe5340cd, 0xab90c371, 0x7f1b284a)`.

- **Credential hazard, hit twice now — read before creating any test asset.**
  A `UStreamMediaSource` stores its URL as a default property **inside the
  binary `.uasset`**, and `Content/` is tracked — so committing one leaks the
  camera password with *nothing readable in the diff*, and the `*.txt` ignore
  rule does not apply. **The remedy is to remove the secret, not hide the
  file:** clear the URL field to a dummy and re-save. That keeps
  `Content/MediaAssets/` committable — the player-override setting is a
  separate field, and it's the part M6's demo wants. **On any credential
  finding: alert Sanjyot and stop. He chooses the remedy — do not gitignore,
  delete, or edit anything unilaterally.** The camera password **contains an
  `@`**, so any check or redaction cutting at the *first* `@` is wrong: search
  the authority section (before the first `/`) and take the *last* `@`, as
  `RedactUrlCredentials` does. State the detector pattern when reporting a scan
  as clean — a verdict is only as good as the pattern behind it.

- **Step 1 is committed and pushed** — `8d73e99` ("IPStreamMedia Player working
  skeleton"), `main` in sync with `origin/main`, working tree clean, full
  credential scan of HEAD clean.

- **Next action: derive Q4 BEFORE writing any step-2 code.** Agreed explicitly
  at the end of Session 9, choosing "derive first" over "code first and derive
  if something bites."
  **Q4 — what runs the decode loop, and how is it shut down?** `FRunnable` +
  `FRunnableThread`, a task, or something else? Where does the FFmpeg session
  state live — on the player, or its own object? What exact ordering guarantees
  `Close()` cannot return while the worker still holds an FFmpeg handle? M1
  proved the cancellation mechanism (`AVIOInterruptCB`, measured 6.039s on an
  unreachable URL) but never combined it with a thread somebody is waiting to
  join. This is the failure class flagged in "Model per phase" below — it
  surfaces as an editor hang, not a compile error.

- **Then M2 step 2** — FFmpeg demux/decode on the worker thread, behind the
  `Open()` that already works. The FFmpeg call sequence itself is **not new**:
  it is M1's, documented call-by-call in `M1FFmpegWalkthrough.md`. Flagged for
  it: `~FIPStreamPlayer` stops being empty (a thread must be shut down there),
  and `CreatePlayer` may want an `#if WITH_FFMPEG` guard so a failed DLL load
  returns `nullptr` rather than a player that can never decode. Then step 3
  (samples into `FMediaSamples`, enable `AlwaysPullNewestVideoFrame`) and step 4
  (`UMediaTexture` in PIE).

- **Still-open thread from Block C:** which `EMediaEvent`s actually fire
  around a reconnect. Flagged, not resolved — an M4 concern, doesn't block
  step 1.
- Phase 1 = RTSP only. SRT is Phase 2, RTMP is Phase 3.

## Key decisions made so far

Full rationale in [Docs/Architecture.md §12](Docs/Architecture.md). Summary:

| # | Decision |
|---|---|
| D1 | RTSP first; SRT Phase 2; RTMP Phase 3 |
| D2 | Proper Media Framework player, not a bespoke API |
| D3 | FFmpeg, LGPL (pinned build is v3, not the default 2.1 — see Architecture §4), **shared DLLs, dynamic linking. Never static, never `--enable-gpl`** |
| D4 | Third-party binaries committed via Git LFS, not fetched by a setup script |
| D5 | Plugin's own code is MIT |
| D6 | Two modules: runtime (`IPStreamMedia`) + factory (`IPStreamMediaFactory`). A factory is **metadata about a player**, queryable where the player can't load — *not* about registration ordering (rationale corrected 2026-09-12) |
| D7 | FFmpeg wrapped as `ModuleType.External` |
| D8 | Week-1 spike is **throwaway** — no Media Framework in it at all |
| D9 | Sample timing: latest-frame-wins first, behind a policy interface |
| D10 | Reconnect is transparent, inside the session, with backoff |
| D11 | No latency target — a documented, reproducible method and whatever number falls out |
| D12 | M3 (GPU colour conversion) is the designated **cuttable** milestone |
| D13 | D9's latest-frame-wins built on `IMediaPlayer`'s V2 timing model (`FMediaTimeStamp`/`SequenceIndex`), not V1 |
| D14 | `FIPStreamPlayer` **owns** a `TUniquePtr<FMediaSamples>`; implements `IMediaCache`/`IMediaControls`/`IMediaTracks`/`IMediaView` directly |

**Do not re-litigate these** unless there is new technical evidence.

## Active constraints

**Time**
- Evenings and weekends only. Roughly 8 weekends budgeted for Phase 1.
- On notice at current job; applying for roles **Sept–Nov 2026**.
- **It must ship. A 70%-finished plugin is worth zero.** When schedule and scope
  collide, cut M3 (that is what it is designated for) — never ship incomplete.

**Technical**
- UE **5.3**. Win64 only. Linux / Android / Quest deferred.
- One stream, one texture. Multi-stream deferred.
- Test source: one LAN IP camera exposing two RTSP streams. **Phase 1 targets
  the secondary stream — HEVC, 1280×720, 25 fps, GOP = 50 frames / 2.0s,
  admin-editable and measured clean.** The primary stream (1920×1080) runs a
  proprietary adaptive-codec feature ("InstaStream") with an unpredictable,
  non-adjustable ~8s GOP — deferred, not abandoned; see
  [Docs/Architecture.md §3](Docs/Architecture.md) and
  [Docs/TestSource.md](Docs/TestSource.md) for the full measurement.
- **This camera is shared** — accessed through an NVR with at least one other
  active user. **Do not reconfigure camera-side settings** (resolution,
  bitrate, GOP, disabling InstaStream, etc.) **without explicitly raising it
  first**, in any milestone, even ones that seem to only need a temporary
  change. Testing 1080p later requires disabling InstaStream on a shared
  device — that is a deliberate, discussed decision each time, not a default.
- PCM A-law audio present on both streams; permanently out of scope for
  Phase 1.
- Public repository, so licensing is a hard constraint, not a preference. See D3.

**Secrets and credentials — hard rule, learned from an actual incident (see
session log, 2026-09-05):**
- The camera's RTSP URL contains embedded credentials
  (`rtsp://user:pass@host/...`). **Never write the full URL, in plaintext, into
  any file that gets committed** — docs, scratch captures, code comments, none
  of it. `Docs/TestSource.md`'s convention — `rtsp://admin:***@192.168.0.131:...`
  — is the pattern to follow everywhere.
- **Raw diagnostic captures (`ffprobe`/`ffplay`/future FFmpeg debug output) are
  local-only, never committed.** They routinely open with the full connection
  URL. Summarize and redact findings into `Docs/` instead — that's the
  permanent artifact; the raw capture is disposable once its numbers are
  extracted. `.gitignore` blocks `output*.txt` for this reason.
- Before any commit that includes new files, actively check for the raw
  credential pattern, not just trust that redaction happened — this is exactly
  how it slipped through once already.

**Scope discipline**
- Scope creep is the single most likely cause of failure on this project.
  **Push back when scope widens.** The non-goals list in
  [Docs/Architecture.md §2](Docs/Architecture.md) is pre-refused — treat it as
  binding, not advisory.

**Framing — affects all user-facing writing**
- The demo must be in a **game context** (a live feed on an in-game surveillance
  monitor). **Never a broadcast or virtual-production context.** Sanjyot is
  deliberately moving away from broadcast/VP roles.
- The README, devlog, and any write-up read as **engine integration** — Media
  Framework, threading, GPU colour conversion, ThirdParty build integration —
  with streaming as the payload, not the subject.
- **No art or DCC work.** No Blender, no Photoshop. Primitives, engine defaults,
  and free marketplace assets only.

## How we work together

### This is a learning project, not only a build project

The goal is not a working plugin. The goal is a working plugin **that Sanjyot can
defend line by line in an interview.** Those are different targets, and the second
is the harder one. Everything below follows from that.

**Explain everything.**
- Never assume a term is known because it sits next to something he does know.
  Operational experience running RTSP/SRT/RTMP in live broadcast **does not
  imply** knowledge of codec internals, Media Framework, Unreal's build system,
  or licensing mechanics. These are separate bodies of knowledge.
- He will say so when something is already familiar. **Err toward explaining.**
- **Explanation length is not a cost on this project.** Do not compress to save
  his time — he has explicitly asked for the full version and will skip what he
  already knows. Brevity that omits the "why" is a failure here, not efficiency.

**Define jargon on first use**, then add the term to
[Docs/Glossary.md](Docs/Glossary.md). No unexplained acronym should survive a
message.

**Every non-obvious line of code needs a stated reason** — not what it does, but
why it is this and not the alternative.

**Record the alternative that lost.** "Why did you do it that way and not X?" is
the interview question. Answering it requires having considered X on the record.
This is already the pattern in the session log; extend it to code.

**Concept prep precedes each milestone.** Before starting a milestone, cover the
concepts it depends on — no code until the ideas are in place.

### Where the study notes live — LOCAL ONLY, not in the repo

`M1FFmpegWalkthrough.md`, `M1DelayLoadRCA.md`, `M2DesignDerivation.md` and
`M2PlayerWalkthrough.md` live in **`Docs/IssuesAndWalkthroughs/`, which is
gitignored**. They are Sanjyot's private study notes and he has decided not to
publish them — this repository is public.

Consequences to respect:

- **Never link to them with markdown links from a tracked file.** Every
  reference in `CLAUDE.md`, `Docs/`, and the session log is a plain backticked
  filename for exactly this reason; a link would dangle for anyone reading the
  repo on GitHub.
- **They are not backed up by git.** Treat them as the only copy.
- Keep writing to them exactly as before — the ignore changes where they are
  published, not whether they are maintained.

### Code walkthrough docs

**For any code touching an unfamiliar API surface — a C-style library like
FFmpeg being the concrete case so far, but not limited to it — a dedicated
walkthrough doc is standard practice, not optional.** This emerged directly
from M1: extensive explanation of surrounding C++ mechanics (pointers,
linkage, `extern "C"`) and domain theory (video coding concepts) did not, by
itself, add up to Sanjyot actually understanding the code he'd just typed in
and compiled — call-by-call reasoning through the API sequence itself was
the missing piece, and it had to be taught as its own, separate pass.

**Format**, established by
`M1FFmpegWalkthrough.md` — treat it as the
template for the next one:

- One file per unfamiliar-API code unit, named `Docs/<Milestone><Subject>
  Walkthrough.md`. Kept separate from `Architecture.md` (system design) and
  `Glossary.md` (term definitions) — this is specifically call-by-call code
  reasoning: what each line does, why this call and not an alternative, what
  it hands back and why the next line needs that.
- **Chunked and checked, not one full pass.** One logical "job" per section;
  confirm each chunk actually lands before moving to the next. A single long
  narrated walkthrough is harder to absorb than several short ones.
- Explain the code's own logic first. Surrounding language mechanics and
  domain theory are supporting material, not a substitute for it.
- Update the doc live, chunk by chunk, as each one is covered in
  conversation — not written up after the fact from memory. Chat is
  ephemeral; the doc is what has to survive to an actual interview.
- Sanjyot adds comments to the code himself, in his own words, once he
  understands a piece. Suggest comment wording in chat; don't write comments
  into source files unless he asks.

### Design derivation — hard rule, from M2 onward

**Never report a design conclusion drawn from reading engine or reference-plugin
source. Hand over the evidence and let Sanjyot read it.** Established Session 8,
after M2 opened with ~15 engine file reads compressed into ~6 sentences of
conclusion followed by code.

**Why:** what that transmits is **precedent, not reasoning**. "Electra composes
`FMediaSamples`, so we compose `FMediaSamples`" is exactly the answer that
collapses under the interview follow-up — *"why does Electra do it that way?"* —
and he'd be defending a choice he never made. M1 was throwaway per D8 so
silver-platter delivery was survivable; **everything from M2 on is permanent.**

**The cycle:**

1. State the **question**, with no answer attached.
2. Give **exact file paths and line ranges**, plus the contrast or tension to
   look for — not a summary of them. Engine source is at `F:\EpicGames\UE_5.3`.
   He reads in his own editor, where he can navigate and jump to definitions.
3. **He answers first.** Do not pre-state the conclusion and have him confirm it.
4. Correct misreadings, fill gaps, settle it together.
5. Record question + evidence + decision + **the alternative that lost** (and
   the condition that would reopen it) in
   `M2DesignDerivation.md`.

**Be selective or this burns the schedule.** Apply it to decisions that are
expensive to unwind. Do **not** apply it to stub return values, boilerplate
overrides, or anything one-line reversible.

**"I don't know why" is preferred over a confident post-hoc rationalisation of
engine convention.** Shipped engine code sometimes diverges for historical
reasons, and sometimes it is simply worse than what we'd write — Q3 found a
relative-path `#include` in `WmfMediaFactoryModule.cpp` that we deliberately do
not copy. Engine precedent is evidence of what works, not proof of what's best.

**This has already caught two real errors in one session** — D6's recorded
rationale was wrong, and a Glossary entry claimed one class conventionally
inherits all five `IMediaPlayer` sub-interfaces when it's four.

### Code delivery — hard rule

**Never write code into project files without asking first.** Every time code is
due, ask which:

- **(a)** posted in chat, for Sanjyot to read and type in himself, or
- **(b)** written directly to project files by Claude.

Offer **(a)** as the default — typing it forces reading it, which is the point of
this project — but do not insist if he wants (b).

| | Files | Permission |
|---|---|---|
| **Free rein** | `.md`, anything under `Docs/`, other documentation | No need to ask |
| **Must ask** | `.cpp`, `.h`, `.cs` (incl. `.Build.cs`, `.Target.cs`), `.uplugin`, `.ini`, `.bat` / `.ps1`, anything under `Content/` | Ask every time |

### Model per phase

Project state lives **on disk** (`CLAUDE.md`, `Docs/Architecture.md`,
`Docs/Glossary.md`, the session log), not in a conversation's context. Switching
models therefore costs nothing — any model reads four files and is current. Pick
per task rather than committing to one for the project.

| Work | Model | Why |
|---|---|---|
| Concept prep (Blocks A–E), glossary, docs, devlog drafting | **Sonnet** | Explanation-heavy, high-volume, well-established material. Faster, which matters across many long teaching sessions. |
| Routine implementation once the concepts are settled | **Sonnet** | Known shape, known API, reasoning already done. |
| **M1 debugging** — delay-load failures, unresolved externals against FFmpeg import libs, editor-works-packaged-fails | **Opus** | Diagnose-from-thin-evidence problems; this is the class where the depth gap shows. |
| **M4 lifecycle** — shutdown deadlocks against blocking network reads | **Opus** | Subtle, timing-dependent races that are easy to "fix" in a way that only relocates them. |
| Reopening an architectural decision, or a licensing edge case | **Opus** | Should be rare — D1–D12 are settled — but these are expensive to get wrong. |

**Caveat that applies to every model.** UE 5.3's Media Framework is a relatively
obscure API surface. Exact signatures — `IMediaTextureSample`'s contract,
`FMediaSamples`, which sub-interfaces `IMediaPlayer` exposes **in 5.3
specifically** — are more prone to confident confabulation than mainstream APIs.

**Read the engine headers on disk before asserting a signature.** Start from
`Engine/Source/Runtime/Media/Public/` and
`Engine/Source/Runtime/MediaUtils/Public/`. Recall is not evidence. This applies
to Opus as much as to Sonnet; it simply matters more the smaller the model.

### Measurement and narrative

Prefers showing measured improvement over time to presenting a polished first
result. This is a devlogged project; milestones are designed to produce
comparable before/after numbers.

## Session log

[`UE_IPStream_Session_Log.md`](UE_IPStream_Session_Log.md) in this folder is the
running discussion history for this project.

**It must always be up to date.** Append an entry at the end of any session that
produces a decision, a milestone change, a measurement, or a change of direction.
Update the "Current state" block at the top of that file at the same time.

This is what makes transitions between conversations seamless — a new chat should
be able to read `CLAUDE.md` plus the session log and pick up exactly where the
last one stopped, with no re-derivation and no re-litigating settled decisions.
