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
#include "libavutil/log.h"
#include "avs3_stat_dec.h"
#include "avs3_prot_dec.h"
#include "avs3_prot_com.h"

#ifndef MCR_INTEGRATE
void Avs3InverseQC(AVS3DecoderHandle hAvs3Dec)
#else
void Avs3InverseQC(AVS3DecoderHandle hAvs3Dec, short nChans)
#endif
{
    short ch;
#ifndef MCR_INTEGRATE
    const short nChans = hAvs3Dec->numChansOutput;
#endif
	float (*featureOut)[2] = hAvs3Dec->featureOut;
    for (ch = 0; ch < nChans; ch++)
    {
        AVS3_DEC_CORE_HANDLE hDecCore = hAvs3Dec->hDecCore[ch];

        // get number of spectral lines for NF calculation
        int16_t numLinesNoiseFill = 0;
        if (hDecCore->bwePresent) {
            numLinesNoiseFill = hDecCore->bweConfig.bweStartLine;
        }
        else {
            numLinesNoiseFill = hDecCore->frameLength;
        }

#ifndef SUPPORT_NNTYPE_LC
        MdctDequantDecodeHyper(hAvs3Dec->baseCodecSt, hAvs3Dec->contextCodecSt, &hDecCore->neuralQcData,
            featureOut, numLinesNoiseFill, hDecCore->numGroups, hDecCore->groupIndicator);
#else
        if (hAvs3Dec->nnTypeConfig == NN_TYPE_DEFAULT_MAIN) {
            MdctDequantDecodeHyper(hAvs3Dec->baseCodecSt, hAvs3Dec->contextCodecSt, &hDecCore->neuralQcData,
                featureOut, numLinesNoiseFill, hDecCore->numGroups, hDecCore->groupIndicator);
        }
        else if (hAvs3Dec->nnTypeConfig == NN_TYPE_DEFAULT_LC) {
            MdctDequantDecodeHyperLc(hAvs3Dec->baseCodecSt, hAvs3Dec->contextCodecSt, &hDecCore->neuralQcData,
                featureOut, numLinesNoiseFill, hDecCore->numGroups, hDecCore->groupIndicator);
        }
#endif

        // copy feature back to st
        for (int16_t i = 0; i < FRAME_LEN; i++) {
            hDecCore->origSpectrum[i] = featureOut[i][0];
        }

        // grouping for short window
        SpectrumDegroupingDec(hDecCore->origSpectrum, hDecCore->frameLength, hDecCore->transformType, hDecCore->groupIndicator);
    }

    return;
}


void Avs3PostSynthesis(
    AVS3_DEC_CORE_HANDLE hDecCore, 
    float *synth
#ifdef MC_LFE_PROC
    , short isLfe
#endif
)
{
#ifdef BWE_DEVELOPE
    // BWE
    if (hDecCore->bwePresent == 1) {
        BweApplyDec(&hDecCore->bweConfig, &hDecCore->bweDecData, hDecCore->origSpectrum);
    }
#endif

#ifdef TD_SHAPING
    // Inverse TNS
    TnsDec(&hDecCore->tnsData, hDecCore->origSpectrum, hDecCore->transformType == ONLY_SHORT_WINDOW);
#endif

#ifdef FD_SHAPING
    // Inverse fd spectrum shaping
    Avs3FdInvSpectrumShaping(hDecCore->lsfVqIndex, hDecCore->origSpectrum, hDecCore->lsfLbrFlag);
#ifdef POST_SHAPING
    // post processing the shaped spectrum, in low bitrate
    if (hDecCore->lsfLbrFlag == 1 && hDecCore->transformType != ONLY_SHORT_WINDOW) {
        SpecPostShaping(hDecCore->origSpectrum, hDecCore->bweConfig.bweStartLine, 1);
    }
#endif
#endif

#ifdef MC_LFE_PROC
    // Clear HF mdct lines for LFE channel in MC mode
    if (isLfe == 1) {
        McLfeProc(hDecCore->origSpectrum);
    }
#endif

    // spectrum degrouping
    if (hDecCore->transformType == ONLY_SHORT_WINDOW)
    {
        MdctSpectrumDeinterleave(hDecCore->origSpectrum, hDecCore->frameLength, N_BLOCK_SHORT);
    }

    // inverse MDCT and OLA
    Avs3InverseMdctDecoder(hDecCore, synth);

    return;
}


void Avs3InverseMdctDecoder(AVS3_DEC_CORE_HANDLE hDecCore, float output[BLOCK_LEN_LONG]) 
{
    AVS3_CORE_CONFIG_DATA_HANDLE hCoreConfig = hDecCore->hCoreConfig;

    float *winLeft = hDecCore->winLeft;
    float *winRight = hDecCore->winRight;
    float *tdaSignal = hDecCore->tdaSignal;
    short overlapSize;

    SetZero(tdaSignal + BLOCK_LEN_LONG, BLOCK_LEN_LONG);

	Mvf2f(hDecCore->origSpectrum, tdaSignal, BLOCK_LEN_LONG);

    if (hDecCore->transformType != ONLY_SHORT_WINDOW)
    {
        overlapSize = hCoreConfig->overlapLongSize;

        /* Inverse MDCT */
		IMDCT(tdaSignal, 2 * overlapSize);

        /* Get window shape */
        GetWindowShape(hCoreConfig, hDecCore->transformType, winLeft, winRight);

        /* Window signal */
        WindowSignal(hCoreConfig, tdaSignal, tdaSignal, hDecCore->transformType, winLeft, winRight);

        /* Overlap-add */
        Vadd(tdaSignal, hDecCore->synthBuffer, tdaSignal, overlapSize);

        /* however current frame is a transition frame or long window, stored all the half frame including zero padding part and flat part, it make synthesis easily */
        Mvf2f(tdaSignal + overlapSize, hDecCore->synthBuffer, overlapSize);

        /* Output */
        Mvf2f(tdaSignal, output, overlapSize);
    }
    else
    {
        float *tmpSynthBuffer = hDecCore->tmpSynthBuffer;
		float *winShort = hDecCore->winShort;
		float *tmpSynth = hDecCore->tmpSynth;
        const short synthOffset = hCoreConfig->overlapPaddingSize;

        overlapSize = hCoreConfig->overlapShortSize;

        /* get last frame overlap-add buffer for the first short block */
        Mvf2f(hDecCore->synthBuffer + synthOffset, tmpSynthBuffer, overlapSize);

        /* get window shape */
        GetWindowShape(hCoreConfig, hDecCore->transformType, winLeft, winRight);

        /* loop through blocks */
        for (short block = 0; block < N_BLOCK_SHORT; block++)
        {
            SetZero(winShort, 2 * overlapSize);

            Mvf2f(tdaSignal + block * overlapSize, winShort, overlapSize);

            /* Inverse MDCT */
            IMDCT(winShort, 2 * overlapSize);

            /* Windowing left part */
            VMult(winShort, winLeft, winShort, overlapSize);

            /* Windowing right part */
            VMult(winShort + overlapSize, winRight, winShort + overlapSize, overlapSize);

            /* overlap add */
            Vadd(winShort, tmpSynthBuffer, winShort, overlapSize);

            /* update inter-frame synthesis buffer */
            Mvf2f(winShort + overlapSize, tmpSynthBuffer, overlapSize);

            /* Output */
            Mvf2f(winShort, tmpSynth + block * overlapSize, overlapSize);
        }

        /* output part 1 (last frame) */
        Mvf2f(hDecCore->synthBuffer, output, synthOffset);

        /* output part 2 (current frame )*/
        Mvf2f(tmpSynth, output + synthOffset, hDecCore->frameLength - synthOffset);

        /* synthesis buffer part 1 */
        Mvf2f(tmpSynth + hDecCore->frameLength - synthOffset, hDecCore->synthBuffer, synthOffset);

        /* synthesis buffer part 2 */
        Mvf2f(tmpSynthBuffer, hDecCore->synthBuffer + synthOffset, overlapSize);

        /* synthesis buffer part 3 (zero padding part) */
        SetZero(hDecCore->synthBuffer + synthOffset + overlapSize, hDecCore->frameLength - (synthOffset + overlapSize));
    }

    return;
}

static void Avs3DecodeSynthesis(AVS3DecoderHandle hAvs3Dec, float synth[MAX_CHANNELS][FRAME_LEN])
{
#ifdef METADATA_EXT
    Avs3MetadataDec(hAvs3Dec);
#endif

    if (hAvs3Dec->avs3CodecFormat == AVS3_MONO_FORMAT)
    {
#ifdef MONO_INTEGRATE
        Avs3MonoDec(hAvs3Dec, synth);
#endif
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_STEREO_FORMAT)
    {
#ifndef MCR_INTEGRATE
        Avs3StereoDec(hAvs3Dec, synth);
#else
        if (hAvs3Dec->hDecStereo->useMcr == 0) {
            Avs3StereoDec(hAvs3Dec, synth);
        }
        else {
            Avs3StereoMcrDec(hAvs3Dec, synth);
        }
#endif
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_MC_FORMAT)
    {
#ifdef MC_ENABLE
        Avs3McDec(hAvs3Dec, synth);
#endif
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_HOA_FORMAT)
    {
        Avs3HoaDec(hAvs3Dec, synth);
    }
#ifdef MIX_DEVELOPE
    else if (hAvs3Dec->avs3CodecFormat == AVS3_MIX_FORMAT)
    {
        Avs3MixDec(hAvs3Dec, synth);
    }
#endif
}

void Avs3Decode(AVS3DecoderHandle hAvs3Dec, short data[MAX_CHANNELS * FRAME_LEN])
{
    float (*synth)[FRAME_LEN] = hAvs3Dec->synth;
    const short frameLength = hAvs3Dec->frameLength;
    const short nChans = hAvs3Dec->numChansOutput;

    Avs3DecodeSynthesis(hAvs3Dec, synth);

    Avs3SynthOutput(
        synth,
        frameLength,
        nChans,
        data);

    hAvs3Dec->initFrame = 0;
}

void Avs3DecodePlanar(AVS3DecoderHandle hAvs3Dec, short *data[MAX_CHANNELS])
{
    float (*synth)[FRAME_LEN] = hAvs3Dec->synth;
    const short frameLength = hAvs3Dec->frameLength;
    const short nChans = hAvs3Dec->numChansOutput;

    Avs3DecodeSynthesis(hAvs3Dec, synth);

    Avs3SynthOutputPlanar(synth, frameLength, nChans, data);

    hAvs3Dec->initFrame = 0;
}