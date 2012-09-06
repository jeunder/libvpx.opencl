/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_INVTRANS_H
#define __INC_INVTRANS_H

#include "vpx_config.h"
#include "vpx_rtcd.h"
#include "idct.h"
#include "blockd.h"
#include "onyxc_int.h"

#if CONFIG_MULTITHREAD
#include "vpx_mem/vpx_mem.h"
#endif

static void eob_adjust(char *eobs, short *diff)
{
    /* eob adjust.... the idct can only skip if both the dc and eob are zero */
    int js;
    for(js = 0; js < 16; js++)
    {
        if((eobs[js] == 0) && (diff[0] != 0))
            eobs[js]++;
        diff+=16;
    }
}

static void vp8_inverse_transform_mby(MACROBLOCKD *xd,
                                      const VP8_COMMON_RTCD *rtcd)
{
    short *DQC = xd->dequant_y1;

    if (xd->mode_info_context->mbmi.mode != SPLITMV)
    {
        /* do 2nd order transform on the dc block */
        if (xd->eobs[24] > 1)
        {
            IDCT_INVOKE(&rtcd->idct, iwalsh16)
                (&xd->block[24].dqcoeff_base[xd->block[24].dqcoeff_offset], xd->qcoeff);
        }
        else
        {
            IDCT_INVOKE(&rtcd->idct, iwalsh1)
                (&xd->block[24].dqcoeff_base[xd->block[24].dqcoeff_offset], xd->qcoeff);
        }
        eob_adjust(xd->eobs, xd->qcoeff);

        DQC = xd->dequant_y1_dc;
    }
    vp8_dequant_idct_add_y_block
                    (xd->qcoeff, DQC,
                     xd->dst.y_buffer,
                     xd->dst.y_stride, xd->eobs);
}
#endif
