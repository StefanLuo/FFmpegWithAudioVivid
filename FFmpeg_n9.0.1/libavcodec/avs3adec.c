/*
 * AVS3A (Audio Vivid) decoder wrapper for FFmpeg
 * Fixed: Robust model loading and logic persistence
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

#include "avs3a/avs3_prot_dec.h"
#include "avs3a/avs3_stat_dec.h"
#include "avs3a/avs3_stat_com.h"

#define AVS3_AUDIO_SYNC_WORD 0xFFF

typedef struct AVS3ADecoderContext {
    const AVClass *class;
    AVS3DecoderHandle hAvs3Dec;
    FILE *fModel;
    int initialized;
    int sample_rate;
    int channels;
    int frame_size;
    char *model_path;
} AVS3ADecoderContext;

static av_cold int avs3a_decode_init(AVCodecContext *avctx)
{
    AVS3ADecoderContext *s = avctx->priv_data;

    s->sample_rate = avctx->sample_rate > 0 ? avctx->sample_rate : 48000;
    s->channels = avctx->ch_layout.nb_channels > 0 ? avctx->ch_layout.nb_channels : 2;
    s->frame_size = 1024;
    s->initialized = 0;
    s->fModel = NULL;

    s->hAvs3Dec = (AVS3DecoderHandle)calloc(1, sizeof(AVS3Decoder));
    if (!s->hAvs3Dec)
        return AVERROR(ENOMEM);

    /* 尝试在多个可能的位置打开模型文件 */
    s->fModel = fopen("model.bin", "rb");
    if (!s->fModel) {
#ifdef _WIN32
        char path[MAX_PATH];
        if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
            char *last_slash = strrchr(path, '\\');
            if (last_slash) {
                strcpy(last_slash + 1, "model.bin");
                s->fModel = fopen(path, "rb");
            }
        }
#endif
    }
    /* 如果用户通过 AVOption 传入了路径 */
    if (!s->fModel && s->model_path) {
        s->fModel = fopen(s->model_path, "rb");
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

    if (!s->hAvs3Dec) return AVERROR(EINVAL);
    if (avpkt->size <= 0) return AVERROR_INVALIDDATA;

    /* 核心修复：只有在尚未初始化且模型缺失时，才报致命错 */
    if (!s->initialized && !s->fModel) {
        av_log(avctx, AV_LOG_ERROR, "AVS3A model.bin not found! Put it next to ffplay.exe\n");
        return AVERROR(ENOENT);
    }

    mem_file = fmemopen((void *)avpkt->data, avpkt->size, "rb");
    if (!mem_file) return AVERROR(ENOMEM);

    if (!s->initialized) {
        if (!Avs3ParseBsFrameHeader(s->hAvs3Dec, mem_file, 1, NULL)) {
            fclose(mem_file);
            return AVERROR_INVALIDDATA;
        }

        /* 使用已经打开的模型文件句柄初始化解码器 */
        Avs3InitDecoder(s->hAvs3Dec, &s->fModel);

        /* 初始化后立即关闭文件，s->fModel 变为 NULL 是正常的，不再作为报错依据 */
        if (s->fModel) {
            fclose(s->fModel);
            s->fModel = NULL;
        }
        s->initialized = 1;

        fseek(mem_file, 0, SEEK_SET);

        s->sample_rate = s->hAvs3Dec->outputFs;
        s->channels = s->hAvs3Dec->numChansOutput;
        s->frame_size = s->hAvs3Dec->frameLength;
        avctx->sample_rate = s->sample_rate;
        av_channel_layout_uninit(&avctx->ch_layout);
        av_channel_layout_default(&avctx->ch_layout, s->channels);
    }

    if (!ReadBitstream(s->hAvs3Dec, mem_file)) {
        fclose(mem_file);
        return AVERROR_INVALIDDATA;
    }
    fclose(mem_file);

    n_channels = s->hAvs3Dec->numChansOutput;
    frame_len = s->hAvs3Dec->frameLength;

    output_buf = (short *)av_malloc(frame_len * n_channels * sizeof(short));
    if (!output_buf) return AVERROR(ENOMEM);

    Avs3Decode(s->hAvs3Dec, output_buf);
    frame->nb_samples = frame_len;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_free(output_buf);
        return ret;
    }

    for (int ch = 0; ch < n_channels; ch++) {
        int16_t *dst = (int16_t *)frame->extended_data[ch];
        for (int i = 0; i < frame_len; i++)
            dst[i] = output_buf[i * n_channels + ch];
    }

    av_free(output_buf);
    if (s->hAvs3Dec->hBitstream) ResetBitstream(s->hAvs3Dec->hBitstream);
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
    { "model_path", "Path to model.bin", OFFSET(model_path), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AD },
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
