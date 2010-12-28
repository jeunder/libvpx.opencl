/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vpx_ports/config.h"
#include "onyxd_int.h"

void vp8_arch_opencl_decode_init(VP8D_COMP *pbi)
{
    /* Override current functions with OpenCL replacements: */
#if CONFIG_RUNTIME_CPU_DETECT
    //pbi->mb.rtcd                     = &pbi->common.rtcd;
    //pbi->dequant.block               = vp8_dequantize_b_cl;
    //pbi->dequant.idct_add            = vp8_dequant_idct_add_cl;
    //pbi->dequant.dc_idct_add         = vp8_dequant_dc_idct_add_cl;
    //pbi->dequant.dc_idct_add_y_block = vp8_dequant_dc_idct_add_y_block_cl;
    //pbi->dequant.idct_add_y_block    = vp8_dequant_idct_add_y_block_cl;
    //pbi->dequant.idct_add_uv_block   = vp8_dequant_idct_add_uv_block_cl;
    //pbi->dboolhuff.start             = vp8dx_start_decode_cl;
    //pbi->dboolhuff.fill              = vp8dx_bool_decoder_fill_cl;
    //pbi->dboolhuff.debool = vp8dx_decode_bool_cl;
    //pbi->dboolhuff.devalue = vp8dx_decode_value_cl;
#endif
}