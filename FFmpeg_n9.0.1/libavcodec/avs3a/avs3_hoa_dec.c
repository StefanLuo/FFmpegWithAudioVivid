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
#include <assert.h>
#include "avs3_stat_dec.h"
#include "avs3_prot_dec.h"
#include "avs3_prot_com.h"
#include "avs3_rom_com.h"

static void Avs3HoaDecoderReconfig(AVS3DecoderHandle hAvs3Dec, short* nchans, short* totalBits)
{
    hAvs3Dec->hDecHoa->hHoaConfig->totalBitrate = hAvs3Dec->totalBitrate;

    HoaBitrateConfigTable(hAvs3Dec->hDecHoa->hHoaConfig);

    *nchans = hAvs3Dec->hDecHoa->hHoaConfig->nTotalChansTransport;

    hAvs3Dec->numChansOutput = hAvs3Dec->hDecHoa->hHoaConfig->nTotalChansTransport;

#ifdef AVS3_HOA_FULL_SUPPORT
    hAvs3Dec->hDecHoa->numVL = hAvs3Dec->hDecHoa->hHoaConfig->spatialAnalysis ? hAvs3Dec->hDecHoa->hHoaConfig->nTotalForeChans : 0;
#else
    hAvs3Dec->hDecHoa->numVote = hAvs3Dec->hDecHoa->hHoaConfig->spatialAnalysis ? hAvs3Dec->hDecHoa->hHoaConfig->nTotalForeChans : 0;
#endif

#ifndef SUPPORT_HIGH_BR_MIX
    *totalBits = hAvs3Dec->bitsPerFrame;
#else
    *totalBits = (short)hAvs3Dec->bitsPerFrame;
#endif

    return;
}

#ifdef AVS3_HOA_BUG_FIXED
static void InverseSubBandMS(float x0[], float x1[], const short startLines, const short stopLine)
{
    short i;
    float tmpValue;
    float const c = (float)(sqrt(2.f) / 2.f);

    for (i = startLines; i < stopLine; i++)
    {
        tmpValue = x0[i];
        x0[i] = (x0[i] + x1[i]) * c;
        x1[i] = (tmpValue - x1[i]) * c;
    }

    return;
}
#else
static void InverseSubBandMS(float x0[],float x1[], const short stopLine)
{
    short i;
    float tmpValue;
    float const c = (float)(sqrt(2.f) / 2.f);

    for (i = 0; i < stopLine; i++)
    {
        tmpValue = x0[i];
        x0[i] = (x0[i] + x1[i]) * c;
        x1[i] = (tmpValue - x1[i]) * c;
    }

    return;
}
#endif

static void IndexToChannel(const short pairIdx, short* ch1, short* ch2, const short nChannels)
{
    short i, j;
    short tmpIdx = 0;

    for (j = 1; j < nChannels; j++)
    {
        for (i = 0; i < j; i++)
        {
            if (tmpIdx == pairIdx)
            {
                *ch1 = i;
                *ch2 = j;

                return;
            }
            else
            {
                tmpIdx++;
            }
        }
    }

    return;
}

static void Avs3HoaInverseDMX(AVS3DecoderHandle hAvs3Dec)
{
    short i, ch, ch1, ch2, groupIdx;
    AVS3_HOA_DEC_DATA_HANDLE hDecHoa = hAvs3Dec->hDecHoa;
    short nChans;
    float qratio = 0.f;
    short groupChOffset = 0;
    const short nGroups = hDecHoa->hHoaConfig->nTotalChanGroups;
    const short lenFrame = hDecHoa->hHoaConfig->frameLength;

    nChans = hDecHoa->hHoaConfig->nTotalChansTransport;

    groupIdx = 0;
    while (groupIdx < nGroups)
    {
        for (i = 0; i < hDecHoa->pairIdx[groupIdx]; i++)
        {
            groupChOffset = hDecHoa->hHoaConfig->groupChOffset[groupIdx];

            IndexToChannel(hDecHoa->chIdx[groupIdx][i], &ch1, &ch2, hDecHoa->hHoaConfig->groupChans[groupIdx]);

            ch1 += groupChOffset;
            ch2 += groupChOffset;
#ifdef AVS3_HOA_BUG_FIXED
            for (short sfb = 0; sfb < N_SFB_HOA_LBR - 1; sfb++) {

#ifndef HOA_BUGFIX_UPMIX
                if (hDecHoa->sfbMask[groupIdx][sfb]) {
#else
                if (hDecHoa->sfbMask[groupIdx][i][sfb]) {
#endif

                    InverseSubBandMS(hAvs3Dec->hDecCore[ch1]->origSpectrum, hAvs3Dec->hDecCore[ch2]->origSpectrum,
                        hoa_sfb_table_low_bitrate[sfb], hoa_sfb_table_low_bitrate[sfb + 1]);
                }
            }
#else
            InverseSubBandMS(hAvs3Dec->hDecCore[ch1]->origSpectrum, hAvs3Dec->hDecCore[ch2]->origSpectrum, hAvs3Dec->hDecHoa->hHoaConfig->frameLength);
#endif
        }

        groupIdx++;
    }

    for (ch = 0; ch < nChans; ch++)
    {
#ifndef HOA_ILD_CBQUANT
        if (hDecHoa->groupILD[ch] != 0)
        {
            if (hDecHoa->flagNrg[ch])
            {
                qratio = (float)hDecHoa->groupILD[ch] / HOA_ILD_RANGE;
            }
            else
            {
                qratio = (float)HOA_ILD_RANGE / hDecHoa->groupILD[ch];
            }
#else
        if (hDecHoa->groupILD[ch] != MC_ILD_CBLEN)
        {
            qratio = mcIldCodebook[hDecHoa->groupILD[ch]];
#endif

            for (i = 0; i < lenFrame; i++)
            {
                hAvs3Dec->hDecCore[ch]->origSpectrum[i] *= qratio;
            }
        }
    }

    return;
}

static void HoaCoreDec(AVS3_HOA_DEC_DATA_HANDLE hEncHoa, float output[MAX_HOA_CHANNELS][HOA_LEN_FRAME48k], const short lenFrame)
{
    short i, j;
    short ch, row, cols, k;

    AVS3_HOA_CONFIG_DATA_HANDLE hConfig = hEncHoa->hHoaConfig;
#ifdef AVS3_HOA_FULL_SUPPORT
    const short nPreChans = hEncHoa->numVL;
#else
    const short nPreChans = hConfig->nTotalForeChans;
#endif
    const short nChansOutput = hConfig->nTotalChansInput;
    const short nTotalResChans = hConfig->nTotalResChans;

    float matSignalBasis[HOA_LEN_FRAME48k + HOA_LEN_FRAME48k][MAX_HOA_BASIS];
    float matBasisCoefs[L_HOA_BASIS_ROWS][MAX_HOA_BASIS];
    float recoverySignal[MAX_HOA_CHANNELS][HOA_LEN_FRAME48k];

    float samples[L_HOA_BASIS_ROWS];
    float tmp1[L_HOA_BASIS_ROWS];
    float tmp2[L_HOA_BASIS_ROWS];

    short anglePair[2];

    SetZero(samples, L_HOA_BASIS_ROWS);
    SetZero(tmp1, L_HOA_BASIS_ROWS);
    SetZero(tmp2, L_HOA_BASIS_ROWS);

    /* initialization. */
    for (ch = 0; ch < nChansOutput; ch++)
    {
        SetFloat(recoverySignal[ch], 0.f, lenFrame);
    }

    /* get speaker basis index. */
    for (i = 0; i < nPreChans; i++)
    {
        short idx = hEncHoa->delayBasisIdx[0][i];

        MvShort2Short(avs3_hoa_fixed_angle_basis_matrix[idx], anglePair, 2);

        GetSingleNeighborBasisCoeff(anglePair, tmp1);

#ifdef AVS3_HOA_FULL_SUPPORT
        for (j = 0; j < nChansOutput; j++)
#else
        for (j = 0; j < L_HOA_BASIS_ROWS; j++)
#endif
        {
            matBasisCoefs[j][i] = tmp1[j];
        }
    }

    for (row = 0; row < nPreChans; row++)
    {
        for (cols = 0; cols < lenFrame; cols++)
        {
            matSignalBasis[cols][row] = output[row][cols];
        }
    }

    /* recovery signals. */
    for (row = 0; row < lenFrame; row++)
    {
        for (cols = 0; cols < nPreChans; cols++)
        {
            tmp1[cols] = matSignalBasis[row][cols];
        }

#ifdef AVS3_HOA_FULL_SUPPORT
        for (k = 0; k < nChansOutput; k++)
#else
        for (k = 0; k < L_HOA_BASIS_ROWS; k++)
#endif
        {
            for (cols = 0; cols < nPreChans; cols++)
            {
                tmp2[cols] = matBasisCoefs[k][cols];
            }

            recoverySignal[k][row] = (float)Dotp(tmp1, tmp2, nPreChans);
        }
    }

    /* get final result signals. */
    for (row = 0; row < nTotalResChans; row++)
    {
        for (cols = 0; cols < lenFrame; cols++)
        {
            recoverySignal[row][cols] += output[row + nPreChans][cols];
        }
    }

    for (row = 0; row < nChansOutput; row++)
    {
        Mvf2f(recoverySignal[row], output[row], lenFrame);
    }

    return;
}

static void HoaPostSynthesisFilter(AVS3DecoderHandle hAvs3Dec, float output[MAX_HOA_CHANNELS][FRAME_LEN], const short frameLength)
{
    short i, j, ch;
    short subFrame;
    float synthBuffer[HOA_OVERLAP_SIZE];
    float win[BLOCK_LEN_LONG];
    float* signal = NULL;

    AVS3_HOA_DEC_DATA_HANDLE hDecHoa = hAvs3Dec->hDecHoa;
    AVS3_HOA_CONFIG_DATA_HANDLE hHoaConfig = hDecHoa->hHoaConfig;
    const short overlapSize = hHoaConfig->overlapSize;
    const short synthChannelsOutput = hHoaConfig->nTotalChansInput;

    hAvs3Dec->numChansOutput = hHoaConfig->nTotalChansInput;

    /* MDCT */
    for (ch = 0; ch < hHoaConfig->nTotalChansTransport; ch++)
    {
        signal = hDecHoa->decSignalInput[ch] - overlapSize;
        for (subFrame = 0; subFrame < N_BLOCK_HOA; subFrame++)
        {
            /* windowing left part */
            for (i = 0; i < overlapSize; i++)
            {
                win[i] = signal[i] * hHoaConfig->hoaWindow[i];
            }

            /* window right part */
            for (i = 0; i < overlapSize; i++)
            {
                win[i + overlapSize] = signal[i + overlapSize] * hHoaConfig->hoaWindow[overlapSize - i - 1];
            }

            MDCT(win, hDecHoa->decSpecturm[ch] + subFrame * overlapSize, 2 * overlapSize);

            signal += overlapSize;
        }
    }

    if (hHoaConfig->spatialAnalysis) 
    {
        /* Transport signals to recovery signals */
        HoaCoreDec(hDecHoa, hDecHoa->decSpecturm, frameLength);
    }

    /* HOA signal Synthesis*/
    for (ch = 0; ch < synthChannelsOutput; ch++)
    {
        SetZero(output[ch], frameLength);

        Mvf2f(hDecHoa->decSynthBuffer[ch], synthBuffer, overlapSize);

        for (subFrame = 0; subFrame < N_BLOCK_HOA; subFrame++)
        {
            SetZero(win, BLOCK_LEN_LONG);

            Mvf2f(hDecHoa->decSpecturm[ch] + subFrame * overlapSize, win, overlapSize);

            IMDCT(win, 2 * overlapSize);

            /* windowing left part */
            for (i = 0; i < overlapSize; i++)
            {
                win[i] *= hHoaConfig->hoaWindow[i];
            }

            /* windowing right part */
            for (i = 0; i < overlapSize; i++)
            {
                win[i + overlapSize] *= hHoaConfig->hoaWindow[overlapSize - i - 1];
            }

            Vadd(win, synthBuffer, win, overlapSize);

            Mvf2f(win + overlapSize, synthBuffer, overlapSize);

            Mvf2f(win, output[ch] + subFrame * overlapSize, overlapSize);
        }

        Mvf2f(synthBuffer, hDecHoa->decSynthBuffer[ch], overlapSize);
    }

    /* update */
    for (i = 0; i < HOA_DELAY_BASIS - 1; i++)
    {
#ifdef AVS3_HOA_FULL_SUPPORT
        for (j = 0; j < hDecHoa->numVL; j++)
#else
        for (j = 0; j < hDecHoa->numVote; j++)
#endif
        {
            hDecHoa->delayBasisIdx[i][j] = hDecHoa->delayBasisIdx[i + 1][j];
        }
    }


#ifdef AVS3_HOA_FULL_SUPPORT
    MvShort2Short(hDecHoa->basisIdx, hDecHoa->delayBasisIdx[HOA_DELAY_BASIS - 1], hDecHoa->numVL);
#else
    MvShort2Short(hDecHoa->basisIdx, hDecHoa->delayBasisIdx[HOA_DELAY_BASIS - 1], hDecHoa->numVote);
#endif

    return;
}

void Avs3HoaDec(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN]) 
{
    short ch;
    short nChans = 0;
    short totalBits = 0;
    short availableBits = 0;
    short channelBytes[MAX_CHANNELS] = {0};
    AVS3_DEC_CORE_HANDLE hDecCore = NULL;
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream = hAvs3Dec->hBitstream;
    const short frameLength = hAvs3Dec->frameLength;

    int16_t numGroups[MAX_CHANNELS];

    Avs3HoaDecoderReconfig(hAvs3Dec, &nChans, &totalBits);

    // decode core side info
    for (ch = 0; ch < nChans; ch++) 
    {
        DecodeCoreSideBits(hAvs3Dec->hDecCore[ch], hBitstream);
    }

    // grouping info
    for (ch = 0; ch < nChans; ch++) {
        DecodeGroupBits(hAvs3Dec->hDecCore[ch], hBitstream);
        numGroups[ch] = hAvs3Dec->hDecCore[ch]->numGroups;
    }

    // decode mode side info
    DecodeHoaSideBits(hAvs3Dec->hDecHoa, hBitstream);

    /* Bits verification */
#ifndef SUPPORT_NNTYPE_LC
    availableBits = GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, numGroups, nChans);
#else
#ifndef SUPPORT_HIGH_BR_MIX
    availableBits = GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, numGroups, nChans, hAvs3Dec->nnTypeConfig);
#else
    availableBits = (short)GetAvailableBits(hAvs3Dec->bitsPerFrame, hBitstream->nextBitPos, numGroups, nChans, hAvs3Dec->nnTypeConfig);
#endif
#endif

    HoaSplitBytesGroup(hAvs3Dec->hDecHoa->hHoaConfig, channelBytes, hAvs3Dec->hDecHoa->groupBitsRatio, hAvs3Dec->hDecHoa->bitsRatio, availableBits);

    // decode QC bits
    for (ch = 0; ch < nChans; ch++) 
    {
#ifndef SUPPORT_NNTYPE_LC
        DecodeQcBits(hAvs3Dec->hDecCore[ch], hBitstream, channelBytes[ch]);
#else
        DecodeQcBits(hAvs3Dec->hDecCore[ch], hAvs3Dec->nnTypeConfig, hBitstream, channelBytes[ch]);
#endif
    }

    // inverse QC for all channels
#ifndef MCR_INTEGRATE
    Avs3InverseQC(hAvs3Dec);
#else
    Avs3InverseQC(hAvs3Dec, nChans);
#endif

    /* Inverse DMX */
    Avs3HoaInverseDMX(hAvs3Dec);

    // inverse MDCT and OLA
    for (ch = 0; ch < nChans; ch++)
    {
        hDecCore = hAvs3Dec->hDecCore[ch];

        // post synthesis, including bwe, tns, fd shaping, degrouping and inv MDCT
#ifndef MC_LFE_PROC
        Avs3PostSynthesis(hDecCore, hAvs3Dec->hDecHoa->decSignalInput[ch]);
#else
        Avs3PostSynthesis(hDecCore, hAvs3Dec->hDecHoa->decSignalInput[ch], 0);
#endif
    }

    /* HOA synthesis */
    HoaPostSynthesisFilter(hAvs3Dec, synth, frameLength);

    /* update */
    for (ch = 0; ch < hAvs3Dec->numChansOutput; ch++)
    {
        Mvf2f(hAvs3Dec->hDecHoa->decHoaDelayBuffer[ch] + frameLength, hAvs3Dec->hDecHoa->decHoaDelayBuffer[ch], frameLength);
    }

    return;
}
