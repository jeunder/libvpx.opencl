/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx_config.h"
#include "vpx_rtcd.h"
#include "vp8/common/idct.h"
#include "vp8/common/blockd.h"

extern void vp8_dequantize_b_impl_mmx(short *sq, short *dq, short *q);

void vp8_dequantize_b_mmx(BLOCKD *d, short *DQC)
{
    short *sq = (short *) &d->qcoeff_base[d->qcoeff_offset];
    short *dq = (short *) &d->dqcoeff_base[d->dqcoeff_offset];
    vp8_dequantize_b_impl_mmx(sq, dq, DQC);
}

void vp8_dequant_idct_add_y_block_mmx
            (short *q, short *dq,
             unsigned char *dst, int stride, char *eobs)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        if (eobs[0] > 1)
            vp8_dequant_idct_add_mmx (q, dq, dst, stride);
        else if (eobs[0] == 1)
        {
            vp8_dc_only_idct_add_mmx (q[0]*dq[0], dst, stride, dst, stride);
            ((int *)q)[0] = 0;
        }

        if (eobs[1] > 1)
            vp8_dequant_idct_add_mmx (q+16, dq, dst+4, stride);
        else if (eobs[1] == 1)
        {
            vp8_dc_only_idct_add_mmx (q[16]*dq[0], dst+4, stride,
                                      dst+4, stride);
            ((int *)(q+16))[0] = 0;
        }

        if (eobs[2] > 1)
            vp8_dequant_idct_add_mmx (q+32, dq, dst+8, stride);
        else if (eobs[2] == 1)
        {
            vp8_dc_only_idct_add_mmx (q[32]*dq[0], dst+8, stride,
                                      dst+8, stride);
            ((int *)(q+32))[0] = 0;
        }

        if (eobs[3] > 1)
            vp8_dequant_idct_add_mmx (q+48, dq, dst+12, stride);
        else if (eobs[3] == 1)
        {
            vp8_dc_only_idct_add_mmx (q[48]*dq[0], dst+12, stride,
                                      dst+12, stride);
            ((int *)(q+48))[0] = 0;
        }

        q    += 64;
        dst  += 4*stride;
        eobs += 4;
    }
}

void vp8_dequant_idct_add_uv_block_mmx
            (short *q, short *dq,
             unsigned char *dstu, unsigned char *dstv, int stride, char *eobs)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (eobs[0] > 1)
            vp8_dequant_idct_add_mmx (q, dq, dstu, stride);
        else if (eobs[0] == 1)
        {
            vp8_dc_only_idct_add_mmx (q[0]*dq[0], dstu, stride, dstu, stride);
            ((int *)q)[0] = 0;
        }

        if (eobs[1] > 1)
            vp8_dequant_idct_add_mmx (q+16, dq, dstu+4, stride);
        else if (eobs[1] == 1)
        {
            vp8_dc_only_idct_add_mmx (q[16]*dq[0], dstu+4, stride,
                                      dstu+4, stride);
            ((int *)(q+16))[0] = 0;
        }

        q    += 32;
        dstu += 4*stride;
        eobs += 2;
    }

    for (i = 0; i < 2; i++)
    {
        if (eobs[0] > 1)
            vp8_dequant_idct_add_mmx (q, dq, dstv, stride);
        else if (eobs[0] == 1)
        {
            vp8_dc_only_idct_add_mmx (q[0]*dq[0], dstv, stride, dstv, stride);
            ((int *)q)[0] = 0;
        }

        if (eobs[1] > 1)
            vp8_dequant_idct_add_mmx (q+16, dq, dstv+4, stride);
        else if (eobs[1] == 1)
        {
            vp8_dc_only_idct_add_mmx (q[16]*dq[0], dstv+4, stride,
                                      dstv+4, stride);
            ((int *)(q+16))[0] = 0;
        }

        q    += 32;
        dstv += 4*stride;
        eobs += 2;
    }
}
