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
#include <string.h>
#include "avs3_prot_dec.h"
#include "avs3_stat_com.h"

FILE* WriteWavHeader(const char* fileName, const short nChans, const long fs)
{
    FILE* fWav = NULL;

    AVS3_WAVE_HEADER_DATA wavHeader;

    /* RIFF */
    strncpy(wavHeader.chunkID, "RIFF", sizeof(wavHeader.chunkID));

    /* RIFF size */
    wavHeader.riffSize = 0;

    /* Format */
    strncpy(wavHeader.format, "WAVE", sizeof(wavHeader.format));

    /* Format ID */
    strncpy(wavHeader.subchunkID, "fmt ", sizeof(wavHeader.subchunkID));

    /* Format length */
    wavHeader.subchunkSize = 16;

    /* Wave */
    wavHeader.audioFormat = 1;

    /* Channels */
    wavHeader.numChannels = nChans;

    /* sampling rate */
    wavHeader.sampleRate = (int32_t)fs;
    
    /* bit depth */
    wavHeader.bitDepth = 16;

    /* align */
    wavHeader.blockAlign = (wavHeader.bitDepth >> 3)* wavHeader.numChannels;

    /* Bytes rate */
    wavHeader.byteRate = wavHeader.sampleRate * wavHeader.blockAlign;

    /* Data tag */
    strncpy(wavHeader.dataID, "data", sizeof(wavHeader.dataID));

    wavHeader.dataSize = 0;

    /*
    if ((fWav = fopen(fileName, "wb+")) == NULL)
    {
        fprintf(stderr, "Open wave file error!\n");
        exit(-1);
    }
    */

    if (fWav) {
        fwrite(&wavHeader, sizeof(wavHeader), 1, fWav);
        fflush(fWav);
    }

    return fWav;
}
