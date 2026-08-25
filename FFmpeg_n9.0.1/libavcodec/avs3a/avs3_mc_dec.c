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

#ifdef MC_ENABLE

void Avs3McDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN])
{
    short i;
    AVS3_DEC_CORE_HANDLE hDecCore = NULL;
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream = hAvs3Dec->hBitstream;
    AVS3_MC_DEC_HANDLE hMcdec;

    short totalBits = 0;
#ifndef SUPPORT_HIGH_BR_MIX
    short availableBits = 0;
#else
    int32_t availableBits = 0;
#endif
    short availableBytes = 0;
    short channelBytes[MAX_CHANNELS] = { 0 };
    short nChans;
#ifndef IMPR_MIX_BIT_ALLOC
    short chBitRatios[MAX_CHANNELS];
#else
    short chBitRatios[MAX_CHANNELS] = { 0 };
#endif

#ifdef MC_LFE_PROC
    short isLfe;
#endif

    int16_t numGroups[MAX_CHANNELS];

    nChans = hAvs3Dec->numChansOutput;
    hMcdec = hAvs3Dec->hMcDec;

    // decode core side info
    for (i = 0; i < nChans; i++) {
        DecodeCoreSideBits(hAvs3Dec->hDecCore[i], hBitstream);
    }

    // grouping info
    for (i = 0; i < nChans; i++) {
        DecodeGroupBits(hAvs3Dec->hDecCore[i], hBitstream);
        numGroups[i] = hAvs3Dec->hDecCore[i]->numGroups;
    }

    // decode mode side info
    DecodeMcSideBits(hMcdec, hBitstream, chBitRatios);

    // bit alloc
#ifndef SUPPORT_NNTYPE_LC
    availableBits = GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, numGroups, nChans);
#else
    availableBits = GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, numGroups, nChans, hAvs3Dec->nnTypeConfig);
#endif
    availableBytes = (short)floor((float)availableBits / 8.0f);

#ifndef IMPR_MIX_BIT_ALLOC
    McBitsAllocation(availableBits, chBitRatios, hMcdec->channelNum, channelBytes, hMcdec->lfeExist, hMcdec->lfeBytes);
#else
    McBitsAllocationHasSiL(availableBits, chBitRatios, hMcdec->channelNum + hMcdec->objNum, channelBytes, hMcdec->silFlag, hMcdec->lfeExist, hMcdec->lfeBytes);
#endif

    // decode QC bits
    for (i = 0; i < nChans; i++) {
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
    Avs3InverseQC(hAvs3Dec, nChans);
#endif

    Avs3McacDec(hMcdec);

    for (i = 0; i < nChans; i++)
    {
        hDecCore = hAvs3Dec->hDecCore[i];

#ifdef MC_LFE_PROC
        isLfe = 0;
        if (hMcdec->lfeExist == 1 && hMcdec->lfeChIdx == i) {
            isLfe = 1;
        }
#endif

        // post synthesis, including bwe, tns, fd shaping, degrouping and inv MDCT
#ifndef MC_LFE_PROC
        Avs3PostSynthesis(hDecCore, synth[i]);
#else
        Avs3PostSynthesis(hDecCore, synth[i], isLfe);
#endif
    }

    return;
}

#endif