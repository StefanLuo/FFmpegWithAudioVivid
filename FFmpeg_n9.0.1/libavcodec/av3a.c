/*
 * Audio Vivid Codec
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <stdio.h>
#include "get_bits.h"
#include "libavutil/channel_layout.h"
#include "libavutil/samplefmt.h"
#include "av3a.h"

// Use definitions from avs3a/avs3_rom_com.c
extern const McChanelConfig avpriv_mcChannelConfigTable[AVS3_SIZE_MC_CONFIG_TABLE];
extern const CodecBitrateConfig avpriv_codecBitrateConfigTable[CHANNEL_CONFIG_UNKNOWN];

const int avpriv_avs3_samplingrate_table[AVS3_SIZE_FS_TABLE] = {
    192000, 96000, 48000, 44100, 32000, 24000, 22050, 16000, 8000};

uint16_t avpriv_read_av3a_frame_header(AVS3AHeaderInfo *hdf, const uint8_t *buf, const int byte_size){

    GetBitContext gb;

    uint8_t nn_type, codec_id, content_type, samping_rate_index, hoa_order, resolution_index, bitdepth, resolution;
    AVS3AChannelConfig channel_config;

    int16_t channels, objects;
    uint64_t channel_layout = 0;
    int64_t bitratePerObj, bitrateBedMc, total_bitrate;
    uint8_t coding_profile, num_chan_index, obj_brt_idx, bed_brt_idx, brt_idx;

    // 增加边界检查，防止 4K 流切片导致的内存越界
    if (byte_size < MAX_NBYTES_FRAME_HEADER) {
        return AVERROR_INVALIDDATA;
    }

    // Read max header length into bs buffer
    init_get_bits8(&gb, buf, MAX_NBYTES_FRAME_HEADER);

    // 12 bits for frame sync word
    if (get_bits(&gb, 12) != AVS3_AUDIO_SYNC_WORD){
        return AVERROR_INVALIDDATA;
    }

    // 4 bits for codec id
    codec_id = get_bits(&gb, 4);
    if (codec_id != 2){
        return AVERROR_INVALIDDATA;
    }

    // 1 bits for anc data
    if (get_bits(&gb, 1) != 0){
        return AVERROR_INVALIDDATA;
    }

    // 3 bits nn type
    nn_type = get_bits(&gb, 3); 

    // 3 bits for coding profile
    coding_profile = get_bits(&gb, 3);

    // 4 bits for sampling index
    samping_rate_index = get_bits(&gb, 4);

    // skip 8 bits for CRC first part
    skip_bits(&gb, 8);

    if (coding_profile == 0){

        content_type = 0;

        // 7 bits for mono/stereo/MC
        num_chan_index = get_bits(&gb, 7);

        channel_config = (AVS3AChannelConfig)num_chan_index;
        
        switch (channel_config)
        {
        case CHANNEL_CONFIG_MONO:
            channels = 1;
            channel_layout = AV_CH_LAYOUT_MONO;
            break;
        case CHANNEL_CONFIG_STEREO:
            channels = 2;
            channel_layout = AV_CH_LAYOUT_STEREO;
            break;
        case CHANNEL_CONFIG_MC_4_0:
            channels = 4;
            channel_layout = AV_CH_LAYOUT_4POINT0;
            break;
        case CHANNEL_CONFIG_MC_5_1:
            channels = 6;
            channel_layout = AVS3P3_CH_LAYOUT_5POINT1;
            break;
        case CHANNEL_CONFIG_MC_7_1:
            channels = 8;
            channel_layout = AV_CH_LAYOUT_7POINT1;
            break;
        case CHANNEL_CONFIG_MC_5_1_2:
            channels = 8;
            channel_layout = AV_CH_LAYOUT_5POINT1POINT2_BACK;
            break;
        case CHANNEL_CONFIG_MC_5_1_4:
            channels = 10;
            channel_layout = AV_CH_LAYOUT_5POINT1POINT4_BACK;
            break;
        case CHANNEL_CONFIG_MC_7_1_2:
            channels = 10;
            channel_layout = AV_CH_LAYOUT_7POINT1POINT2;
            break;
        case CHANNEL_CONFIG_MC_7_1_4:
            channels = 12;
            channel_layout = AV_CH_LAYOUT_7POINT1POINT4_BACK;
            break;
        case CHANNEL_CONFIG_MC_22_2:
            channels = 24;
            channel_layout = AV_CH_LAYOUT_22POINT2;
            break;
        default:
            break;
        }
    }
    else if (coding_profile == 1){

        // sound bed type, 2bits
        uint8_t soundBedType = get_bits(&gb, 2);

        if (soundBedType == 0){   

            content_type = 1;

            // for only objects
            // object number, 7 bits
            objects = get_bits(&gb, 7);
            objects += 1;

            // bitrate index for each obj, 4 bits
            obj_brt_idx = get_bits(&gb, 4);

            total_bitrate = avpriv_codecBitrateConfigTable[CHANNEL_CONFIG_MONO].bitrateTable[obj_brt_idx] * objects;
        }
        else if (soundBedType == 1){

            content_type = 2;

            // for MC + objs
            // channel number index, 7 bits
            num_chan_index = get_bits(&gb, 7);

            // bitrate index for sound bed, 4 bits
            bed_brt_idx = get_bits(&gb, 4);

            // object number, 7 bits
            objects = get_bits(&gb, 7);
            objects += 1;

            // bitrate index for each obj, 4 bits
            obj_brt_idx = get_bits(&gb, 4);

            // channelNumIdx for sound bed
            channel_config = (AVS3AChannelConfig)num_chan_index;

            // sound bed bitrate
            bitrateBedMc = avpriv_codecBitrateConfigTable[channel_config].bitrateTable[bed_brt_idx];

            // numChannels for sound bed
            for (int16_t i = 0; i < AVS3_SIZE_MC_CONFIG_TABLE; i++)
            {
                if (channel_config == avpriv_mcChannelConfigTable[i].channelNumConfig)
                {
                    channels = avpriv_mcChannelConfigTable[i].numChannels;
                }
            }

            // bitrate per obj
            bitratePerObj = avpriv_codecBitrateConfigTable[CHANNEL_CONFIG_MONO].bitrateTable[obj_brt_idx];

            // add num chans and num objs to get total chans
            /* channels += objects; */

            total_bitrate = bitrateBedMc + bitratePerObj * objects;
        }
    }
    else if (coding_profile == 2){
        content_type = 3;
        
        // 4 bits for HOA order
        hoa_order = get_bits(&gb, 4);
        hoa_order += 1;

        switch (hoa_order)
        {
        case 1:
            channels = 4;
            channel_config = CHANNEL_CONFIG_HOA_ORDER1;
            break;
        case 2:
            channels = 9;
            channel_config = CHANNEL_CONFIG_HOA_ORDER2;
            break;
        case 3:
            channels = 16;
            channel_config = CHANNEL_CONFIG_HOA_ORDER3;
            break;
        default:
            break;
        }
    }else{
        return AVERROR_INVALIDDATA;
    }

    // 2 bits for bit depth
    resolution_index = get_bits(&gb, 2);
    switch (resolution_index){
    case 0:
        bitdepth = AV_SAMPLE_FMT_U8;
        resolution = 8;
        break;
    case 1:
        bitdepth = AV_SAMPLE_FMT_S16;
        resolution = 16;
        break;
    case 2:
        resolution = 24;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    if (coding_profile != 1){
        // 4 bits for bitrate index
        brt_idx = get_bits(&gb, 4);
        total_bitrate = avpriv_codecBitrateConfigTable[channel_config].bitrateTable[brt_idx];
    }

    // 8 bits for CRC second part
    skip_bits(&gb, 8);

    /* AVS3P6 M6954-v3 */
    hdf->codec_id = codec_id;
    hdf->sampling_rate_index = samping_rate_index;
    hdf->sampling_rate = avpriv_avs3_samplingrate_table[samping_rate_index];
    hdf->bitdepth = bitdepth;

    hdf->nn_type = nn_type;
    hdf->content_type = content_type;

    if(hdf->content_type == 0){
        hdf->channel_num_index = num_chan_index;
        hdf->channels = channels;
        hdf->objects = 0;
        hdf->total_channels = channels; 
        hdf->channel_layout = channel_layout;
    }else if(hdf->content_type == 1){
        hdf->objects = objects;
        hdf->channels = 0;
        hdf->total_channels = objects; 
    }else if(hdf->content_type == 2){
        hdf->channel_num_index = num_chan_index;
        hdf->channels = channels;
        hdf->objects = objects;
        hdf->total_channels = channels + objects; 
        hdf->channel_layout = channel_layout;
    }else if(hdf->content_type == 3){
        hdf->hoa_order = hoa_order;
        hdf->channels = channels;
        hdf->total_channels = channels; 
    }else{
        return AVERROR_INVALIDDATA;
    }

    hdf->total_bitrate = total_bitrate;
    hdf->resolution = resolution;
    hdf->resolution_index = resolution_index;

    return 0;
}