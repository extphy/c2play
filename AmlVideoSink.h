#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extern "C"
{
	// aml_lib
#include <codec.h>
}

#include <vector>

#include "Codec.h"
#include "Element.h"
#include "InPin.h"


class AmlVideoSinkElement : public Element
{
	const unsigned long PTS_FREQ = 90000;
	const long EXTERNAL_PTS = (1);
	const long SYNC_OUTSIDE = (2);

	std::vector<unsigned char> videoExtraData;
	//AVCodecID codec_id;
	double frameRate;
	codec_para_t codecContext;
	//void* extraData = nullptr;
	//int extraDataSize = 0;
	/*AVRational time_base;*/
	double lastTimeStamp = -1;

	bool isFirstVideoPacket = true;
	bool isAnnexB = false;
	bool isExtraDataSent = false;
	long estimatedNextPts = 0;

	VideoStreamType videoFormat = VideoStreamType::Unknown;
	InPinSPTR videoPin;
	bool isFirstData = true;
	std::vector<unsigned char> extraData;


	void SetupHardware()
	{
		memset(&codecContext, 0, sizeof(codecContext));

		codecContext.stream_type = STREAM_TYPE_ES_VIDEO;
		codecContext.has_video = 1;
		codecContext.am_sysinfo.param = (void*)0; //(EXTERNAL_PTS | SYNC_OUTSIDE);
		codecContext.am_sysinfo.rate = 96000.5 * (1.0 / frameRate);
		//codecContext.noblock = 1;

		switch (videoFormat)
		{
		case VideoStreamType::Mpeg2:
			printf("AmlVideoSink - VIDEO/MPEG2\n");

			codecContext.video_type = VFORMAT_MPEG12;
			codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_UNKNOW;
			break;

		case VideoStreamType::Mpeg4:
			printf("AmlVideoSink - VIDEO/MPEG4\n");

			codecContext.video_type = VFORMAT_MPEG4;
			codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_MPEG4_5;
			//VIDEO_DEC_FORMAT_MP4; //VIDEO_DEC_FORMAT_MPEG4_3; //VIDEO_DEC_FORMAT_MPEG4_4; //VIDEO_DEC_FORMAT_MPEG4_5;
			break;

		case VideoStreamType::Avc:
		{
			printf("AmlVideoSink - VIDEO/H264\n");

			codecContext.video_type = VFORMAT_H264_4K2K;
			codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_H264_4K2K;
		}
		break;

		case VideoStreamType::Hevc:
			printf("AmlVideoSink - VIDEO/HEVC\n");

			codecContext.video_type = VFORMAT_HEVC;
			codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_HEVC;
			break;


			//case CODEC_ID_VC1:
			//	printf("stream #%d - VIDEO/VC1\n", i);
			//	break;

		default:
			printf("AmlVideoSink - VIDEO/UNKNOWN(%d)\n", (int)videoFormat);
			throw NotSupportedException();
		}

		printf("\tfps=%f ", frameRate);

		printf("am_sysinfo.rate=%d ",
			codecContext.am_sysinfo.rate);

		printf("\n");




		int api = codec_init(&codecContext);
		if (api != 0)
		{
			printf("codec_init failed (%x).\n", api);
			exit(1);
		}

		codec_reset(&codecContext);

		//api = codec_set_syncenable(&codecContext, 1);
		//if (api != 0)
		//{
		//	printf("codec_set_syncenable failed (%x).\n", api);
		//	exit(1);
		//}

		WriteToFile("/sys/class/graphics/fb0/blank", "1");
	}

	void ProcessBuffer(AVPacketBufferSPTR buffer)
	{
		//AVPacketBufferPTR buffer = std::static_pointer_cast<AVPacketBuffer>(buf);
		AVPacket* pkt = buffer->GetAVPacket();


		unsigned char* nalHeader = (unsigned char*)pkt->data;

#if 0
		printf("Header (pkt.size=%x):\n", pkt.size);
		for (int j = 0; j < 16; ++j)	//nalHeaderLength
		{
			printf("%02x ", nalHeader[j]);
		}
		printf("\n");
#endif

		if (isFirstVideoPacket)
		{
			printf("Header (pkt.size=%x):\n", pkt->size);
			for (int j = 0; j < 16; ++j)	//nalHeaderLength
			{
				printf("%02x ", nalHeader[j]);
			}
			printf("\n");

			if (nalHeader[0] == 0 && nalHeader[1] == 0 &&
				nalHeader[2] == 0 && nalHeader[3] == 1)
			{
				isAnnexB = true;
			}

			isFirstVideoPacket = false;

			printf("isAnnexB=%u\n", isAnnexB);
		}


#if 1
		if (pkt->pts != AV_NOPTS_VALUE)
		{
			double timeStamp = av_q2d(buffer->TimeBase()) * pkt->pts;
			unsigned long pts = (unsigned long)(timeStamp * PTS_FREQ);
			//printf("pts = %lu, timeStamp = %f\n", pts, timeStamp);
			if (codec_checkin_pts(&codecContext, pts))
			{
				printf("codec_checkin_pts failed\n");
			}

			estimatedNextPts = pkt->pts + pkt->duration;
			lastTimeStamp = timeStamp;
		}
		else
		{
			//printf("WARNING: AV_NOPTS_VALUE for codec_checkin_pts (duration=%x).\n", pkt.duration);

			if (pkt->duration > 0)
			{
				// Estimate PTS
				double timeStamp = av_q2d(buffer->TimeBase()) * estimatedNextPts;
				unsigned long pts = (unsigned long)(timeStamp * PTS_FREQ);

				if (codec_checkin_pts(&codecContext, pts))
				{
					printf("codec_checkin_pts failed\n");
				}

				estimatedNextPts += pkt->duration;
				lastTimeStamp = timeStamp;

				//printf("WARNING: Estimated PTS for codec_checkin_pts (timeStamp=%f).\n", timeStamp);
			}
		}
#endif


		if (isAnnexB)
		{
			SendCodecData(pkt->data, pkt->size);
		}
		else if (!isAnnexB &&
			(videoFormat == VideoStreamType::Avc ||
			 videoFormat == VideoStreamType::Hevc))
		{
			//unsigned char* nalHeader = (unsigned char*)pkt.data;

			// Five least significant bits of first NAL unit byte signify nal_unit_type.
			int nal_unit_type;
			const int nalHeaderLength = 4;

			while (nalHeader < (pkt->data + pkt->size))
			{
				switch (videoFormat)
				{
					case VideoStreamType::Avc:
					//if (!isExtraDataSent)
					{
						// Copy AnnexB data if NAL unit type is 5
						nal_unit_type = nalHeader[nalHeaderLength] & 0x1F;

						if (nal_unit_type == 5)
						{
							ConvertH264ExtraDataToAnnexB();

							SendCodecData(&videoExtraData[0], videoExtraData.size());
						}

						isExtraDataSent = true;
					}
					break;

				case VideoStreamType::Hevc:
					//if (!isExtraDataSent)
					{
						nal_unit_type = (nalHeader[nalHeaderLength] >> 1) & 0x3f;

						/* prepend extradata to IRAP frames */
						if (nal_unit_type >= 16 && nal_unit_type <= 23)
						{
							HevcExtraDataToAnnexB();

							SendCodecData(&videoExtraData[0], videoExtraData.size());
						}

						isExtraDataSent = true;
					}
					break;

				}


				// Overwrite header NAL length with startcode '0x00000001' in *BigEndian*
				int nalLength = nalHeader[0] << 24;
				nalLength |= nalHeader[1] << 16;
				nalLength |= nalHeader[2] << 8;
				nalLength |= nalHeader[3];

				nalHeader[0] = 0;
				nalHeader[1] = 0;
				nalHeader[2] = 0;
				nalHeader[3] = 1;

				SendCodecData(nalHeader, nalLength);

				nalHeader += nalLength + 4;
			}
		}


#if 0
		if (!isAnnexB)
		{
			unsigned char* data = (unsigned char*)pkt->data;

			printf("Post AnnexB Header (pkt.size=%x):\n", pkt->size);
			for (int j = 0; j < 16; ++j)	//nalHeaderLength
			{
				printf("%02x ", data[j]);
			}
			printf("\n");
		}
#endif



		//printf("AmlVideoSinkElement: timeStamp=%f\n", lastTimeStamp);


		//// Send the data to the codec
		//int api = 0;
		//int offset = 0;
		//
		//// Check buffer level
		//buf_status status = { 0 };
		//while (true)
		//{
		//	api = codec_get_vbuf_state(&codecContext, &status);
		//	if (api != 0)
		//	{
		//		printf("AmlVideoSinkElement: codec_get_vbuf_state failed.\n");
		//	}

		//	if (status.free_len > (pkt->size))
		//	{
		//		break;
		//	}

		//	//printf("AmlVideoSinkElement: buffer status free=%d\n", status.free_len);
		//	usleep(1);
		//}


		//while (api == -EAGAIN || offset < pkt->size)
		//{
		//	api = codec_write(&codecContext, pkt->data + offset, pkt->size - offset);
		//	if (api < 0)
		//	{				
		//		printf("codec_write error: %x\n", api);
		//		//codec_reset(&codecContext);
		//	}
		//	else
		//	{
		//		offset += api;
		//		//printf("codec_write send %x bytes of %x total.\n", api, pkt.size);
		//	}

		//	//usleep(1);
		//}
	}
	
	void SendCodecData(unsigned char* data, int length)
	{
		int api;
		buf_status status;

		while (true)
		{
			api = codec_get_vbuf_state(&codecContext, &status);
			if (api != 0)
			{
				printf("AmlVideoSinkElement: codec_get_vbuf_state failed.\n");
			}

			if (status.free_len > length)
			{
				break;
			}

			//printf("AmlVideoSinkElement: buffer status free=%d\n", status.free_len);
			usleep(1);
		}

		int offset = 0;
		while (api == -EAGAIN || offset < length)
		{
			api = codec_write(&codecContext, data + offset, length - offset);
			if (api < 0)
			{
				printf("codec_write error: %x\n", api);
				//codec_reset(&codecContext);
			}
			else
			{
				offset += api;
				//printf("codec_write send %x bytes of %x total.\n", api, pkt.size);
			}

			// The sleep is needed to prevent codec write errors
			usleep(1);
		}
	}

public:

	virtual void Initialize() override
	{
		ClearOutputPins();
		ClearInputPins();

		// TODO: Pin format negotiation

		// Create a video pin
		VideoPinInfoSPTR info = std::make_shared<VideoPinInfo>();
		info->StreamType = VideoStreamType::Unknown;
		info->FrameRate = 0;

		ElementWPTR weakPtr = shared_from_this();
		videoPin = std::make_shared<InPin>(weakPtr, info);
		AddInputPin(videoPin);
	}

	virtual void DoWork() override
	{
		if (isFirstData)
		{
			OutPinSPTR otherPin = videoPin->Source();
			if (otherPin)
			{
				if (otherPin->Info()->Category() != MediaCategory::Video)
				{
					throw InvalidOperationException("AmlVideoSink: Not connected to a video pin.");
				}

				VideoPinInfoSPTR info = std::static_pointer_cast<VideoPinInfo>(otherPin->Info());
				videoFormat = info->StreamType;
				frameRate = info->FrameRate;
				extraData = *(info->ExtraData);

				printf("AmlVideoSink: ExtraData size=%ld\n", extraData.size());

				SetupHardware();

				isFirstData = false;
			}
		}

		BufferSPTR buffer;
		while (Inputs()->Item(0)->TryGetFilledBuffer(&buffer))
		{
			AVPacketBufferSPTR avPacketBuffer = std::static_pointer_cast<AVPacketBuffer>(buffer);
			//printf("AmlVideoSinkElement: Got Buffer.\n");

			ProcessBuffer(avPacketBuffer);

			Inputs()->Item(0)->PushProcessedBuffer(buffer);
		}

		Inputs()->Item(0)->ReturnProcessedBuffers();
	}

	virtual void ChangeState(MediaState oldState, MediaState newState) override
	{
		// TODO: pause video

		Element::ChangeState(oldState, newState);
	}


private:
	static void WriteToFile(const char* path, const char* value)
	{
		int fd = open(path, O_RDWR | O_TRUNC, 0644);
		if (fd < 0)
		{
			printf("WriteToFile open failed: %s = %s\n", path, value);
			throw Exception();
		}

		if (write(fd, value, strlen(value)) < 0)
		{
			printf("WriteToFile write failed: %s = %s\n", path, value);
			throw Exception();
		}

		close(fd);
	}

	void ConvertH264ExtraDataToAnnexB()
	{
		void* video_extra_data = &extraData[0];
		int video_extra_data_size = extraData.size();

		videoExtraData.clear();

		if (video_extra_data_size > 0)
		{
			unsigned char* extraData = (unsigned char*)video_extra_data;

			// http://aviadr1.blogspot.com/2010/05/h264-extradata-partially-explained-for.html

			const int spsStart = 6;
			int spsLen = extraData[spsStart] * 256 + extraData[spsStart + 1];

			videoExtraData.push_back(0);
			videoExtraData.push_back(0);
			videoExtraData.push_back(0);
			videoExtraData.push_back(1);

			for (int i = 0; i < spsLen; ++i)
			{
				videoExtraData.push_back(extraData[spsStart + 2 + i]);
			}


			int ppsStart = spsStart + 2 + spsLen + 1; // 2byte sbs len, 1 byte pps start code
			int ppsLen = extraData[ppsStart] * 256 + extraData[ppsStart + 1];

			videoExtraData.push_back(0);
			videoExtraData.push_back(0);
			videoExtraData.push_back(0);
			videoExtraData.push_back(1);

			for (int i = 0; i < ppsLen; ++i)
			{
				videoExtraData.push_back(extraData[ppsStart + 2 + i]);
			}

		}

#if 0
		printf("EXTRA DATA = ");

		for (int i = 0; i < videoExtraData.size(); ++i)
		{
			printf("%02x ", videoExtraData[i]);

		}

		printf("\n");
#endif
	}

	void HevcExtraDataToAnnexB()
	{
		//void* video_extra_data = ExtraData();
		//int video_extra_data_size = ExtraDataSize();
		void* video_extra_data = &extraData[0];
		int video_extra_data_size = extraData.size();


		videoExtraData.clear();

		if (video_extra_data_size > 0)
		{
			unsigned char* extraData = (unsigned char*)video_extra_data;

			// http://fossies.org/linux/ffmpeg/libavcodec/hevc_mp4toannexb_bsf.c

			int offset = 21;
			int length_size = (extraData[offset++] & 3) + 1;
			int num_arrays = extraData[offset++];

			//printf("HevcExtraDataToAnnexB: length_size=%d, num_arrays=%d\n", length_size, num_arrays);


			for (int i = 0; i < num_arrays; i++)
			{
				int type = extraData[offset++] & 0x3f;
				int cnt = extraData[offset++] << 8 | extraData[offset++];

				for (int j = 0; j < cnt; j++)
				{
					videoExtraData.push_back(0);
					videoExtraData.push_back(0);
					videoExtraData.push_back(0);
					videoExtraData.push_back(1);

					int nalu_len = extraData[offset++] << 8 | extraData[offset++];
					for (int k = 0; k < nalu_len; ++k)
					{
						videoExtraData.push_back(extraData[offset++]);
					}
				}
			}

#if 0
			printf("EXTRA DATA = ");

			for (int i = 0; i < videoExtraData.size(); ++i)
			{
				printf("%02x ", videoExtraData[i]);
			}

			printf("\n");
#endif
		}
	}

};



class AmlVideoSink : public Sink, public virtual IClockSink
{
	const unsigned long PTS_FREQ = 90000;
	const long EXTERNAL_PTS = (1);
	const long SYNC_OUTSIDE = (2);

	std::vector<unsigned char> videoExtraData;
	AVCodecID codec_id;
	double frameRate;
	codec_para_t codecContext;
	void* extraData = nullptr;
	int extraDataSize = 0;
	AVRational time_base;
	double lastTimeStamp = -1;

public:

	void* ExtraData() const
	{
		return extraData;
	}
	void SetExtraData(void* value)
	{
		extraData = value;
	}

	int ExtraDataSize() const
	{
		return extraDataSize;
	}
	void SetExtraDataSize(int value)
	{
		extraDataSize = value;
	}

	double GetLastTimeStamp() const
	{
		return lastTimeStamp;
	}



	AmlVideoSink(AVCodecID codec_id, double frameRate, AVRational time_base)
		: Sink(),
		codec_id(codec_id),
		frameRate(frameRate),
		time_base(time_base)
	{
		memset(&codecContext, 0, sizeof(codecContext));

		codecContext.stream_type = STREAM_TYPE_ES_VIDEO;
		codecContext.has_video = 1;
		codecContext.am_sysinfo.param = (void*)(0 ); // EXTERNAL_PTS | SYNC_OUTSIDE
		codecContext.am_sysinfo.rate = 96000.5 * (1.0 / frameRate);

		switch (codec_id)
		{
		case CODEC_ID_MPEG2VIDEO:
			printf("AmlVideoSink - VIDEO/MPEG2\n");

			codecContext.video_type = VFORMAT_MPEG12;
			codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_UNKNOW;
			break;

		case CODEC_ID_MPEG4:
			printf("AmlVideoSink - VIDEO/MPEG4\n");

			codecContext.video_type = VFORMAT_MPEG4;
			codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_MPEG4_5;
			//VIDEO_DEC_FORMAT_MP4; //VIDEO_DEC_FORMAT_MPEG4_3; //VIDEO_DEC_FORMAT_MPEG4_4; //VIDEO_DEC_FORMAT_MPEG4_5;
			break;

		case CODEC_ID_H264:
		{
			printf("AmlVideoSink - VIDEO/H264\n");

			codecContext.video_type = VFORMAT_H264_4K2K;
			codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_H264_4K2K;
		}
		break;

		case AV_CODEC_ID_HEVC:
			printf("AmlVideoSink - VIDEO/HEVC\n");

			codecContext.video_type = VFORMAT_HEVC;
			codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_HEVC;
			break;


		//case CODEC_ID_VC1:
		//	printf("stream #%d - VIDEO/VC1\n", i);
		//	break;

		default:
			printf("AmlVideoSink - VIDEO/UNKNOWN(%x)\n", codec_id);
			throw NotSupportedException();
		}

		printf("\tfps=%f ", frameRate);

		printf("am_sysinfo.rate=%d ",
			codecContext.am_sysinfo.rate);


		//int width = codecCtxPtr->width;
		//int height = codecCtxPtr->height;

		//printf("SAR=(%d/%d) ",
		//	streamPtr->sample_aspect_ratio.num,
		//	streamPtr->sample_aspect_ratio.den);

		// TODO: DAR

		printf("\n");

	


		int api = codec_init(&codecContext);
		if (api != 0)
		{
			printf("codec_init failed (%x).\n", api);
			exit(1);
		}

		//api = codec_set_syncenable(&codecContext, 1);
		//if (api != 0)
		//{
		//	printf("codec_set_syncenable failed (%x).\n", api);
		//	exit(1);
		//}


		WriteToFile("/sys/class/graphics/fb0/blank", "1");
	}

	~AmlVideoSink()
	{
		if (IsRunning())
		{
			Stop();
		}

		codec_close(&codecContext);

		WriteToFile("/sys/class/graphics/fb0/blank", "0");
	}



	// Inherited via IClockSink
	virtual void SetTimeStamp(double value) override
	{
		unsigned long pts = (unsigned long)(value * PTS_FREQ);

#if 0
		int vpts = codec_get_vpts(&codecContext);
		int drift = vpts - pts;

		printf("Clock drift = %f\n", drift / (double)PTS_FREQ);
#endif

		int codecCall = codec_set_pcrscr(&codecContext, (int)pts);
		if (codecCall != 0)
		{
			printf("codec_set_pcrscr failed.\n");
		}
	}


	virtual void Flush() override
	{
		Sink::Flush();

		codec_reset(&codecContext);
	}

	// Member
private:
	void ConvertH264ExtraDataToAnnexB()
	{
		void* video_extra_data = ExtraData();
		int video_extra_data_size = ExtraDataSize();

		videoExtraData.clear();

		if (video_extra_data_size > 0)
		{
			unsigned char* extraData = (unsigned char*)video_extra_data;

			// http://aviadr1.blogspot.com/2010/05/h264-extradata-partially-explained-for.html

			const int spsStart = 6;
			int spsLen = extraData[spsStart] * 256 + extraData[spsStart + 1];

			videoExtraData.push_back(0);
			videoExtraData.push_back(0);
			videoExtraData.push_back(0);
			videoExtraData.push_back(1);

			for (int i = 0; i < spsLen; ++i)
			{
				videoExtraData.push_back(extraData[spsStart + 2 + i]);
			}


			int ppsStart = spsStart + 2 + spsLen + 1; // 2byte sbs len, 1 byte pps start code
			int ppsLen = extraData[ppsStart] * 256 + extraData[ppsStart + 1];

			videoExtraData.push_back(0);
			videoExtraData.push_back(0);
			videoExtraData.push_back(0);
			videoExtraData.push_back(1);

			for (int i = 0; i < ppsLen; ++i)
			{
				videoExtraData.push_back(extraData[ppsStart + 2 + i]);
			}

		}

#if 0
		printf("EXTRA DATA = ");

		for (int i = 0; i < videoExtraData.size(); ++i)
		{
			printf("%02x ", videoExtraData[i]);

		}

		printf("\n");
#endif
	}

	void HevcExtraDataToAnnexB()
	{
		void* video_extra_data = ExtraData();
		int video_extra_data_size = ExtraDataSize();


		videoExtraData.clear();

		if (video_extra_data_size > 0)
		{
			unsigned char* extraData = (unsigned char*)video_extra_data;

			// http://fossies.org/linux/ffmpeg/libavcodec/hevc_mp4toannexb_bsf.c

			int offset = 21;
			int length_size = (extraData[offset++] & 3) + 1;
			int num_arrays = extraData[offset++];

			//printf("HevcExtraDataToAnnexB: length_size=%d, num_arrays=%d\n", length_size, num_arrays);


			for (int i = 0; i < num_arrays; i++)
			{
				int type = extraData[offset++] & 0x3f;
				int cnt = extraData[offset++] << 8 | extraData[offset++];

				for (int j = 0; j < cnt; j++)
				{
					videoExtraData.push_back(0);
					videoExtraData.push_back(0);
					videoExtraData.push_back(0);
					videoExtraData.push_back(1);

					int nalu_len = extraData[offset++] << 8 | extraData[offset++];
					for (int k = 0; k < nalu_len; ++k)
					{
						videoExtraData.push_back(extraData[offset++]);
					}
				}
			}

#if 0
			printf("EXTRA DATA = ");

			for (int i = 0; i < videoExtraData.size(); ++i)
			{
				printf("%02x ", videoExtraData[i]);
			}

			printf("\n");
#endif
		}
	}

	static void WriteToFile(const char* path, const char* value)
	{
		int fd = open(path, O_RDWR | O_TRUNC, 0644);
		if (fd < 0)
		{
			printf("WriteToFile open failed: %s = %s\n", path, value);
			throw Exception();
		}

		if (write(fd, value, strlen(value)) < 0)
		{
			printf("WriteToFile write failed: %s = %s\n", path, value);
			throw Exception();
		}

		close(fd);
	}


protected:
	virtual void WorkThread() override
	{
		printf("AmlVideoSink entering running state.\n");


		bool isFirstVideoPacket = true;
		bool isAnnexB = false;
		bool isExtraDataSent = false;
		long estimatedNextPts = 0;
		MediaState lastState = State();


		codec_resume(&codecContext);

		while (IsRunning())
		{
			MediaState state = State();

			if (state != MediaState::Play)
			{
				if (lastState == MediaState::Play)
				{
					int ret = codec_pause(&codecContext);
				}

				usleep(1);
			}
			else
			{
				if (lastState == MediaState::Pause)
				{
					int ret = codec_resume(&codecContext);
				}

				AVPacketBufferPtr buffer;

				if (!TryGetBuffer(&buffer))
				{
					usleep(1);
				}
				else
				{
					AVPacket* pkt = buffer->GetAVPacket();


					unsigned char* nalHeader = (unsigned char*)pkt->data;

#if 0
					printf("Header (pkt.size=%x):\n", pkt.size);
					for (int j = 0; j < 16; ++j)	//nalHeaderLength
					{
						printf("%02x ", nalHeader[j]);
					}
					printf("\n");
#endif

					if (isFirstVideoPacket)
					{
						printf("Header (pkt.size=%x):\n", pkt->size);
						for (int j = 0; j < 16; ++j)	//nalHeaderLength
						{
							printf("%02x ", nalHeader[j]);
						}
						printf("\n");


						//// DEBUG
						//printf("Codec ExtraData=%p ExtraDataSize=%x\n", video_extra_data, video_extra_data_size);
						//
						//unsigned char* ptr = (unsigned char*)video_extra_data;
						//printf("ExtraData :\n");
						//for (int j = 0; j < video_extra_data_size; ++j)
						//{					
						//	printf("%02x ", ptr[j]);
						//}
						//printf("\n");
						//

						//int extraDataCall = codec_write(&codecContext, video_extra_data, video_extra_data_size);
						//if (extraDataCall == -EAGAIN)
						//{
						//	printf("ExtraData codec_write failed.\n");
						//}
						////


						if (nalHeader[0] == 0 && nalHeader[1] == 0 &&
							nalHeader[2] == 0 && nalHeader[3] == 1)
						{
							isAnnexB = true;
						}

						isFirstVideoPacket = false;

						printf("isAnnexB=%u\n", isAnnexB);
					}


					if (!isAnnexB &&
						(codec_id == CODEC_ID_H264 ||
						 codec_id == AV_CODEC_ID_HEVC))
					{
						//unsigned char* nalHeader = (unsigned char*)pkt.data;

						// Five least significant bits of first NAL unit byte signify nal_unit_type.
						int nal_unit_type;
						const int nalHeaderLength = 4;

						while (nalHeader < (pkt->data + pkt->size))
						{
							switch (codec_id)
							{
							case CODEC_ID_H264:
								//if (!isExtraDataSent)
							{
								// Copy AnnexB data if NAL unit type is 5
								nal_unit_type = nalHeader[nalHeaderLength] & 0x1F;

								if (nal_unit_type == 5)
								{
									ConvertH264ExtraDataToAnnexB();

									int api = codec_write(&codecContext, &videoExtraData[0], videoExtraData.size());
									if (api <= 0)
									{
										printf("extra data codec_write error: %x\n", api);
									}
									else
									{
										//printf("extra data codec_write OK\n");
									}
								}

								isExtraDataSent = true;
							}
							break;

							case AV_CODEC_ID_HEVC:
								//if (!isExtraDataSent)
							{
								nal_unit_type = (nalHeader[nalHeaderLength] >> 1) & 0x3f;

								/* prepend extradata to IRAP frames */
								if (nal_unit_type >= 16 && nal_unit_type <= 23)
								{
									HevcExtraDataToAnnexB();

									int attempts = 10;
									int api;
									while (attempts > 0)
									{
										api = codec_write(&codecContext, &videoExtraData[0], videoExtraData.size());
										if (api <= 0)
										{
											usleep(1);
											--attempts;
										}
										else
										{
											//printf("extra data codec_write OK\n");
											break;
										}

									}

									if (attempts == 0)
									{
										printf("extra data codec_write error: %x\n", api);
									}
								}

								isExtraDataSent = true;
							}
							break;

							}


							// Overwrite header NAL length with startcode '0x00000001' in *BigEndian*
							int nalLength = nalHeader[0] << 24;
							nalLength |= nalHeader[1] << 16;
							nalLength |= nalHeader[2] << 8;
							nalLength |= nalHeader[3];

							nalHeader[0] = 0;
							nalHeader[1] = 0;
							nalHeader[2] = 0;
							nalHeader[3] = 1;

							nalHeader += nalLength + 4;
						}
					}


					if (pkt->pts != AV_NOPTS_VALUE)
					{
						double timeStamp = av_q2d(time_base) * pkt->pts;
						unsigned long pts = (unsigned long)(timeStamp * PTS_FREQ);
						//printf("pts = %lu, timeStamp = %f\n", pts, timeStamp);
						if (codec_checkin_pts(&codecContext, pts))
						{
							printf("codec_checkin_pts failed\n");
						}

						estimatedNextPts = pkt->pts + pkt->duration;
						lastTimeStamp = timeStamp;
					}
					else
					{
						//printf("WARNING: AV_NOPTS_VALUE for codec_checkin_pts (duration=%x).\n", pkt.duration);

						if (pkt->duration > 0)
						{
							// Estimate PTS
							double timeStamp = av_q2d(time_base) * estimatedNextPts;
							unsigned long pts = (unsigned long)(timeStamp * PTS_FREQ);

							if (codec_checkin_pts(&codecContext, pts))
							{
								printf("codec_checkin_pts failed\n");
							}

							estimatedNextPts += pkt->duration;
							lastTimeStamp = timeStamp;

							//printf("WARNING: Estimated PTS for codec_checkin_pts (timeStamp=%f).\n", timeStamp);
						}
					}


					// Send the data to the codec
					int api = 0;
					int offset = 0;
					do
					{
						api = codec_write(&codecContext, pkt->data + offset, pkt->size - offset);
						if (api == -EAGAIN)
						{
							usleep(1);
						}
						else if (api == -1)
						{
							// TODO: Sometimes this error is returned.  Ignoring it
							// does not seem to have any impact on video display
						}
						else if (api < 0)
						{
							printf("codec_write error: %x\n", api);
							//codec_reset(&codecContext);
						}
						else if (api > 0)
						{
							offset += api;
							//printf("codec_write send %x bytes of %x total.\n", api, pkt.size);
						}

					} while (api == -EAGAIN || offset < pkt->size);
				}

			}

			lastState = state;
		}

		printf("AmlVideoSink exiting running state.\n");
	}

};


typedef std::shared_ptr<AmlVideoSink> AmlVideoSinkPtr;
