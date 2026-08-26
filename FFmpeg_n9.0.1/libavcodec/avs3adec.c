/*
 * AVS3A (Audio Vivid) decoder wrapper for FFmpeg
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

#ifdef _WIN32
#include "compat/fmemopen_win.h"
#endif
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* AVS3A decoder includes */
#include "avs3a/avs3_prot_dec.h"
#include "avs3a/avs3_stat_dec.h"
#include "avs3a/avs3_stat_com.h"

#define AVS3_AUDIO_SYNC_WORD 0xFFF

typedef struct AVS3ADecoderContext {
    AVS3DecoderHandle hAvs3Dec;
    FILE *fModel;
    int initialized;
    int sample_rate;
    int channels;
    int frame_size;
} AVS3ADecoderContext;

static av_cold int avs3a_decode_init(AVCodecContext *avctx)
{
    AVS3ADecoderContext *s = avctx->priv_data;

    memset(s, 0, sizeof(*s));

    s->sample_rate = avctx->sample_rate > 0 ? avctx->sample_rate : 48000;
    s->channels = avctx->ch_layout.nb_channels > 0 ? avctx->ch_layout.nb_channels : 2;
    s->frame_size = 1024;

    /* Allocate decoder handle */
    s->hAvs3Dec = (AVS3DecoderHandle)calloc(1, sizeof(AVS3Decoder));
    if (!s->hAvs3Dec)
        return AVERROR(ENOMEM);

    /* 智能加载模型逻辑：解决拖拽无声问题 */
    s->fModel = fopen("model.bin", "rb"); // 1. 尝试当前目录
    if (!s->fModel) {
#ifdef _WIN32
        char path[MAX_PATH];
        if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
            char *last_slash = strrchr(path, '\\');
            if (last_slash) {
                strcpy(last_slash + 1, "model.bin");
                s->fModel = fopen(path, "rb"); // 2. 尝试程序同级目录
            }
        }
#endif
    }

    if (!s->fModel) {
        const char *model_path = getenv("AVS3A_MODEL_PATH");
        if (model_path)
            s->fModel = fopen(model_path, "rb");
    }
    if (!s->fModel) {
        av_log(avctx, AV_LOG_ERROR, "Cannot open model.bin\n");
        free(s->hAvs3Dec);
        s->hAvs3Dec = NULL;
        return AVERROR_EXTERNAL;
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_S16P;

    return 0;
}

static int avs3a_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                              int *got_frame, AVPacket *avpkt)
{
    AVS3ADecoderContext *s = avctx->priv_data;
    FILE *mem_file;
    short *output_buf;
    int ret;
    int n_channels, frame_len;

    if (!s->hAvs3Dec)
        return AVERROR(EINVAL);
    if (avpkt->size <= 0)
        return AVERROR_INVALIDDATA;

    /* Create memory file from packet data (includes frame header from demuxer) */
    mem_file = fmemopen((void *)avpkt->data, avpkt->size, "rb");
    if (!mem_file)
        return AVERROR(ENOMEM);

    /* On the first frame, parse the frame header first to learn the channel
     * layout / sample rate, then initialize the decoding core. This mirrors
     * the standalone decoder flow (Avs3ParseBsFrameHeader before Avs3InitDecoder). */
    if (!s->initialized) {
        if (!Avs3ParseBsFrameHeader(s->hAvs3Dec, mem_file, 1, NULL)) {
            fclose(mem_file);
            av_log(avctx, AV_LOG_ERROR, "Failed to parse AVS3A frame header\n");
            return AVERROR_INVALIDDATA;
        }

        Avs3InitDecoder(s->hAvs3Dec, &s->fModel);
        if (s->fModel) {
            fclose(s->fModel);
            s->fModel = NULL;
        }
        s->initialized = 1;

        /* 关键修复：重置文件指针。因为 Avs3ParseBsFrameHeader 已经移动了指针，
         * 必须回位后 ReadBitstream 才能读到完整的同步字和帧数据。 */
        fseek(mem_file, 0, SEEK_SET);

        /* Update avctx before ff_get_buffer is called */
        s->sample_rate = s->hAvs3Dec->outputFs;
        s->channels = s->hAvs3Dec->numChansOutput;
        s->frame_size = s->hAvs3Dec->frameLength;
        avctx->sample_rate = s->sample_rate;

        /* 智能声道布局映射：解决 12 通道等高级全景声的驱动兼容性 */
        av_channel_layout_uninit(&avctx->ch_layout);
        switch (s->hAvs3Dec->channelNumConfig) {
            case CHANNEL_CONFIG_MONO:
                av_channel_layout_default(&avctx->ch_layout, 1);
                break;
            case CHANNEL_CONFIG_STEREO:
                av_channel_layout_default(&avctx->ch_layout, 2);
                break;
            case CHANNEL_CONFIG_MC_4_0:
                av_channel_layout_from_mask(&avctx->ch_layout, AV_CH_LAYOUT_4POINT0);
                break;
            case CHANNEL_CONFIG_MC_5_1:
                av_channel_layout_from_mask(&avctx->ch_layout, AV_CH_LAYOUT_5POINT1);
                break;
            case CHANNEL_CONFIG_MC_7_1:
                av_channel_layout_from_mask(&avctx->ch_layout, AV_CH_LAYOUT_7POINT1);
                break;
            case CHANNEL_CONFIG_MC_5_1_2:
                av_channel_layout_from_mask(&avctx->ch_layout, AV_CH_LAYOUT_5POINT1POINT2_BACK);
                break;
            case CHANNEL_CONFIG_MC_5_1_4:
                av_channel_layout_from_mask(&avctx->ch_layout, AV_CH_LAYOUT_5POINT1POINT4_BACK);
                break;
            case CHANNEL_CONFIG_MC_7_1_2:
                av_channel_layout_from_mask(&avctx->ch_layout, AV_CH_LAYOUT_7POINT1POINT2);
                break;
            case CHANNEL_CONFIG_MC_7_1_4:
                av_channel_layout_from_mask(&avctx->ch_layout, AV_CH_LAYOUT_7POINT1POINT4_BACK);
                break;
            case CHANNEL_CONFIG_MC_22_2:
                av_channel_layout_from_mask(&avctx->ch_layout, AV_CH_LAYOUT_22POINT2);
                break;
            default:
                av_channel_layout_default(&avctx->ch_layout, s->channels);
                break;
        }

        av_log(avctx, AV_LOG_INFO, "AVS3A bitstream: %d Hz, %d channels, %d samples/frame\n",
               s->sample_rate, s->channels, s->frame_size);
    }

    /* Read a full frame (header + payload) */
    if (!ReadBitstream(s->hAvs3Dec, mem_file)) {
        /* 容错处理：如果当前位置读取失败，尝试在数据中寻找下一个 AVS3 同步字 (0x1FF) */
        uint8_t sync_search[2];
        fseek(mem_file, 0, SEEK_SET);
        int found = 0;
        while (fread(sync_search, 1, 2, mem_file) == 2) {
            uint16_t sw = ((uint16_t)sync_search[0] << 4) | (sync_search[1] >> 4);
            if (sw == AVS3_AUDIO_SYNC_WORD) {
                fseek(mem_file, -2, SEEK_CUR);
                if (ReadBitstream(s->hAvs3Dec, mem_file)) {
                    found = 1;
                    break;
                }
            }
            fseek(mem_file, -1, SEEK_CUR);
        }

        if (!found) {
            fclose(mem_file);
            av_log(avctx, AV_LOG_ERROR, "Failed to read AVS3A bitstream after sync search\n");
            return AVERROR_INVALIDDATA;
        }
    }
    fclose(mem_file);

    n_channels = s->hAvs3Dec->numChansOutput;
    frame_len = s->hAvs3Dec->frameLength;

    if (frame_len <= 0 || n_channels <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame parameters: len=%d, chans=%d\n", frame_len, n_channels);
        return AVERROR_INVALIDDATA;
    }

    output_buf = (short *)av_malloc(frame_len * n_channels * sizeof(short));
    if (!output_buf)
        return AVERROR(ENOMEM);

    /* Decode one frame */
    Avs3Decode(s->hAvs3Dec, output_buf);
    av_log(avctx, AV_LOG_DEBUG, "Decoded %d samples for %d channels\n", frame_len, n_channels);

    frame->nb_samples = frame_len;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_free(output_buf);
        return ret;
    }

    /* Copy decoded data to output frame. Avs3SynthOutput produces interleaved
     * S16 (sample-major: output_buf[i * n_channels + ch]); convert to planar.
     * For >8 channels, use extended_data instead of data. */
    for (int ch = 0; ch < n_channels; ch++) {
        int16_t *dst = (int16_t *)frame->extended_data[ch];
        for (int i = 0; i < frame_len; i++)
            dst[i] = output_buf[i * n_channels + ch];
    }

    av_free(output_buf);

    if (s->hAvs3Dec->hBitstream)
        ResetBitstream(s->hAvs3Dec->hBitstream);

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int avs3a_decode_close(AVCodecContext *avctx)
{
    AVS3ADecoderContext *s = avctx->priv_data;

    if (s->fModel) {
        fclose(s->fModel);
        s->fModel = NULL;
    }
    if (s->hAvs3Dec) {
        if (s->initialized) {
            /* Avs3DecoderDestroy frees the decoder core and the hAvs3Dec
             * struct itself (including its neural codec handles). */
            Avs3DecoderDestroy(s->hAvs3Dec);
        } else {
            /* Decode core was never initialized (no Avs3InitDecoder), so the
             * neural codec handles are still NULL; Avs3DecoderDestroy would
             * call DestroyModel(NULL) -> exit(-1). Just free the struct. */
            free(s->hAvs3Dec);
        }
        s->hAvs3Dec = NULL;
    }

    return 0;
}

const FFCodec ff_avs3_audio_decoder = {
    .p.name         = "avs3_audio",
    .p.long_name    = "AVS3 Audio Vivid (AVS3-P3)",
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AVS3_AUDIO,
    .priv_data_size = sizeof(AVS3ADecoderContext),
    .init           = avs3a_decode_init,
    FF_CODEC_DECODE_CB(avs3a_decode_frame),
    .close          = avs3a_decode_close,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_S16P),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};