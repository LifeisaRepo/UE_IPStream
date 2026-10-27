# IPStreamMedia

An Unreal Engine 5.3 plugin for playing live RTSP camera streams through Unreal's Media Framework, with FFmpeg doing the decoding. The end goal is a live camera feed on an in-game monitor.

Still a work in progress, but the core works now. You give a normal Media Player an `rtsp://` URL with IPStreamMedia as its player, and the live video plays on a Media Texture like any other media. Decoding runs on its own thread, it tries to reconnect if the stream drops, and the usual Media Player events fire (opened, failed, suspended, resumed, closed). Blueprint can also read the video's size, frame rate and codec.

A first rough measurement puts it at about 250 ms from camera to screen on my setup. I'll write up the method once I've repeated it properly.

Packaged demo and plugin, a bit behind the code: [v0.1-beta](https://github.com/LifeisaRepo/UE_IPStream/releases/tag/v0.1-beta)

Known issues:
- Memory grows by a few MB a minute while a stream is open. It looks like it's in FFmpeg's packet reading, and I've parked it for now.
- Tested with one camera so far (H.265, 720p, 25 fps).

I'm writing about the whole process as I go. Start here: [IPStreamMedia #1](https://sanjyotdahale.dev/devlogs/2026/08/23/ipstream_1.html)

Windows only for now.

## License

My code is MIT, see [LICENSE.md](LICENSE.md). FFmpeg is LGPL v3 and ships with its own license and notice in [Plugins/IPStreamMedia/ThirdParty/FFmpeg](Plugins/IPStreamMedia/ThirdParty/FFmpeg/).
