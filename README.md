# IPStreamMedia

An Unreal Engine 5.3 plugin for playing live RTSP camera streams through Unreal's Media Framework, with FFmpeg doing the decoding. The end goal is a live camera feed on an in-game monitor.

Still a work in progress, but the core works now. You give a normal Media Player an `rtsp://` URL with IPStreamMedia as its player, and the live video plays on a Media Texture like any other media. Decoding runs on its own thread, it tries to reconnect if the stream drops, and the usual Media Player events fire (opened, failed, suspended, resumed, closed). Blueprint can also read the video's size, frame rate and codec.

A first rough measurement puts it at about 250 ms from camera to screen on my setup. I'll write up the method once I've repeated it properly.

Packaged demo and plugin: [v0.1.2-beta](https://github.com/LifeisaRepo/UE_IPStream/releases/tag/v0.1.2-beta)

Known issues:
- In Development builds and the editor, on NVIDIA cards, memory grows by about 10 MB a minute, stream or no stream. It turned out not to be the plugin: the graphics driver holds on to Unreal's PIX profiling markers while D3D12's lightweight DRED is on. Shipping builds are fine, and in a Development build `-dpcvars=r.D3D12.LightweightDRED=0` avoids it.
- Tested with one camera (H.265, 720p, 25 fps) and OBS (H.264, 720p, 25 fps) so far.

I'm writing about the whole process as I go. Start here: [IPStreamMedia #1](https://sanjyotdahale.dev/devlogs/2026/08/23/ipstream_1.html)

Windows only for now.

## License

My code is MIT, see [LICENSE.md](LICENSE.md). FFmpeg is LGPL v3 and ships with its own license and notice in [Plugins/IPStreamMedia/ThirdParty/FFmpeg](Plugins/IPStreamMedia/ThirdParty/FFmpeg/).
