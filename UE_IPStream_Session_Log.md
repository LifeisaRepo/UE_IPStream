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

**As of:** 2026-08-22 (Session 1)
**Phase:** 1 — RTSP ingest
**Status:** Architecture agreed and documented. No code written. Nothing scaffolded.

**Next action:** **concept prep, not execution.** Sanjyot has asked to begin with
education. Cover the M0/M1 concept set — video coding fundamentals, FFmpeg's
library layout, Unreal's build system and module model — before running tooling
or writing code.

**Then:** M0 — prove the camera stream with `ffprobe` / `ffplay` outside Unreal,
using the FFmpeg build that will ship. Owned by Sanjyot; needs the camera.

**Then:** repo prep — `.gitignore` / `.gitattributes` fix
([Architecture §9](Docs/Architecture.md)) plus a plugin skeleton that compiles
and loads with no FFmpeg calls in it.

**Open items** (all resolved by M0, none blocking):
1. Does the camera expose a substream? If yes, M1 targets it and HEVC moves to M2.
2. Camera GOP length — bounds first-frame latency and therefore the "10 seconds"
   in the M1 pass criterion.
3. Does the pinned FFmpeg build include libsrt? (`ffmpeg -protocols`) Determines
   whether Phase 2 needs an FFmpeg rebuild.

**Milestone position:** M0 not started.

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

**Loose end:** a numbered request from Sanjyot began at item 2 — item 1 was never
stated and has not been captured anywhere.

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
