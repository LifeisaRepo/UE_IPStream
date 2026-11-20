// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

#include <atomic>

extern "C"
{
#include "libavutil/channel_layout.h"
}

// Forward declarartions instead of includes
// Apart from the usual reasons, avformat.h pulls a large tree; keeping it out of
// a header keeps it from being re-parsed everywhere
struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;
struct SwrContext;

/**
* @brief Outcome of one FIPStreamFFmpegSession::DecodeNext() call.
* The worker loop branches on this.
*/
enum class EIPStreamDecodeResult : uint8
{
	// A decoded video frame is ready for GetFrameInfo() and ConvertFrameToBGRA(), until the next DecodeNext().
	GotFrame,

	// A decoded audio frame is ready for GetAudioInfo() and ConvertAudioToFloat(), until the next DecodeNext()
	GotAudio,

	// Packets were fed in, no frames came out yet. Normal. Call again.
	NoFrameYet,

	// RequestStop() was seen. Unwind; Do not retry, do not reconnect.
	Aborted,

	// The stream ended or the connection dropped. Close, then reconnect.
	StreamEnded,

	// Read or decode failed in a way retrying won't fix. Close, then reconnect.
	Error
};

/**
 * @brief Metadata of the decoded frame that the worker needs to know about, saved in Unreal types.
 */
struct FIPStreamFrameInfo
{
	FIntPoint Dim = FIntPoint(0, 0);
	FTimespan Time = FTimespan::Zero();
	FTimespan Duration = FTimespan::Zero();
	
	// Used for profiling
	uint64 PacketCycles = 0;
};

/**
 * @brief Metadata of the decoded audio frame, saved in Unreal types.
 */
struct FIPStreamAudioInfo
{
	int32 Channels = 0;
	int32 SampleRate = 0;

	int32 Frames = 0;

	FTimespan Time = FTimespan::Zero();
};

/**
 * @brief Owns one FFmpeg connection to one stream: demux, decode, and the
 * interrupt state that makes both of them interruptible.
 * @brief ---
 * @brief THREAD AFFINITY: while the worker thread runs, every member is
 * worker-thread only, except RequestStop() and IsStopRequested(), which any
 * thread may call (hence bStopRequested is atomic). Before the thread starts
 * and after it has been joined, the owning thread may use it.
 */
class FIPStreamFFmpegSession
{
public:
	FIPStreamFFmpegSession();
	~FIPStreamFFmpegSession();

	// Ensures that this type of object cannot be copied or moved
	UE_NONCOPYABLE(FIPStreamFFmpegSession)

	// Connects and opens the decoder. Blocking but interruptible
	bool Open(const FString& Url);

	// One step of demux/decode loop. Blocking but interruptible
	EIPStreamDecodeResult DecodeNext();

	// Frees everything that Open() allocates. Safe to call when already closed
	void Close();

	bool IsOpen() const;

	// Valid only after DecodeNext() returned GotFrame, until the next call
	bool GetFrameInfo(FIPStreamFrameInfo& OutInfo);

	// Converts the current frame to BGRA straight into Dst. DstStrides is the number of bytes per row.
	// Valid only after DecodeNext() returned GotFrame, until the next call
	bool ConvertFrameToBGRA(uint8* Dst, int32 DstStride);

	// Valid only after DecodeNext() returned GotAudio, until it's next call
	bool GetAudioInfo(FIPStreamAudioInfo& OutInfo);

	/**
	 * @brief Converts the current audio frame to interleaved float straight into Dst, which
	 * has room for MaxFrames x Channels floats. Valid only after DecodeNext() returned GotAudio(), until the next call
	 * @param Dst pointer to the first float
	 * @param MaxFrames how much room Dst has, per channel
	 * @return Frames written per channel; <= 0 means nothing usable.
	 */
	int32 ConvertAudioToFloat(float* Dst, int32 MaxFrames);

	// True while the current connection has a working audio decoder
	bool HasAudio() const;

	// Name of the stream's codec, e.g. 'hevc'. Empty when closed.
	FString GetCodecName() const;

	// Name of the audio codec, e.g. 'aac'. Empty when there's no audio
	FString GetAudioCodecName() const;

	// Profiling
	FString GetDecoderDelayInfo();
	
	//~ Thread boundary - Following two functions are callable from any thread

	void RequestStop();
	bool IsStopRequested() const;

private:

	// The AVIOInterruptCB entry point
	static int InterruptCallback(void* Opaque);

	// Sets the time for which the InterruptCB will wait before interrupting a blocking call
	void SetDeadline(double TimeoutSeconds);

	// Finds and opens the audio decoder. On failure: Warning, and the connection stays video-only
	void OpenAudioDecoder();

	// Logs the first RTCP sender report seen on each stream. Once per connection
	void NoteSenderReport(int32 StreamIndex);

	// Handle a bad audio packet or frame
	void DropAudioPacket(const TCHAR* Where, int ReturnCode);

	
	//~ Worker-thread-only state

	AVFormatContext* FormatContext = nullptr;
	AVCodecContext* CodecContext = nullptr;
	AVPacket* Packet = nullptr;
	AVFrame* Frame = nullptr;
	int32 VideoStreamIndex = INDEX_NONE;

	SwsContext* ScaleContext = nullptr;

	// Read once per connection on Open(). 0 means unknown.
	int32 TimebaseNum = 0;
	int32 TimebaseDen = 0;
	FTimespan FramePeriod = FTimespan::Zero();

	// Previous frame's info
	bool bHaveLastFrame = false;
	FTimespan LastTime = FTimespan::Zero();
	FTimespan LastDuration = FTimespan::Zero();

	int32 FramesLogged = 0;

	// Audio decoder; null when connection has not usable audio
	AVCodecContext* AudioCodecContext = nullptr;
	AVFrame* AudioFrame = nullptr;
	int32 AudioStreamIndex = INDEX_NONE;
	int32 AudioTimebaseNum = 0;
	int32 AudioTimebaseDen = 0;

	// Previous audio frames info
	bool bHaveLastAudio = false;
	FTimespan AudioLastTime = FTimespan::Zero();
	FTimespan AudioLastDuration = FTimespan::Zero();

	int32 AudioFramesLogged = 0;

	// swresample setup
	SwrContext* ResampleContext = nullptr;
	AVChannelLayout ResampleInLayout{};
	int32 ResampleInFormat = -1;
	int32 ResampleInRate = 0;

	// Persistent across reconnects, for the session's whole life
	uint64 AudioPacketsDropped = 0;

	// Have we seen the first RTCP sender report yet ?
	bool bVideoSenderReportLogged = false;
	bool bAudioSenderReportLogged = false;
	double OpenedSeconds = 0.0;

	// Set using SetDeadline() before a blocking call is called
	double DeadlineSeconds = 0.0;

	
	//~ Cross-thread state

	// UE has suggested using `std::atomic` as `TAtomic` is up for deprecation.
	std::atomic<bool> bStopRequested{ false };

};
