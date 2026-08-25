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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "avs3_stat_dec.h"
#include "avs3_prot_dec.h"
#include "avs3_prot_com.h"


void Avs3StereoDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN])
{
    short i;
    AVS3_DEC_CORE_HANDLE hDecCore = NULL;
    AVS3_DEC_CORE_HANDLE hDecCoreL = hAvs3Dec->hDecCore[0];
    AVS3_DEC_CORE_HANDLE hDecCoreR = hAvs3Dec->hDecCore[1];
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream = hAvs3Dec->hBitstream;

    short totalBits = 0;
    short availableBits = 0;
    short availableBytes = 0;
    short channelBytes[STEREO_CHANNELS] = { 0 };

    int16_t numGroups[STEREO_CHANNELS];

    // decode core side info
    for (i = 0; i < STEREO_CHANNELS; i++) {
        DecodeCoreSideBits(hAvs3Dec->hDecCore[i], hBitstream);
    }

    // grouping info
    for (i = 0; i < STEREO_CHANNELS; i++) {
        DecodeGroupBits(hAvs3Dec->hDecCore[i], hBitstream);
        numGroups[i] = hAvs3Dec->hDecCore[i]->numGroups;
    }

    // decode mode side info
#ifndef MCR_INTEGRATE
    DecodeStereoSideBits(hAvs3Dec->hDecStereo, hBitstream);
#else
    DecodeStereoSideBits(hAvs3Dec, hBitstream);
#endif

    // bit split
#ifndef SUPPORT_NNTYPE_LC
    availableBits = GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, numGroups, STEREO_CHANNELS);
#else
#ifndef SUPPORT_HIGH_BR_MIX
    availableBits = GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, numGroups, STEREO_CHANNELS, hAvs3Dec->nnTypeConfig);
#else
    availableBits = (short)GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, numGroups, STEREO_CHANNELS, hAvs3Dec->nnTypeConfig);
#endif
#endif
    StereoBitsAllocation(availableBits, hAvs3Dec->hDecStereo->bitsRatio, channelBytes);

    // decode QC bits
    for (i = 0; i < STEREO_CHANNELS; i++) {
#ifndef SUPPORT_NNTYPE_LC
        DecodeQcBits(hAvs3Dec->hDecCore[i], hBitstream, channelBytes[i]);
#else
        DecodeQcBits(hAvs3Dec->hDecCore[i], hAvs3Dec->nnTypeConfig, hBitstream, channelBytes[i]);
#endif
    }

    // inverse QC for all channels
#ifndef MCR_INTEGRATE
    Avs3InverseQC(hAvs3Dec);
#else
    Avs3InverseQC(hAvs3Dec, STEREO_CHANNELS);
#endif

    // inverse ms process
    if (hAvs3Dec->hDecStereo->isMS == 1) {
        StereoInvMsProcess(hDecCoreL->origSpectrum, hDecCoreR->origSpectrum, hAvs3Dec->frameLength, hAvs3Dec->hDecStereo->ILD);
    }

    for (i = 0; i < STEREO_CHANNELS; i++)
    {
        hDecCore = hAvs3Dec->hDecCore[i];

        // post synthesis, including bwe, tns, fd shaping, degrouping and inv MDCT
#ifndef MC_LFE_PROC
        Avs3PostSynthesis(hDecCore, synth[i]);
#else
        Avs3PostSynthesis(hDecCore, synth[i], 0);
#endif
    }

    return;
}


#ifdef MCR_INTEGRATE
void Avs3StereoMcrDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN])
{
    short i;
    AVS3_DEC_CORE_HANDLE hDecCore = NULL;
    AVS3_DEC_CORE_HANDLE hDecCoreL = hAvs3Dec->hDecCore[0];
    AVS3_DEC_CORE_HANDLE hDecCoreR = hAvs3Dec->hDecCore[1];
    AVS3_STEREO_DEC_HANDLE hDecStereo = hAvs3Dec->hDecStereo;
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream = hAvs3Dec->hBitstream;

    short availableBits = 0;
    short channelBytes[STEREO_CHANNELS] = { 0 };

    int16_t numGroups;

    // decode core side info
    for (i = 0; i < STEREO_CHANNELS; i++) {
        DecodeCoreSideBits(hAvs3Dec->hDecCore[i], hBitstream);
    }

    // grouping info
    // only left channel for MCR mode
    DecodeGroupBits(hDecCoreL, hBitstream);
    numGroups = hDecCoreL->numGroups;

    // decode mode side info
    DecodeStereoSideBits(hAvs3Dec, hBitstream);

    // bit split
    // number channel is 1 for MCR mode
#ifndef SUPPORT_NNTYPE_LC
    availableBits = GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, &numGroups, 1);
#else
#ifndef SUPPORT_HIGH_BR_MIX
    availableBits = GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, &numGroups, 1, hAvs3Dec->nnTypeConfig);
#else
    availableBits = (short)GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, &numGroups, 1, hAvs3Dec->nnTypeConfig);
#endif
#endif

    // allcoation bits between channels
    // all bits for left channel in mcr mode
    channelBytes[0] = (short)floor((float)availableBits / 8.0f);
    channelBytes[1] = 0;

    // decode QC bits
    // only left channel for MCR mode
#ifndef SUPPORT_NNTYPE_LC
    DecodeQcBits(hDecCoreL, hBitstream, channelBytes[0]);
#else
    DecodeQcBits(hDecCoreL, hAvs3Dec->nnTypeConfig, hBitstream, channelBytes[0]);
#endif

    // inverse QC for all channels
    // number channel is 1 for MCR mode
    Avs3InverseQC(hAvs3Dec, 1);

    // inverse MCR process
    McrDecode(&hDecStereo->mcrData, &hDecStereo->mcrConfig, hDecCoreL->origSpectrum, hDecCoreR->origSpectrum,
        hDecCoreL->transformType == ONLY_SHORT_WINDOW);

    for (i = 0; i < STEREO_CHANNELS; i++)
    {
        hDecCore = hAvs3Dec->hDecCore[i];

        // post synthesis, including bwe, tns, fd shaping, degrouping and inv MDCT
        Avs3PostSynthesis(hDecCore, synth[i], 0);
    }

    return;
}
#endif