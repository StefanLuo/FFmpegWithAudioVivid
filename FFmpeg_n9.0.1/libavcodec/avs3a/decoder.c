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
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "avs3_cnst_com.h"
#include "avs3_prot_dec.h"
#include "avs3_rom_com.h"
#include "avs3_extern_decoder.h"

#ifdef DEBUG_META
extern FILE *fori = NULL;
#endif

int avs3_decoder(int argc, char* argv[])
{
    static long frame = 0;

    FILE *fBitstream = NULL;
    FILE *fOutput = NULL;
    FILE* fModel = NULL;
    AVS3DecoderHandle hAvs3Dec = NULL;
    short data[MAX_CHANNELS * FRAME_LEN];
    short ret = 0;

    /*
#ifdef DEBUG_META
    fori = fopen("log_dec.txt", "w");
#endif
    */

    if ((hAvs3Dec = (AVS3DecoderHandle)malloc(sizeof(AVS3Decoder))) == NULL)
    {
        fprintf(stderr, "Can not allocate memory for AVS3 decoder structure!\n");
        exit(-1);
    }

    /* Get command line */
    GetAvs3DecoderCommandLine(hAvs3Dec, argc, argv, &fBitstream, &fOutput);

    /* Init decoder */
    Avs3InitDecoder(hAvs3Dec, &fModel);

    while ((ret = ReadBitstream(hAvs3Dec, fBitstream)) != 0)
    {
        fprintf(stdout, "%-8ld\b\b\b\b\b\b\b\b", frame);

        Avs3Decode(hAvs3Dec, data);

        ResetBitstream(hAvs3Dec->hBitstream);

        WriteSynthData(data, fOutput, hAvs3Dec->numChansOutput, hAvs3Dec->frameLength);

        frame++;
    }

    SynthWavHeader(fOutput);

    fprintf(stdout, "Decoding of %ld frames finished\n\n", frame);

    if (fBitstream) {
        fclose(fBitstream);
    }

    if (fModel) {
        fclose(fModel);
    }

    if (fOutput) {
        fclose(fOutput);
    }

#ifdef DEBUG_META
    if (fori) {
        fclose(fori);
    }
#endif

    Avs3DecoderDestroy(hAvs3Dec);

    return 0;
}