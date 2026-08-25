/*
 * AVS3A (Audio Vivid) decoder wrapper for FFmpeg
 */

#define _POSIX_C_SOURCE 200809L

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

        /* Update avctx before ff_get_buffer is called */
        s->sample_rate = s->hAvs3Dec->outputFs;
        s->channels = s->hAvs3Dec->numChansOutput;
        s->frame_size = s->hAvs3Dec->frameLength;
        avctx->sample_rate = s->sample_rate;
        av_channel_layout_uninit(&avctx->ch_layout);
        av_channel_layout_default(&avctx->ch_layout, s->channels);

        av_log(avctx, AV_LOG_INFO, "AVS3A bitstream: %d Hz, %d channels, %d samples/frame\n",
               s->sample_rate, s->channels, s->frame_size);
    }

    /* Read a full frame (header + payload) */
    if (!ReadBitstream(s->hAvs3Dec, mem_file)) {
        fclose(mem_file);
        av_log(avctx, AV_LOG_ERROR, "Failed to read AVS3A bitstream\n");
        return AVERROR_INVALIDDATA;
    }
    fclose(mem_file);

    n_channels = s->hAvs3Dec->numChansOutput;
    frame_len = s->hAvs3Dec->frameLength;

    output_buf = (short *)av_malloc(frame_len * n_channels * sizeof(short));
    if (!output_buf)
        return AVERROR(ENOMEM);

    /* Decode one frame */
    Avs3Decode(s->hAvs3Dec, output_buf);

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