# M0 — Test Source Findings

**Status:** Both streams characterized. **Phase 1 targets the secondary stream (`subtype=1`).**
**Last updated:** 2026-09-22 (header only; findings below unchanged)

Camera URLs:

- **Secondary — Phase 1 target:** `rtsp://admin:***@192.168.0.131:554/cam/realmonitor?channel=2&subtype=1` (720p, HEVC, GOP 2.0 s)
- Primary — deferred: `rtsp://admin:***@192.168.0.131:554/cam/realmonitor?channel=2&subtype=0` (1080p, InstaStream, ~8 s GOP)

Dahua-style RTSP URL scheme (`realmonitor` path is the signature). By convention,
`subtype=0` is the main stream and `subtype=1` is the substream — verified for this
camera (see "Secondary stream" below).

---

## Stream summary (`ffprobe`)

```
Stream #0:0: Video: hevc (Main), yuv420p(tv), 1920x1080, 25 fps, 25 tbr, 90k tbn
Stream #0:1: Audio: pcm_alaw, 8000 Hz, mono, s16, 64 kb/s
```

| Property | Value |
|---|---|
| Video codec | HEVC / H.265, Main profile |
| Resolution | 1920×1080 |
| Frame rate | 25 fps |
| Pixel format | `yuv420p`, **limited range** (`tv`, 16–235) |
| Audio codec | PCM A-law, 8 kHz mono — out of scope, confirmed present |

---

## Frame-type analysis

Two captures via `ffprobe -show_frames -select_streams v`:

**5-second capture (123 frames):** 1×I, 122×P, 0×B.
**30-second capture (746 frames):** 4×I, 742×P, 0×B.

### No B-frames — confirmed

Zero B-frames across 746 frames. Consistent with a live/low-latency encoder
profile — B-frames require buffering future frames for bidirectional prediction,
which a camera optimizing for live viewing avoids.

### I-frame vs P-frame size — measured, not estimated

```
I-frame:  260,427 – 269,483 bytes
P-frame:      400 – 1,017 bytes   (typically ~500–700)
```

**Roughly a 270–670× size difference** between an intra-coded frame and an
inter-coded one. This is the concrete, on-camera number behind the general
"video compression achieves 100–600×" figure — spatial-only coding (I-frame) vs.
temporal diff coding (P-frame) against the previous frame.

### GOP length — measured: **~8 seconds (~200 frames)**

I-frame timestamps from the 30-second capture:

| I-frame | `pts_time` | Δ from previous |
|---|---|---|
| 1 | N/A (first frame in capture) | — |
| 2 | 8.115000 | ~8.115s |
| 3 | 16.155000 | 8.040s |
| 4 | 24.174000 | 8.019s |

**Consistent ~8.0–8.1 second interval across three consecutive gaps** — this is
a deliberate encoder configuration, not noise. At 25 fps that's ≈200 frames per
GOP.

**This is the load-bearing finding from M0.** A decoder joining this stream at a
random point waits, in the worst case, **up to ~8 seconds** before any decodable
frame arrives — regardless of code quality, network speed, or anything on the
Unreal side. On average (uniform random join time), expect roughly **4 seconds**
of pure GOP-wait before first-frame decode even becomes possible.

### Consequence for the M1 pass criterion

The original M1 criterion specified "within 10 seconds." An 8-second GOP eats
nearly the entire budget in the worst case, leaving almost no margin for actual
connection setup, SDP negotiation, decode, format conversion, and texture upload
— all of which also take real time. **A legitimate, correctly-working spike could
fail this criterion purely on bad luck of connection timing.**

Two independent fixes, not mutually exclusive:

1. **Revise the M1 timeout** to something that has real margin over the measured
   worst case — e.g. 12–15 seconds — now that the number is measured rather than
   guessed.
2. **Lower the camera's I-frame interval** via its admin web UI (commonly
   labeled "I Frame Interval" on Dahua-style cameras, sometimes expressed as a
   multiplier of frame rate). This is worth doing regardless of M1, because the
   same 8-second blackout will recur on every reconnect once M4's transparent
   reconnect logic is running in the real demo — an 8-second freeze every time
   the feed hiccups is a real product-quality problem, not just a benchmark
   nuisance.

   Tradeoff to know before changing it: a shorter GOP means more I-frames per
   second, and I-frames are far larger than P-frames (see above) — so a shorter
   GOP raises average bitrate for the same visual quality, unless the camera's
   bitrate control is capped (CBR), in which case quality drops instead.
   **Decision deferred to Sanjyot** — needs a look at what the camera's encode
   settings page actually exposes.

---

## Substream — checked

`subtype=1` on the same URL pattern resolves to **1280×720, 25 fps, HEVC** — same
codec family as the main stream, just lower resolution. Its "I Frame Interval"
field shows the identical locked "2 seconds" as the main stream (see below).

**Deprioritized as an M1 target.** Per the corrected reasoning in Session 1 (the
codec itself doesn't reduce FFmpeg decode risk — only GOP or resolution would,
and there's no evidence this stream's GOP differs), and no independent GOP
measurement has been taken on it. Not worth the additional capture unless a
reason emerges to revisit.

## GOP mismatch — root cause found

**First check was of the NVR's web UI, not the camera's own** — this was a
mistake in following the investigation, corrected once identified. The NVR
showed "2 seconds," locked, identically on both streams; that value turned out
to be a red herring, not the actual explanation.

**Checking the camera's own UI directly revealed the real cause:** the primary
stream (`subtype=0`) runs a proprietary feature labeled **"InstaStream"** in
this camera's firmware — almost certainly this vendor's branded name for an
adaptive/smart-codec mode, matching the general hypothesis raised in Session 1.
With InstaStream active, the "I Frame Interval" field is disabled entirely —
the feature manages GOP internally and unpredictably (measured ~8.0–8.1s, not
adjustable).

**The secondary stream (`subtype=1`) does not run InstaStream**, and its
"I Frame Interval" field is a normal, editable setting, defaulting to **`50`**
— i.e. 50 frames. Confirmed by direct measurement below: this value is
trustworthy.

**Resolved. Not investigating InstaStream itself further** — the primary stream
decodes fine with plain FFmpeg (confirmed in the very first `ffprobe` capture),
so it's not an incompatible or broken codec, just a worse-behaved GOP scheme
that happens not to be the stream this project ends up using.

## Secondary stream (`subtype=1`) — full characterization

```
Stream #0:0: Video: hevc, yuv420p, 1280x720, 25 fps
```

| Property | Value |
|---|---|
| Resolution | 1280×720 |
| Codec | HEVC (no InstaStream) |
| Frame rate | 25 fps |
| I-frame interval (UI, editable) | 50 (frames) — **confirmed accurate** |

**30-second capture:** 15×I, 734×P, 0×B — 749 total frames.

**GOP measurement — 14 consecutive I-frame gaps:**

```
2.000, 2.000, 2.020, 2.000, 2.020, 2.000, 2.000,
2.020, 2.000, 2.000, 2.020, 2.000, 2.000, 2.020
```

Average 2.007s → **50.1 frames**, matching the UI's "50" value almost exactly
(the `.02`s are ordinary RTP timing jitter, not a second I-frame pattern).
**This is the cleanest, most predictable data this project has produced so
far** — a genuine contrast with the primary stream's opaque, unmeasurable-any-
other-way GOP.

**Frame sizes:** I-frames ~143,000–154,000 bytes; P-frames typically
1,000–3,500 bytes — still a clear ~40–140× intra/inter size ratio, smaller
than the primary stream's 270–670× partly because 720p has less raw pixel data
to begin with, and partly reflects this profile's own bitrate/quantization
settings.

## Recommendation: switch Phase 1's target to the secondary stream

**Decided 2026-08-22, pending Sanjyot's confirmation** (see session log). Three
reasons:

1. **Worst-case join latency drops from ~8s to ~2s** — a materially better
   number for the deliverable's own measured-latency section, and it directly
   softens the M4 reconnect-freeze concern (a 2s blip vs. an 8s one).
2. **The I-frame interval is a real, editable knob on this stream** — a
   concrete axis for showing measured improvement over time (try 50 vs. 25 vs.
   12 frames, measure join latency at each), which doesn't exist at all on the
   primary stream since InstaStream owns that decision.
3. **720p plausibly fits the "game-context, never broadcast" framing better**
   — a slightly lower-res feed reads more like an in-universe security camera
   than broadcast-quality video.

## Decisions locked from this data

- **Phase 1 target stream: secondary (`subtype=1`, 720p, HEVC, GOP=50/2.0s)** —
  pending confirmation, not yet reflected in `Architecture.md` §3.
- **M1 pass-criterion timeout** — was revised 10s→15s against the primary
  stream's ~8s worst case; now reconsidered given the secondary's ~2s worst
  case. Final number pending the stream-switch confirmation.
- **Design implication for M4, logged for later, not yet acted on:** even at
  ~2s, reconnect will still show a brief freeze — worth deciding at M4 whether
  to mask it (freeze last good frame) rather than show black.

## Confirmed — 2026-08-22

**Phase 1 targets the secondary stream.** `Architecture.md` §3 and the M1 pass
criterion (revised to 8 seconds) updated accordingly. Primary stream deferred,
not abandoned — testing it later means disabling InstaStream on the camera,
which is a shared device (accessed via NVR, at least one other active user) and
will not be touched without explicitly raising it first. See `CLAUDE.md`.

## Remaining open items

- [ ] **Parameter set delivery (in-band vs. SDP-only).** Not directly tested.
      Soft signal: both streams decode cleanly with no special flags. Not
      currently blocking; revisit only on a mysterious decode failure.
- [ ] **What InstaStream actually is** — resolved as "not our problem," not
      pursued further. The primary stream remains a documented curiosity, not
      a blocker.
