/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include <stdlib.h>

//ACW: Remove me after debugging.
#include <stdio.h>

#include "filter_cl.h"

#define SIXTAP_FILTER_LEN 6
#define MAX_NUM_PLATFORMS 4
#define CL_TRIED_BUT_FAILED 1
#define CL_NOT_INITIALIZED -1
#define BLOCK_HEIGHT_WIDTH 4


int cl_initialized = CL_NOT_INITIALIZED;
VP8_COMMON_CL cl_data;
int pass=0;

#define USE_LOCAL_SIZE 0
#if USE_LOCAL_SIZE
size_t local;
#endif

extern void vp8_filter_block2d_first_pass
(
    unsigned char *src_ptr,
    int *output_ptr,
    unsigned int src_pixels_per_line,
    unsigned int pixel_step,
    unsigned int output_height,
    unsigned int output_width,
    const short *vp8_filter
);

extern void vp8_filter_block2d_second_pass
(
    int *src_ptr,
    unsigned char *output_ptr,
    int output_pitch,
    unsigned int src_pixels_per_line,
    unsigned int pixel_step,
    unsigned int output_height,
    unsigned int output_width,
    const short *vp8_filter
);

char *read_file(const char* file_name){
    long pos;
    char *bytes;
    size_t amt_read;
    
    FILE *f = fopen(file_name, "rb");
    if (f == NULL)
        return NULL;

    fseek(f, 0, SEEK_END);
    pos = ftell(f);
    fseek(f, 0, SEEK_SET);

    bytes = malloc(pos);
    if (bytes == NULL){
        fclose(f);
        return NULL;
    }

    amt_read = fread(bytes, pos, 1, f);
    if (amt_read != 1){
        free(bytes);
        fclose(f);
        return NULL;
    }

    fclose(f);

    return bytes;
}

/**
 *
 */
void cl_destroy() {
    if (cl_data.filterData){
        clReleaseMemObject(cl_data.filterData);
        cl_data.filterData = NULL;
    }

    if (cl_data.srcData){
        clReleaseMemObject(cl_data.srcData);
        cl_data.srcData = NULL;
    }

    if (cl_data.destData){
        clReleaseMemObject(cl_data.destData);
        cl_data.destData = NULL;
    }

    //Release the objects that we've allocated on the GPU
    if (cl_data.program)
        clReleaseProgram(cl_data.program);
    if (cl_data.filter_block2d_first_pass_kernel)
        clReleaseKernel(cl_data.filter_block2d_first_pass_kernel);
    if (cl_data.filter_block2d_second_pass_kernel)
        clReleaseKernel(cl_data.filter_block2d_second_pass_kernel);
    if (cl_data.commands)
        clReleaseCommandQueue(cl_data.commands);
    if (cl_data.context)
        clReleaseContext(cl_data.context);

    cl_data.program = NULL;
    cl_data.filter_block2d_first_pass_kernel = NULL;
    cl_data.filter_block2d_second_pass_kernel = NULL;
    cl_data.commands = NULL;
    cl_data.context = NULL;

    cl_initialized = CL_NOT_INITIALIZED;

    return;
}


int cl_init_filter_block2d() {
    // Connect to a compute device
    int err;
    char *kernel_src;
    cl_platform_id platform_ids[MAX_NUM_PLATFORMS];
    cl_uint num_found;
    err = clGetPlatformIDs(MAX_NUM_PLATFORMS, platform_ids, &num_found);

    if (err != CL_SUCCESS) {
        printf("Couldn't query platform IDs\n");
        return CL_TRIED_BUT_FAILED;
    }
    if (num_found == 0) {
        printf("No platforms found\n");
        return CL_TRIED_BUT_FAILED;
    }
    //printf("Found %d platforms\n", num_found);

    //Favor the GPU, but fall back to any other available device if necessary
    err = clGetDeviceIDs(platform_ids[0], CL_DEVICE_TYPE_GPU, 1, &cl_data.device_id, NULL);
    if (err != CL_SUCCESS) {
        err = clGetDeviceIDs(platform_ids[0], CL_DEVICE_TYPE_ALL, 1, &cl_data.device_id, NULL);
        if (err != CL_SUCCESS) {
            printf("Error: Failed to create a device group!\n");
            return CL_TRIED_BUT_FAILED;
        }
    }

    // Create the compute context
    cl_data.context = clCreateContext(0, 1, &cl_data.device_id, NULL, NULL, &err);
    if (!cl_data.context) {
        printf("Error: Failed to create a compute context!\n");
        return CL_TRIED_BUT_FAILED;
    }

    // Create a command queue
    cl_data.commands = clCreateCommandQueue(cl_data.context, cl_data.device_id, 0, &err);
    if (!cl_data.commands || err != CL_SUCCESS) {
        printf("Error: Failed to create a command queue!\n");
        return CL_TRIED_BUT_FAILED;
    }

    // Create the compute program from the file-defined source code
    kernel_src = read_file(filter_cl_file_name);
    if (kernel_src != NULL){
        printf("creating program from source file\n");
        cl_data.program = clCreateProgramWithSource(cl_data.context, 1, &kernel_src, NULL, &err);
        free(kernel_src);
    } else {
        cl_destroy();
        printf("Couldn't find OpenCL source files. \nUsing software path.\n");
        return CL_TRIED_BUT_FAILED;
    }

    if (!cl_data.program) {
        printf("Error: Couldn't compile program\n");
        return CL_TRIED_BUT_FAILED;
    }
    //printf("Created Program\n");

    // Build the program executable
    err = clBuildProgram(cl_data.program, 0, NULL, compileOptions, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t len;
        char buffer[20480];

        printf("Error: Failed to build program executable!\n");
        clGetProgramBuildInfo(cl_data.program, cl_data.device_id, CL_PROGRAM_BUILD_LOG, sizeof (buffer), &buffer, &len);
        printf("Compile output: %s\n", buffer);
        return CL_TRIED_BUT_FAILED;
    }
    //printf("Built executable\n");

    // Create the compute kernel in the program we wish to run
    cl_data.filter_block2d_first_pass_kernel = clCreateKernel(cl_data.program, "vp8_filter_block2d_first_pass_kernel", &err);
    cl_data.filter_block2d_second_pass_kernel = clCreateKernel(cl_data.program, "vp8_filter_block2d_second_pass_kernel", &err);
    if (!cl_data.filter_block2d_first_pass_kernel || 
            !cl_data.filter_block2d_second_pass_kernel ||
            err != CL_SUCCESS) {
        printf("Error: Failed to create compute kernel!\n");
        return CL_TRIED_BUT_FAILED;
    }
    //printf("Created kernel\n");

#if USE_LOCAL_SIZE
    // Get the maximum work group size for executing the kernel on the device
    err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof (local), &local, NULL);
    if (err != CL_SUCCESS) {
        printf("Error: Failed to retrieve kernel work group info! %d\n", err);
        return CL_TRIED_BUT_FAILED;
    }
    //printf("local=%d\n",local);
#endif

    //Filter size doesn't change. Allocate buffer once, and just replace contents
    //on each kernel execution.
    cl_data.filterData = clCreateBuffer(cl_data.context, CL_MEM_READ_ONLY, sizeof (short) * SIXTAP_FILTER_LEN, NULL, NULL);
    if (!cl_data.filterData){
        printf("Error: Failed to allocate filter buffer\n");
        return CL_TRIED_BUT_FAILED;
    }

    //Initialize these to null pointers
    cl_data.srcData = NULL;
    cl_data.destData = NULL;

    return CL_SUCCESS;
}

void vp8_filter_block2d_first_pass_cl
(
        unsigned char *src_ptr,
        int *output_ptr,
        unsigned int src_pixels_per_line,
        unsigned int pixel_step,
        unsigned int output_height,
        unsigned int output_width,
#ifdef FILTER_OFFSET
        int filter_offset
#else
        const short *vp8_filter
#endif
        ) {

    int err;
#define SHOW_OUTPUT_1ST 0
#define SHOW_OUTPUT_2ND 1
#if SHOW_OUTPUT_1ST
    int j;
#endif
    size_t global;

    //Calculate size of input and output arrays
    int dest_len = output_height * output_width;

    //Copy the -2*pixel_step bytes because the filter algorithm accesses negative indexes
    int src_len = (dest_len + ((dest_len-1)/output_width)*(src_pixels_per_line - output_width) + 5 * (int)pixel_step);

    if (cl_initialized != CL_SUCCESS){
        if (cl_initialized == CL_NOT_INITIALIZED){
            cl_initialized = cl_init_filter_block2d();
        }
        if (cl_initialized != CL_SUCCESS){
            vp8_filter_block2d_first_pass(src_ptr, output_ptr, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
            return;
        }
    }

    // Create input/output buffers in device memory
    cl_data.srcData = clCreateBuffer(cl_data.context, CL_MEM_READ_ONLY, sizeof (unsigned char) * src_len, NULL, NULL);
    cl_data.intData = clCreateBuffer(cl_data.context, CL_MEM_READ_WRITE, sizeof (int) * dest_len, NULL, NULL);

    //printf("srcData=%p\tdestData=%p\tfilterData=%p\n",srcData,destData,filterData);
    if (!cl_data.srcData || !cl_data.intData) {
        printf("Error: Failed to allocate device memory. Using CPU path!\n");
        cl_destroy();
        cl_initialized = CL_TRIED_BUT_FAILED;
        vp8_filter_block2d_first_pass(src_ptr, output_ptr, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
    }

    // Copy input and filter data to device
    err = clEnqueueWriteBuffer(cl_data.commands, cl_data.srcData, CL_FALSE, 0,
            sizeof (unsigned char) * src_len, src_ptr-(2*(int)pixel_step), 0, NULL, NULL);

#ifndef FILTER_OFFSET
    err = clEnqueueWriteBuffer(cl_data.commands, cl_data.filterData, CL_FALSE, 0,
            sizeof (short) * SIXTAP_FILTER_LEN, vp8_filter, 0, NULL, NULL);
#endif
    if (err != CL_SUCCESS) {
        clFinish(cl_data.commands); //Wait for commands to finish so pointers are usable again.
        cl_destroy();
        cl_initialized = CL_TRIED_BUT_FAILED;
        printf("Error: Failed to write to source array!\n");
        vp8_filter_block2d_first_pass(src_ptr, output_ptr, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
        return;
    }

    // Set kernel arguments
    err = 0;
    err = clSetKernelArg(cl_data.filter_block2d_first_pass_kernel, 0, sizeof (cl_mem), &cl_data.srcData);
    err |= clSetKernelArg(cl_data.filter_block2d_first_pass_kernel, 1, sizeof (cl_mem), &cl_data.intData);
    err |= clSetKernelArg(cl_data.filter_block2d_first_pass_kernel, 2, sizeof (unsigned int), &src_pixels_per_line);
    err |= clSetKernelArg(cl_data.filter_block2d_first_pass_kernel, 3, sizeof (unsigned int), &pixel_step);
    err |= clSetKernelArg(cl_data.filter_block2d_first_pass_kernel, 4, sizeof (unsigned int), &output_height);
    err |= clSetKernelArg(cl_data.filter_block2d_first_pass_kernel, 5, sizeof (unsigned int), &output_width);
#ifdef FILTER_OFFSET
    err |= clSetKernelArg(cl_data.filter_block2d_first_pass_kernel, 6, sizeof (int), &filter_offset);
#else
    err |= clSetKernelArg(cl_data.filter_block2d_first_pass_kernel, 6, sizeof (cl_mem), &cl_data.filterData);
#endif
    if (err != CL_SUCCESS) {
        cl_destroy();
        cl_initialized=CL_TRIED_BUT_FAILED;
        printf("Error: Failed to set kernel arguments! %d\n", err);
        vp8_filter_block2d_first_pass(src_ptr, output_ptr, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
        return;
    }
    //printf("Set kernel arguments\n");

    // Execute the kernel
    global = output_width*output_height; //How many threads do we need?
#if USE_LOCAL_SIZE
    //NOTE: if local<global, global MUST be evenly divisible by local or the
    //      kernel will fail.
    printf("local=%d, global=%d\n", local, global);
    err = clEnqueueNDRangeKernel(cl_data.commands, cl_data.filter_block2d_first_pass_kernel, 1, NULL, &global, ((local<global)? &local: &global) , 0, NULL, NULL);
#else
    err = clEnqueueNDRangeKernel(cl_data.commands, cl_data.filter_block2d_first_pass_kernel, 1, NULL, &global, NULL , 0, NULL, NULL);
#endif
    if (err) {
        clFinish(cl_data.commands);
        cl_destroy();
        cl_initialized = CL_TRIED_BUT_FAILED;
        printf("Error: Failed to execute kernel!\n");
        vp8_filter_block2d_first_pass(src_ptr, output_ptr, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
        return;
    }
    //printf("Kernel queued\n");

    // Read back the result data from the device
    err = clEnqueueReadBuffer(cl_data.commands, cl_data.intData, CL_FALSE, 0, sizeof (int) * dest_len, output_ptr, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        clFinish(cl_data.commands);
        cl_destroy();
        cl_initialized = CL_TRIED_BUT_FAILED;
        printf("Error: Failed to read output array! %d\n", err);
        vp8_filter_block2d_first_pass(src_ptr, output_ptr, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
        return;
    }

    clFinish(cl_data.commands);
    
#if SHOW_OUTPUT_1ST

    //Run C code so that we can compare output for correctness.
    int c_output[output_height*output_width];
    pass++;
    vp8_filter_block2d_first_pass(src_ptr, c_output, src_pixels_per_line, pixel_step, output_height, output_width, vp8_filter);


    for (j=0; j < dest_len; j++){
        if (output_ptr[j] != c_output[j]){
            printf("pass %d, dest_len %d, output_ptr[%d] = %d, c[%d]=%d\n", pass, dest_len, j, output_ptr[j], j, c_output[j]);
            //exit(1);
        }
    }
#endif

    // Release memory that is only used once
    clReleaseMemObject(cl_data.srcData);
    cl_data.srcData = NULL;

    return;

}

void vp8_filter_block2d_second_pass_cl
(
        int *src_ptr,
        unsigned int src_len,
        int offset,
        unsigned char *output_ptr,
        int output_pitch,
        unsigned int src_pixels_per_line,
        unsigned int pixel_step,
        unsigned int output_height,
        unsigned int output_width,
#ifdef FILTER_OFFSET
        int filter_offset
#else
        const short *vp8_filter
#endif
        ) {

    int err;
    int *src_bak = malloc(sizeof(int)*src_len);
#if SHOW_OUTPUT_2ND
    //Run C code so that we can compare output for correctness.
    unsigned char c_output[output_pitch*output_width];
    int j;
#endif
    size_t global;

    //Calculate size of input and output arrays
    //int dest_len = output_width-1+(output_pitch*(output_height-1));
    int dest_len = output_width+(output_pitch*output_height);

    //Copy the -2*pixel_step bytes because the filter algorithm accesses negative indexes
    //int src_len = (dest_len + ((dest_len-1)/output_width)*(src_pixels_per_line - output_width) + 5 * (int)pixel_step);
    if (!src_bak){
        printf("Couldn't allocate src_bak");
        exit(1);
    }

    if (cl_initialized != CL_SUCCESS){
            vp8_filter_block2d_second_pass(&src_ptr[offset], output_ptr, output_pitch, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
    }

    // Create input/output buffers in device memory
    cl_data.destData = clCreateBuffer(cl_data.context, CL_MEM_WRITE_ONLY, sizeof (unsigned char) * dest_len, NULL, NULL);

    if (!cl_data.destData) {
        printf("Error: Failed to allocate device memory. Using CPU path!\n");
        cl_initialized = CL_TRIED_BUT_FAILED;
        vp8_filter_block2d_second_pass(&src_ptr[offset], output_ptr, output_pitch, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
    }
    //printf("Created buffers on device\n");

    // Copy input and filter data to device
    //err = clEnqueueWriteBuffer(cl_data.commands, cl_data.intData, CL_FALSE, 0,
    //        sizeof (int) * src_len, src_ptr, 0, NULL, NULL);
    err = clEnqueueReadBuffer(cl_data.commands, cl_data.intData, CL_FALSE, 0, sizeof (int) * src_len, src_bak, 0, NULL, NULL);
    clFinish(cl_data.commands);
    printf("Checking src_bak\n");
/*
    for (j=0; j < src_len; j++){
        printf("j=%d\n",j);
        if (src_ptr[j] != src_bak[j]){
            printf("src copy doesn't match\n");
            exit(1);
        }
    }
*/

    //err = clEnqueueWriteBuffer(cl_data.commands, cl_data.intData, CL_FALSE, 0,
    //        sizeof (int) * src_len, (&src_ptr[offset])-(2*(int)pixel_step), 0, NULL, NULL);

#ifndef FILTER_OFFSET
    err = clEnqueueWriteBuffer(cl_data.commands, cl_data.filterData, CL_FALSE, 0,
            sizeof (short) * SIXTAP_FILTER_LEN, vp8_filter, 0, NULL, NULL);
#endif
    if (err != CL_SUCCESS) {
        clFinish(cl_data.commands); //Wait for commands to finish so pointers are usable again.
        cl_destroy();
        cl_initialized = CL_TRIED_BUT_FAILED;
        printf("Error: Failed to write to source array!\n");
        vp8_filter_block2d_second_pass(&src_ptr[offset], output_ptr, output_pitch, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
        return;
    }

    // Set kernel arguments
    err = 0;
    err = clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 0, sizeof (cl_mem), &cl_data.intData);
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 1, sizeof (int), &offset);
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 2, sizeof (cl_mem), &cl_data.destData);
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 3, sizeof (int), &output_pitch);
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 4, sizeof (unsigned int), &src_pixels_per_line);
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 5, sizeof (unsigned int), &pixel_step);
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 6, sizeof (unsigned int), &output_height);
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 7, sizeof (unsigned int), &output_width);
#ifdef FILTER_OFFSET
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 8, sizeof (int), &filter_offset);
#else
    err |= clSetKernelArg(cl_data.filter_block2d_second_pass_kernel, 8, sizeof (cl_mem), &cl_data.filterData);
#endif
    if (err != CL_SUCCESS) {
        cl_destroy();
        cl_initialized=CL_TRIED_BUT_FAILED;
        printf("Error: Failed to set kernel arguments! %d\n", err);
        vp8_filter_block2d_second_pass(&src_ptr[offset], output_ptr, output_pitch, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
        return;
    }
    //printf("Set kernel arguments\n");

    // Execute the kernel
    global = output_width*output_height; //How many threads do we need?
#if USE_LOCAL_SIZE
    //NOTE: if local<global, global MUST be evenly divisible by local or the
    //      kernel will fail.
    printf("local=%d, global=%d\n", local, global);
    err = clEnqueueNDRangeKernel(cl_data.commands, cl_data.filter_block2d_second_pass_kernel, 1, NULL, &global, ((local<global)? &local: &global) , 0, NULL, NULL);
#else
    err = clEnqueueNDRangeKernel(cl_data.commands, cl_data.filter_block2d_second_pass_kernel, 1, NULL, &global, NULL , 0, NULL, NULL);
#endif
    if (err) {
        clFinish(cl_data.commands);
        cl_destroy();
        cl_initialized = CL_TRIED_BUT_FAILED;
        printf("Error: Failed to execute kernel!\n");
        vp8_filter_block2d_second_pass(&src_ptr[offset], output_ptr, output_pitch, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
        return;
    }

    printf("Kernel enqueued %d\n", pass++);
    clFinish(cl_data.commands);

    vp8_filter_block2d_second_pass(&src_ptr[offset], output_ptr, output_pitch, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
    return;

    printf("Reading memory\n");
    // Read back the result data from the device
    err = clEnqueueReadBuffer(cl_data.commands, cl_data.destData, CL_FALSE, 0, sizeof (unsigned char) * dest_len, output_ptr, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        clFinish(cl_data.commands);
        cl_destroy();
        cl_initialized = CL_TRIED_BUT_FAILED;
        printf("Error: Failed to read output array! %d\n", err);
        vp8_filter_block2d_second_pass(&src_ptr[offset], output_ptr, output_pitch, src_pixels_per_line, pixel_step, output_height, output_width, FILTER_REF);
        return;
    }

    clFinish(cl_data.commands);
    printf("done reading memory\n");

#if SHOW_OUTPUT_2ND
    //pass++;
    vp8_filter_block2d_second_pass(&src_ptr[offset], c_output, output_pitch, src_pixels_per_line, pixel_step, output_height, output_width, vp8_filter);

    for (j=0; j < dest_len; j++){
        if (output_ptr[j] != c_output[j]){
            printf("pass %d, dest_len %d, output_width %d, output_height %d, output_pitch %d, output_ptr[%d] = %c, c[%d]=%c\n", pass, dest_len, output_width, output_height, output_pitch, j, output_ptr[j], j, c_output[j]);
            //exit(1);
        }
    }
#endif
    printf("releasing memory\n");

    // Release memory that is only used once
    clReleaseMemObject(cl_data.intData);
    clReleaseMemObject(cl_data.destData);
    cl_data.intData = NULL;
    cl_data.destData = NULL;
    
    printf("done releasing\n");

    return;

}

void vp8_filter_block2d_cl
(
        unsigned char *src_ptr,
        unsigned char *output_ptr,
        unsigned int src_pixels_per_line,
        int output_pitch,
#ifdef FILTER_OFFSET
        int xoffset,
#else
        const short *HFilter,
#endif
        const short *VFilter
        ) {
    int FData[9 * 4]; /* Temp data buffer used in filtering */

    /* First filter 1-D horizontally... */
#ifdef FILTER_OFFSET
    vp8_filter_block2d_first_pass_cl(src_ptr - (2 * src_pixels_per_line), FData, src_pixels_per_line, 1, 9, 4, xoffset);
#else
    vp8_filter_block2d_first_pass_cl(src_ptr - (2 * src_pixels_per_line), FData, src_pixels_per_line, 1, 9, 4, HFilter);
#endif
    
    /* then filter vertically... */
    vp8_filter_block2d_second_pass_cl(FData, 9*4, 8, output_ptr, output_pitch, 4, 4, 4, 4, VFilter);
}

void vp8_block_variation_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int *HVar,
        int *VVar
        ) {
    int i, j;
    unsigned char *Ptr = src_ptr;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            *HVar += abs((int) Ptr[j] - (int) Ptr[j + 1]);
            *VVar += abs((int) Ptr[j] - (int) Ptr[j + src_pixels_per_line]);
        }

        Ptr += src_pixels_per_line;
    }
}

void vp8_sixtap_predict_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        unsigned char *dst_ptr,
        int dst_pitch
        ) {

    const short *HFilter;
    const short *VFilter;

    HFilter = sub_pel_filters[xoffset]; /* 6 tap */
    VFilter = sub_pel_filters[yoffset]; /* 6 tap */

#ifdef FILTER_OFFSET
    vp8_filter_block2d_cl(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, xoffset, VFilter);
#else
    vp8_filter_block2d_cl(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter);
#endif
}

void vp8_sixtap_predict8x8_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        unsigned char *dst_ptr,
        int dst_pitch
        ) {
    const short *HFilter;
    const short *VFilter;
    int FData[13 * 16]; /* Temp data buffer used in filtering */

    HFilter = sub_pel_filters[xoffset]; /* 6 tap */
    VFilter = sub_pel_filters[yoffset]; /* 6 tap */

    /* First filter 1-D horizontally... */
#ifdef FILTER_OFFSET
    vp8_filter_block2d_first_pass_cl(src_ptr - (2 * src_pixels_per_line), FData, src_pixels_per_line, 1, 13, 8, xoffset);
#else
    vp8_filter_block2d_first_pass_cl(src_ptr - (2 * src_pixels_per_line), FData, src_pixels_per_line, 1, 13, 8, HFilter);
#endif

    /* then filter vertically... */
    vp8_filter_block2d_second_pass_cl(FData, 13*16, 16, dst_ptr, dst_pitch, 8, 8, 8, 8, VFilter);

}

void vp8_sixtap_predict8x4_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        unsigned char *dst_ptr,
        int dst_pitch
        ) {
    const short *HFilter;
    const short *VFilter;
    int FData[13 * 16]; /* Temp data buffer used in filtering */

    HFilter = sub_pel_filters[xoffset]; /* 6 tap */
    VFilter = sub_pel_filters[yoffset]; /* 6 tap */

    /* First filter 1-D horizontally... */
#ifdef FILTER_OFFSET
    vp8_filter_block2d_first_pass_cl(src_ptr - (2 * src_pixels_per_line), FData, src_pixels_per_line, 1, 9, 8, xoffset);
#else
    vp8_filter_block2d_first_pass_cl(src_ptr - (2 * src_pixels_per_line), FData, src_pixels_per_line, 1, 9, 8, HFilter);
#endif

    /* then filter vertically... */
    vp8_filter_block2d_second_pass_cl(FData, 13*16, 16, dst_ptr, dst_pitch, 8, 8, 4, 8, VFilter);

}

void vp8_sixtap_predict16x16_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        unsigned char *dst_ptr,
        int dst_pitch
        ) {
    const short *HFilter;
    const short *VFilter;
    int FData[21 * 24]; /* Temp data buffer used in filtering */

    HFilter = sub_pel_filters[xoffset]; /* 6 tap */
    VFilter = sub_pel_filters[yoffset]; /* 6 tap */

    /* First filter 1-D horizontally... */
#ifdef FILTER_OFFSET
    vp8_filter_block2d_first_pass_cl(src_ptr - (2 * src_pixels_per_line), FData, src_pixels_per_line, 1, 21, 16, xoffset);
#else
    vp8_filter_block2d_first_pass_cl(src_ptr - (2 * src_pixels_per_line), FData, src_pixels_per_line, 1, 21, 16, HFilter);
#endif

    /* then filter vertically... */
    vp8_filter_block2d_second_pass_cl(FData, 21*24, 32, dst_ptr, dst_pitch, 16, 16, 16, 16, VFilter);

}

/****************************************************************************
 *
 *  ROUTINE       : filter_block2d_bil_first_pass
 *
 *  INPUTS        : UINT8  *src_ptr          : Pointer to source block.
 *                  UINT32 src_pixels_per_line : Stride of input block.
 *                  UINT32 pixel_step        : Offset between filter input samples (see notes).
 *                  UINT32 output_height     : Input block height.
 *                  UINT32 output_width      : Input block width.
 *                  INT32  *vp8_filter          : Array of 2 bi-linear filter taps.
 *
 *  OUTPUTS       : INT32 *output_ptr        : Pointer to filtered block.
 *
 *  RETURNS       : void
 *
 *  FUNCTION      : Applies a 1-D 2-tap bi-linear filter to the source block in
 *                  either horizontal or vertical direction to produce the
 *                  filtered output block. Used to implement first-pass
 *                  of 2-D separable filter.
 *
 *  SPECIAL NOTES : Produces INT32 output to retain precision for next pass.
 *                  Two filter taps should sum to VP8_FILTER_WEIGHT.
 *                  pixel_step defines whether the filter is applied
 *                  horizontally (pixel_step=1) or vertically (pixel_step=stride).
 *                  It defines the offset required to move from one input
 *                  to the next.
 *
 ****************************************************************************/
void vp8_filter_block2d_bil_first_pass_cl
(
        unsigned char *src_ptr,
        unsigned short *output_ptr,
        unsigned int src_pixels_per_line,
        int pixel_step,
        unsigned int output_height,
        unsigned int output_width,
        const int *vp8_filter
        ) {
    unsigned int i, j;

    for (i = 0; i < output_height; i++) {
        for (j = 0; j < output_width; j++) {
            /* Apply bilinear filter */
            output_ptr[j] = (((int) src_ptr[0] * vp8_filter[0]) +
                    ((int) src_ptr[pixel_step] * vp8_filter[1]) +
                    (VP8_FILTER_WEIGHT / 2)) >> VP8_FILTER_SHIFT;
            src_ptr++;
        }

        /* Next row... */
        src_ptr += src_pixels_per_line - output_width;
        output_ptr += output_width;
    }
}

/****************************************************************************
 *
 *  ROUTINE       : filter_block2d_bil_second_pass
 *
 *  INPUTS        : INT32  *src_ptr          : Pointer to source block.
 *                  UINT32 src_pixels_per_line : Stride of input block.
 *                  UINT32 pixel_step        : Offset between filter input samples (see notes).
 *                  UINT32 output_height     : Input block height.
 *                  UINT32 output_width      : Input block width.
 *                  INT32  *vp8_filter          : Array of 2 bi-linear filter taps.
 *
 *  OUTPUTS       : UINT16 *output_ptr       : Pointer to filtered block.
 *
 *  RETURNS       : void
 *
 *  FUNCTION      : Applies a 1-D 2-tap bi-linear filter to the source block in
 *                  either horizontal or vertical direction to produce the
 *                  filtered output block. Used to implement second-pass
 *                  of 2-D separable filter.
 *
 *  SPECIAL NOTES : Requires 32-bit input as produced by filter_block2d_bil_first_pass.
 *                  Two filter taps should sum to VP8_FILTER_WEIGHT.
 *                  pixel_step defines whether the filter is applied
 *                  horizontally (pixel_step=1) or vertically (pixel_step=stride).
 *                  It defines the offset required to move from one input
 *                  to the next.
 *
 ****************************************************************************/
void vp8_filter_block2d_bil_second_pass_cl
(
        unsigned short *src_ptr,
        unsigned char *output_ptr,
        int output_pitch,
        unsigned int src_pixels_per_line,
        unsigned int pixel_step,
        unsigned int output_height,
        unsigned int output_width,
        const int *vp8_filter
        ) {
    unsigned int i, j;
    int Temp;

    for (i = 0; i < output_height; i++) {
        for (j = 0; j < output_width; j++) {
            /* Apply filter */
            Temp = ((int) src_ptr[0] * vp8_filter[0]) +
                    ((int) src_ptr[pixel_step] * vp8_filter[1]) +
                    (VP8_FILTER_WEIGHT / 2);
            output_ptr[j] = (unsigned int) (Temp >> VP8_FILTER_SHIFT);
            src_ptr++;
        }

        /* Next row... */
        src_ptr += src_pixels_per_line - output_width;
        output_ptr += output_pitch;
    }
}

/****************************************************************************
 *
 *  ROUTINE       : filter_block2d_bil
 *
 *  INPUTS        : UINT8  *src_ptr          : Pointer to source block.
 *                  UINT32 src_pixels_per_line : Stride of input block.
 *                  INT32  *HFilter         : Array of 2 horizontal filter taps.
 *                  INT32  *VFilter         : Array of 2 vertical filter taps.
 *
 *  OUTPUTS       : UINT16 *output_ptr       : Pointer to filtered block.
 *
 *  RETURNS       : void
 *
 *  FUNCTION      : 2-D filters an input block by applying a 2-tap
 *                  bi-linear filter horizontally followed by a 2-tap
 *                  bi-linear filter vertically on the result.
 *
 *  SPECIAL NOTES : The largest block size can be handled here is 16x16
 *
 ****************************************************************************/
void vp8_filter_block2d_bil_cl
(
        unsigned char *src_ptr,
        unsigned char *output_ptr,
        unsigned int src_pixels_per_line,
        unsigned int dst_pitch,
        const int *HFilter,
        const int *VFilter,
        int Width,
        int Height
        ) {

    unsigned short FData[17 * 16]; /* Temp data buffer used in filtering */

    /* First filter 1-D horizontally... */
    vp8_filter_block2d_bil_first_pass_cl(src_ptr, FData, src_pixels_per_line, 1, Height + 1, Width, HFilter);

    /* then 1-D vertically... */
    vp8_filter_block2d_bil_second_pass_cl(FData, output_ptr, dst_pitch, Width, Width, Height, Width, VFilter);
}

void vp8_bilinear_predict4x4_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        unsigned char *dst_ptr,
        int dst_pitch
        ) {
    const int *HFilter;
    const int *VFilter;

    HFilter = bilinear_filters[xoffset];
    VFilter = bilinear_filters[yoffset];

    vp8_filter_block2d_bil_cl(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter, 4, 4);

}

void vp8_bilinear_predict8x8_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        unsigned char *dst_ptr,
        int dst_pitch
        ) {
    const int *HFilter;
    const int *VFilter;

    HFilter = bilinear_filters[xoffset];
    VFilter = bilinear_filters[yoffset];

    vp8_filter_block2d_bil_cl(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter, 8, 8);

}

void vp8_bilinear_predict8x4_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        unsigned char *dst_ptr,
        int dst_pitch
        ) {
    const int *HFilter;
    const int *VFilter;

    HFilter = bilinear_filters[xoffset];
    VFilter = bilinear_filters[yoffset];

    vp8_filter_block2d_bil_cl(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter, 8, 4);

}

void vp8_bilinear_predict16x16_cl
(
        unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        unsigned char *dst_ptr,
        int dst_pitch
        ) {
    const int *HFilter;
    const int *VFilter;

    HFilter = bilinear_filters[xoffset];
    VFilter = bilinear_filters[yoffset];

    vp8_filter_block2d_bil_cl(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter, 16, 16);
}