# M1 Spike — FFmpeg Code Walkthrough

This is the line-by-line "why" for the FFmpeg call sequence in
[`IPStreamM1Spike.cpp`](../Plugins/IPStreamMedia/Source/IPStreamMedia/Private/IPStreamM1Spike.cpp),
kept separate from [Architecture.md](Architecture.md) and
[Glossary.md](Glossary.md) because those cover *why the system is shaped
this way* and *what the terms mean* — this doc covers *what each line is
actually doing and why it's this call and not another one*. Built for one
purpose: being able to defend this code in an interview, not just knowing it
compiled.

Chunked deliberately — one stage of the pipeline per section, matching how
this was actually taught. Questions and follow-up clarifications get folded
into the relevant section as they come up, so this stays a clean reference
rather than a chat transcript.

---

## 1. Setup — allocating the session, wiring the timeout

```cpp
const FTCHARToUTF8 UrlUtf8(*RtspUrl);

AVFormatContext* FormatContext = avformat_alloc_context();

FInterruptState InterruptState;
InterruptState.DeadlineSeconds = FPlatformTime::Seconds() + 6.0;
FormatContext->interrupt_callback.callback = &CheckInterruptDeadline;
FormatContext->interrupt_callback.opaque = &InterruptState;
```

**`AVFormatContext` is "the session."** Every FFmpeg call from here on
operates on this one object — it's the handle representing "our attempt to
talk to this camera." Nothing has connected to anything yet at this point;
this block only builds the empty session object and configures it, before
any network activity happens.

**`avformat_alloc_context()`'s only job is to allocate that empty object and
hand you a pointer to it.** No URL, no network call — just "give me a blank
session struct, zeroed out, ready to be configured." Compare this to
`new FMyStruct()` in C++ — same idea, just a C-style allocator function
instead of `new`, because FFmpeg has no constructors to run.

**Why we need it *empty and in hand* before doing anything else:** the next
three lines fill in one field on it — `interrupt_callback` — and you can
only set a field on a struct that already exists. This is the concrete
payoff of the `void*`/function-pointer mechanics (covered separately, see
[Glossary.md §5](Glossary.md)):
`FormatContext->interrupt_callback.callback = &CheckInterruptDeadline` is
literally "here's a function to run later," and `.opaque = &InterruptState`
is literally "here's a label-less locker holding today's deadline, so that
function has something to check against when FFmpeg actually calls it."
`InterruptState.DeadlineSeconds` is set to "six seconds from right now" — a
plain wall-clock cutoff.

**What this buys us, concretely:** every blocking FFmpeg call later in this
function (`avformat_open_input`, `av_read_frame`) periodically calls
`CheckInterruptDeadline` internally, mid-operation, and if it returns
non-zero (more than 6 seconds have passed since we started), FFmpeg
abandons whatever it was blocked on and returns an error instead of hanging
forever. Without this block, a wrong URL or a dead camera doesn't fail — it
**freezes the entire editor**, because this whole function runs on the game
thread. This is the one piece of real robustness in an otherwise disposable
spike, and it's here specifically because the alternative (no timeout)
makes the spike untestable — not because M1 asks for reconnect/error-
handling polish.

*Confirmed understood 2026-09-09.*

---

## 2. Opening the connection — the options dictionary, `avformat_open_input`

```cpp
AVDictionary* Options = nullptr;
av_dict_set(&Options, "rtsp_transport", "tcp", 0);
av_dict_set(&Options, "probesize", "32768", 0);
av_dict_set(&Options, "analyzeduration", "0", 0);

if (avformat_open_input(&FormatContext, UrlUtf8.Get(), nullptr, &Options) < 0)
{
    UE_LOG(LogIPStreamMedia, Error, TEXT("M1 spike: failed to open %s"), *RtspUrl);
    av_dict_free(&Options);
    return nullptr; // avformat_open_input already freed FormatContext on this path
}
av_dict_free(&Options);
```

**`AVDictionary` is FFmpeg's generic options bag — a key/value map of
strings.** Same root cause as everything else that's felt unfamiliar here: C
has no keyword arguments, no per-caller-varying default parameters in the
C++ sense, and no clean way to hand a function "a flexible, growable set of
named settings." A string-to-string dictionary is the C-era answer: instead
of `avformat_open_input(url, rtsp_transport, probesize, analyzeduration,
...)` with a hundred rarely-used optional parameters bolted onto every call
site that ever needs one of them, you build a small bag of only the settings
*you* care about, and hand the whole bag in as one argument.

**Why `av_dict_set` takes `&Options` (a pointer to our pointer), not just
`Options`:** the exact same shape as `avformat_open_input(&FormatContext,
...)` below, for the same reason — adding an entry to a dictionary can
require growing it in memory, meaning it can end up living at a different
address than before. `av_dict_set` needs the ability to update *our own
local variable* `Options` to point at the new location if that happens.
Handing it plain `Options` would let it edit what's *at* that address, but
not repoint our variable itself if the whole thing moved. `&Options` gives
it write-access to the variable, not just to what the variable currently
points at.

**Why these three specific options, not others:** all three exist to fight
FFmpeg's defaults, which are tuned for reading files, not joining a live
stream fast.
- `rtsp_transport=tcp` — forces RTP-over-TCP instead of the default UDP.
  UDP is marginally lower latency but can drop or reorder packets; TCP is
  reliable. For a spike that has to prove "one recognizable frame within 8
  seconds," reliability wins over shaving milliseconds.
- `probesize=32768` — how many *bytes* FFmpeg reads before deciding it
  understands the stream well enough to proceed. The library default is
  sized for large on-disk files where reading extra megabytes up front is
  free; on a live join, that same default adds real, visible startup
  delay for no benefit.
- `analyzeduration=0` — the time-based equivalent of the above: don't spend
  extra wall-clock time analyzing beyond the bare minimum.

Together: get to "I understand this stream" as fast as possible, at the
cost of trusting the stream's own claims about itself slightly more than a
cautious file-reader would.

**`avformat_open_input`'s four arguments, in order:**
1. `&FormatContext` — our pre-allocated session from §1, passed the same
   pointer-to-pointer way as `Options`, for the same underlying reason
   (even though *this specific call* won't reallocate it, since we already
   gave it a real object — the function's signature has to support both
   "you gave me `NULL`, I'll allocate one" and "you gave me a real one,
   I'll use it," so it always takes the address).
2. `UrlUtf8.Get()` — the URL, as the `char*` FFmpeg's C API wants.
3. `nullptr` for the format — "auto-detect what kind of input this is"
   rather than forcing a specific demuxer.
4. `&Options` — handed in, and handed back mutated: FFmpeg removes every
   option it actually recognized and consumed, leaving behind only ones it
   didn't understand. That's why `av_dict_free(&Options)` appears on
   **both** the failure and success paths — we still own whatever's left in
   that bag either way, and it's never freed for us automatically the way
   `FormatContext` is on the failure path.

**The `< 0` check is FFmpeg's universal convention, not specific to this
call:** every function in the library that can fail returns a negative
`AVERROR` code on failure and zero-or-positive on success. This exact
pattern (`if (SomeCall(...) < 0)`) repeats for essentially every FFmpeg call
in the rest of this file — one rule learned once, not six conventions to
memorize.

**Why the failure branch skips `avformat_close_input`:** verified against
the header comment earlier — on failure, `avformat_open_input` has
*already* freed `FormatContext` itself and set our pointer to `nullptr`.
Calling `avformat_close_input` again here would operate on something
already gone.

*Confirmed understood 2026-09-09.*

---

## 3. Finding the video stream — `avformat_find_stream_info`, the `streams[]` loop

```cpp
if (avformat_find_stream_info(FormatContext, nullptr) < 0)
{
    UE_LOG(LogIPStreamMedia, Error, TEXT("M1 spike: failed to read stream info"));
    avformat_close_input(&FormatContext);
    return nullptr;
}

int32 VideoStreamIndex = -1;
for (uint32 Index = 0; Index < FormatContext->nb_streams; ++Index)
{
    if (FormatContext->streams[Index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        VideoStreamIndex = Index;
        break;
    }
}
if (VideoStreamIndex == -1)
{
    UE_LOG(LogIPStreamMedia, Error, TEXT("M1 spike: no video stream in %s"), *RtspUrl);
    avformat_close_input(&FormatContext);
    return nullptr;
}
```

**Why this is a separate step from `avformat_open_input`, not folded into
it:** opening the session only negotiates *that* a connection exists — for
RTSP specifically, that's the DESCRIBE/SETUP handshake, getting back the SDP
(session description). It doesn't necessarily dig deep enough to know
everything about each stream's codec in detail. `avformat_find_stream_info`
is the step that goes further — it may read and buffer some actual packets
to fill in details the handshake alone didn't give it. This is the same
knob as `probesize`/`analyzeduration` from §2: "how much do I read before I
claim to understand this" is a tunable depth, and FFmpeg deliberately splits
"connect" and "understand the streams" into two calls so that depth is
controllable independently of the connection itself. The second parameter,
`nullptr`, would let you pass per-stream tuning options here (one
dictionary per stream) — not needed, so `nullptr`.

**`FormatContext->nb_streams` and `FormatContext->streams[]`:** a session
can carry more than one elementary stream at once. `nb_streams` is how many
exist; `streams[]` is the array of them. This project's own camera is a
concrete reason you can't just assume index `0` is the video: **the PCM
A-law audio track already documented in `TestSource.md` is a second stream
sitting in this exact array**, alongside the video one — this loop is
precisely the code that skips over it.

**`codecpar` vs. an actual decoder — worth being precise about:**
`AVCodecParameters` (verified against `codec_par.h`) is *descriptive
metadata about a stream* — codec ID, dimensions, and so on — more like a
spec sheet than a working thing. It cannot decode anything by itself. The
actual machine that turns compressed packets into pictures is a separate
object, built *from* this metadata — that's §4. Right now, we're only
reading the spec sheet to figure out which stream is worth building that
machine for.

**`codec_type == AVMEDIA_TYPE_VIDEO`:** a stream's `codec_type` says what
*kind* of stream it is — video, audio, subtitles, data. Loop until the
video one is found, record its index, `break` — this spike only ever wants
the first (and here, only) video stream.

**The second failure check (`VideoStreamIndex == -1`):** if the loop never
finds a video stream, `VideoStreamIndex` stays `-1`. Failing cleanly here
matters because §4 is about to use this index to index into `streams[]` —
using `-1` as an array index there wouldn't fail loudly, it would just read
memory that isn't a valid stream, a far worse bug to chase than a clean
early return.

*Confirmed understood 2026-09-09.*

---

## 4. Opening the decoder — `avcodec_find_decoder` through `avcodec_open2`

```cpp
AVCodecParameters* CodecParams = FormatContext->streams[VideoStreamIndex]->codecpar;
const AVCodec* Decoder = avcodec_find_decoder(CodecParams->codec_id);
if (!Decoder)
{
    UE_LOG(LogIPStreamMedia, Error, TEXT("M1 spike: no decoder for this codec"));
    avformat_close_input(&FormatContext);
    return nullptr;
}

AVCodecContext* CodecContext = avcodec_alloc_context3(Decoder);
avcodec_parameters_to_context(CodecContext, CodecParams);

if (avcodec_open2(CodecContext, Decoder, nullptr) < 0)
{
    UE_LOG(LogIPStreamMedia, Error, TEXT("M1 spike: failed to open decoder"));
    avcodec_free_context(&CodecContext);
    avformat_close_input(&FormatContext);
    return nullptr;
}
```

**A distinction worth being exact about — `AVCodec` vs. `AVCodecContext`.**
`avcodec_find_decoder(CodecParams->codec_id)` takes the codec ID off our
stream's spec sheet (e.g. "this is HEVC") and looks up which decoder
*implementation* FFmpeg has registered for it. What comes back, `const
AVCodec*`, is not something with any state — more like a type descriptor,
the same relationship a `UClass*` has to an actual spawned object. It just
says "here is the HEVC-decoding algorithm," shared and stateless, one per
codec type in the whole process.

That distinction matters because of something already in the glossary:
*"P-frame — compressed as a difference from a previous frame."*
Reconstructing a P-frame requires remembering the previous frame — that's
**state**, and state can't live on the shared, stateless `AVCodec`. It has
to live on a separate, per-stream *instance*. That instance is
`AVCodecContext`, and the rest of this block is building one.

**Why check `!Decoder`:** it can come back null if this FFmpeg build wasn't
compiled with support for that particular codec. This is exactly the kind
of check that catches a real, already-known project risk — the camera
outputs HEVC, and the pinned FFmpeg build's HEVC support was specifically
verified back in `TestSource.md`/M0. If a future rebuild of the FFmpeg
binaries ever dropped HEVC support, this is the clean, loud failure point
that would catch it, instead of a null-pointer crash three calls later with
no useful message.

**`avcodec_alloc_context3(Decoder)` — now we build the actual instance,**
tied to that specific decoder implementation. Same `NewObject<T>(SomeClass)`
-shaped idea as before: allocate an empty instance of "the HEVC decoder,"
nothing configured yet.

**`avcodec_parameters_to_context(CodecContext, CodecParams)`** copies the
spec-sheet data from §3 — resolution, codec ID, and things like the SPS/PPS
parameter sets already covered in the glossary — *into* that fresh
instance. Filling in configuration before initialization, the same shape
as setting properties on an object before calling its own `Init()`.

**`avcodec_open2(CodecContext, Decoder, nullptr)`** is that `Init()` — given
everything just copied in, actually set up internal decode buffers and
state, ready to accept packets. The `nullptr` is another options-dictionary
slot, same idea as §2's, unused here.

**Worth naming explicitly: this is the third time the same shape has shown
up.** Allocate empty → fill in configuration → "open" to actually activate
it. `AVFormatContext` did exactly this in §1–2 (`avformat_alloc_context` →
set `interrupt_callback` → `avformat_open_input`); `AVCodecContext` just did
it again. Not six unrelated API designs to memorize — one FFmpeg idiom,
applied twice.

**Cleanup order on the failure path:** `avcodec_free_context(&CodecContext)`
runs *before* `avformat_close_input(&FormatContext)` — free what was
acquired last, first. Same reverse-order-unwind principle from the
DLL-unloading code in `IPStreamMediaModule.cpp`, not a new rule.

*Confirmed understood 2026-09-09.*

---

## 5. The decode loop — `av_read_frame`, `avcodec_send_packet`/`avcodec_receive_frame`

```cpp
AVPacket* Packet = av_packet_alloc();
AVFrame* Frame = av_frame_alloc();
UTexture2D* ResultTexture = nullptr;

while (av_read_frame(FormatContext, Packet) >= 0)
{
    if (Packet->stream_index == VideoStreamIndex &&
        avcodec_send_packet(CodecContext, Packet) == 0 &&
        avcodec_receive_frame(CodecContext, Frame) == 0)
    {
        ResultTexture = BuildTextureFromFrame(Frame);
        av_packet_unref(Packet);
        break;
    }
    av_packet_unref(Packet);
}
```

**`av_packet_alloc()` / `av_frame_alloc()` — two more empty containers, but
reused across every loop iteration, not allocated fresh each time.**
`AVPacket` (one chunk of still-compressed data) and `AVFrame` (one decoded
picture) are already in the glossary; what's new here is the lifecycle: real
playback would run this loop dozens of times a second, and allocating/
freeing a packet object on every single one would be wasteful. So the
pattern is: allocate once, then wipe-and-refill the same object every
iteration. M1 only needs one frame, but the loop is shaped the way a real
decode loop is shaped, for exactly this reason.

**`while (av_read_frame(FormatContext, Packet) >= 0)`** pulls the next
packet off the network into `Packet`, reusing it each time. This is the
actual blocking network read the interrupt callback from §1 exists to
bound — if the connection dies mid-loop, this is the call that would
otherwise hang forever.

**`Packet->stream_index == VideoStreamIndex`** — the session can carry more
than one stream interleaved on the wire, and this camera specifically does:
the PCM A-law audio track from `TestSource.md` arrives mixed in with the
video packets. This check is the literal line that discards every audio
packet — "demux," made concrete.

**Why decoding is two calls, `avcodec_send_packet` then
`avcodec_receive_frame`, not one:** checked both doc comments directly.
`avcodec_send_packet`'s says the packet is always fully consumed, but "if it
contains multiple frames... will require you to call
`avcodec_receive_frame()` multiple times afterwards" — and can return
`AVERROR(EAGAIN)` meaning "I'm not ready to accept more input, drain output
first." `avcodec_receive_frame`'s comment has the mirror image: `EAGAIN`
there means "no output yet, send more input first." Packets and frames
genuinely don't correspond 1:1, in *either* direction — a decoder can need
several packets before it produces its first frame (the buffering the
glossary already flags for B-frames and reference frames), and can
occasionally need to be drained of more than one frame per packet. Splitting
"feed" and "fetch" into two separate calls is FFmpeg's way of exposing that
back-and-forth honestly instead of forcing it through one call that can't
represent "not yet."

**Why this loop checks `== 0` specifically, not the general `< 0` failure
convention from earlier — a deliberate simplification, not an
inconsistency:** `EAGAIN` from either call isn't really a *failure* — it's
"try again, not ready yet" — so a production decode loop would check for it
specifically and keep feeding packets without treating it as an error. This
spike doesn't make that distinction: anything other than a clean `0` from
either call just means "this iteration didn't produce a frame, read another
packet and try again," lumping genuine errors in with "not ready yet." Fine
for a one-shot "get exactly one frame" spike — a continuous M2+ decode loop
would need to tell the two apart, since a real error shouldn't be retried
forever the way `EAGAIN` should.

**`ResultTexture = BuildTextureFromFrame(Frame); ... break;`** is where "one
frame passes, no continuous playback" — the M1 scope boundary from
`Architecture.md` — physically lives in the code. The moment one frame
decodes successfully, the loop stops.

**`av_packet_unref(Packet)` on both the success path and the fall-through
path** — checked its doc comment directly: *"Unreference the buffer
referenced by the packet and reset the remaining packet fields to their
default values."* It wipes the packet's contents back to blank without
destroying the `AVPacket` struct itself — the reuse pattern from the top of
this section: allocate once before the loop, wipe-and-refill every
iteration via `unref` (this call) followed by the next `av_read_frame`, and
only truly free the struct once, after the loop — where §6 picks up.

*Confirmed understood 2026-09-09.*

---

## 6. Cleanup — the four free/close calls at the end

```cpp
av_frame_free(&Frame);
av_packet_free(&Packet);
avcodec_free_context(&CodecContext);
avformat_close_input(&FormatContext);

return ResultTexture;
```

**Four calls, matching the four things allocated across the whole
function** — `Frame` (§5), `Packet` (§5), `CodecContext` (§4),
`FormatContext` (§1). Every `alloc` gets exactly one matching teardown call
here.

**The order is reverse of acquisition — the third time this exact principle
has shown up**, after the DLL-handle unloading in `IPStreamMediaModule.cpp`
and the decoder-then-format cleanup in §4's failure path. `Frame`/`Packet`
were allocated last (§5), so freed first; `CodecContext` (§4) next;
`FormatContext` (§1) — the very first thing allocated — last. A real,
general convention at this point, not three separate coincidences.

**`av_frame_free(&Frame)` / `av_packet_free(&Packet)` are the *real*
teardown**, not the wipe-and-reuse from `av_packet_unref` in §5. These
destroy the struct itself and null our pointer — same double-pointer
reasoning as everywhere else in this file.

**`avcodec_free_context(&CodecContext)`** tears down the decoder instance
and all its internal state. **`avformat_close_input(&FormatContext)`**
tears down the session — closes the network connection, frees every
`AVStream` living inside it. Freed last because it's the "outermost" thing
everything else was built from.

**A structural pattern worth being explicit about — cleanup looks different
in different places in this file, and that's deliberate, not
inconsistent.** Every early-return in §2–§4 frees only what had actually
been allocated *by that point* — the §2 open-failure branch never touches
`CodecContext`, because it doesn't exist yet there. This block is
different: not another early-return, but the single unconditional cleanup
that runs after the loop, at a point where all four objects are guaranteed
to exist — reached whether the loop found a frame (`break`) or ran out of
packets without one (§1's 6-second deadline firing, or the stream ending).
If asked "why not a `goto cleanup` calling everything unconditionally from
every failure branch," the answer is: you can't unconditionally free
something that was never allocated yet on that particular path.

**`return ResultTexture`** is either the real texture, or `nullptr` — the
same `nullptr`-means-failure contract the header's doc comment already
promises, reached by an early return (§2–§4) or by falling out of the loop
having never decoded a frame in time.

*Confirmed understood 2026-09-09.*

---

## 7. `BuildTextureFromFrame` — converting the frame to BGRA

```cpp
SwsContext* ScaleContext = sws_getContext(
    Frame->width, Frame->height, static_cast<AVPixelFormat>(Frame->format),
    Frame->width, Frame->height, AV_PIX_FMT_BGRA,
    SWS_BILINEAR, nullptr, nullptr, nullptr);
if (!ScaleContext)
{
    UE_LOG(LogIPStreamMedia, Error, TEXT("M1 spike: sws_getContext failed"));
    return nullptr;
}

const int32 DestStride = Frame->width * 4;
TArray<uint8> BgraBuffer;
BgraBuffer.SetNumUninitialized(DestStride * Frame->height);

uint8* DestPlanes[1] = { BgraBuffer.GetData() };
int32 DestStrides[1] = { DestStride };
sws_scale(ScaleContext, Frame->data, Frame->linesize, 0, Frame->height, DestPlanes, DestStrides);
sws_freeContext(ScaleContext);
```

**The problem this solves:** the decoder hands back YUV420P (three separate
planes, per the glossary). A GPU texture needs packed BGRA — one interleaved
buffer, four bytes per pixel. This is the CPU pixel-format conversion
`Architecture.md` calls out explicitly as M1's scope.

**`sws_getContext(...)` is a third kind of "context" object, but it *doesn't*
follow the alloc→configure→open pattern from §1 and §4 — worth noticing the
difference, not just the similarity.** `AVFormatContext` and
`AVCodecContext` both needed a separate later step to activate, because
there was more information to gather first (a network handshake, stream
metadata). `sws_getContext` takes everything it needs as plain arguments in
one call — source width/height/format, destination width/height/format, an
algorithm flag, and three `nullptr`s for optional tuning not needed here —
and comes back fully ready to use immediately. No second "open" call exists
for it. Same idiom family, but not every member needs every step.

Two things worth being precise about in the parameter list: source and
destination width/height are **identical** — this call does pure format
conversion, not resizing, even though "sws" (software scale) can do both.
`SWS_BILINEAR` is a resampling filter choice that only matters when
resolution changes; it's supplied because the parameter is required, not
because it does meaningful work in this specific call.

**`DestStride = Frame->width * 4`** is the glossary's own "Stride (pitch)"
entry made concrete: bytes-per-row for a packed BGRA buffer is width × 4
bytes-per-pixel, no padding, since we're defining our own buffer's layout
rather than reading someone else's.

**`BgraBuffer.SetNumUninitialized(...)`, deliberately not zeroed** —
`sws_scale` is about to overwrite every byte, so zero-filling first would be
wasted work. Correct specifically because nothing ever reads from it before
`sws_scale` writes to it.

**`uint8* DestPlanes[1]` and `int32 DestStrides[1]` — arrays of size *one*,
and that's the interesting part.** `sws_scale`'s signature is
general-purpose: it has to support planar destination formats too
(converting back *to* YUV420P, say), which would need multiple plane
pointers and strides, one per plane. Our destination, BGRA, is packed — a
single interleaved plane — so the array only ever needs one slot. The
"Planar / semi-planar / packed" distinction from the very first section of
the glossary is the reason this parameter is shaped as an array at all.

**`sws_scale(ScaleContext, Frame->data, Frame->linesize, 0, Frame->height,
DestPlanes, DestStrides)`:** `Frame->data` is the *source's* version of the
same plane-pointers-array idea — three entries for YUV420P (Y, U, V),
already populated by the decoder. `Frame->linesize` is the source's
per-plane stride array — and per-plane matters, because in 4:2:0 the chroma
planes are half-width, so their stride genuinely differs from luma's. `0`
and `Frame->height` say "process every row, starting from the top, all in
one call" — `sws_scale` supports partial row-ranges (useful for streaming
large images through in chunks), not needed here since one whole frame
converts in one call.

**`sws_freeContext(ScaleContext)`** — no matching "close" step needed
beforehand, consistent with there having been no "open" step either.

*Confirmed understood 2026-09-09.*

## 8. `BuildTextureFromFrame` — writing the pixels into a `UTexture2D`

```cpp
UTexture2D* Texture = UTexture2D::CreateTransient(Frame->width, Frame->height, PF_B8G8R8A8);
FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
FMemory::Memcpy(MipData, BgraBuffer.GetData(), BgraBuffer.Num());
Mip.BulkData.Unlock();
Texture->UpdateResource();
```

Not deep-walked to the same level as the FFmpeg sections above — this is
ordinary UE API, already familiar territory. Briefly: `CreateTransient`
(checked against the engine source directly) only allocates Mip 0's CPU-side
buffer, uninitialized — it never touches the GPU. Locking that mip,
`memcpy`-ing the converted BGRA buffer in, and unlocking fills it with real
pixels; `UpdateResource()` is the call that actually uploads it and makes
the texture live. `PF_B8G8R8A8` is `CreateTransient`'s own default and
matches `AV_PIX_FMT_BGRA` byte-for-byte, which is why no further format
translation happens between §7's output buffer and this one.

---

## Summary — the whole function in one pass

1. **Setup** — allocate an empty session, wire a 6-second deadline into it
   so nothing below can hang the editor.
2. **Open** — set fast-join options, connect, hand back ownership of
   whatever options weren't understood.
3. **Find the video stream** — the session can carry more than one stream
   (this camera's audio track included); search for the video one.
4. **Open the decoder** — look up the implementation for this stream's
   codec, build a stateful instance of it, configure it from the stream's
   metadata, activate it.
5. **Decode loop** — pull packets, discard non-video ones, feed the
   decoder, stop the moment one frame comes out.
6. **Cleanup** — free everything allocated, in reverse order, unconditionally,
   once every object is guaranteed to exist; return the texture or `nullptr`.
7. **Convert** (`BuildTextureFromFrame`) — CPU color-convert the one decoded
   frame from YUV420P to packed BGRA.
8. **Upload** (`BuildTextureFromFrame`) — write those BGRA bytes into a new
   `UTexture2D` and push it live.

Three FFmpeg idioms recur throughout, once each is learned: **alloc → configure
→ open** (shows up for the session and the decoder, but *not* for the
swscale context — §7 flags exactly why not), **the `< 0` = error convention**
(every fallible call in this file), and **reverse-order teardown** (DLL
handles, decoder-then-session, and the final cleanup block).

