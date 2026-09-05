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

**Architecture agreed and documented. No code written. Nothing scaffolded.**

- Full design lives in [Docs/Architecture.md](Docs/Architecture.md) — module
  layout, Media Framework contract, threading, licensing, milestones M0–M6.
- **Next action: concept prep, not execution.** Sanjyot has asked to start with
  education. Cover the M0/M1 concept set — video coding fundamentals, FFmpeg's
  library layout, Unreal's build system and module model — before any tooling is
  run or code is written. See *How we work together* below.
- **Then: M0** — prove the camera stream with `ffprobe` / `ffplay` outside
  Unreal, using the FFmpeg build that will ship. Owned by Sanjyot; needs the
  camera.
- **Then: repo prep** — `.gitignore` / `.gitattributes` fix (Architecture §9) and
  the plugin skeleton that compiles and loads with no FFmpeg calls in it.
- Phase 1 = RTSP only. SRT is Phase 2, RTMP is Phase 3.

## Key decisions made so far

Full rationale in [Docs/Architecture.md §12](Docs/Architecture.md). Summary:

| # | Decision |
|---|---|
| D1 | RTSP first; SRT Phase 2; RTMP Phase 3 |
| D2 | Proper Media Framework player, not a bespoke API |
| D3 | FFmpeg, LGPL 2.1, **shared DLLs, dynamic linking. Never static, never `--enable-gpl`** |
| D4 | Third-party binaries committed via Git LFS, not fetched by a setup script |
| D5 | Plugin's own code is MIT |
| D6 | Two modules: runtime (`IPStreamMedia`) + factory (`IPStreamMediaFactory`) |
| D7 | FFmpeg wrapped as `ModuleType.External` |
| D8 | Week-1 spike is **throwaway** — no Media Framework in it at all |
| D9 | Sample timing: latest-frame-wins first, behind a policy interface |
| D10 | Reconnect is transparent, inside the session, with backoff |
| D11 | No latency target — a documented, reproducible method and whatever number falls out |
| D12 | M3 (GPU colour conversion) is the designated **cuttable** milestone |

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
