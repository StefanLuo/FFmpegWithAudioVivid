/*
 * Audio Vivid (AVS3 Audio) parser
 *
 * Splits the elementary stream into individual frames. The frame header
 * carries the sampling rate and total bitrate, from which the exact frame
 * size (7-byte header + payload) can be derived:
 *
 *     frame_size = ceil(total_bitrate * 1024 / (sampling_rate * 8))
 *
 * This is required because an MPEG-TS PES packet may contain several complete
 * audio frames (and a frame may span a PES boundary); the previous pass-through
 * implementation only ever forwarded the whole PES payload as one "frame".
 */

#include "libavutil/samplefmt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/channel_layout.h"
#include "parser.h"
#include "parser_internal.h"
#include "get_bits.h"
#include "av3a.h"

typedef struct AVS3AParseContext {
    ParseContext pc;
    int frame_size;   /* total size in bytes of one frame (header + payload) */
    int remaining;    /* bytes still needed to complete the current frame */
} AVS3AParseContext;

static int raw_av3a_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                          const uint8_t **poutbuf, int *poutbuf_size,
                          const uint8_t *buf, int buf_size)
{
    AVS3AParseContext *ps = s->priv_data;
    ParseContext *pc = &ps->pc;
    int next = END_NOT_FOUND;

    /* Determine the frame size from the header once; it stays constant for
     * the whole stream (constant bitrate AAC-style frames). */
    if (!ps->frame_size && buf_size >= MAX_NBYTES_FRAME_HEADER) {
        uint8_t header[MAX_NBYTES_FRAME_HEADER];
        AVS3AHeaderInfo hdf = { 0 };

        memcpy(header, buf, MAX_NBYTES_FRAME_HEADER);
        if (avpriv_read_av3a_frame_header(&hdf, header, MAX_NBYTES_FRAME_HEADER) == 0 &&
            hdf.total_bitrate > 0 && hdf.sampling_rate > 0) {
            ps->frame_size = (int)((hdf.total_bitrate * AVS3_AUDIO_FRAME_SIZE +
                                    (uint64_t)hdf.sampling_rate * 8 - 1) /
                                   ((uint64_t)hdf.sampling_rate * 8));
            ps->remaining = 0;

            avctx->sample_rate = hdf.sampling_rate;
            avctx->bit_rate    = hdf.total_bitrate;
            av_channel_layout_uninit(&avctx->ch_layout);
            if (hdf.channel_layout)
                av_channel_layout_from_mask(&avctx->ch_layout, hdf.channel_layout);
            else
                avctx->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
            avctx->ch_layout.nb_channels = hdf.total_channels;
            avctx->frame_size = AVS3_AUDIO_FRAME_SIZE;
            s->format = hdf.bitdepth;
        }
    }

    if (ps->frame_size) {
        if (!ps->remaining)
            ps->remaining = ps->frame_size;

        if (ps->remaining <= buf_size) {
            next = ps->remaining;
            ps->remaining = 0;
        } else {
            ps->remaining -= buf_size;
        }
    }

    if (ff_combine_frame(pc, next, &buf, &buf_size) < 0 || !buf_size) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

const FFCodecParser ff_av3a_parser = {
    PARSER_CODEC_LIST(AV_CODEC_ID_AVS3_AUDIO),
    .priv_data_size = sizeof(AVS3AParseContext),
    .parse          = raw_av3a_parse,
    .close          = ff_parse_close,
};