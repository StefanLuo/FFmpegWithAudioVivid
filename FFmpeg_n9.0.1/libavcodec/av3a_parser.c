/*
 * Audio Vivid (AVS3 Audio) parser
 *
 * Splits the elementary stream into individual AV3A frames.
 *
 * Frame size:
 *
 *     frame_size =
 *         ceil(total_bitrate * 1024 /
 *              (sampling_rate * 8))
 *
 * The parser keeps the normal FFmpeg ParseContext + ff_combine_frame()
 * mechanism for assembling frames across input boundaries.
 *
 * In addition, a small 8-byte synchronization buffer is used only while
 * searching for a new AV3A frame header. This avoids touching any internal
 * fields of ParseContext and allows the parser to recover after bytes have
 * been lost/discarded in an MPEG-TS stream.
 */

#include <stdint.h>
#include <string.h>

#include "libavutil/error.h"

#include "avcodec.h"
#include "av3a.h"
#include "get_bits.h"
#include "parser.h"
#include "parser_internal.h"


/*
 * Keep enough bytes to detect a 9-byte header crossing parser calls.
 *
 * MAX_NBYTES_FRAME_HEADER is 9 in the current FFmpeg tree.
 */
#define AVS3A_SYNC_TAIL_SIZE (MAX_NBYTES_FRAME_HEADER - 1)

/*
 * Maximum safety limit for a single encoded frame.
 *
 * All current AVS3 bitrate tables are far below this value.
 */
#define AVS3A_MAX_FRAME_SIZE (16 * 1024 * 1024)


typedef struct AVS3AParseContext {
    ParseContext pc;

    /*
     * Size of the current AV3A frame.
     *
     * 0 means that no frame is currently being assembled and a new
     * AV3A header must be located.
     */
    int frame_size;

    /*
     * Bytes still required to finish the current frame.
     *
     * This includes bytes already stored in ParseContext::pc.buffer.
     */
    int remaining;

    /*
     * Synchronization bytes kept between parser calls while looking
     * for a new AV3A frame header.
     *
     * This buffer is completely independent from ParseContext.
     */
    uint8_t sync_buf[AVS3A_SYNC_TAIL_SIZE];

    int sync_size;
} AVS3AParseContext;


/*
 * -------------------------------------------------------------------------
 * Header validation
 * -------------------------------------------------------------------------
 *
 * avpriv_read_av3a_frame_header() assumes that the configuration/bitrate
 * indexes obtained from the bitstream are valid before indexing its tables.
 *
 * While searching for synchronization we may inspect arbitrary bytes, so
 * validate the dangerous indexes before calling the shared header parser.
 */
static int avs3a_header_is_safe(
    const uint8_t *buf,
    int buf_size)
{
    GetBitContext gb;

    int codec_id;
    int nn_type;
    int coding_profile;
    int sampling_rate_index;
    int resolution_index;
    int bitrate_index;
    int channel_config;
    int sound_bed_type;
    int hoa_order;

    const int32_t *bitrate_table;

    if (!buf || buf_size < MAX_NBYTES_FRAME_HEADER)
        return 0;

    init_get_bits8(
        &gb,
        buf,
        MAX_NBYTES_FRAME_HEADER);

    /*
     * 12 bits: sync word.
     */
    if (get_bits(&gb, 12) != AVS3_AUDIO_SYNC_WORD)
        return 0;

    /*
     * 4 bits: codec id.
     */
    codec_id = get_bits(&gb, 4);

    /*
     * Current av3a.c only accepts codec_id == 2.
     */
    if (codec_id != 2)
        return 0;

    /*
     * 1 bit: ancillary data index.
     */
    if (get_bits(&gb, 1) != 0)
        return 0;

    /*
     * 3 bits: neural-network type.
     */
    nn_type = get_bits(&gb, 3);
    (void)nn_type;

    /*
     * 3 bits: coding profile.
     */
    coding_profile = get_bits(&gb, 3);

    if (coding_profile > 2)
        return 0;

    /*
     * 4 bits: sampling-rate index.
     */
    sampling_rate_index = get_bits(&gb, 4);

    if (sampling_rate_index < 0 ||
        sampling_rate_index >= AVS3_SIZE_FS_TABLE) {
        return 0;
    }

    /*
     * First CRC/check field.
     */
    skip_bits(&gb, 8);

    /*
     * ------------------------------------------------------------------
     * Profile 0: mono / stereo / MC
     * ------------------------------------------------------------------
     */
    if (coding_profile == 0) {

        /*
         * 7 bits: channel-number index.
         */
        channel_config = get_bits(&gb, 7);

        if (channel_config < CHANNEL_CONFIG_MONO ||
            channel_config > CHANNEL_CONFIG_MC_7_1_4) {
            return 0;
        }

        bitrate_table =
            avpriv_codecBitrateConfigTable[channel_config].bitrateTable;

        /*
         * Current av3a.c has NULL bitrate tables for MC_10_2 and MC_22_2.
         */
        if (!bitrate_table)
            return 0;

        /*
         * resolution_index: 2 bits.
         */
        resolution_index = get_bits(&gb, 2);

        if (resolution_index > 2)
            return 0;

        /*
         * bitrate_index: 4 bits.
         */
        bitrate_index = get_bits(&gb, 4);

        if (bitrate_index < 0 ||
            bitrate_index >= AVS3_SIZE_BITRATE_TABLE) {
            return 0;
        }

        if (bitrate_table[bitrate_index] <= 0)
            return 0;

        return 1;
    }

    /*
     * ------------------------------------------------------------------
     * Profile 1: object / MC + object
     * ------------------------------------------------------------------
     */
    if (coding_profile == 1) {

        /*
         * soundBedType: 2 bits.
         */
        sound_bed_type = get_bits(&gb, 2);

        /*
         * soundBedType == 0:
         *
         *   object number: 7 bits
         *   bitrate index per object: 4 bits
         */
        if (sound_bed_type == 0) {

            int objects;
            int object_bitrate_index;

            objects = get_bits(&gb, 7) + 1;
            (void)objects;

            object_bitrate_index = get_bits(&gb, 4);

            bitrate_table =
                avpriv_codecBitrateConfigTable[
                    CHANNEL_CONFIG_MONO].bitrateTable;

            if (!bitrate_table)
                return 0;

            if (object_bitrate_index < 0 ||
                object_bitrate_index >= AVS3_SIZE_BITRATE_TABLE) {
                return 0;
            }

            if (bitrate_table[object_bitrate_index] <= 0)
                return 0;
        }

        /*
         * soundBedType == 1:
         *
         *   channel number index: 7 bits
         *   bed bitrate index: 4 bits
         *   object number: 7 bits
         *   object bitrate index: 4 bits
         */
        else if (sound_bed_type == 1) {

            int bed_bitrate_index;
            int object_bitrate_index;
            int objects;

            channel_config = get_bits(&gb, 7);

            if (channel_config < CHANNEL_CONFIG_MONO ||
                channel_config > CHANNEL_CONFIG_MC_7_1_4) {
                return 0;
            }

            bitrate_table =
                avpriv_codecBitrateConfigTable[channel_config].bitrateTable;

            if (!bitrate_table)
                return 0;

            bed_bitrate_index = get_bits(&gb, 4);

            if (bed_bitrate_index < 0 ||
                bed_bitrate_index >= AVS3_SIZE_BITRATE_TABLE) {
                return 0;
            }

            if (bitrate_table[bed_bitrate_index] <= 0)
                return 0;

            objects = get_bits(&gb, 7) + 1;
            (void)objects;

            object_bitrate_index = get_bits(&gb, 4);

            bitrate_table =
                avpriv_codecBitrateConfigTable[
                    CHANNEL_CONFIG_MONO].bitrateTable;

            if (!bitrate_table)
                return 0;

            if (object_bitrate_index < 0 ||
                object_bitrate_index >= AVS3_SIZE_BITRATE_TABLE) {
                return 0;
            }

            if (bitrate_table[object_bitrate_index] <= 0)
                return 0;
        }
        else {
            /*
             * Current av3a.c only handles soundBedType 0 and 1.
             */
            return 0;
        }

        /*
         * Common resolution field.
         */
        resolution_index = get_bits(&gb, 2);

        if (resolution_index > 2)
            return 0;

        /*
         * Profile 1 has no generic bitrate_index here.
         */
        return 1;
    }

    /*
     * ------------------------------------------------------------------
     * Profile 2: HOA
     * ------------------------------------------------------------------
     */
    if (coding_profile == 2) {

        /*
         * HOA order: 4 bits + 1.
         */
        hoa_order = get_bits(&gb, 4) + 1;

        if (hoa_order < 1 || hoa_order > 3)
            return 0;

        channel_config =
            CHANNEL_CONFIG_HOA_ORDER1 + (hoa_order - 1);

        bitrate_table =
            avpriv_codecBitrateConfigTable[channel_config].bitrateTable;

        /*
         * HOA1 / HOA2 can be NULL depending on the build configuration.
         */
        if (!bitrate_table)
            return 0;

        /*
         * resolution_index: 2 bits.
         */
        resolution_index = get_bits(&gb, 2);

        if (resolution_index > 2)
            return 0;

        /*
         * bitrate_index: 4 bits.
         */
        bitrate_index = get_bits(&gb, 4);

        if (bitrate_index < 0 ||
            bitrate_index >= AVS3_SIZE_BITRATE_TABLE) {
            return 0;
        }

        if (bitrate_table[bitrate_index] <= 0)
            return 0;

        return 1;
    }

    return 0;
}


/*
 * Parse one AV3A header with the common FFmpeg header parser.
 */
static int avs3a_parse_header(
    AVS3AHeaderInfo *hdf,
    const uint8_t *buf,
    int buf_size)
{
    if (!hdf ||
        !buf ||
        buf_size < MAX_NBYTES_FRAME_HEADER) {
        return AVERROR_INVALIDDATA;
    }

    if (!avs3a_header_is_safe(buf, buf_size))
        return AVERROR_INVALIDDATA;

    memset(hdf, 0, sizeof(*hdf));

    if (avpriv_read_av3a_frame_header(
            hdf,
            buf,
            MAX_NBYTES_FRAME_HEADER) != 0) {
        return AVERROR_INVALIDDATA;
    }

    if (hdf->sampling_rate <= 0 ||
        hdf->total_bitrate <= 0 ||
        hdf->total_channels <= 0) {
        return AVERROR_INVALIDDATA;
    }

    return 0;
}


/*
 * Find the first valid AV3A header in a buffer.
 *
 * Returns:
 *
 *     1 = found
 *     0 = not found
 */
static int avs3a_find_header(
    const uint8_t *buf,
    int buf_size,
    int *header_offset,
    AVS3AHeaderInfo *hdf)
{
    int i;

    if (!buf ||
        buf_size < MAX_NBYTES_FRAME_HEADER ||
        !header_offset ||
        !hdf) {
        return 0;
    }

    for (i = 0;
         i <= buf_size - MAX_NBYTES_FRAME_HEADER;
         i++) {

        /*
         * Cheap 12-bit sync pre-filter:
         *
         *   0xFF
         *   high nibble of next byte == 0xF
         */
        if (buf[i] != 0xFF)
            continue;

        if ((buf[i + 1] & 0xF0) != 0xF0)
            continue;

        if (avs3a_parse_header(
                hdf,
                buf + i,
                buf_size - i) == 0) {

            *header_offset = i;
            return 1;
        }
    }

    return 0;
}


/*
 * Calculate complete AV3A frame size.
 *
 * Must match:
 *
 *     avs3_parser.c
 *     av3a.c
 *
 *     ceil(total_bitrate * 1024 /
 *          (sampling_rate * 8))
 */
static int avs3a_calculate_frame_size(
    int64_t total_bitrate,
    int sampling_rate)
{
    uint64_t numerator;
    uint64_t denominator;
    uint64_t frame_size;

    if (total_bitrate <= 0 ||
        sampling_rate <= 0) {
        return AVERROR_INVALIDDATA;
    }

    numerator =
        (uint64_t)total_bitrate * AVS3_AUDIO_FRAME_SIZE;

    denominator =
        (uint64_t)sampling_rate * 8;

    frame_size =
        (numerator + denominator - 1) / denominator;

    if (frame_size == 0 ||
        frame_size > AVS3A_MAX_FRAME_SIZE) {
        return AVERROR_INVALIDDATA;
    }

    return (int)frame_size;
}


/*
 * Update codec properties from an AV3A header.
 */
static void avs3a_update_codec_context(
    AVCodecParserContext *s,
    AVCodecContext *avctx,
    const AVS3AHeaderInfo *hdf)
{
    avctx->sample_rate = hdf->sampling_rate;
    avctx->bit_rate = hdf->total_bitrate;
    avctx->frame_size = AVS3_AUDIO_FRAME_SIZE;

    av_channel_layout_uninit(
        &avctx->ch_layout);

    if (hdf->channel_layout) {

        av_channel_layout_from_mask(
            &avctx->ch_layout,
            hdf->channel_layout);

    } else {

        avctx->ch_layout.order =
            AV_CHANNEL_ORDER_UNSPEC;

        avctx->ch_layout.nb_channels =
            hdf->total_channels;
    }

    s->format = hdf->bitdepth;
}


/*
 * Save the final synchronization bytes.
 *
 * This function does not touch ParseContext.
 */
static void avs3a_save_sync_tail(
    AVS3AParseContext *ps,
    const uint8_t *buf,
    int buf_size)
{
    int total_size;
    int keep;
    int start;
    int i;

    total_size =
        ps->sync_size + buf_size;

    keep =
        FFMIN(AVS3A_SYNC_TAIL_SIZE, total_size);

    start =
        total_size - keep;

    for (i = 0; i < keep; i++) {

        int source_index =
            start + i;

        if (source_index < ps->sync_size) {

            ps->sync_buf[i] =
                ps->sync_buf[source_index];

        } else {

            ps->sync_buf[i] =
                buf[source_index - ps->sync_size];
        }
    }

    ps->sync_size = keep;
}


/*
 * Clear the synchronization buffer.
 */
static void avs3a_clear_sync(
    AVS3AParseContext *ps)
{
    ps->sync_size = 0;
}


/*
 * Assemble one AV3A frame using FFmpeg's normal ParseContext.
 *
 * The data passed here is already known to begin at the AV3A frame.
 *
 * Returns:
 *
 *     > 0 : number of input bytes consumed from 'buf'
 *     -1  : no complete frame yet
 *
 * The caller handles parser return semantics.
 */
static int avs3a_consume_frame_data(
    AVS3AParseContext *ps,
    const uint8_t **poutbuf,
    int *poutbuf_size,
    const uint8_t *buf,
    int buf_size,
    int *used)
{
    int ret;

    *poutbuf = NULL;
    *poutbuf_size = 0;
    *used = 0;

    if (ps->remaining <= 0)
        return 0;

    /*
     * Enough input to finish the frame.
     */
    if (buf_size >= ps->remaining) {

        int next =
            ps->remaining;

        ret =
            ff_combine_frame(
                &ps->pc,
                next,
                &buf,
                &buf_size);

        if (ret < 0) {

            /*
             * Do not return AVERROR from a parser.
             *
             * On parser allocation failure there is no useful frame
             * to return; reset the frame state and consume this input.
             */
            ps->frame_size = 0;
            ps->remaining = 0;

            *used = next;

            return 0;
        }

        *poutbuf = buf;
        *poutbuf_size = buf_size;

        *used = next;

        ps->frame_size = 0;
        ps->remaining = 0;

        return next;
    }

    /*
     * Current input is only a partial frame.
     */
    {
        int available =
            buf_size;

        ret =
            ff_combine_frame(
                &ps->pc,
                END_NOT_FOUND,
                &buf,
                &buf_size);

        /*
         * END_NOT_FOUND is the normal incomplete-frame result.
         */
        if (ret >= 0) {

            /*
             * This should not normally happen with END_NOT_FOUND.
             * Treat it as an internal parser failure.
             */
            ps->frame_size = 0;
            ps->remaining = 0;

            *used = available;

            return 0;
        }

        if (ret != -1) {

            ps->frame_size = 0;
            ps->remaining = 0;

            *used = available;

            return 0;
        }

        ps->remaining -= available;

        *used = available;

        return -1;
    }
}


/*
 * -------------------------------------------------------------------------
 * Main parser
 * -------------------------------------------------------------------------
 */
static int raw_av3a_parse(
    AVCodecParserContext *s,
    AVCodecContext *avctx,
    const uint8_t **poutbuf,
    int *poutbuf_size,
    const uint8_t *buf,
    int buf_size)
{
    AVS3AParseContext *ps =
        s->priv_data;

    AVS3AHeaderInfo hdf;

    int header_offset;
    int frame_size;
    int ret;
    int used;

    *poutbuf = NULL;
    *poutbuf_size = 0;

    if (buf_size <= 0)
        return 0;


    /*
     * ==============================================================
     * 1. Currently assembling a frame
     * ==============================================================
     *
     * Do NOT resynchronize inside a known frame. We don't know whether
     * a byte pattern inside the coded payload is an actual sync word.
     *
     * Once this frame has been completed, the next parser invocation
     * starts with a fresh header search.
     */
    if (ps->frame_size > 0) {

        ret =
            avs3a_consume_frame_data(
                ps,
                poutbuf,
                poutbuf_size,
                buf,
                buf_size,
                &used);

        /*
         * Whether the frame was completed or not, 'used' is exactly the
         * amount consumed from the caller input.
         */
        return used;
    }


    /*
     * ==============================================================
     * 2. No current frame:
     *
     *    Find a fresh AV3A header.
     * ==============================================================
     */


    /*
     * --------------------------------------------------------------
     * 2a. We have synchronization tail bytes from the previous call.
     *
     * Search across the previous/current boundary.
     * --------------------------------------------------------------
     */
    if (ps->sync_size > 0) {

        uint8_t probe[
            AVS3A_SYNC_TAIL_SIZE +
            MAX_NBYTES_FRAME_HEADER
        ];

        int probe_size;
        int probe_input_size;
        int probe_header_offset;

        /*
         * Use enough new bytes to fully validate a header that starts
         * in the retained synchronization tail.
         */
        probe_input_size =
            FFMIN(
                buf_size,
                MAX_NBYTES_FRAME_HEADER);

        memcpy(
            probe,
            ps->sync_buf,
            ps->sync_size);

        if (probe_input_size > 0) {

            memcpy(
                probe + ps->sync_size,
                buf,
                probe_input_size);
        }

        probe_size =
            ps->sync_size + probe_input_size;

        probe_header_offset = -1;

        if (probe_size >= MAX_NBYTES_FRAME_HEADER &&
            avs3a_find_header(
                probe,
                probe_size,
                &probe_header_offset,
                &hdf)) {

            /*
             * ------------------------------------------------------
             * Header begins inside the old synchronization tail.
             * ------------------------------------------------------
             */
            if (probe_header_offset < ps->sync_size) {

                int old_part_offset =
                    probe_header_offset;

                int old_part_size =
                    ps->sync_size - old_part_offset;

                const uint8_t *old_part =
                    ps->sync_buf + old_part_offset;

                frame_size =
                    avs3a_calculate_frame_size(
                        hdf.total_bitrate,
                        hdf.sampling_rate);

                if (frame_size < 0) {

                    /*
                     * Invalid frame size. Drop synchronization state
                     * and consume the current input.
                     */
                    avs3a_clear_sync(ps);

                    return buf_size;
                }

                ps->frame_size = frame_size;
                ps->remaining = frame_size;

                avs3a_update_codec_context(
                    s,
                    avctx,
                    &hdf);

                /*
                 * The bytes beginning at probe_header_offset are the
                 * beginning of the current AV3A frame.
                 *
                 * They consist of:
                 *
                 *   old sync tail bytes
                 *   +
                 *   current parser input
                 */
                avs3a_clear_sync(ps);

                /*
                 * Feed the old part first.
                 */
                if (old_part_size > 0) {

                    if (old_part_size >= ps->remaining) {

                        /*
                         * Impossible for normal AV3A frames because the
                         * old part is at most 8 bytes, but keep this
                         * defensive path.
                         */
                        int old_size =
                            ps->remaining;

                        const uint8_t *old_ptr =
                            old_part;

                        int old_available =
                            old_part_size;

                        ret =
                            ff_combine_frame(
                                &ps->pc,
                                old_size,
                                &old_ptr,
                                &old_available);

                        if (ret >= 0) {

                            *poutbuf = old_ptr;
                            *poutbuf_size = old_available;

                            ps->frame_size = 0;
                            ps->remaining = 0;

                            /*
                             * No current input was consumed.
                             */
                            return 0;
                        }

                        ps->frame_size = 0;
                        ps->remaining = 0;

                        return 0;
                    }

                    ret =
                        ff_combine_frame(
                            &ps->pc,
                            END_NOT_FOUND,
                            &old_part,
                            &old_part_size);

                    if (ret >= 0) {

                        ps->frame_size = 0;
                        ps->remaining = 0;

                        return 0;
                    }

                    /*
                     * The retained old bytes are now part of the
                     * current AV3A frame.
                     */
                    ps->remaining -= old_part_size;
                }

                /*
                 * All bytes in the current parser input occur after
                 * the retained synchronization bytes.
                 */
                ret =
                    avs3a_consume_frame_data(
                        ps,
                        poutbuf,
                        poutbuf_size,
                        buf,
                        buf_size,
                        &used);

                return used;
            }

            /*
             * ------------------------------------------------------
             * Header begins in the current input.
             *
             * Any old sync bytes are stale/garbage.
             * ------------------------------------------------------
             */
            header_offset =
                probe_header_offset -
                ps->sync_size;

            avs3a_clear_sync(ps);

            if (header_offset < 0)
                header_offset = 0;

            if (header_offset > 0)
                return header_offset;

            /*
             * Header starts exactly at current input byte 0.
             */
            frame_size =
                avs3a_calculate_frame_size(
                    hdf.total_bitrate,
                    hdf.sampling_rate);

            if (frame_size < 0)
                return 1;

            ps->frame_size = frame_size;
            ps->remaining = frame_size;

            avs3a_update_codec_context(
                s,
                avctx,
                &hdf);

            ret =
                avs3a_consume_frame_data(
                    ps,
                    poutbuf,
                    poutbuf_size,
                    buf,
                    buf_size,
                    &used);

            return used;
        }

        /*
         * Boundary search did not find a valid header.
         *
         * Search the current input directly too, because it may contain
         * a complete header later in the buffer.
         */
        if (buf_size >= MAX_NBYTES_FRAME_HEADER &&
            avs3a_find_header(
                buf,
                buf_size,
                &header_offset,
                &hdf)) {

            /*
             * Old synchronization bytes are no longer relevant.
             */
            avs3a_clear_sync(ps);

            /*
             * Return bytes before the recovered header.
             */
            if (header_offset > 0)
                return header_offset;

            frame_size =
                avs3a_calculate_frame_size(
                    hdf.total_bitrate,
                    hdf.sampling_rate);

            if (frame_size < 0)
                return 1;

            ps->frame_size = frame_size;
            ps->remaining = frame_size;

            avs3a_update_codec_context(
                s,
                avctx,
                &hdf);

            ret =
                avs3a_consume_frame_data(
                    ps,
                    poutbuf,
                    poutbuf_size,
                    buf,
                    buf_size,
                    &used);

            return used;
        }

        /*
         * No header found.
         *
         * Save the final 8 bytes for the next parser invocation.
         */
        avs3a_save_sync_tail(
            ps,
            buf,
            buf_size);

        /*
         * All current input has been copied into our small sync buffer.
         */
        return buf_size;
    }


    /*
     * --------------------------------------------------------------
     * 2b. No synchronization tail.
     *
     * Search the current input directly.
     * --------------------------------------------------------------
     */
    if (buf_size >= MAX_NBYTES_FRAME_HEADER &&
        avs3a_find_header(
            buf,
            buf_size,
            &header_offset,
            &hdf)) {

        /*
         * Header found later in the current input.
         */
        if (header_offset > 0)
            return header_offset;

        /*
         * Header starts at byte zero.
         */
        frame_size =
            avs3a_calculate_frame_size(
                hdf.total_bitrate,
                hdf.sampling_rate);

        if (frame_size < 0)
            return 1;

        ps->frame_size = frame_size;
        ps->remaining = frame_size;

        avs3a_update_codec_context(
            s,
            avctx,
            &hdf);

        ret =
            avs3a_consume_frame_data(
                ps,
                poutbuf,
                poutbuf_size,
                buf,
                buf_size,
                &used);

        return used;
    }


    /*
     * --------------------------------------------------------------
     * No valid AV3A header in the current input.
     *
     * Keep the last 8 bytes so that a header spanning the next parser
     * call can be reconstructed.
     * --------------------------------------------------------------
     */
    avs3a_save_sync_tail(
        ps,
        buf,
        buf_size);

    /*
     * All current bytes have been copied into sync_buf.
     */
    return buf_size;
}


/*
 * FFmpeg 9.0.1 parser registration.
 *
 * FFCodecParser / PARSER_CODEC_LIST are defined in parser_internal.h.
 */
const FFCodecParser ff_av3a_parser = {
    PARSER_CODEC_LIST(AV_CODEC_ID_AVS3_AUDIO),

    .priv_data_size = sizeof(AVS3AParseContext),

    .parse = raw_av3a_parse,

    .close = ff_parse_close,
};