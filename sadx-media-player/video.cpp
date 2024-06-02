// TODO: Improve prerendering (ditch vectors, second thread, preallocate buffers)

#include <DShow.h>
#include <chrono>
#include <thread>
#include <vector>
#include "bass_vgmstream.h"
#include "sadx-media-player.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}

char msg[4096];

class VideoPlayer
{
private:
	std::thread* pVideoThread = nullptr;

	AVFormatContext* pFormatContext = nullptr;
	AVPacket* pPacket = nullptr;
	AVFrame* pFrame = nullptr;

	AVCodecContext* pVideoCodecContext = nullptr;
	SwsContext* pSwsContext = nullptr;
	AVFrame* pVideoFrame = nullptr;
	uint8_t* framebuffer = nullptr;

	AVCodecContext* pAudioCodecContext = nullptr;
	SwrContext* pSwrContext = nullptr;
	AVFrame* pAudioFrame = nullptr;
	HSTREAM BassHandle = NULL;

	int video_stream_index = -1;
	int audio_stream_index = -1;

	unsigned int width = 0;
	unsigned int height = 0;

	bool opened = false;
	bool play = false;
	bool finished = false;
	bool update = false;

	std::chrono::steady_clock::time_point real_time;
	double video_time = 0.0;
	double next = 0.0;

	struct Frame
	{
		double video_time;
		uint8_t* data;
		size_t size;
	};

	std::vector<Frame> video_frames;

	void DecodeAudio(AVStream* pStream)
	{
		if (avcodec_send_packet(pAudioCodecContext, pPacket) < 0)
		{
			return;
		}

		if (avcodec_receive_frame(pAudioCodecContext, pFrame) < 0)
		{
			return;
		}

		if (swr_convert_frame(pSwrContext, pAudioFrame, pFrame) < 0)
		{
			OutputDebugStringA("[video] Failed to convert audio frame.\n");
			return;
		}

		int size = av_samples_get_buffer_size(&pAudioFrame->linesize[0], pAudioFrame->ch_layout.nb_channels, pAudioFrame->nb_samples, AV_SAMPLE_FMT_FLT, 0);
		if (size < 0)
		{
			OutputDebugStringA("[video] Failed to send audio buffer to BASS.\n");
			return;
		}

		BASS_StreamPutData(BassHandle, pAudioFrame->data[0], size);
	}

	void DecodeVideo(AVStream* pStream)
	{
		if (avcodec_send_packet(pVideoCodecContext, pPacket) < 0)
		{
			return;
		}

		if (avcodec_receive_frame(pVideoCodecContext, pFrame) < 0)
		{
			return;
		}

		sws_scale(pSwsContext,
			pFrame->data,
			pFrame->linesize,
			0,
			pFrame->height,
			pVideoFrame->data,
			pVideoFrame->linesize);

		size_t size = width * height * 4;
		auto data = (uint8_t*)malloc(size);
		memcpy(data, pVideoFrame->data[0], size);

		Frame frame;
		frame.video_time = pFrame->best_effort_timestamp * av_q2d(pStream->time_base);
		frame.size = size;
		frame.data = data;

		video_frames.push_back(frame);
	}

	void Decode()
	{
		// Queue frames
		if (video_frames.size() < 5)
		{
			int ret = av_read_frame(pFormatContext, pPacket);

			if (ret < 0)
			{
				if (ret == AVERROR_EOF)
				{
					finished = true; // TODO: wait until queue has been rendered
				}
			}

			if (pPacket->stream_index == video_stream_index)
			{
				DecodeVideo(pFormatContext->streams[video_stream_index]);
			}
			else if (pPacket->stream_index == audio_stream_index)
			{
				DecodeAudio(pFormatContext->streams[audio_stream_index]);
			}

			av_packet_unref(pPacket);
		}

		// Run video (TODO: different thread)
		if (video_frames.size())
		{
			if (video_time >= video_frames[0].video_time)
			{
				update = false;
				memcpy(framebuffer, video_frames[0].data, video_frames[0].size);
				update = true;

				free(video_frames[0].data);
				video_frames.erase(video_frames.begin());
			}
		}
	}

	void m_VideoThread()
	{
		while (1)
		{
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - real_time);

			if (!opened)
				break;

			if (elapsed.count() == 0)
				continue;

			real_time = now;

			if (!play || finished)
				continue;

			video_time += (double)elapsed.count() * 0.90;

			Decode();
		}
	}

	static void VideoThread(VideoPlayer* _this)
	{
		_this->m_VideoThread();
	}

public:
	unsigned int Width()
	{
		return width;
	}

	unsigned int Height()
	{
		return height;
	}

	bool Finished()
	{
		return finished;
	}

	bool GetFrameBuffer(uint8_t* pBuffer)
	{
		if (opened && update)
		{
			memcpy(pBuffer, framebuffer, width * height * 4);
			update = false;
			return true;
		}
		return false;
	}

	void Play()
	{
		play = true;
		if (BassHandle)
		{
			BASS_ChannelPlay(BassHandle, FALSE);
		}
	}

	void Pause()
	{
		play = false;
		if (BassHandle)
		{
			BASS_ChannelStop(BassHandle);
		}
	}

	bool Open(const char* path, bool sfd)
	{
		if (opened)
		{
			Close();
		}

		pFormatContext = avformat_alloc_context();
		if (!pFormatContext)
		{
			OutputDebugStringA("[video] Failed to initialize ffmpeg.\n");
			return false;
		}

		if (sfd)
		{
			OutputDebugStringA("[video] SFD compatibility mode.\n");
			pFormatContext->audio_codec_id = AV_CODEC_ID_ADPCM_ADX;
		}

		if (avformat_open_input(&pFormatContext, path, NULL, NULL) != 0 ||
			avformat_find_stream_info(pFormatContext, NULL) < 0)
		{
			sprintf(msg, "[video] Failed to open % s.\n", path);
			OutputDebugStringA(msg);
			return false;
		}

		avio_seek(pFormatContext->pb, 0, SEEK_SET);

		video_stream_index = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (video_stream_index < 0)
		{
			OutputDebugStringA("[video] No video stream found.\n");
			return false;
		}

		const AVCodec* pVideoCodec = avcodec_find_decoder(pFormatContext->streams[video_stream_index]->codecpar->codec_id);
		pFormatContext->video_codec = pVideoCodec;

		pVideoCodecContext = avcodec_alloc_context3(pVideoCodec);
		if (!pVideoCodecContext)
		{
			OutputDebugStringA("[video] Failed to initialize video codec context.\n");
			return false;
		}

		AVStream* pVideoStream = pFormatContext->streams[video_stream_index];

		avformat_seek_file(pFormatContext, 0, 0, 0, pFormatContext->streams[0]->duration, 0);

		if (avcodec_parameters_to_context(pVideoCodecContext, pVideoStream->codecpar) < 0 ||
			avcodec_open2(pVideoCodecContext, pVideoCodec, NULL) < 0)
		{
			OutputDebugStringA("[video] Failed to initialize video codec.\n");
			return false;
		}

		width = pVideoCodecContext->width;
		height = pVideoCodecContext->height;

		pSwsContext = sws_getContext(
			pVideoCodecContext->width,
			pVideoCodecContext->height,
			pVideoCodecContext->pix_fmt,
			width,
			height,
			AV_PIX_FMT_BGRA,
			SWS_BICUBIC,
			NULL,
			NULL,
			NULL);

		if (pSwsContext == NULL)
		{
			OutputDebugStringA("[video] Failed to initialize video conversion.\n");
			return false;
		}

		pPacket = av_packet_alloc();
		if (!pPacket)
		{
			OutputDebugStringA("[video] Failed to allocate packet\n");
			return false;
		}

		pFrame = av_frame_alloc();
		if (!pFrame)
		{
			OutputDebugStringA("[video] Failed to allocate packet frame.\n");
			return false;
		}

		pVideoFrame = av_frame_alloc();
		if (!pVideoFrame)
		{
			OutputDebugStringA("[video] Failed to allocate video output frame.\n");
			return false;
		}

		pVideoFrame->format = AV_PIX_FMT_BGRA;
		pVideoFrame->width = width;
		pVideoFrame->height = height;

		if (av_frame_get_buffer(pVideoFrame, 0) < 0)
		{
			OutputDebugStringA("[video] Failed to allocate video output frame buffer.\n");
			return false;
		}

		framebuffer = (uint8_t*)av_malloc(width * height * 4);
		if (!framebuffer)
		{
			OutputDebugStringA("[video] Failed to allocate frame buffer.\n");
			return false;
		}

		audio_stream_index = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
		if (audio_stream_index >= 0)
		{
			const AVCodec* pAudioCodec = avcodec_find_decoder(pFormatContext->streams[audio_stream_index]->codecpar->codec_id);
			pFormatContext->audio_codec = pAudioCodec;

			pAudioCodecContext = avcodec_alloc_context3(pAudioCodec);
			if (!pAudioCodecContext)
			{
				OutputDebugStringA("[video] Failed to initialize audio codec context.\n");
				return false;
			}

			AVStream* pAudioStream = pFormatContext->streams[audio_stream_index];

			if (avcodec_parameters_to_context(pAudioCodecContext, pAudioStream->codecpar) < 0 ||
				avcodec_open2(pAudioCodecContext, pAudioCodec, NULL) < 0)
			{
				OutputDebugStringA("[video] failed to initialize audio codec.\n");
				return false;
			}

			// Initialize resampler
			if (swr_alloc_set_opts2(&pSwrContext, &pAudioCodecContext->ch_layout, AV_SAMPLE_FMT_FLT, pAudioCodecContext->sample_rate,
				&pAudioCodecContext->ch_layout, (AVSampleFormat)pAudioStream->codecpar->format, pAudioStream->codecpar->sample_rate, 0, nullptr) < 0)
			{
				OutputDebugStringA("[video] Failed to initialize audio conversion.\n");
				return false;
			}

			// Force set audio channel layout for SFD
			if (sfd)
			{
				pAudioCodecContext->ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
				pAudioCodecContext->ch_layout.u.mask = AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT;
			}

			pAudioFrame = av_frame_alloc();
			if (!pAudioFrame)
			{
				OutputDebugStringA("[video] Failed to allocate audio output frame.\n");
				return false;
			}

			pAudioFrame->sample_rate = pAudioCodecContext->sample_rate;
			pAudioFrame->ch_layout = pAudioCodecContext->ch_layout;
			pAudioFrame->format = AV_SAMPLE_FMT_FLT;

			BassHandle = BASS_StreamCreate(pAudioCodecContext->sample_rate, pAudioCodecContext->ch_layout.nb_channels, BASS_SAMPLE_FLOAT, STREAMPROC_PUSH, NULL);
			if (!BassHandle)
			{
				OutputDebugStringA("[video] Failed to initialize audio library.");
				return false;
			}

			BASS_ChannelPlay(BassHandle, FALSE);
		}

		video_time = pVideoStream->start_time * av_q2d(pVideoStream->time_base);
		opened = true;

		real_time = std::chrono::steady_clock::now();
		pVideoThread = new std::thread(VideoThread, this);
		return true;
	}

	void Close()
	{
		if (opened)
		{
			play = false;
			opened = false;
			finished = false;

			if (pVideoThread)
			{
				pVideoThread->join();
				delete pVideoThread;
				pVideoThread = nullptr;
			}

			if (pFormatContext) avformat_close_input(&pFormatContext);
			if (pPacket) av_packet_free(&pPacket);
			if (pFrame) av_frame_free(&pFrame);

			if (pVideoCodecContext) avcodec_free_context(&pVideoCodecContext);
			if (pSwsContext) sws_freeContext(pSwsContext);
			if (pVideoFrame) av_frame_free(&pVideoFrame);
			if (framebuffer) av_free(framebuffer);

			if (pAudioCodecContext) avcodec_free_context(&pAudioCodecContext);
			if (pSwrContext) swr_free(&pSwrContext);
			if (pAudioFrame) av_frame_free(&pAudioFrame);
			if (BassHandle) { BASS_StreamFree(BassHandle); BassHandle = NULL; };
		}
	}

	VideoPlayer() = default;
	~VideoPlayer()
	{
		Close();
	}
};

static VideoPlayer player;

extern "C"
{
	__declspec(dllexport) void ffPlayerPlay()
	{
		return player.Play();
	}

	__declspec(dllexport) void ffPlayerPause()
	{
		return player.Pause();
	}

	__declspec(dllexport) bool ffPlayerFinished()
	{
		return player.Finished();
	}

	__declspec(dllexport) bool ffPlayerOpen(const char* path, bool sfd)
	{
		return player.Open(path, sfd);
	}

	__declspec(dllexport) void ffPlayerClose()
	{
		return player.Close();
	}

	__declspec(dllexport) bool ffPlayerGetFrameBuffer(unsigned char* pBuffer)
	{
		return player.GetFrameBuffer(pBuffer);
	}

	__declspec(dllexport) unsigned int ffPlayerWidth()
	{
		return player.Width();
	}

	__declspec(dllexport) unsigned int ffPlayerHeight()
	{
		return player.Height();
	}
}