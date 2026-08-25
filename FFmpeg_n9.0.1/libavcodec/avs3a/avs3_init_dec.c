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
#include <assert.h>
#include <math.h>

#include "avs3_stat_dec.h"
#include "avs3_prot_com.h"
#include "avs3_prot_dec.h"

#ifndef BWE_DEVELOPE
static void InitDecoderCore(AVS3_DEC_CORE_HANDLE hDecCore, const short frameLength)
#else
static void InitDecoderCore(
    AVS3_DEC_CORE_HANDLE hDecCore, 
    const short frameLength,
    const short codecFormat,
    const long totalBitrate,
    const short nChans
)
#endif
{
    AVS3_CORE_CONFIG_DATA_HANDLE hCoreConfig = NULL;

    hDecCore->frameLength = frameLength;

    SetZero(hDecCore->origSpectrum, FRAME_LEN);
    SetZero(hDecCore->synthBuffer, MAX_OVL_SIZE);

#ifdef FD_SHAPING
    // init FD spectrum shaping
    SetShort(hDecCore->lsfVqIndex, 0, LSF_CB_NUM_HBR);
    if (((float)totalBitrate / (float)nChans) > LSF_Q_LBR_THRESH) {
        hDecCore->lsfLbrFlag = 0;
    }
    else {
        hDecCore->lsfLbrFlag = 1;
    }
#endif

#ifdef TD_SHAPING
    // init TNS data
    TnsParaInit(&hDecCore->tnsData);
#endif

#ifdef BWE_DEVELOPE
    // init bwe
    hDecCore->bwePresent = GetBwePresent(codecFormat, totalBitrate, nChans);
    if (hDecCore->bwePresent == 1) {
        BweGetConfig(&hDecCore->bweConfig, codecFormat, totalBitrate, nChans);
        InitBweDecData(&hDecCore->bweDecData);
    }
#endif

    // init core config
    if ((hCoreConfig = (AVS3_CORE_CONFIG_DATA_HANDLE)malloc(sizeof(AVS3_CORE_CONFIG_DATA))) == NULL)
    {
        fprintf(stderr, "Can not allocate memory for AVS3 decoder Core configure structure.\n");
        exit(-1);
    }

    InitCoreConfig(hCoreConfig, hDecCore->frameLength);

    hDecCore->hCoreConfig = hCoreConfig;

    return;
}


static void Avs3DecCreateMono(AVS3DecoderHandle hAvs3Dec)
{
#ifdef MONO_INTEGRATE
    AVS3_MONO_DEC_HANDLE hDecMono = NULL;

    // core coder st malloc and init
    if ((hAvs3Dec->hDecCore[0] = (AVS3_DEC_CORE_HANDLE)malloc(sizeof(AVS3_DEC_CORE_DATA))) == NULL)
    {
        fprintf(stderr, "Can not allocate memory for AVS3 decoder core data structure.\n");
        exit(-1);
    }

    /* init decoder core */
#ifndef BWE_DEVELOPE
    InitDecoderCore(hAvs3Dec->hDecCore[0], hAvs3Dec->frameLength);
#else
    InitDecoderCore(hAvs3Dec->hDecCore[0], hAvs3Dec->frameLength, hAvs3Dec->avs3CodecFormat,
        hAvs3Dec->totalBitrate, 1);
#endif

    if ((hDecMono = (AVS3_MONO_DEC_HANDLE)malloc(sizeof(AVS3_MONO_DEC_DATA))) == NULL)
    {
        fprintf(stderr, "Can not allocate memory for AVS3 Stereo decoder data structure.\n");
        exit(-1);
    }

    hAvs3Dec->hDecMono = hDecMono;
#endif

    return;
}

static void Avs3DecCreateStereo(AVS3DecoderHandle hAvs3Dec)
{
    short ch;
    AVS3_STEREO_DEC_HANDLE hDecStereo = NULL;

    // core coder st malloc and init
    for (ch = 0; ch < STEREO_CHANNELS; ch++)
    {
        if ((hAvs3Dec->hDecCore[ch] = (AVS3_DEC_CORE_HANDLE)malloc(sizeof(AVS3_DEC_CORE_DATA))) == NULL)
        {
            fprintf(stderr, "Can not allocate memory for AVS3 decoder core data structure.\n");
            exit(-1);
        }

        /* init decoder core */
#ifndef BWE_DEVELOPE
        InitDecoderCore(hAvs3Dec->hDecCore[ch], hAvs3Dec->frameLength);
#else
        InitDecoderCore(hAvs3Dec->hDecCore[ch], hAvs3Dec->frameLength, hAvs3Dec->avs3CodecFormat,
            hAvs3Dec->totalBitrate, STEREO_CHANNELS);
#endif
    }

    if ((hDecStereo = (AVS3_STEREO_DEC_HANDLE)malloc(sizeof(AVS3_STEREO_DEC_DATA))) == NULL)
    {
        fprintf(stderr, "Can not allocate memory for AVS3 Stereo decoder data structure.\n");
        exit(-1);
    }

#ifdef MCR_INTEGRATE
    // set on/off of MCR
    hDecStereo->useMcr = 0;
    if (hAvs3Dec->totalBitrate <= TH_BR_MCR_STEREO) {
        hDecStereo->useMcr = 1;
    }
    // init MCR config and data
    if (hDecStereo->useMcr == 1) {
        InitMcrConfig(&hDecStereo->mcrConfig);
        InitMcrData(&hDecStereo->mcrData);
    }
#endif

    hAvs3Dec->hDecStereo = hDecStereo;

    return;
}


#ifdef MC_ENABLE
static void Avs3DecCreateMc(AVS3DecoderHandle hAvs3Dec)
{
    short ch;
    short i;
    AVS3_MC_DEC_HANDLE hMdctMcDec = NULL;

    // core coder st malloc and init
    for (ch = 0; ch < hAvs3Dec->numChansOutput; ch++)
    {
        if ((hAvs3Dec->hDecCore[ch] = (AVS3_DEC_CORE_HANDLE)malloc(sizeof(AVS3_DEC_CORE_DATA))) == NULL)
        {
            fprintf(stderr, "Can not allocate memory for AVS3 decoder core data structure.\n");
            exit(-1);
        }

        /* init decoder core */
#ifndef BWE_DEVELOPE
        InitDecoderCore(hAvs3Dec->hDecCore[ch], hAvs3Dec->frameLength);
#else

#ifndef MIX_EXT
        if (LFE_STATUS) {
#else
        if (hAvs3Dec->hasLfe) {
#endif
            // if LFE exist, exclude LFE channel from number channels in core init
            InitDecoderCore(hAvs3Dec->hDecCore[ch], hAvs3Dec->frameLength, hAvs3Dec->avs3CodecFormat,
                hAvs3Dec->totalBitrate, hAvs3Dec->numChansOutput - 1);
        }
        else {
            InitDecoderCore(hAvs3Dec->hDecCore[ch], hAvs3Dec->frameLength, hAvs3Dec->avs3CodecFormat,
                hAvs3Dec->totalBitrate, hAvs3Dec->numChansOutput);
        }
#endif
    }

    if ((hMdctMcDec = (AVS3_MC_DEC_HANDLE)malloc(sizeof(AVS3_MC_DEC_DATA))) == NULL)
    {
        fprintf(stderr, "Can not allocate memory for AVS3 Mc decoder data structure.\n");
        exit(-1);
    }

    hAvs3Dec->hMcDec = hMdctMcDec;

    hMdctMcDec->channelNum = hAvs3Dec->numChansOutput;

#ifndef MIX_EXT
    hMdctMcDec->lfeExist = LFE_STATUS;
    hMdctMcDec->lfeChIdx = LFE_CHANNEL_INDEX;
#else
    // set lfe status
    hMdctMcDec->lfeExist = hAvs3Dec->hasLfe;
    if (hMdctMcDec->lfeExist == 0) {
        hMdctMcDec->lfeChIdx = -1;
    }
    else {
        hMdctMcDec->lfeChIdx = LFE_CHANNEL_INDEX;
    }

#ifdef IMPR_MIX_BIT_ALLOC
    if (hAvs3Dec->isMixedContent == 1) {
        hMdctMcDec->channelNum = hAvs3Dec->numChansOutput - hAvs3Dec->numObjsOutput;
        hMdctMcDec->isMixed = 1;

        /* obj releted */
        hMdctMcDec->objNum = hAvs3Dec->numObjsOutput;
    }
    else {
        hMdctMcDec->channelNum = hAvs3Dec->numChansOutput;
        hMdctMcDec->isMixed = 0;

        /* obj releted */
        hMdctMcDec->objNum = 0;
    }
#endif

#endif

    if (hMdctMcDec->lfeExist) {
        hMdctMcDec->coupleChNum = hMdctMcDec->channelNum - 1;
#ifndef IMPR_MIX_BIT_ALLOC
        hMdctMcDec->lfeBytes = GetLfeAllocBytes(hAvs3Dec->totalBitrate, hMdctMcDec->coupleChNum);
#else
        if (hMdctMcDec->isMixed && (hAvs3Dec->soundBedType == 1)) {
            hMdctMcDec->lfeBytes = GetLfeAllocBytes(hAvs3Dec->bitrateBedMc, hMdctMcDec->coupleChNum);
        }
        else {
            hMdctMcDec->lfeBytes = GetLfeAllocBytes(hAvs3Dec->totalBitrate, hMdctMcDec->coupleChNum);
        }
#endif
    }
    else {
        hMdctMcDec->coupleChNum = hMdctMcDec->channelNum;
        hMdctMcDec->lfeBytes = 0;
    }

    hMdctMcDec->pairCnt = 0;
#ifndef IMPR_MIX_BIT_ALLOC
    hMdctMcDec->bitsPairIndex = max(1, (short)(floor((log(hMdctMcDec->coupleChNum * (hMdctMcDec->coupleChNum - 1) / 2 - 1) / log(2.))) + 1));
#else
    hMdctMcDec->bitsPairIndex = max(1, (short)(floor((log((hMdctMcDec->channelNum + hMdctMcDec->objNum) *
        (hMdctMcDec->channelNum + hMdctMcDec->objNum - 1) / 2 - 1) / log(2.))) + 1));
#endif

    i = 0;
#ifndef IMPR_MIX_BIT_ALLOC
    for (ch = 0; ch < hAvs3Dec->numChansOutput; ch++)
#else
    for (ch = 0; ch < hAvs3Dec->numChansOutput - hMdctMcDec->objNum; ch++)
#endif
    {
        if (ch != hMdctMcDec->lfeChIdx || !hMdctMcDec->lfeExist)
        {
            hMdctMcDec->mcSpectrum[i] = hAvs3Dec->hDecCore[ch]->origSpectrum;
            i++;
        }
    }

    if (hMdctMcDec->lfeExist)
    {
#ifndef IMPR_MIX_BIT_ALLOC
        hMdctMcDec->mcSpectrum[hAvs3Dec->numChansOutput - 1] = hAvs3Dec->hDecCore[hMdctMcDec->lfeChIdx]->origSpectrum;
#else
        hMdctMcDec->mcSpectrum[hAvs3Dec->numChansOutput - hMdctMcDec->objNum - 1] = hAvs3Dec->hDecCore[hMdctMcDec->lfeChIdx]->origSpectrum;
#endif
    }

#ifdef IMPR_MIX_BIT_ALLOC
    for (ch = hAvs3Dec->numChansOutput - hMdctMcDec->objNum; ch < hAvs3Dec->numChansOutput; ch++)
    {
        hMdctMcDec->mcSpectrum[ch] = hAvs3Dec->hDecCore[ch]->origSpectrum;
    }
#endif

    return;
}
#endif


static void Avs3DecCreateHoa(AVS3DecoderHandle hAvs3Dec)
{
    short ch, group, offset;
    AVS3_HOA_DEC_DATA_HANDLE hDecHoa = NULL;
    AVS3_HOA_CONFIG_DATA_HANDLE hHoaConfig = NULL;
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream = NULL;

    /* HOA data structure */
    if ((hDecHoa = (AVS3_HOA_DEC_DATA_HANDLE)malloc(sizeof(AVS3_HOA_DEC_DATA))) == NULL)
    {
        fprintf(stderr, "Can not allocate memory for AVS3 HOA decoder data structure.\n");
        exit(-1);
    }

    /* HOA data configure structure */
    if ((hHoaConfig = (AVS3_HOA_CONFIG_DATA_HANDLE)malloc(sizeof(AVS3_HOA_CONFIG_DATA))) == NULL)
    {
        fprintf(stderr, "Can not allocate memory for AVS3 HOA data configuration structure.\n");
        exit(-1);
    }

    Avs3HoaInitConfig(hHoaConfig, hAvs3Dec->numChansOutput, hAvs3Dec->frameLength, hAvs3Dec->bwidth, hAvs3Dec->totalBitrate);

    // init core coder for hoa transport channels
    for (ch = 0; ch < hAvs3Dec->numChansOutput; ch++)
    {
        if ((hAvs3Dec->hDecCore[ch] = (AVS3_DEC_CORE_HANDLE)malloc(sizeof(AVS3_DEC_CORE_DATA))) == NULL)
        {
            fprintf(stderr, "Can not allocate memory for AVS3 encoder core data structure.\n");
            exit(-1);
        }

        // init core encoder
#ifndef BWE_DEVELOPE
        InitDecoderCore(hAvs3Dec->hDecCore[ch], hAvs3Dec->frameLength);
#else
#ifdef AVS3_HOA_FULL_SUPPORT
        InitDecoderCore(hAvs3Dec->hDecCore[ch], hAvs3Dec->frameLength, hHoaConfig->innerFormat,
            hAvs3Dec->totalBitrate, hHoaConfig->nTotalChansTransport);
#else
        InitDecoderCore(hAvs3Dec->hDecCore[ch], hAvs3Dec->frameLength, hAvs3Dec->avs3CodecFormat,
            hAvs3Dec->totalBitrate, hHoaConfig->nTotalChansTransport);
#endif
#endif
    }

    for (ch = 0; ch < MAX_HOA_CHANNELS; ch++)
    {
        SetZero(hDecHoa->decHoaDelayBuffer[ch], HOA_LEN_FRAME48k + HOA_LEN_FRAME48k);
        SetZero(hDecHoa->decSpecturm[ch], HOA_LEN_FRAME48k);
        SetZero(hDecHoa->decSynthBuffer[ch], HOA_OVERLAP_SIZE);
        hDecHoa->decSignalInput[ch] = hDecHoa->decHoaDelayBuffer[ch] + HOA_LEN_FRAME48k;
    }

    for (group = 0; group < hHoaConfig->nTotalChanGroups; group++)
    {
        offset = hHoaConfig->groupChOffset[group];
        for (ch = 0; ch < hHoaConfig->groupChans[group]; ch++)
        {
            hAvs3Dec->hDecCore[ch + offset]->bwePresent = hHoaConfig->groupBwe[group];
        }
    }

    SetShort(hDecHoa->basisIdx, 0, MAX_HOA_BASIS);

    for (short i = 0; i < HOA_DELAY_BASIS; i++)
    {
        SetShort(hDecHoa->delayBasisIdx[i], 0, MAX_HOA_BASIS);
    }

#ifdef AVS3_HOA_FULL_SUPPORT
    hDecHoa->numVL = 0;
    hDecHoa->sceneType = 0;
#endif

    hDecHoa->hHoaConfig = hHoaConfig;
    hAvs3Dec->hDecHoa = hDecHoa;

    return;
}

#ifdef MIX_DEVELOPE
static void Avs3DecCreateMix(AVS3DecoderHandle hAvs3Dec)
{
    Avs3DecCreateMc(hAvs3Dec);

    return;
}
#endif

#ifdef METADATA_EXT
static void Avs3DecCreateMetaData(AVS3DecoderHandle hAvs3Dec)
{
    Avs3DecMetadataHandle hMetadata = NULL;

    if ((hMetadata = (Avs3DecMetadataHandle)malloc(sizeof(Avs3DecMetadata))) == NULL) {
        fprintf(stderr, "Can not allocate memory for AVS3 metadata structure.\n");
        return;
    }

    hAvs3Dec->hMetadataDec = hMetadata;
    return;
}
#endif

void Avs3InitDecoder(AVS3DecoderHandle hAvs3Dec, FILE** fModel)
{
    short ch;
    AVS3_BSTEREAM_DATA_DEC_HANDLE hBitstream = NULL;

    hAvs3Dec->initFrame = 1;
    hAvs3Dec->lastTotalBrate = hAvs3Dec->totalBitrate;

#ifndef SUPPORT_HIGH_BR_MIX
    hAvs3Dec->bitsPerFrame = (short)(((float)hAvs3Dec->totalBitrate / (float)hAvs3Dec->outputFs) * hAvs3Dec->frameLength);
#else
    hAvs3Dec->bitsPerFrame = (int32_t)(((float)hAvs3Dec->totalBitrate / (float)hAvs3Dec->outputFs) * hAvs3Dec->frameLength);
#endif

#ifdef BS_HEADER_COMPAT
    /* subtract frame bs header bits */
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
#endif

    hAvs3Dec->modelType = HYPER;
    if (*fModel == NULL)
    {
        fprintf(stderr, "Can not open model file.\n");
        exit(-1);
    }
    InitNeuralCodec(*fModel, &hAvs3Dec->baseCodecSt, &hAvs3Dec->contextCodecSt, hAvs3Dec->modelType);

    if ((hBitstream = (AVS3_BSTEREAM_DATA_DEC_HANDLE)malloc(sizeof(AVS3_BSTEREAM_DEC_DATA))) == NULL) 
    {
        fprintf(stderr, "Can not allocate memory for AVS3 bitstream data structure.\n");
        exit(-1);
    }

    ResetBitstream(hBitstream);

    /* init core decoder */
    for (ch = 0; ch < MAX_CHANNELS; ch++)
    {
        hAvs3Dec->hDecCore[ch] = NULL;
    }

    /* init decoder routine */
    if (hAvs3Dec->avs3CodecFormat == AVS3_MONO_FORMAT)
    {
        Avs3DecCreateMono(hAvs3Dec);
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_STEREO_FORMAT)
    {
        Avs3DecCreateStereo(hAvs3Dec);
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_MC_FORMAT)
    {
#ifdef MC_ENABLE
        Avs3DecCreateMc(hAvs3Dec);
#endif
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_HOA_FORMAT)
    {
        Avs3DecCreateHoa(hAvs3Dec);
    }
#ifdef MIX_DEVELOPE
    else if (hAvs3Dec->avs3CodecFormat == AVS3_MIX_FORMAT) {
        Avs3DecCreateMix(hAvs3Dec);
    }
#endif
    else
    {
        assert(!"Not support codec format in AVS3!\n");
    }

#ifdef METADATA_EXT
    // Init metadata handle
    Avs3DecCreateMetaData(hAvs3Dec);
#endif

    hAvs3Dec->hBitstream = hBitstream;

    return;
}

void Avs3DecoderDestroy(AVS3DecoderHandle hAvs3Dec)
{
    short i;

    // free core encoder st
    for (i = 0; i < MAX_CHANNELS; i++)
    {
        if (hAvs3Dec->hDecCore[i] != NULL)
        {
            if (hAvs3Dec->hDecCore[i]->hCoreConfig != NULL)
            {
                free(hAvs3Dec->hDecCore[i]->hCoreConfig);
                hAvs3Dec->hDecCore[i]->hCoreConfig = NULL;
            }

            free(hAvs3Dec->hDecCore[i]);
            hAvs3Dec->hDecCore[i] = NULL;
        }
    }

    if (hAvs3Dec->avs3CodecFormat == AVS3_MONO_FORMAT)
    {

    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_STEREO_FORMAT)
    {
        if (hAvs3Dec->hDecStereo != NULL)
        {
            free(hAvs3Dec->hDecStereo);
            hAvs3Dec->hDecStereo = NULL;
        }
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_HOA_FORMAT)
    {
        if (hAvs3Dec->hDecHoa->hHoaConfig != NULL)
        {
            free(hAvs3Dec->hDecHoa->hHoaConfig);
            hAvs3Dec->hDecHoa->hHoaConfig = NULL;
        }

        if (hAvs3Dec->hDecHoa != NULL)
        {
            free(hAvs3Dec->hDecHoa);
            hAvs3Dec->hDecHoa = NULL;
        }
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_MC_FORMAT) 
    {
        if (hAvs3Dec->hMcDec != NULL) 
        {
            free(hAvs3Dec->hMcDec);
            hAvs3Dec->hMcDec = NULL;
        }
    }
    else if (hAvs3Dec->avs3CodecFormat == AVS3_MIX_FORMAT)
    {
        if (hAvs3Dec->hMcDec != NULL)
        {
            free(hAvs3Dec->hMcDec);
            hAvs3Dec->hMcDec = NULL;
        }
    }

#ifdef METADATA_EXT
    if (hAvs3Dec->hMetadataDec != NULL) {
        free(hAvs3Dec->hMetadataDec);
        hAvs3Dec->hMetadataDec = NULL;
    }
#endif

    // free neural qc handle
    DestroyNeuralCodec(&hAvs3Dec->baseCodecSt, &hAvs3Dec->contextCodecSt);

    if (hAvs3Dec->baseCodecSt != NULL)
    {
        free(hAvs3Dec->baseCodecSt);
        hAvs3Dec->baseCodecSt = NULL;
    }

    if (hAvs3Dec->contextCodecSt != NULL)
    {
        free(hAvs3Dec->contextCodecSt);
        hAvs3Dec->contextCodecSt = NULL;
    }

    /* free bitstream */
    if (hAvs3Dec->hBitstream != NULL) 
    {
        free(hAvs3Dec->hBitstream);
        hAvs3Dec->hBitstream = NULL;
    }

    // free top level st
    if (hAvs3Dec != NULL)
    {
        free(hAvs3Dec);
        hAvs3Dec = NULL;
    }

    return;
}
