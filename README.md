# IPStreamMedia

An Unreal Engine 5.3 plugin for playing live RTSP camera streams through Unreal's Media Framework, with FFmpeg doing the decoding. The end goal is a live camera feed on an in-game monitor.

This is a work in progress. Right now the plugin connects to a camera, decodes the video on its own thread and reconnects if the stream drops. The frames don't reach a texture yet, so there's nothing to see in game. That's what I'm working on next.

I'm writing about the whole process as I go. Start here: [IPStreamMedia #1](https://sanjyotdahale.dev/devlogs/2026/08/23/ipstream_1.html)

Windows only for now.

## License

My code is MIT, see [LICENSE.md](LICENSE.md). FFmpeg is LGPL v3 and ships with its own license and notice in [Plugins/IPStreamMedia/ThirdParty/FFmpeg](Plugins/IPStreamMedia/ThirdParty/FFmpeg/).
