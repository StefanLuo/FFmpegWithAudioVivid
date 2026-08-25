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

#ifndef AVS3_PROT_DEC_H
#define AVS3_PROT_DEC_H

#include <stdio.h>
#include "avs3_stat_dec.h"
#include "avs3_cnst_com.h"

void GetAvs3DecoderCommandLine(AVS3DecoderHandle hAvs3Dec, const int argc, char *argv[], FILE **fBitstream, FILE **fOutput);

#ifdef BS_HEADER_COMPAT
short Avs3ParseBsFrameHeader(
    AVS3DecoderHandle hAvs3Dec,
    FILE *fBitstream,
    int16_t isInitFrame,
    uint16_t *crcBs
);
#endif

FILE* WriteWavHeader(const char* fileName, const short nChans, const long fs);

void WriteSynthData(const short* data, FILE* file, const short nChans, const short frameLength);

void SynthWavHeader(FILE* file);

void Avs3InitDecoder(AVS3DecoderHandle hAvs3Dec, FILE** fModel);

void Avs3DecoderDestroy(AVS3DecoderHandle hAvs3Dec);

void ResetBitstream(AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream);

short ReadBitstream(AVS3DecoderHandle hAvs3Dec, FILE* fBitstream);

uint16_t GetNextIndice(uint8_t *bitstream, uint32_t *nextBitPos, int16_t numBits);

#ifndef MCR_INTEGRATE
void DecodeStereoSideBits(AVS3_STEREO_DEC_HANDLE hDecStereo, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream);
#else
void DecodeStereoSideBits(AVS3DecoderHandle hAvs3Dec, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream);
#endif

#ifdef MC_ENABLE
void DecodeMcSideBits(
    AVS3_MC_DEC_HANDLE hDecMc,
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream,
    short chBitRatios[MAX_CHANNELS]
);
#endif

void DecodeHoaSideBits(AVS3_HOA_DEC_DATA_HANDLE hDecHoa, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream);

void DecodeCoreSideBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream);

void DecodeGroupBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream);

#ifndef SUPPORT_NNTYPE_LC
void DecodeQcBits(AVS3_DEC_CORE_HANDLE hDecCore, AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream, const short channelBytes);
#else
void DecodeQcBits(
    AVS3_DEC_CORE_HANDLE hDecCore,
    NnTypeConfig nnTypeConfig,
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream,
    const short channelBytes);
#endif

#ifdef MC_ENABLE
void Avs3McDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN]);
#endif

void Avs3StereoDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN]);

#ifdef MCR_INTEGRATE
void Avs3StereoMcrDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN]);
#endif

#ifdef MONO_INTEGRATE
void Avs3MonoDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN]);
#endif

#ifdef MIX_DEVELOPE
void Avs3MixDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN]);
#endif

void Avs3HoaDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN]);

#ifndef MCR_INTEGRATE
void Avs3InverseQC(AVS3DecoderHandle hAvs3Dec);
#else
void Avs3InverseQC(AVS3DecoderHandle hAvs3Dec, short nChans);
#endif

void Avs3PostSynthesis(
    AVS3_DEC_CORE_HANDLE hDecCore,
    float *synth
#ifdef MC_LFE_PROC
    , short isLfe
#endif
);

#ifdef METADATA_EXT
void Avs3MetadataDec(AVS3DecoderHandle hAvs3Dec);
#endif

void Avs3Decode(AVS3DecoderHandle hAvs3Dec, short data[MAX_CHANNELS * FRAME_LEN]);

void Avs3InverseMdctDecoder(AVS3_DEC_CORE_HANDLE hEncCore, float output[BLOCK_LEN_LONG]);

#endif