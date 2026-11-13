// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.
//
// LeakHarness: IPStreamMedia's FFmpeg calls with no Unreal around them.
// Opens a stream the way FIPStreamFFmpegSession::Open() does, reads packets on a thread
// started the way FRunnableThread starts one, and prints this process's private memory
// every 5 s. Grows like the plugin -> the leak is in our FFmpeg calls. Flat -> it needs
// something about running inside Unreal.
//
// Usage: run.bat "<url>" [seconds, default 300] [--decode] [--timer1ms]

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#include <timeapi.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4244)

extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/dict.h"
}
#pragma warning(pop)

namespace
{
    // Same values as IPStreamFFmpegSession
    constexpr double ConnectTimeoutSeconds = 6.0;
    constexpr double ReadTimeoutSeconds = 2.0;

    const DWORD SampleIntervalMs = 5000;

    double NowSeconds()
    {
        static const double SecondsPerCycle = []
        {
            LARGE_INTEGER Frequency;
            QueryPerformanceFrequency(&Frequency);
            return 1.0 / static_cast<double>(Frequency.QuadPart);
        }();

        LARGE_INTEGER Cycles;
        QueryPerformanceCounter(&Cycles);
        return static_cast<double>(Cycles.QuadPart) * SecondsPerCycle;
    }

    double PrivateMegabytes()
    {
        PROCESS_MEMORY_COUNTERS_EX Counters{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&Counters), sizeof(Counters));
        return static_cast<double>(Counters.PrivateUsage) / (1024.0 * 1024.0);
    }

    struct FHarness
    {
        const char* Url = nullptr;
        bool bDecode = false;

        std::atomic<bool> bStopRequested{ false };
        std::atomic<unsigned long long> PacketsRead { 0 };
        std::atomic<unsigned long long> FramesDecoded { 0 };

        double DeadlineSeconds = 0.0;
    };

    int InterruptCallback(void* Opaque)
    {
        const FHarness* Harness = static_cast<const FHarness*>(Opaque);
        if (Harness->bStopRequested.load(std::memory_order_relaxed))
        {
            return 1;
        }
        return NowSeconds() > Harness->DeadlineSeconds ? 1 : 0;
        
    }

    void SetDeadline(FHarness& Harness, double TimeoutSeconds)
    {
        Harness.DeadlineSeconds = NowSeconds() + TimeoutSeconds;
    }

    struct FSession
    {
        AVFormatContext* FormatContext = nullptr;
        AVCodecContext* CodecContext = nullptr;
        AVPacket* Packet = nullptr;
        AVFrame* Frame = nullptr;

        ~FSession()
        {
            av_frame_free(&Frame);
            av_packet_free(&Packet);
            avcodec_free_context(&CodecContext);
            avformat_close_input(&FormatContext);
        }
    };

    void PrintError(const char* Call, int Result)
    {
        char Text[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(Result, Text, sizeof(Text));
        std::fprintf(stderr, "# %s failed: %d (%s)\n", Call, Result, Text);
    }

    bool OpenSession(FHarness& Harness, FSession& Session, int& OutVideoStreamIndex)
    {
        Session.FormatContext = avformat_alloc_context();
        if (!Session.FormatContext)
        {
            std::fprintf(stderr, "# avformat_alloc_context failed\n");
            return false;
        }

        Session.FormatContext->interrupt_callback.callback = &InterruptCallback;
        Session.FormatContext->interrupt_callback.opaque = &Harness;

        AVDictionary* Options = nullptr;
        av_dict_set(&Options, "rtsp_transport", "tcp", 0);
        av_dict_set(&Options, "probesize", "32768", 0);

        SetDeadline(Harness, ConnectTimeoutSeconds);
        const int OpenResult = avformat_open_input(&Session.FormatContext, Harness.Url, nullptr, &Options);
        av_dict_free(&Options);
        if (OpenResult < 0)
        {
            PrintError("avformat_open_input", OpenResult);
            return false;
        }

        SetDeadline(Harness, ConnectTimeoutSeconds);
        const int StreamInfoResult = avformat_find_stream_info(Session.FormatContext, nullptr);
        if (StreamInfoResult < 0)
        {
            PrintError("avformat_find_stream_info", StreamInfoResult);
            return false;
        }

        int VideoStreamIndex = -1;
        for (unsigned int i = 0; i < Session.FormatContext->nb_streams; i++)
        {
                if (Session.FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                {   
                    VideoStreamIndex = static_cast<int>(i);
                    break;
                }
                
        }
        if (VideoStreamIndex < 0)
        {
            std::fprintf(stderr, "# No video stream\n");
            return false;
        }

        const AVStream* VideoStream = Session.FormatContext->streams[VideoStreamIndex];
        const AVCodec* Decoder = avcodec_find_decoder(VideoStream->codecpar->codec_id);
        if (!Decoder)
        {
            std::fprintf(stderr, "# No decoder for codec id %d\n", VideoStream->codecpar->codec_id);
            return false;
        }

        Session.CodecContext = avcodec_alloc_context3(Decoder);
        if (!Session.CodecContext || avcodec_parameters_to_context(Session.CodecContext, VideoStream->codecpar) < 0)
        {
            std::fprintf(stderr, "# Failed to build codec context\n");
            return false;
    
        }
        Session.CodecContext->pkt_timebase = VideoStream->time_base;

        if (avcodec_open2(Session.CodecContext, Decoder, nullptr) < 0)
        {
            std::fprintf(stderr, "# avcodec_open2 failed\n");
            return false;
        }

        Session.Packet = av_packet_alloc();
        Session.Frame = av_frame_alloc();
        if (!Session.Packet || !Session.Frame)
        {
            std::fprintf(stderr, "# Packet/Frame allocation failed\n");
            return false;
        }

        std::fprintf(stderr, "# Opened: %u streams, video is stream %d, codec %s\n", Session.FormatContext->nb_streams, VideoStreamIndex, avcodec_get_name(VideoStream->codecpar->codec_id));
        
        OutVideoStreamIndex = VideoStreamIndex;
        return true;        
    }

    // Mirrors the worker's loop (IPStreamDecodeWorker.cpp, DecodeStream()) around
	// FIPStreamFFmpegSession::DecodeNext() (:206-286). One connection, no reconnect.
	DWORD WINAPI ReadThread(LPVOID Param)
	{
		FHarness& Harness = *static_cast<FHarness*>(Param);

		FSession Session;
		int VideoStreamIndex = -1;
		if (!OpenSession(Harness, Session, VideoStreamIndex))
		{
			return 1;
		}

		while (!Harness.bStopRequested.load(std::memory_order_relaxed))
		{
			const int ReceiveResult = avcodec_receive_frame(Session.CodecContext, Session.Frame);
			if (ReceiveResult == 0)
			{
				Harness.FramesDecoded.fetch_add(1, std::memory_order_relaxed);
				continue;
			}
			if (ReceiveResult != AVERROR(EAGAIN))
			{
				PrintError("avcodec_receive_frame", ReceiveResult);
				return 1;
			}

			SetDeadline(Harness, ReadTimeoutSeconds);
			const int ReadResult = av_read_frame(Session.FormatContext, Session.Packet);
			if (ReadResult < 0)
			{
				if (Harness.bStopRequested.load(std::memory_order_relaxed))
				{
					return 0;	// our own stop, not an error
				}
				PrintError("av_read_frame", ReadResult);
				return 1;
			}
			Harness.PacketsRead.fetch_add(1, std::memory_order_relaxed);

			// Without --decode this is the split run's SkipDecode path: every packet unreffed, none sent.
			if (!Harness.bDecode || Session.Packet->stream_index != VideoStreamIndex)
			{
				av_packet_unref(Session.Packet);
				continue;
			}

			const int SendResult = avcodec_send_packet(Session.CodecContext, Session.Packet);
			av_packet_unref(Session.Packet);
			if (SendResult < 0)
			{
				PrintError("avcodec_send_packet", SendResult);
				return 1;
			}
		}
		return 0;
	}

	// Least-squares slope of Y against X (seconds), per minute.
	double SlopePerMinute(const std::vector<double>& X, const std::vector<double>& Y)
	{
		const double N = static_cast<double>(X.size());
		double MeanX = 0.0;
		double MeanY = 0.0;
		for (size_t i = 0; i < X.size(); ++i)
		{
			MeanX += X[i];
			MeanY += Y[i];
		}
		MeanX /= N;
		MeanY /= N;

		double Sxy = 0.0;
		double Sxx = 0.0;
		for (size_t i = 0; i < X.size(); ++i)
		{
			Sxy += (X[i] - MeanX) * (Y[i] - MeanY);
			Sxx += (X[i] - MeanX) * (X[i] - MeanX);
		}
		return 60.0 * Sxy / Sxx;
	}

	// Shows which DLL files were loaded: how they're loaded is the one difference left on the list.
	void PrintDllPath(const char* Name)
	{
		char Path[MAX_PATH] = {};
		const HMODULE Module = GetModuleHandleA(Name);
		if (Module)
		{
			GetModuleFileNameA(Module, Path, MAX_PATH);
		}
		std::fprintf(stderr, "# %s: %s\n", Name, Module ? Path : "not loaded");
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: run.bat \"<url>\" [seconds, default 300] [--decode] [--timer1ms]\n");
		return 2;
	}

	FHarness Harness;
	Harness.Url = argv[1];
	int RunSeconds = 300;
	bool bTimer1Ms = false;
	for (int i = 2; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--decode") == 0)
		{
			Harness.bDecode = true;
		}
		else if (std::strcmp(argv[i], "--timer1ms") == 0)
		{
			bTimer1Ms = true;
		}
		else if (std::atoi(argv[i]) > 0)
		{
			RunSeconds = std::atoi(argv[i]);
		}
	}

	// Unreal asks for a 1 ms timer tick at start-up (WindowsPlatformMisc.cpp:603). Done before
	// the read thread starts, so that thread runs under it from its first call, as in Unreal.
	if (bTimer1Ms)
	{
		timeBeginPeriod(1);
	}

	std::fprintf(stderr, "# LeakHarness: %d s, decode %s, 1 ms timer %s. URL not printed.\n",
		RunSeconds, Harness.bDecode ? "on" : "off", bTimer1Ms ? "on" : "off");
	PrintDllPath("avformat-63.dll");
	PrintDllPath("avcodec-63.dll");
	PrintDllPath("avutil-61.dll");

	// As FRunnableThread does on Windows (WindowsRunnableThread.h:166): default stack size,
	// created suspended, then resumed.
	const HANDLE Thread = CreateThread(nullptr, 0, &ReadThread, &Harness, STACK_SIZE_PARAM_IS_A_RESERVATION | CREATE_SUSPENDED, nullptr);
	if (!Thread)
	{
		std::fprintf(stderr, "# CreateThread failed: %lu\n", GetLastError());
		return 1;
	}
	ResumeThread(Thread);

	std::vector<double> Times;
	std::vector<double> Megabytes;
	bool bThreadEndedEarly = false;
	const double StartSeconds = NowSeconds();

	std::printf("seconds,private_mb,packets,frames\n");
	while (NowSeconds() - StartSeconds < RunSeconds)
	{
		if (WaitForSingleObject(Thread, SampleIntervalMs) == WAIT_OBJECT_0)
		{
			bThreadEndedEarly = true;
			break;
		}

		const double Seconds = NowSeconds() - StartSeconds;
		const double Mb = PrivateMegabytes();
		std::printf("%.1f,%.2f,%llu,%llu\n", Seconds, Mb,
			Harness.PacketsRead.load(std::memory_order_relaxed),
			Harness.FramesDecoded.load(std::memory_order_relaxed));
		std::fflush(stdout);

		if (Seconds >= 10.0)	// skip start-up, as the LLM fits do
		{
			Times.push_back(Seconds);
			Megabytes.push_back(Mb);
		}
	}

	Harness.bStopRequested.store(true, std::memory_order_relaxed);
	WaitForSingleObject(Thread, INFINITE);
	DWORD ThreadResult = 0;
	GetExitCodeThread(Thread, &ThreadResult);
	CloseHandle(Thread);

	if (bTimer1Ms)
	{
		timeEndPeriod(1);	// every timeBeginPeriod needs a matching timeEndPeriod
	}

	std::fprintf(stderr, "# Read thread %s (exit code %lu). After close: %.2f MB.\n",
		bThreadEndedEarly ? "ended early" : "stopped", ThreadResult, PrivateMegabytes());
	if (Times.size() >= 3)
	{
		std::fprintf(stderr, "# Slope from 10 s on: %+.2f MB/min over %zu samples (%.2f -> %.2f MB).\n",
			SlopePerMinute(Times, Megabytes), Times.size(), Megabytes.front(), Megabytes.back());
	}
	return bThreadEndedEarly ? 1 : 0;


}