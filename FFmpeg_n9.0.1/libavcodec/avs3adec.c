/*
 * AVS3A (Audio Vivid) decoder wrapper for FFmpeg
 * Fixed: Robust model loading and logic persistence
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include "avs3a/avs3_prot_dec.h"
#include "avs3a/avs3_stat_dec.h"
#include "avs3a/avs3_stat_com.h"

typedef struct AVS3ADecoderContext {
    const AVClass *class;
    AVS3DecoderHandle hAvs3Dec;
    FILE *fModel;
	int header_parsed;
    int initialized;
    int sample_rate;
    int channels;
    int frame_size;
    char *av3a_model_path;
} AVS3ADecoderContext;

static int avs3a_open_model(
    AVCodecContext *avctx,
    AVS3ADecoderContext *s)
{
    if (s->fModel) return 0;
    if (s->av3a_model_path &&
        s->av3a_model_path[0] != '\0') {
        s->fModel = fopen(
            s->av3a_model_path,
            "rb");
        if (!s->fModel) {
            int err = AVERROR(errno);
            av_log(
                avctx,
                AV_LOG_ERROR,
                "AV3A: failed to open av3a model '%s': errno=%d\n",
                s->av3a_model_path,
                errno);
            return err;
        }
        return 0;
    }
#ifdef _WIN32
    {
        char path[MAX_PATH];
        if (GetModuleFileNameA(NULL, path, sizeof(path))) {
            char *last_slash = strrchr(path, '\\');
            if (last_slash) {
                strcpy(last_slash + 1, "model.bin");
                s->fModel = fopen(path, "rb");
                if (s->fModel) {
                    av_log(
                        avctx,
                        AV_LOG_DEBUG,
                        "AV3A: opened Windows model: %s\n",
                        path);
                    return 0;
                }
            }
        }
    }
#endif

    return AVERROR(ENOENT);
}

static av_cold int avs3a_decode_init(AVCodecContext *avctx)
{
    AVS3ADecoderContext *s = avctx->priv_data;

    s->sample_rate = avctx->sample_rate > 0 ?
                     avctx->sample_rate : 48000;

    s->channels = avctx->ch_layout.nb_channels > 0 ?
                  avctx->ch_layout.nb_channels : 2;

    s->frame_size = 1024;
	s->header_parsed = 0;
    s->initialized = 0;
    s->fModel = NULL;

    s->hAvs3Dec = (AVS3DecoderHandle)calloc(1, sizeof(AVS3Decoder));
    if (!s->hAvs3Dec)
        return AVERROR(ENOMEM);

    avctx->sample_fmt = AV_SAMPLE_FMT_S16P;

    return 0;
}

static int avs3a_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                              int *got_frame, AVPacket *avpkt)
{
    AVS3ADecoderContext *s = avctx->priv_data;
    int ret;
    int n_channels;
    int frame_len;

    if (!s->hAvs3Dec) {
        return AVERROR(EINVAL);
    }

    if (!avpkt || avpkt->size <= 0 || !avpkt->data) {
        return AVERROR_INVALIDDATA;
    }

    if (!s->header_parsed) {
		if (!Avs3ParseBsFrameHeader(
				s->hAvs3Dec,
				NULL,
				avpkt->data,
				avpkt->size,
				1,
				NULL,
				NULL)) {
			return AVERROR_INVALIDDATA;
		}

		s->sample_rate = s->hAvs3Dec->outputFs;
		s->channels = s->hAvs3Dec->numChansOutput;
		s->frame_size = s->hAvs3Dec->frameLength;

		avctx->frame_size = s->frame_size;
		avctx->sample_rate = s->sample_rate;

		av_channel_layout_uninit(&avctx->ch_layout);

		switch (s->hAvs3Dec->channelNumConfig) {
			case CHANNEL_CONFIG_MONO:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_MONO);
				break;
	
			case CHANNEL_CONFIG_STEREO:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_STEREO);
				break;
	
			case CHANNEL_CONFIG_MC_4_0:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_4POINT0);
				break;
	
			case CHANNEL_CONFIG_MC_5_1:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_5POINT1);
				break;
	
			case CHANNEL_CONFIG_MC_7_1:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_7POINT1);
				break;
	
			case CHANNEL_CONFIG_MC_5_1_2:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_5POINT1POINT2_BACK);
				break;
	
			case CHANNEL_CONFIG_MC_5_1_4:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_5POINT1POINT4_BACK);
				break;
	
			case CHANNEL_CONFIG_MC_7_1_2:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_7POINT1POINT2);
				break;
	
			case CHANNEL_CONFIG_MC_7_1_4:
				av_channel_layout_from_mask(
					&avctx->ch_layout,
					AV_CH_LAYOUT_7POINT1POINT4_BACK);
				break;
	
			default:
				av_channel_layout_default(
					&avctx->ch_layout,
					s->channels);
				break;
        }

		s->header_parsed = 1;
	}

    if (!s->initialized) {
		/*
		 * During libavformat probing, the temporary decoder
		 * may not have a model path.
		 */
		if (!s->av3a_model_path ||
			s->av3a_model_path[0] == '\0') {
#ifdef _WIN32
			if (avs3a_open_model(avctx, s) < 0) {
				*got_frame = 0;
				return avpkt->size;
			}
#else
			*got_frame = 0;
			return avpkt->size;
#endif
		}
		/*
		 * Initialize the AVS3 decoder using the opened model.
		 *
		 * Avs3InitDecoder() may consume/replace the model handle.
		 */
		ret = avs3a_open_model(avctx, s);
		if (ret < 0) return ret;
		Avs3InitDecoder(s->hAvs3Dec, &s->fModel);
		if (s->fModel) {
			fclose(s->fModel);
			s->fModel = NULL;
		}

		s->initialized = 1;
	}

	/*
	 * Read and parse the current AVS3A frame directly from the packet.
	 */
	if (!ReadBitstreamMemory(
			s->hAvs3Dec,
			avpkt->data,
			avpkt->size)) {
		return AVERROR_INVALIDDATA;
	}

	n_channels = s->hAvs3Dec->numChansOutput;
	frame_len = s->hAvs3Dec->frameLength;

	if (n_channels <= 0 ||
		n_channels > MAX_CHANNELS ||
		frame_len <= 0 ||
		frame_len > FRAME_LEN) {
		return AVERROR_INVALIDDATA;
	}

	frame->nb_samples = frame_len;

	if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
		return ret;
	}

	av_channel_layout_copy(&frame->ch_layout, &avctx->ch_layout);

	short *output_planes[MAX_CHANNELS] = {0};

	for (int ch = 0; ch < n_channels; ch++) {
		output_planes[ch] =
			(short *)frame->extended_data[ch];
	}

	/*
	 * Decode directly into AVFrame's planar S16 buffers.
	 */
	Avs3DecodePlanar(
		s->hAvs3Dec,
		output_planes);

	if (s->hAvs3Dec->hBitstream) {
		ResetBitstream(s->hAvs3Dec->hBitstream);
	}

	*got_frame = 1;
	return avpkt->size;
}

static av_cold int avs3a_decode_close(AVCodecContext *avctx)
{
    AVS3ADecoderContext *s = avctx->priv_data;
    if (s->fModel) { fclose(s->fModel); s->fModel = NULL; }
    if (s->hAvs3Dec) {
        if (s->initialized) Avs3DecoderDestroy(s->hAvs3Dec);
        else free(s->hAvs3Dec);
        s->hAvs3Dec = NULL;
    }
    return 0;
}

#define OFFSET(x) offsetof(AVS3ADecoderContext, x)
#define AD AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption avs3a_options[] = {
    { "av3a_model_path", "Path to model.bin", OFFSET(av3a_model_path), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AD },
    { NULL },
};

static const AVClass avs3a_decoder_class = {
    .class_name = "avs3_audio",
    .item_name  = av_default_item_name,
    .option     = avs3a_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_avs3_audio_decoder = {
    .p.name         = "avs3_audio",
    .p.long_name    = "AVS3 Audio Vivid (AVS3-P3)",
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AVS3_AUDIO,
    .p.priv_class   = &avs3a_decoder_class,
    .priv_data_size = sizeof(AVS3ADecoderContext),
    .init           = avs3a_decode_init,
    FF_CODEC_DECODE_CB(avs3a_decode_frame),
    .close          = avs3a_decode_close,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_S16P),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
