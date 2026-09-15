/* ====================================================================================================================

  The copyright in this software is being made available under the License included below.
  No express or implied licenses to any party's patent rights are granted by this license.

  Copyright (c) 2022, HUAWEI TECHNOLOGIES CO., LTD. All rights reserved.
  Copyright (c) 2022, XIAOMI COMMUNICATIONS CO., LTD. All rights reserved.
  Copyright (c) 2022, BEIJING ZITIAO NETWORK TECHNOLOGY CO., LTD. All rights reserved.
  Copyright (c) 2022, BEIJING SINECORE MICROSEMI TECHNOLOGY CO., LTD. All rights reserved.
  Copyright (c) 2022, WAVARTS TECHNOLOGIES CO., LTD. All rights reserved.
  Copyright (c) 2022, PEKING UNIVERSITY. All rights reserved.
  Copyright (c) 2022, TSINGHUA UNIVERSITY. All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are permitted only for
  the purpose of developing standards within Audio and Video Coding Standard Workgroup of China (AVS) and for testing and
  promoting such standards. The following conditions are required to be met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and
      the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
      the following disclaimer in the documentation and/or other materials provided with the distribution.
    * The name of the above copyright owners may not be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

==================================================================================================================== */

#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include "avs3_options.h"
#include "avs3_rom_com.h"
#include "avs3_stat_com.h"
#include "avs3_prot_com.h"
#include "avs3_prot_dec.h"

void ResetBitstream(AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream)
{
    short i;

    for (i = 0; i < MAX_BS_BYTES; i++)
    {
        hBitstream->bitstream[i] = 0;
    }

    hBitstream->nextBitPos = 0;

    return;
}


#ifdef BS_HEADER_COMPAT
short Avs3ParseBsFrameHeader(
    AVS3DecoderHandle hAvs3Dec,
    FILE *fBitstream,
    const uint8_t *data,
    size_t data_size,
    int16_t isInitFrame,
    uint16_t *crcBs,
    int32_t *headerBsBytes
)
{
    uint8_t headerBs[MAX_NBYTES_FRAME_HEADER];
	uint32_t nextBitPos = 0;

	if (!hAvs3Dec || (!fBitstream && !data)) {
		return AVS3_FALSE;
	}

	/*
     * Read header.
     *
     * FILE mode:
     *     fread from file
     *
     * Memory mode:
     *     copy from AVPacket buffer
     */
	if (data) {
		if (data_size < MAX_NBYTES_FRAME_HEADER) {
			return AVS3_FALSE;
		}

		memset(headerBs,0,sizeof(headerBs));

		memcpy(headerBs, data, MAX_NBYTES_FRAME_HEADER);
	} else {
		if (fread(headerBs, sizeof(uint8_t), MAX_NBYTES_FRAME_HEADER, fBitstream) != MAX_NBYTES_FRAME_HEADER) {
			return AVS3_FALSE;
		}
	}

    // Sync word, 12 bits
    uint16_t syncWord;
    syncWord = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_SYNC_WORD);
    // Check sync word
    if (syncWord != SYNC_WORD_COMPAT) {
        return AVS3_FALSE;
    }

    // audio codec id, 4 bits
    uint16_t audioCodecId;
    audioCodecId = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_AUDIO_CODEC_ID);
    // Check audio codec id, should be 2 for HW branch
    if (audioCodecId != 2) {
        return AVS3_FALSE;
    }

    // anc data index, fixed to 0 in HW branch, 1 bit
    uint16_t ancDataIndex;
    ancDataIndex = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_ANC_DATA_INDEX);
    // Check anc data index
    if (ancDataIndex == 1) {
        return AVS3_FALSE;
    }

#ifdef SUPPORT_NNTYPE_LC
    // NN type, 3 bit
    // 0 for default main, 1 for default lc
    uint16_t nnTypeConfig;
    nnTypeConfig = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_NN_TYPE);
#endif

    // coding profile, 3 bit
    // 0 for mono/stereo/mc, 1 for channel + obj mix, 2 for hoa
    uint16_t codingProfile;
    codingProfile = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_CODING_PROFILE);

    // sampling rate index, 4 bit
    uint16_t samplingRateIdx;
    samplingRateIdx = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_SAMPLING_RATE_INDEX);

    // CRC first part
    uint16_t crcTmp;
    crcTmp = (uint16_t)GetNextIndice(headerBs, &nextBitPos, AVS3_BS_BYTE_SIZE);
    crcTmp = crcTmp << AVS3_BS_BYTE_SIZE;

    uint16_t channelNumIdx;
    uint16_t numObjs;
    uint16_t hoaOrder;
#ifdef MIX_EXT
    uint16_t soundBedType;
    uint16_t bitrateIdxPerObj;
    uint16_t bitrateIdxBedMc;
#endif
    if (codingProfile == 0) {
        // channel number index
        // for mono/stereo/mc, 7 bits
        channelNumIdx = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_CHANNEL_NUMBER_INDEX);
    } else if (codingProfile == 1) {
#ifndef MIX_EXT
        // for channel + obj mix
        // channel number index, 7 bits
        channelNumIdx = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_CHANNEL_NUMBER_INDEX);
        // object number, 7 bits
        numObjs = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_NUM_OBJS);
        numObjs += 1;
#else
        // sound bed type, 2bits
        soundBedType = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_SOUNDBED_TYPE);

        if (soundBedType == 0) {
            // for only objs
            // object number, 7 bits
            numObjs = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_NUM_OBJS);
            numObjs += 1;
            // bitrate index for each obj, 4 bits
            bitrateIdxPerObj = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_BITRATE_INDEX);
        }
        else if (soundBedType == 1) {
            // for MC+objs
            // channel number index, 7 bits
            channelNumIdx = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_CHANNEL_NUMBER_INDEX);
            // bitrate index for sound bed, 4 bits
            bitrateIdxBedMc = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_BITRATE_INDEX);

            // object number, 7 bits
            numObjs = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_NUM_OBJS);
            numObjs += 1;
            // bitrate index for each obj, 4 bits
            bitrateIdxPerObj = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_BITRATE_INDEX);
        }
#endif
    } else if (codingProfile == 2) {
        // for HOA, 4 bits
        hoaOrder = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_HOA_ORDER);
        hoaOrder += 1;
    }

    // resolution, i.e. bitDepth, 2 bits
    uint16_t resolution;
    resolution = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_RESOLUTION);

    // bitrate index, 4 bits
    uint16_t bitrateIdx;
#ifndef MIX_EXT
    bitrateIdx = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_BITRATE_INDEX);
#else
    if (codingProfile != 1) {
        bitrateIdx = (uint16_t)GetNextIndice(headerBs, &nextBitPos, NBITS_BITRATE_INDEX);
    }
#endif

    // second part of CRC, 8 bits
    crcTmp += (uint16_t)GetNextIndice(headerBs, &nextBitPos, AVS3_BS_BYTE_SIZE);

    // rewind bs file if needed
	/*
     * Important:
     *
     * FILE mode:
     *     restore FILE position
     *
     * Memory mode:
     *     only report header length
     */
	int32_t parsedHeaderBytes = (int32_t)((nextBitPos + 7) / 8);

    if(headerBsBytes){
        *headerBsBytes = parsedHeaderBytes;
    }

    if (fBitstream) {
        if (isInitFrame) {
            fseek(fBitstream, 0, SEEK_SET);
        } else if (parsedHeaderBytes < MAX_NBYTES_FRAME_HEADER) {
#ifndef MIX_EXT
            fseek(fBitstream, -1, SEEK_CUR);
#else
            fseek(fBitstream, parsedHeaderBytes - MAX_NBYTES_FRAME_HEADER, SEEK_CUR);
#endif
        }
    }

    // Config decoder
    // sampling frequency
    hAvs3Dec->outputFs = avs3SamplingRateTable[samplingRateIdx];

    // frame length
    hAvs3Dec->frameLength = GetFrameLength(hAvs3Dec->outputFs);

    // bitdepth
    if (resolution == 0) {
        hAvs3Dec->bitDepth = 8;
    } else if (resolution == 1) {
        hAvs3Dec->bitDepth = 16;
    } else if (resolution == 2) {
        hAvs3Dec->bitDepth = 24;
    }

#ifdef SUPPORT_NNTYPE_LC
    // NN type config
    hAvs3Dec->nnTypeConfig = (NnTypeConfig)nnTypeConfig;
#endif

    // Codec format and bitrate
    if (codingProfile == 0) {
        // mono/stereo/mc
#ifdef MIX_EXT
        hAvs3Dec->isMixedContent = 0;
#endif
        hAvs3Dec->channelNumConfig = (ChannelNumConfig)channelNumIdx;
        if (hAvs3Dec->channelNumConfig == CHANNEL_CONFIG_MONO) {
            // mono
            hAvs3Dec->avs3CodecFormat = AVS3_MONO_FORMAT;
            hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            hAvs3Dec->numChansOutput = 1;
        } else if (hAvs3Dec->channelNumConfig == CHANNEL_CONFIG_STEREO) {
            // stereo
            hAvs3Dec->avs3CodecFormat = AVS3_STEREO_FORMAT;
            hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            hAvs3Dec->numChansOutput = 2;
        } else if (hAvs3Dec->channelNumConfig <= CHANNEL_CONFIG_MC_7_1_4) {
            // mc
            hAvs3Dec->avs3CodecFormat = AVS3_MC_FORMAT;
            hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            for (int16_t i = 0; i < AVS3_SIZE_MC_CONFIG_TABLE; i++) {
                if (hAvs3Dec->channelNumConfig == avpriv_mcChannelConfigTable[i].channelNumConfig) {
                    hAvs3Dec->numChansOutput = avpriv_mcChannelConfigTable[i].numChannels;
                }
            }
#ifdef MIX_EXT
            hAvs3Dec->hasLfe = 1;
#endif
#ifdef IMPR_MIX_BIT_ALLOC
            if (hAvs3Dec->channelNumConfig == CHANNEL_CONFIG_MC_4_0) {
                hAvs3Dec->hasLfe = 0;
            }
#endif
        } else {
            return AVS3_FALSE;
        }
    } else if (codingProfile == 1) {
        // mix
#ifndef MIX_EXT
        hAvs3Dec->channelNumConfig = (ChannelNumConfig)channelNumIdx;

        hAvs3Dec->avs3CodecFormat = AVS3_MIX_FORMAT;
        hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
        for (int16_t i = 0; i < AVS3_SIZE_MC_CONFIG_TABLE; i++) {
            if (hAvs3Dec->channelNumConfig == avpriv_mcChannelConfigTable[i].channelNumConfig) {
                hAvs3Dec->numChansOutput = avpriv_mcChannelConfigTable[i].numChannels;
            }
        }
        hAvs3Dec->numObjsOutput = numObjs;
        // add num chans and num objs to get total chans
        hAvs3Dec->numChansOutput += hAvs3Dec->numObjsOutput;
#else
        hAvs3Dec->isMixedContent = 1;

        // sound bed type
        hAvs3Dec->soundBedType = soundBedType;

        if (hAvs3Dec->soundBedType == 0) {
            // object number
            hAvs3Dec->numObjsOutput = numObjs;
            hAvs3Dec->numChansOutput = numObjs;

            if (numObjs == 1) {
                hAvs3Dec->avs3CodecFormat = AVS3_MONO_FORMAT;
                hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            }
            else if (numObjs == 2) {
                hAvs3Dec->avs3CodecFormat = AVS3_STEREO_FORMAT;
                hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            }
            else if (numObjs >= 3) {
                hAvs3Dec->avs3CodecFormat = AVS3_MC_FORMAT;
                hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            }

            // channelNumConfig not used for pure objs
            hAvs3Dec->channelNumConfig = CHANNEL_CONFIG_UNKNOWN;

            // bitrate per obj
            hAvs3Dec->bitratePerObj = avpriv_codecBitrateConfigTable[CHANNEL_CONFIG_MONO].bitrateTable[bitrateIdxPerObj];

            // total bitrate, only objs
            hAvs3Dec->totalBitrate = hAvs3Dec->numObjsOutput * hAvs3Dec->bitratePerObj;

            // for pure objs, lfe not exist
            hAvs3Dec->hasLfe = 0;
        }
        else if (hAvs3Dec->soundBedType == 1) {
            hAvs3Dec->avs3CodecFormat = AVS3_MC_FORMAT;
            hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;

            // channelNumIdx for sound bed
            hAvs3Dec->channelNumConfig = (ChannelNumConfig)channelNumIdx;

            // sound bed bitrate
            hAvs3Dec->bitrateBedMc = avpriv_codecBitrateConfigTable[hAvs3Dec->channelNumConfig].bitrateTable[bitrateIdxBedMc];

            // numChannels for sound bed
            for (int16_t i = 0; i < AVS3_SIZE_MC_CONFIG_TABLE; i++) {
                if (hAvs3Dec->channelNumConfig == avpriv_mcChannelConfigTable[i].channelNumConfig) {
                    hAvs3Dec->numChansOutput = avpriv_mcChannelConfigTable[i].numChannels;
                }
            }

            // object number
            hAvs3Dec->numObjsOutput = numObjs;

            // bitrate per obj
            hAvs3Dec->bitratePerObj = avpriv_codecBitrateConfigTable[CHANNEL_CONFIG_MONO].bitrateTable[bitrateIdxPerObj];

            // add num chans and num objs to get total chans
            hAvs3Dec->numChansOutput += hAvs3Dec->numObjsOutput;

            // total bitrate, sound bed + objs
            hAvs3Dec->totalBitrate = hAvs3Dec->bitrateBedMc + hAvs3Dec->numObjsOutput * hAvs3Dec->bitratePerObj;

            // for sound bed + obj mix
#ifndef IMPR_MIX_BIT_ALLOC
            // if sound bed is stereo, no LFE, if sound bed is mc, with lfe
            if (hAvs3Dec->channelNumConfig == CHANNEL_CONFIG_STEREO) {
#else
            // if sound bed is stereo/MC4.0, no LFE, if sound bed is other mc configs, with lfe
            if (hAvs3Dec->channelNumConfig == CHANNEL_CONFIG_STEREO ||
                hAvs3Dec->channelNumConfig == CHANNEL_CONFIG_MC_4_0) {
#endif
                hAvs3Dec->hasLfe = 0;
            }
            else {
                hAvs3Dec->hasLfe = 1;
            }
        }
#endif
    } else if (codingProfile == 2) {
        // hoa
        hAvs3Dec->channelNumConfig = CHANNEL_CONFIG_UNKNOWN;
        if (hoaOrder == 1) {
            hAvs3Dec->channelNumConfig = CHANNEL_CONFIG_HOA_ORDER1;
        } else if (hoaOrder == 2) {
            hAvs3Dec->channelNumConfig = CHANNEL_CONFIG_HOA_ORDER2;
        } else if (hoaOrder == 3) {
            hAvs3Dec->channelNumConfig = CHANNEL_CONFIG_HOA_ORDER3;
        }

        hAvs3Dec->avs3CodecFormat = AVS3_HOA_FORMAT;
        hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
        hAvs3Dec->numChansOutput = (hoaOrder + 1) * (hoaOrder + 1);

#ifdef MIX_EXT
        hAvs3Dec->isMixedContent = 0;
#endif
    }

    // total bitrate
#ifndef MIX_EXT
    hAvs3Dec->totalBitrate = avpriv_codecBitrateConfigTable[hAvs3Dec->channelNumConfig].bitrateTable[bitrateIdx];
#else
    if (hAvs3Dec->isMixedContent == 0) {
        hAvs3Dec->totalBitrate = avpriv_codecBitrateConfigTable[hAvs3Dec->channelNumConfig].bitrateTable[bitrateIdx];
    }
#endif

    // if not first frame
    if (isInitFrame == 0) {
        // copy crc bs
		if (crcBs) {
			*crcBs = crcTmp;
		}

        // update bitrate
        hAvs3Dec->lastTotalBrate = hAvs3Dec->totalBitrate;
#ifndef SUPPORT_HIGH_BR_MIX
        hAvs3Dec->bitsPerFrame = (short)(((float)hAvs3Dec->totalBitrate / (float)hAvs3Dec->outputFs) * hAvs3Dec->frameLength);
#else
        hAvs3Dec->bitsPerFrame = (int32_t)(((float)hAvs3Dec->totalBitrate / (float)hAvs3Dec->outputFs) * hAvs3Dec->frameLength);
#endif

        // subtract frame bs header bits
#ifndef MIX_EXT
        if (hAvs3Dec->avs3CodecFormat == AVS3_MONO_FORMAT) {
            hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_MONO;
        }
        else if (hAvs3Dec->avs3CodecFormat == AVS3_STEREO_FORMAT) {
            hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_STEREO;
        }
        else if (hAvs3Dec->avs3CodecFormat == AVS3_MC_FORMAT) {
            hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_MC;
        }
        else if (hAvs3Dec->avs3CodecFormat == AVS3_HOA_FORMAT) {
            hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_HOA;
        }
        else if (hAvs3Dec->avs3CodecFormat == AVS3_MIX_FORMAT) {
            hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_MIX;
        }
#else
        if (hAvs3Dec->isMixedContent == 0) {
            if (hAvs3Dec->avs3CodecFormat == AVS3_MONO_FORMAT) {
                hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_MONO;
            }
            else if (hAvs3Dec->avs3CodecFormat == AVS3_STEREO_FORMAT) {
                hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_STEREO;
            }
            else if (hAvs3Dec->avs3CodecFormat == AVS3_MC_FORMAT) {
                hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_MC;
            }
            else if (hAvs3Dec->avs3CodecFormat == AVS3_HOA_FORMAT) {
                hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_HOA;
            }
        }
        else {
            if (hAvs3Dec->soundBedType == 0) {
                hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_MIX_SBT0;
            }
            else if (hAvs3Dec->soundBedType == 1) {
                hAvs3Dec->bitsPerFrame -= NBITS_FRAME_HEADER_MIX_SBT1;
            }
        }
#endif
    }

    return AVS3_TRUE;
}
#endif


short ReadBitstream(AVS3DecoderHandle hAvs3Dec, FILE* fBitstream) 
{
    size_t bytesPerFrame = 0;

    uint8_t* bitstream = hAvs3Dec->hBitstream->bitstream;

#ifdef CRC_CHECK
	// crc info from BS and calculated at decoder
    uint16_t crcBs, crcResult;
#endif

    if (fBitstream == NULL) 
    {
        return AVS3_FALSE;
    }

#ifdef BS_HEADER_COMPAT
    /* Read frame header info */
    Avs3ParseBsFrameHeader(
        hAvs3Dec,
        fBitstream,
        NULL,
        0,
        0,
#ifdef CRC_CHECK
        &crcBs,
#else
        NULL,
#endif
        NULL);
#endif

    bytesPerFrame = (uint32_t)(ceil((float)hAvs3Dec->bitsPerFrame / 8));

    /* frame payload */
    fread(bitstream, sizeof(uint8_t), bytesPerFrame, fBitstream);

#ifdef CRC_CHECK
    /* CRC check */
    crcResult = Crc16(bitstream, bytesPerFrame);
    if (crcResult != crcBs) {
        return AVS3_FALSE;
    }
#endif

    return AVS3_TRUE;
}

short ReadBitstreamMemory(AVS3DecoderHandle hAvs3Dec, const uint8_t *data, size_t data_size)
{
    uint8_t *bitstream;
    size_t bytesPerFrame;
    size_t payloadOffset;
    int32_t headerBytes = 0;

#ifdef CRC_CHECK
    uint16_t crcBs = 0;
	uint16_t crcResult;
#endif

    if (!hAvs3Dec || !hAvs3Dec->hBitstream || !data) {
        return AVS3_FALSE;
    }

    /*
     * Parse the current frame header directly from memory.
     *
     * This also updates bitsPerFrame and other per-frame decoder
     * parameters exactly as the original FILE-based path does.
     */
    if (!Avs3ParseBsFrameHeader(
            hAvs3Dec,
            NULL,
            data,
            data_size,
            0,
#ifdef CRC_CHECK
            &crcBs,
#else
            NULL,
#endif
            &headerBytes)) {
        return AVS3_FALSE;
    }
	/*
     * bitsPerFrame has already been updated by the header parser.
     */
    if (hAvs3Dec->bitsPerFrame <= 0) {
        return AVS3_FALSE;
    }

	bytesPerFrame = (size_t)((hAvs3Dec->bitsPerFrame + 7) / 8);

	/*
     * payload starts after header
     */
    payloadOffset = headerBytes;

	/*
     * Make sure the complete payload is inside the packet.
     */
    if (payloadOffset > data_size ||
        bytesPerFrame > data_size - payloadOffset) {
        return AVS3_FALSE;
    }

	bitstream = hAvs3Dec->hBitstream->bitstream;

	memset(bitstream, 0, MAX_BS_BYTES);

    memcpy(bitstream, data + payloadOffset, bytesPerFrame);

#ifdef CRC_CHECK
    crcResult = Crc16(bitstream, bytesPerFrame);

    if (crcResult != crcBs) {
        return AVS3_FALSE;
    }
#endif

    return AVS3_TRUE;
}

uint16_t GetNextIndice(uint8_t *bitstream, uint32_t *nextBitPos, int16_t numBits)
{
    uint16_t value;
    uint32_t byteIndex;
    uint16_t bitIndex;
    uint8_t mask;

    byteIndex = (*nextBitPos) >> 3;
    bitIndex = (*nextBitPos) & 0x7;
    mask = 1 << (7 - bitIndex);

    value = 0;
    for (int16_t i = 0; i < numBits; i++) {

        value <<= 1;
        if ((bitstream[byteIndex] & mask) != 0) {
            value += 1;
        }

        mask >>= 1;
        if (mask == 0) {
            byteIndex += 1;
            bitIndex = 0x7;
            mask = 0x80;
        }
    }

    *nextBitPos += numBits;

    return value;
}

#ifndef MCR_INTEGRATE
void DecodeStereoSideBits(AVS3_STEREO_DEC_HANDLE hDecStereo, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream)
#else
void DecodeStereoSideBits(AVS3DecoderHandle hAvs3Dec, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream)
#endif
{
#ifdef MCR_INTEGRATE
    AVS3_STEREO_DEC_HANDLE hDecStereo = hAvs3Dec->hDecStereo;
#endif

#ifndef MCR_INTEGRATE
    /* ms flag */
    hDecStereo->isMS = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_MS_FLAG);

    if (hDecStereo->isMS) {
        hDecStereo->ILD = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_ENERGY_BALENCE);
    }

    /* bit split ratio*/
    hDecStereo->bitsRatio = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_SPLIT_STEREO);
#else

    if (hDecStereo->useMcr == 0) {
        // MS stereo params
        /* ms flag */
        hDecStereo->isMS = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_MS_FLAG);

        if (hDecStereo->isMS) {
            hDecStereo->ILD = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_ENERGY_BALENCE);
        }

        /* bit split ratio*/
        hDecStereo->bitsRatio = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_SPLIT_STEREO);
    }
    else {
        // MCR stereo params
        MCR_CONFIG_HANDLE mcrConfig = &hDecStereo->mcrConfig;
        MCR_DATA_HANDLE mcrData = &hDecStereo->mcrData;

        // isShortWin flag for left channel
        int16_t isShortWin = (hAvs3Dec->hDecCore[0]->transformType == ONLY_SHORT_WINDOW);

        // MCR vq indices
        // for short frame, 8 bits for each vq index
        // for long/transition frame, 9 bits for each vq index
        for (int16_t i = 0; i < mcrConfig->vqVecNum[isShortWin]; i++) {
            mcrData->vqIdx[0][i] = (int16_t)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, mcrConfig->vqNumBits[isShortWin]);
            mcrData->vqIdx[1][i] = (int16_t)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, mcrConfig->vqNumBits[isShortWin]);
        }
    }

#endif

    return;
}


#ifdef MC_ENABLE
void DecodeMcSideBits(
    AVS3_MC_DEC_HANDLE hDecMc, 
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream, 
    short chBitRatios[MAX_CHANNELS]
)
{
    short i, pair;
    AVS3_MC_PAIR_DATA_HANDLE hPair;
    short channelPairIndex;

#ifdef IMPR_MIX_BIT_ALLOC
    hDecMc->hasSilFlag = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_HASSILFLAG);
    if (hDecMc->hasSilFlag) {
        for (i = 0; i < hDecMc->channelNum + hDecMc->objNum; i++) {
            if ((hDecMc->lfeExist) && (i == hDecMc->lfeChIdx)) {
                hDecMc->silFlag[i] = 0;
                continue;
            }
            hDecMc->silFlag[i] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_SILFLAG);
        }
    }
    else {
        /* set to default value */
        for (i = 0; i < hDecMc->channelNum + hDecMc->objNum; i++) {
            hDecMc->silFlag[i] = 0;
        }
    }
#endif

    /* pairing cnt */
    hDecMc->pairCnt = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, PAIR_NUM_DATA_BITS);

    for (i = 0; i < MAX_CHANNELS; i++) {
#ifndef MC_ILD_CBQUANT
        hDecMc->mcIld[i] = 0;
        hDecMc->scaleFlag[i] = 0;
#else
        hDecMc->mcIld[i] = MC_ILD_CBLEN;
#endif
    }

#ifndef PAIR_INFOR_ORDER
    for (pair = hDecMc->pairCnt - 1; pair >= 0; pair--)
#else
    for (pair = 0; pair < hDecMc->pairCnt; pair++)
#endif
    {
        hPair = &(hDecMc->hPair[pair]);

        /*get channel pair index from BS*/
        channelPairIndex = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, hDecMc->bitsPairIndex);
#ifndef IMPR_MIX_BIT_ALLOC
        Index2PairMapping(hPair, channelPairIndex, hDecMc->coupleChNum);
#else
        Index2PairMapping(hPair, channelPairIndex, hDecMc->channelNum + hDecMc->objNum);
#endif

        hDecMc->mcIld[hPair->ch1] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, MC_EB_BITS);
        hDecMc->mcIld[hPair->ch2] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, MC_EB_BITS);

#ifdef MC_ILD_CBQUANT
        hDecMc->mcIld[hPair->ch1] = AVS3_MIN(hDecMc->mcIld[hPair->ch1], MC_ILD_CBLEN - 1);
        hDecMc->mcIld[hPair->ch2] = AVS3_MIN(hDecMc->mcIld[hPair->ch2], MC_ILD_CBLEN - 1);
#endif

#ifndef MC_ILD_CBQUANT
        hDecMc->scaleFlag[hPair->ch1] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 1);
        hDecMc->scaleFlag[hPair->ch2] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 1);
#endif

    }

#ifndef IMPR_MIX_BIT_ALLOC
    for (i = 0; i < hDecMc->coupleChNum; i++) {
        chBitRatios[i] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_MC_RATIO);
    }
#else
    int j = 0;
    for (i = 0; i < hDecMc->channelNum + hDecMc->objNum; i++) {
        if ((hDecMc->lfeExist) && (i == hDecMc->lfeChIdx)) {
            continue;
        }
        if (hDecMc->silFlag[i] == 1) {
            continue;
        }
        chBitRatios[j] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_MC_RATIO);
        j++;
    }
#endif

    return;
}
#endif


void DecodeHoaSideBits(AVS3_HOA_DEC_DATA_HANDLE hDecHoa, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream) 
{
    short i, groupIdx;
    short nTotalChans;
    short groupChOffset;

    nTotalChans = hDecHoa->hHoaConfig->nTotalChansTransport;

#ifdef AVS3_HOA_FULL_SUPPORT
    hDecHoa->sceneType = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 4);

    hDecHoa->hHoaConfig->spatialAnalysis = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 1);
    
    if (hDecHoa->hHoaConfig->spatialAnalysis)
    {
        hDecHoa->numVL = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 4);
        assert(hDecHoa->numVL > 0);
    }
#else
#ifdef AVS3_HOA_BUG_FIXED    
    short tmp = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 2);
    assert(tmp == 0);
#endif
#endif

#ifdef AVS3_HOA_FULL_SUPPORT
    for (i = 0; i < hDecHoa->numVL; i++)
#else
    /* VL basis */
    for (i = 0; i < hDecHoa->numVote; i++)
#endif
    {
        hDecHoa->basisIdx[i] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, HOA_BASIS_BITS);
    }

    /* read bitstream by groups */
    for (groupIdx = 0; groupIdx < hDecHoa->hHoaConfig->nTotalChanGroups; groupIdx++)
    {
        groupChOffset = hDecHoa->hHoaConfig->groupChOffset[groupIdx];

        /* total channel pair in group */
        hDecHoa->pairIdx[groupIdx] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 4);

        assert(hDecHoa->pairIdx[groupIdx] >= 0);

        if (hDecHoa->pairIdx[groupIdx] > 0)
        {
            /* channels index */
            for (i = 0; i < hDecHoa->pairIdx[groupIdx]; i++)
            {
                hDecHoa->chIdx[groupIdx][i] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, hDecHoa->hHoaConfig->groupIndexBits[groupIdx]);

#ifdef AVS3_HOA_BUG_FIXED
                hDecHoa->dmxMode[groupIdx][i] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 1) + 1;
                assert(hDecHoa->dmxMode[groupIdx][i] == DMX_FULL_MS || hDecHoa->dmxMode[groupIdx][i] == DMX_SFB_MS);

                if (hDecHoa->dmxMode[groupIdx][i] == DMX_SFB_MS) {

                    for (short sfb = 0; sfb < N_SFB_HOA_LBR - 1; sfb++)
                    {
                        hDecHoa->sfbMask[groupIdx][i][sfb] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 1);
                        assert(hDecHoa->sfbMask[groupIdx][i][sfb] == 0 || hDecHoa->sfbMask[groupIdx][i][sfb] == 1);
                    }
                }
                else
                {
                    SetShort(hDecHoa->sfbMask[groupIdx][i], 1, N_SFB_HOA_LBR - 1);
                }
#endif 
            }

#ifndef HOA_ILD_CBQUANT
            /* group energy flag */
            for (i = 0; i < hDecHoa->hHoaConfig->groupChans[groupIdx]; i++)
            {
                hDecHoa->flagNrg[i + groupChOffset] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 1);
            }
#endif

            /* group ILD */
            for (i = 0; i < hDecHoa->hHoaConfig->groupChans[groupIdx]; i++)
            {
#ifndef HOA_ILD_CBQUANT
                hDecHoa->groupILD[i + groupChOffset] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 4);
#else
                hDecHoa->groupILD[i + groupChOffset] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, HOA_ILD_BITS);
#endif
            }
        }
        else
        {
            hDecHoa->pairIdx[groupIdx] = 0;

            for (i = 0; i < hDecHoa->hHoaConfig->groupChans[groupIdx]; i++)
            {
#ifndef HOA_ILD_CBQUANT
                hDecHoa->flagNrg[i + groupChOffset] = 0;
                hDecHoa->groupILD[i + groupChOffset] = 0;
#else
                hDecHoa->groupILD[i + groupChOffset] = MC_ILD_CBLEN;
#endif
            }
        }

        /* group bits */
        hDecHoa->groupBitsRatio[groupIdx] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 4);

        /* Bits ratio */
        for (i = 0; i < hDecHoa->hHoaConfig->groupChans[groupIdx]; i++)
        {
            hDecHoa->bitsRatio[groupIdx][i] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 4);
        }
    }

  

    return;
}


static void DecodeFdShapingSideBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream)
{
    // decode VQ indices
    if (hDecCore->lsfLbrFlag == 0) {
        // high bitrate LSF VQ indices
        hDecCore->lsfVqIndex[0] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE1_CB1_NBITS_HBR);
        hDecCore->lsfVqIndex[1] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE1_CB2_NBITS_HBR);
        hDecCore->lsfVqIndex[2] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE2_CB1_NBITS_HBR);
        hDecCore->lsfVqIndex[3] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE2_CB2_NBITS_HBR);
        hDecCore->lsfVqIndex[4] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE2_CB3_NBITS_HBR);
        hDecCore->lsfVqIndex[5] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE2_CB4_NBITS_HBR);
        hDecCore->lsfVqIndex[6] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE2_CB5_NBITS_HBR);
    }
    else if (hDecCore->lsfLbrFlag == 1) {
        // low bitrate LSF VQ indices
        hDecCore->lsfVqIndex[0] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE1_CB1_NBITS_LBR);
        hDecCore->lsfVqIndex[1] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE1_CB2_NBITS_LBR);
        hDecCore->lsfVqIndex[2] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE2_CB1_NBITS_LBR);
        hDecCore->lsfVqIndex[3] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE2_CB2_NBITS_LBR);
        hDecCore->lsfVqIndex[4] = GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, LSF_STAGE2_CB3_NBITS_LBR);
    }

    return;
}


#ifdef TD_SHAPING

static void DecodeTnsSideBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream)
{
    int16_t found;
    int16_t nBits;
    uint16_t code;
    TnsBsParam *tnsBsParam;

    // loop over filters
    for (int16_t i = 0; i < TNS_MAX_FILTER_NUM; i++) {

        // get handle
        tnsBsParam = &(hDecCore->tnsData.bsParam[i]);

        // get enable flag
        tnsBsParam->enable = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, TNS_NBITS_ENABLE);

        // enabled, read order and huffman codes
        if (tnsBsParam->enable == 1) {

            // get order
            tnsBsParam->order = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, TNS_NBITS_ORDER);
            tnsBsParam->order += 1;

            // get huffman codes
            for (int16_t j = 0; j < tnsBsParam->order; j++) {

                found = 0;
                nBits = 0;
                code = 0;

                while (found == 0) {

                    // read 1 bit from bitstream
                    code = (code << 1) + GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, 1);
                    nBits++;

                    for (int16_t k = 0; k < N_TNS_COEFF_CODES; k++) {
                        // code and nbits same as table item, found
                        if (code == tnsCodingTable[j][k].code &&
                            nBits == tnsCodingTable[j][k].nBits) {

                            tnsBsParam->parcorHuffCode[j] = code;
                            tnsBsParam->parcorNbits[j] = nBits;

                            found = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    return;
}

#endif


#ifdef BWE_DEVELOPE

static void DecodeBweSideBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream)
{
    int16_t i;
    int16_t flag;
    BweConfigHandle bweConfig = &hDecCore->bweConfig;
    BweDecDataHandle bweDecData = &hDecCore->bweDecData;

    // read sfb envelope
    for (i = 0; i < bweConfig->numSfb; i++) {
        bweDecData->sfbEnvQIdx[i] = (int16_t)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_BWE_ENV);
    }

    // read whitening level
    for (i = 0; i < bweConfig->numTiles; i++) {
        flag = (int16_t)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_BWE_WHITEN_ONOFF);
        if (flag == 0) {
            bweDecData->whiteningLevel[i] = BWE_WHITENING_OFF;
        }
        else {
            flag = (int16_t)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_BWE_WHITEN_MIDHIGH);
            if (flag == 0) {
                bweDecData->whiteningLevel[i] = BWE_WHITENING_MID;
            }
            else {
                bweDecData->whiteningLevel[i] = BWE_WHITENING_HIGH;
            }
        }
    }

    return;
}

#endif


void DecodeCoreSideBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream)
{
    // decode transform type
    hDecCore->transformType = (short)GetNextIndice(hBitstream->bitstream, &hBitstream->nextBitPos, NBITS_TRANSFORM_TYPE);

    // decode Fd spectrum shaping info
    DecodeFdShapingSideBits(hDecCore, hBitstream);

#ifdef TD_SHAPING
    // decode TNS info
    DecodeTnsSideBits(hDecCore, hBitstream);
#endif

#ifdef BWE_DEVELOPE
    // decode BWE info
    if (hDecCore->bwePresent == 1) {
        DecodeBweSideBits(hDecCore, hBitstream);
    }
#endif

    return;
}


void DecodeGroupBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream)
{
    int16_t j;

    if (hDecCore->transformType == ONLY_SHORT_WINDOW) {
        // Short window
        // get number groups
        hDecCore->numGroups = GetNextIndice(hBitstream->bitstream, &(hBitstream->nextBitPos), 1) + 1;

        // get group indicator
        if (hDecCore->numGroups == N_GROUP_SHORT_WIN) {
            for (j = 0; j < N_BLOCK_SHORT; j++) {
                hDecCore->groupIndicator[j] = GetNextIndice(hBitstream->bitstream, &(hBitstream->nextBitPos), 1);
            }
        }
        else {
            SetShort(hDecCore->groupIndicator, 0, N_BLOCK_SHORT);
        }
    }
    else {
        // Long window
        // Set number groups to one, reset group indicator to all-zero
        hDecCore->numGroups = 1;
        SetShort(hDecCore->groupIndicator, 0, N_BLOCK_SHORT);
    }

    return;
}


#ifndef SUPPORT_NNTYPE_LC
void DecodeQcBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream, const short channelBytes)
#else
void DecodeQcBits(
    AVS3_DEC_CORE_HANDLE hDecCore,
    NnTypeConfig nnTypeConfig,
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream,
    const short channelBytes)
#endif
{
    NeuralQcData *neuralQcData = &hDecCore->neuralQcData;
    uint32_t* nextBitPos = &(hBitstream->nextBitPos);

    // init QC data structure
    InitNeuralQcData(neuralQcData);

    // read side info
#ifndef SUPPORT_NNTYPE_LC
    neuralQcData->isFeatAmplified = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_IS_FEAT_AMPLIFIED);

    neuralQcData->scaleQIdx = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_FEATURE_SCALE);
#else
    if (nnTypeConfig == NN_TYPE_DEFAULT_MAIN) {
        neuralQcData->isFeatAmplified = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_IS_FEAT_AMPLIFIED);
        neuralQcData->scaleQIdx = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_FEATURE_SCALE);
    }
    else if (nnTypeConfig == NN_TYPE_DEFAULT_LC) {
        neuralQcData->scaleQIdx = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_FEATURE_SCALE_LC);
    }
#endif

    if (hDecCore->numGroups == 1) {
        neuralQcData->nfParamQIdx[0] = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_NF_PARAM);
    }
    else {
        neuralQcData->nfParamQIdx[0] = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_NF_PARAM);
        neuralQcData->nfParamQIdx[1] = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_NF_PARAM);
    }

    neuralQcData->contextNumBytes = GetNextIndice(hBitstream->bitstream, nextBitPos, NBITS_CONTEXT_NUM_BYTES);

    // determine base num bytes
    neuralQcData->baseNumBytes = channelBytes - neuralQcData->contextNumBytes;

    // read context bitstream
    for (int j = 0; j < neuralQcData->contextNumBytes; j++) {
        neuralQcData->contextBitstream[j] = (uint8_t)GetNextIndice(hBitstream->bitstream, nextBitPos, 8);
    }

    // read base bitstream
    for (int j = 0; j < neuralQcData->baseNumBytes; j++) {
        neuralQcData->baseBitstream[j] = (uint8_t)GetNextIndice(hBitstream->bitstream, nextBitPos, 8);
    }

    return;
}
