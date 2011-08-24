/*
 *  Copyright (c) 2011 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "vpx_ports/config.h"
#include "vp8_opencl.h"
#include "blockd_cl.h"
#include "loopfilter_cl.h"

typedef unsigned char uc;

static int first_run = 1;
static VP8_LOOPFILTER_ARGS filter_args[6];

#define VP8_CL_SET_LOOP_ARG(kernel, current, newargs, argnum, type, name) \
    if (current->name != newargs->name){ \
        err |= clSetKernelArg(kernel, argnum, sizeof (type), &newargs->name); \
        current->name = newargs->name; \
    }\


static int vp8_loop_filter_cl_run(
    cl_command_queue cq,
    cl_kernel kernel,
    VP8_LOOPFILTER_ARGS *args,
    int num_planes,
    int num_blocks,
    int max_threads,
    VP8_LOOPFILTER_ARGS *current_args
){

    size_t global[3] = {max_threads, num_planes, num_blocks};
    size_t local_size;
    int err;

    if (first_run){
        memset(filter_args, -1, sizeof(VP8_LOOPFILTER_ARGS)*6);
        first_run = 0;
    }
    
    err = 0;
    VP8_CL_SET_LOOP_ARG(kernel, current_args, args, 0, cl_mem, buf_mem)
    VP8_CL_SET_LOOP_ARG(kernel, current_args, args, 1, cl_mem, offsets_mem)
    VP8_CL_SET_LOOP_ARG(kernel, current_args, args, 2, cl_mem, pitches_mem)
    VP8_CL_SET_LOOP_ARG(kernel, current_args, args, 3, cl_mem, lfi_mem)
    VP8_CL_SET_LOOP_ARG(kernel, current_args, args, 4, cl_mem, filters_mem)
    VP8_CL_SET_LOOP_ARG(kernel, current_args, args, 5, cl_int, use_mbflim)
    VP8_CL_SET_LOOP_ARG(kernel, current_args, args, 6, cl_int, filter_type)
    VP8_CL_SET_LOOP_ARG(kernel, current_args, args, 7, cl_int, cur_iter)
    VP8_CL_CHECK_SUCCESS( cq, err != CL_SUCCESS,
        "Error: Failed to set kernel arguments!\n",,err
    );

    VP8_CL_CALC_LOCAL_SIZE(kernel,&local_size);

    /* Execute the kernel */
    if (local_size < (global[0]*global[1]*global[2]))
        err = clEnqueueNDRangeKernel(cq, kernel, 3, NULL, global, NULL , 0, NULL, NULL);
    else
        err = clEnqueueNDRangeKernel(cq, kernel, 3, NULL, global, global , 0, NULL, NULL);
    
    VP8_CL_CHECK_SUCCESS( cq, err != CL_SUCCESS,
        "Error: Failed to execute kernel!\n",
        printf("err = %d\n",err);,err
    );
        
    return CL_SUCCESS;
}

void vp8_loop_filter_horizontal_edge_cl
(
    MACROBLOCKD *x,
    VP8_LOOPFILTER_ARGS *args,
    int num_planes,
    int num_blocks,
    int max_threads
)
{
    vp8_loop_filter_cl_run(x->cl_commands,
        cl_data.vp8_loop_filter_horizontal_edge_kernel, args, num_planes, num_blocks, max_threads, &filter_args[0]
    );
}

void vp8_loop_filter_vertical_edge_cl
(
    MACROBLOCKD *x,
    VP8_LOOPFILTER_ARGS *args,
    int num_planes,
    int num_blocks,
    int max_threads
)
{
    vp8_loop_filter_cl_run(x->cl_commands,
        cl_data.vp8_loop_filter_vertical_edge_kernel, args, num_planes, num_blocks, max_threads, &filter_args[1]
    );
}

void vp8_mbloop_filter_horizontal_edge_cl
(
    MACROBLOCKD *x,
    VP8_LOOPFILTER_ARGS *args,
    int num_planes,
    int num_blocks,
    int max_threads
)
{
    vp8_loop_filter_cl_run(x->cl_commands,
        cl_data.vp8_mbloop_filter_horizontal_edge_kernel, args, num_planes, num_blocks, max_threads, &filter_args[2]
    );
}


void vp8_mbloop_filter_vertical_edge_cl
(
    MACROBLOCKD *x,
    VP8_LOOPFILTER_ARGS *args,
    int num_planes,
    int num_blocks,
    int max_threads
)
{
    vp8_loop_filter_cl_run(x->cl_commands,
        cl_data.vp8_mbloop_filter_vertical_edge_kernel, args, num_planes, num_blocks, max_threads, &filter_args[3]
    );
}

void vp8_loop_filter_simple_horizontal_edge_cl
(
    MACROBLOCKD *x,
    VP8_LOOPFILTER_ARGS *args,
    int num_planes,
    int num_blocks,
    int max_threads
)
{
    vp8_loop_filter_cl_run(x->cl_commands,
        cl_data.vp8_loop_filter_simple_horizontal_edge_kernel, args, num_planes, num_blocks, max_threads, &filter_args[4]
    );
}

void vp8_loop_filter_simple_vertical_edge_cl
(
    MACROBLOCKD *x,
    VP8_LOOPFILTER_ARGS *args,
    int num_planes,
    int num_blocks,
    int max_threads
)
{
    vp8_loop_filter_cl_run(x->cl_commands,
        cl_data.vp8_loop_filter_simple_vertical_edge_kernel, args, num_planes, num_blocks, max_threads, &filter_args[5]
    );
}
