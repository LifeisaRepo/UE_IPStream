# Latency

How long a frame takes from the source to the screen, and how much of that is the plugin.

All numbers are from a packaged Development build (`v0.1.3`, UE 5.3, Win64), windowed, on one PC.

## Where the time goes

I put timestamps on each frame at four points and recorded the differences in Unreal Insights:

| Point | Where |
|---|---|
| T1 | `av_read_frame` hands us the packet (decode thread) |
| T2 | the decoder gives back the picture (decode thread) |
| T3 | the sample goes into the Media Framework's queue (decode thread) |
| T4 | the render thread asks for the pixels to upload them to the GPU |

T1 travels through the decoder on the packet itself (`AV_CODEC_FLAG_COPY_OPAQUE`), so it stays with the right picture even when the decoder reorders frames.

Source: OBS 30.2.3 with the RTSP Server plugin 3.1.0, on the same PC. x264 `veryfast`, CBR 2500 kb/s, keyframe every 1 s, 1280x720, 25 fps. Medians, in ms:

| | No B-frames | With B-frames |
|---|---|---|
| Decode + reorder (T2 - T1) | 0.8 | 77.6 |
| Convert to BGRA + queue (T3 - T2) | 0.4 | 0.5 |
| Waiting for the engine to pick it up (T4 - T3) | 8.9 | 8.6 |
| **Plugin total (T4 - T1)** | **10.1** | **87.9** |

So the plugin does about 1.2 ms of work per frame. The rest of its 10 ms is the frame waiting for the next game tick and the render thread, which is engine time, not plugin time.

B-frames cost about 80 ms on the decoding side. The decoder has to hold 2 pictures back to put them in display order (`has_b_frames = 2`), and at 25 fps that's 2 x 40 ms. With B-frames off it reports 0 and the delay goes away. This comes from how the stream is encoded. The plugin can't remove it.

## Source to screen

Method: a web page shows the PC clock in milliseconds (`Tools/LatencyClock/index.html`). OBS captures that window and streams it, and the game shows the stream right next to it. A screenshot holds both clocks, so there's only one clock involved. Latency = real clock - streamed clock.

| OBS x264 settings | Screenshots | Median | Range |
|---|---|---|---|
| tune none, B-frames on | 11 | 1500 ms | 1483-1517 |
| tune none, B-frames off | 23 | 1300 ms | 1266-1317 |
| **tune zerolatency, B-frames off** | 11 | **217 ms** | 200-234 |

Out of the 217 ms, about 10 ms is the plugin (from the trace above, same build and stream type; that run itself wasn't traced). The rest is OBS capturing at 25 fps, encoding and serving the stream, then Unreal drawing and presenting the frame.

OBS's default x264 tune held frames for more than a second. Changing it to `zerolatency` was the biggest single change.

B-frames added about 200 ms end to end, of which about 80 ms is the decoder (above). The rest is on the encoder side.

### The camera

Earlier I measured my IP camera (H.265, 720p, 25 fps) at about **250 ms** from camera to screen (25 screenshots, median 248 ms; reading the small digits leaves about ±25 ms of doubt). That used two synced clocks, a phone in front of the camera and the PC, so it's less precise than the OBS method. I haven't split that one into stages yet.

## What's not in these numbers

- The monitor's own delay. A screenshot captures what Windows sends to the screen, before the monitor shows it.
- Readings move in steps of one 60 Hz redraw (16.7 ms), and the streamed clock only changes once per stream frame (40 ms). That's why I give medians.

## Doing it yourself

- Packaged Development build (tracing is compiled out of Shipping), run with
  `-trace=default,counters -tracefile=<path>.utrace`
- Open the trace in Unreal Insights. The counters are under `IPStream/` (`DecoderDelayMs`, `ConvertMs`, `QueueWaitMs`, `OursMs`), and the decode thread has the scopes `IPStream_Read`, `IPStream_Send`, `IPStream_Receive` and `IPStream_HandOff`.
- The log prints the decoder's `has_b_frames` and thread settings on the first frame and every 250 frames.
- For source to screen: open `Tools/LatencyClock/index.html`, have OBS capture only that window (not the whole screen), put the game next to it and take screenshots.
