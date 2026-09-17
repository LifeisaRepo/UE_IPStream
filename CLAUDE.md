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

- **Q4 IS DERIVED AND SETTLED — D15, Session 10 (2026-09-16).** Derived per the
  Session 8 rule: evidence handed over, Sanjyot read and answered first. The
  full record — the primitives, worked example A (`FImgMediaSchedulerThread`),
  worked example B (`FElectraPlayer::CloseInternal` / `DoCloseAsync`), findings
  A1–A5 and B1–B6, the decision, the alternative that lost, and five reopen
  conditions — is in `M2DesignDerivation.md`.
  **D15: a hard join, bought with an interruptible worker.** An owned `FRunnable`
  worker (not a task — D10 needs a worker that outlives any one connection);
  FFmpeg session state in its own object behind a worker-thread-only boundary;
  ImgMedia's four-step shutdown with **step 2 replaced by the interrupt callback
  returning `1`**, since that callback is the only thing that can reach a thread
  parked in `av_read_frame`. One idea kept from Electra: sever every callback
  path back into the player *before* tearing down.
  **The change that makes it work:** M1's callback (`IPStreamSpike.cpp:30-33`)
  knows a wall-clock deadline and nothing else, so after a `Close()` it keeps
  answering "keep going" until that deadline expires. It must read a stop flag
  as well as the clock.
  **Rejected as disproportionate:** Electra's detached async teardown. It buys
  freedom from a wait we can bound to milliseconds and charges nondeterministic
  teardown, a possible second RTSP session against the shared camera, and a
  crash risk unique to us — we `FreeDllHandle` FFmpeg by hand
  (`IPStreamMediaModule.cpp:85-89`), so a detached worker still inside
  `avformat-*.dll` at that moment is executing unmapped memory.

- **Next action: M2 step 2** — FFmpeg demux/decode on the worker thread, behind
  the `Open()` that already works. The FFmpeg call sequence itself is **not
  new**: it is M1's, documented call-by-call in `M1FFmpegWalkthrough.md`; the
  threading is what was new, and D15 settles it.

  **Agreed order of work (Session 10, before a fresh chat):**

  1. **The FFmpeg session object** — owns `AVFormatContext`, `AVCodecContext`
     and the interrupt struct; open / read-and-decode / close. Per D15 this is
     its own type, not loose members on the player: the boundary is that only
     the worker thread touches it.
     **DONE (2026-09-18) — `.h` and `.cpp` both written and reviewed, NOT YET
     COMPILED.** `FIPStreamFFmpegSession`, at
     `Private/IPStreamFFmpegSession.{h,cpp}`, typed in by Sanjyot. Compiling it
     is the next action: nothing calls it yet, so it builds as dead code, which
     is the check that every include and signature is right before the worker
     goes on top. Call-by-call reasoning in `M2ThreadingWalkthrough.md` §1–7.
     Settled along the way: **`std::atomic`, not `TAtomic`**
     (`Templates/Atomic.h:13` — Epic plans to deprecate `TAtomic`; Glossary §10
     carries the note), `UE_NONCOPYABLE`, a five-value `EIPStreamDecodeResult`,
     `relaxed` memory ordering on the flag, `DecodeNext()` asking the decoder
     for a frame *before* reading a packet (which makes `EAGAIN` from
     `avcodec_send_packet` unreachable — the case M1's order silently drops a
     packet on), 6.0 s connect / 2.0 s read timeouts, and **`Close()`
     deliberately not resetting the stop flag** (`Close()` ends a connection;
     `RequestStop()` ends the worker).
     **The stop flag is declared on the session rather than on the
     `FRunnable`** — the interrupt callback gets exactly one `void*`
     (`avio.h:60`), so putting the flag there makes that pointer `this`. Note
     for the interview answer: that is not better *in general*; it is better
     given a C library reaching us through one opaque pointer. Without FFmpeg
     the flag belongs on the `FRunnable`, where ImgMedia puts it.
     **`analyzeduration` was deleted from the options dictionary** — its default
     is already `0`, and `avformat.h:1540` says `0` means "let avformat choose
     using a heuristic", so M1's line set the value to the value it already had
     and M1's comment was wrong. `probesize`'s 32768 is honestly a chosen power
     of two, not a derived number; it caps the worst case rather than speeding
     up the normal one.
  2. ~~**The stop flag in the interrupt callback.**~~ **ABSORBED INTO ITEM 1,
     done 2026-09-18.** The flag lives on the session, and
     `FIPStreamFFmpegSession::InterruptCallback` reads it before consulting the
     clock.
  3. **The worker** — the `FRunnable`: the loop, and the four-step shutdown in
     its destructor (flag → interrupt → `WaitForCompletion()` → `Kill`/free).
  4. **Wire it into the player** — `Open()` starts it, `Close()` joins it,
     `~FIPStreamPlayer` stops being empty. Plus an `#if WITH_FFMPEG` guard on
     `CreatePlayer` so a failed DLL load returns `nullptr` rather than a player
     that can never decode. (`WITH_FFMPEG` already exists —
     `Source/ThirdParty/FFmpeg/FFmpeg.Build.cs:46`.)
  5. **Measure the PIE-stop pause.** D15 commits step 2 to a measurement, not
     just a build: the game-thread pause when PIE stops, via M1's `Tick`-gap
     technique. Single-digit milliseconds confirms D15; consistently beyond
     ~100 ms invalidates its premise and reopens the rejected alternative.

  **Code delivery for step 2: option (a) — posted in chat, Sanjyot types it in.**
  Chosen explicitly at the end of Session 10. Do not write to `.h`/`.cpp`
  without asking again.

  **A walkthrough doc is due and is not optional this time** — his words: *"we
  are getting into core logic now."* New file,
  `Docs/IssuesAndWalkthroughs/M2ThreadingWalkthrough.md`, same format as
  `M1FFmpegWalkthrough.md`: one logical job per section, written **live as each
  chunk is covered in conversation**, not afterwards from memory. Threading is
  the unfamiliar API surface here — `FRunnable`, `FRunnableThread`, `FEvent`,
  `TAtomic` — and `Docs/Glossary.md` §10 now defines all of it.

  Then step 3 (samples into `FMediaSamples`, enable
  `AlwaysPullNewestVideoFrame`) and step 4 (`UMediaTexture` in PIE).

- **The local study notes now live in the Obsidian vault, junctioned back in.**
  Real files at `Q:\Obsidian\LIAR_Learns\UE_IPStream\IssuesAndWalkthroughs`
  (a git repo, `LifeisaRepo/LIAR_Learns`, local NTFS disk);
  `Docs/IssuesAndWalkthroughs` in this project is a **junction** to it, verified
  working in both VS Code and Obsidian, including edits made through the
  junction. `.gitignore:88` is now `Docs/IssuesAndWalkthroughs` with no `/*`, so
  it covers the link entry as well as its contents.
  **Background:** all four notes were deleted on 2026-09-14 and recovered from
  the recycle bin on 2026-09-16 — they had never been tracked by any repo. Keep
  writing to them exactly as before; the junction is transparent.

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
| D15 | Decode loop on an owned `FRunnable` worker; `Close()` **joins** it; M1's interrupt callback gains a stop flag so the join is bounded. Electra's detached async teardown rejected as disproportionate — see `M2DesignDerivation.md` Q4 for the five reopen conditions |

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
- **Coverage is not a cost. Repetition is.** Every "why" must be covered — never
  drop one to save his time. But say each thing **once**, then stop. Restating
  the same point in three different phrasings is the failure this project
  actually suffers from, not brevity.
  *Corrected 2026-09-17.* This bullet previously read "explanation length is not
  a cost on this project", which was read as licence to circle the same idea
  repeatedly. It never meant that.

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

### How to explain — hard rules, from 2026-09-17

**These are not style preferences. They are the highest-priority instructions in
this file for any teaching message, and they survive every session reset.**

Established after M2 step 2 chunk 1 needed three passes and Sanjyot still had to
re-derive it himself. The *content* was correct throughout. The *delivery*
failed. His words: *"This was already too much to understand and learn."*

**1. Never explain jargon with other jargon.**

If a sentence explaining term A introduces term B, it has failed. Go down a
level until every word is either plain English or something already established
in this project.

> Failed this way on 2026-09-17: forward declarations were explained as *"a
> pointer to an incomplete type is itself a complete type"* — jargon explaining
> jargon. The plain version is: *"the compiler knows a pointer is 8 bytes
> without knowing what it points at."*
>
> Also used undefined in one message: *thread affinity, translation unit,
> undefined behaviour, memory model, member initialisation order.*

**2. One idea per chunk. One.**

A chunk is **a single concept**, with at most ~10 lines of code attached. If a
message covers two concepts, it is two chunks and should have been two messages.

> Failed this way on 2026-09-17: "chunk 1" posted a 60-line class declaration
> plus forward declarations, `= delete`, `std::atomic`, where the stop flag
> lives, why the deadline is not atomic, and the two halves of the interrupt
> callback. **That is six chunks.**

**3. Concept first, code second. Never the reverse.**

Do not post a finished file and then explain its parts. That forces him to hold
unfamiliar code in his head while unrelated mechanics are attached to scattered
lines of it.

The working method:

| | |
|---|---|
| **a. Concept** | one idea, plain words, no code or a snippet of a few lines |
| **b. Check** | see rule 4 |
| **c. Repeat** | next idea |
| **d. Assemble** | only once every piece is understood, post the whole file as one listing — **this is where he types it in** |

By step (d) the full listing is a **summary of things he already understands**,
not an introduction to anything. That is the point of the ordering.

**He does not type the code in chunk by chunk** (his decision, 2026-09-17). The
chunks are for reading; the typing happens once, at the assemble step, against
the complete file. His reasoning: typing incrementally is transcription, whereas
typing the whole thing once from a complete picture is a revision pass. So do
not ask him to type after each chunk, and do post the full listing at the end
even when every line of it has already appeared in a chunk.

**4. Check with a question, not "does that make sense?"**

A yes/no question gets a reflexive yes and diagnoses nothing. End a chunk with
either a specific one-line question, or a request to restate it in his own
words. **Asking for a restatement routinely is confirmed welcome (2026-09-17)
— use it, and use it on anything non-trivial.**

> What actually worked on 2026-09-17 was Sanjyot restating the whole thing
> unprompted. That single message exposed four misunderstandings. Nothing before
> it had diagnosed anything. **Ask for the restatement rather than waiting for
> it.**

**5. When he says he does not understand, go down a level — do not re-explain.**

Re-explaining the same idea with more words makes it worse.

> Failed this way on 2026-09-17: the second pass was *longer* than the first.

**6. Fewest words, simplest words.** Straight and to the point. Rules 1–5 do the
real work; this one is the tiebreaker when a sentence could be shorter.

**The walkthrough docs get the same treatment** (confirmed 2026-09-17). They are
exempt only from the chunking rules, which are about pacing a conversation — not
from rules 1 and 6. His test, in his words:

> *"There is no point in reading 500 words if 50 words can do the same job. But
> I said 'same job', not 'similar'."*

So: cut every word that is restatement, hedging, or a second phrasing of a point
already made. **Do not cut a word that carries context** — the "why", the
alternative that lost, the condition that reopens it, the line number that makes
a claim checkable. If shortening would lose any of that, or would force jargon
back in to save space, **leave it long.** Subtraction that costs content is a
worse failure than length.

**What all of this is for:** he must be able to defend **every design decision
and every line of code** in an interview. A message he cannot follow contributes
nothing to that, regardless of how correct it is.

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
