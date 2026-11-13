# LeakHarness

A small console program that runs IPStreamMedia's FFmpeg calls with no Unreal around them, and prints its own memory every 5 seconds.

I wrote it while chasing a memory leak. The packaged game grew by about 10 MB a minute, and I needed to know whether that came from how the plugin calls FFmpeg or from running inside Unreal. The harness opens the stream the same way the plugin does (same options, same timeouts, same interrupt callback) and reads packets on a thread started the way Unreal starts one. It stayed flat. The leak turned out to be Unreal and the NVIDIA driver in Development builds, not the plugin (see the main README).

## Build

```
Tools\LeakHarness\build.bat
```

Needs Visual Studio with the C++ tools (I used 2022). It builds against the plugin's own FFmpeg headers and libraries, into `Tools\LeakHarness\Build\`.

## Run

```
Tools\LeakHarness\run.bat "<rtsp url>" [seconds] [--decode] [--timer1ms]
```

- `seconds`: how long to run, default 300.
- `--decode`: decode the video as well. Without it, packets are read and thrown away.
- `--timer1ms`: ask Windows for a 1 ms timer, as Unreal does at start-up.

It uses the plugin's own FFmpeg DLLs and prints which ones it loaded. The URL is never printed, since a camera URL can contain a password.

The output is one CSV line per sample (`seconds,private_mb,packets,frames`), then the slope of the memory line in MB a minute.

> Vibe-coded using Claude Opus
