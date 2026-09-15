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
#include <assert.h>
#include "avs3_cnst_com.h"
#include "avs3_rom_com.h"
#include "avs3_prot_com.h"
#include "avs3_prot_dec.h"

#define AVS3_DECODER_COMMAND_LINE

void GetAvs3DecoderCommandLine(AVS3DecoderHandle hAvs3Dec, const int argc, char *argv[], FILE **fBitstream, FILE **fOutput)
{
    short i = 1;

#ifndef BS_HEADER_COMPAT
    int tmp;

    while (i < argc - 3)
    {
        if (strcmp(toUpper(argv[i]), "-MONO") == 0)
        {
#ifdef MONO_INTEGRATE
            hAvs3Dec->avs3CodecFormat = AVS3_MONO_FORMAT;
#else
            hAvs3Dec->avs3CodecFormat = AVS3_STEREO_FORMAT;
#endif
            hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            hAvs3Dec->numChansOutput = 1;

            i++;
        }
        else if (strcmp(toUpper(argv[i]), "-STEREO") == 0)
        {
            hAvs3Dec->avs3CodecFormat = AVS3_STEREO_FORMAT;
            hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            hAvs3Dec->numChansOutput = STEREO_CHANNELS;

            i++;
        }
        else if (strcmp(toUpper(argv[i]), "-MC") == 0)
        {
#ifdef MC_ENABLE
            i++;
            hAvs3Dec->avs3CodecFormat = AVS3_MC_FORMAT;
            hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;
            if (i < argc - 3)
            {
                if (sscanf(argv[i], "%d", &tmp) > 0)
                {
                    i++;
                }
            }
            hAvs3Dec->numChansOutput = tmp;
#endif
        }
        else if (strcmp(toUpper(argv[i]), "-HOA") == 0)
        {
            i++;
            hAvs3Dec->avs3CodecFormat = AVS3_HOA_FORMAT;
            hAvs3Dec->avs3CodecCore = AVS3_MDCT_CORE;

            if (i < argc - 3)
            {
                if (sscanf(argv[i], "%d", &tmp) > 0)
                {
                    i++;
                }
            }

            switch (tmp)
            {
            case 0:
                hAvs3Dec->numChansOutput = 1;
                break;
            case 1:
                hAvs3Dec->numChansOutput = 4;
                break;
            case 2:
                hAvs3Dec->numChansOutput = 9;
                break;
            case 3:
                hAvs3Dec->numChansOutput = 16;
                break;
            case 4:
                hAvs3Dec->numChansOutput = 25;
                break;
            default:
                break;
            }
        }
        else
        {
            assert("!Not support codec format in AVS3!\n");
        }
    }

    /* Output sampling rate */
    if (i < argc - 2)
    {
        hAvs3Dec->outputFs = (long)(atoi(argv[i]) * 1000);

        hAvs3Dec->frameLength = GetFrameLength(hAvs3Dec->outputFs);

        i++;
    }
    else
    {
        exit(-1);
    }
#endif

#ifdef AVS3_DECODER_COMMAND_LINE

    char* bitstreamName = NULL;
    char* outfileName = NULL;

    while (i < argc - 1)
    {
        if (strncmp(argv[i], "-if", strlen("-if")) == 0)
        {
            i++;
            bitstreamName = argv[i];
        }
        else if (strncmp(argv[i], "-of", strlen("-of")) == 0)
        {
            i++;
            outfileName = argv[i];
        }

        i++;
    }


    /* Bitstream */
    if ((*fBitstream = fopen(bitstreamName, "rb")) == NULL)
    {
        exit(-1);
    }

#ifdef BS_HEADER_COMPAT
    /* Read bitstream file header info */
    if (*fBitstream) {
        Avs3ParseBsFrameHeader(hAvs3Dec, *fBitstream, NULL, 0, 1, NULL, NULL);
    }
#endif

    /* Output file */
    /*
    const char* strSuffix = ".wav";
    const char* startPtr = strrchr(outfileName, '.');

    if (strncmp(startPtr, strSuffix, strlen(strSuffix)) == 0)
    {
        *fOutput = WriteWavHeader(outfileName, hAvs3Dec->numChansOutput, hAvs3Dec->outputFs);
    }
    else
    {
        if ((*fOutput = fopen(outfileName, "wb+")) == NULL)
        {
            fprintf(stderr, "Error: Output file %s can not be opened!\n", outfileName);
            exit(-1);
        }
    }
    */
    *fOutput = NULL;

#else
    /* Bitstream */
    if (i < argc - 1)
    {
        if ((*fBitstream = fopen(argv[i], "rb")) == NULL)
        {
            exit(-1);
        }

#ifdef BS_HEADER_COMPAT
        /* Read bitstream file header info */
        if (*fBitstream) {
            Avs3ParseBsFrameHeader(hAvs3Dec, *fBitstream, NULL, 0, 1, NULL, NULL);
        }
#endif

        i++;
    }

    /* Output file */
    /*
    if (i < argc)
    {
        const char* strSuffix = ".wav";
        const char* startPtr = strrchr(argv[i], '.');

        if (strncmp(startPtr, strSuffix, strlen(strSuffix)) == 0)
        {
            *fOutput = WriteWavHeader(argv[i], hAvs3Dec->numChansOutput, hAvs3Dec->outputFs);
        }
        else
        {
            if ((*fOutput = fopen(argv[i], "wb+")) == NULL)
            {
                fprintf(stderr, "Error: Output file %s can not be opened!\n", argv[i]);
                exit(-1);
            }
        }
    }
    */
    *fOutput = NULL;
#endif

    return;
}

void WriteSynthData(const short* data, FILE* file, const short nChans, const short frameLength)
{
    if (file)
    {
        fwrite(data, sizeof(short), nChans * frameLength, file);
    }
}

void SynthWavHeader(FILE* file)
{
    AVS3_WAVE_HEADER_DATA wavHeader;

    int32_t totalFileSize = 0L;
    int32_t riffSize = 0L;
    int32_t dataSize = 0L;

    if (file == NULL)
    {
        // fprintf(stderr, "Output file open error!\n");

        return;
    }

    /* get total file size */
    totalFileSize = ftell(file);

    /* get riff size */
    riffSize = totalFileSize - (sizeof(wavHeader.chunkID) + sizeof(wavHeader.riffSize));

    /* get size */
    dataSize = totalFileSize - sizeof(wavHeader);

    /* seek to riff size field */
    fseek(file, sizeof(wavHeader.chunkID), SEEK_SET);

    /* set riff(chunkID) size */
    if (file) {
        fwrite(&riffSize, sizeof(riffSize), 1, file);
    }

    /* seek to data field */
    if (file) {
        fseek(file, sizeof(AVS3_WAVE_HEADER_DATA) - sizeof(wavHeader.dataSize), SEEK_SET);
    }

    /* set dataSize field */
    if (file) {
        fwrite(&dataSize, sizeof(dataSize), 1, file);
    }

    return;
}