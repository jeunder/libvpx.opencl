/*
 *  Copyright (c) 2011 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vp8_opencl.h"

int cl_initialized = CL_NOT_INITIALIZED;
VP8_COMMON_CL cl_data;

//Initialization functions for various CL programs.
extern int cl_init_filter();
extern int cl_init_idct();
extern int cl_init_loop_filter();

//Common CL destructors
extern void cl_destroy_loop_filter();
extern void cl_destroy_filter();
extern void cl_destroy_idct();

//Destructors for encoder/decoder-specific bits
extern void cl_decode_destroy();
extern void cl_encode_destroy();

/**
 * 
 * @param cq
 * @param new_status
 */
void cl_destroy(cl_command_queue cq, int new_status) {

    //Wait on any pending operations to complete... frees up all of our pointers
    if (cq != NULL)
        clFinish(cq);

    //Release the objects that we've allocated on the GPU
    cl_destroy_filter();
    cl_destroy_idct();
    cl_destroy_loop_filter();

#if CONFIG_VP8_DECODER
    if (cl_data.cl_decode_initialized == CL_SUCCESS)
        cl_decode_destroy();
#endif

#if CONFIG_VP8_ENCODER
    //placeholder for if/when encoder CL gets implemented
#endif

    if (cq){
        clReleaseCommandQueue(cq);
    }

    if (cl_data.context){
        clReleaseContext(cl_data.context);
        cl_data.context = NULL;
    }

    cl_initialized = new_status;

    return;
}

/**
 * 
 * @param dev
 * @return
 */
cl_device_type device_type(cl_device_id dev){
    cl_device_type type;
    int err;

    err = clGetDeviceInfo(dev, CL_DEVICE_TYPE, sizeof(type),&type,NULL);
    if (err != CL_SUCCESS)
        return CL_INVALID_DEVICE;
    return type;
}

/**
 * 
 * @return
 */
int cl_common_init() {
    int err,i,dev;
    cl_platform_id platform_ids[MAX_NUM_PLATFORMS];
    cl_uint num_found, num_devices;
    cl_device_id devices[MAX_NUM_DEVICES];

    //Don't allow multiple CL contexts..
    if (cl_initialized != CL_NOT_INITIALIZED)
        return cl_initialized;

    // Connect to a compute device
    err = clGetPlatformIDs(MAX_NUM_PLATFORMS, platform_ids, &num_found);

    if (err != CL_SUCCESS) {
        fprintf(stderr, "Couldn't query platform IDs\n");
        return CL_TRIED_BUT_FAILED;
    }

    if (num_found == 0) {
        fprintf(stderr, "No platforms found\n");
        return CL_TRIED_BUT_FAILED;
    }

    //printf("Enumerating %d platform(s)\n", num_found);
    //Enumerate the platforms found
    for (i = 0; i < num_found; i++){
    	char buf[2048];
        char version[2048];
        char features[2048];
        size_t len;
        
    	err = clGetPlatformInfo( platform_ids[i], CL_PLATFORM_VENDOR, sizeof(buf), buf, &len);
    	if (err != CL_SUCCESS){
            fprintf(stderr, "Error retrieving platform vendor for platform %d",i);
            continue;
    	}
    	//printf("Platform %d: %s\n",i,buf);

        //First, check version. If higher than 1.0, don't bother checking extensions
        err = clGetPlatformInfo( platform_ids[i], CL_PLATFORM_VERSION, sizeof(version), version, NULL);
        if (err != CL_SUCCESS){
            fprintf(stderr, "Error retrieving version for platform %d (%s)\n",i,buf);
            continue;
        }
        //printf("Version: %s\n",version);
        if (strstr(version,"1.0")){
            //Check if byte-addressable stores are enabled.. we need this feature
            err = clGetPlatformInfo( platform_ids[i], CL_PLATFORM_EXTENSIONS, sizeof(features), features, NULL);
            if (err != CL_SUCCESS){
                fprintf(stderr, "Error retrieving extension list for platform %d (%s)\n",i,buf);
                continue;
            }
            //printf("Platform: %s\nExtensions: %s\n",buf,features);
        }
        
    	//Try to find a valid compute device
    	//Favor the GPU, but fall back to any other available device if necessary
#ifdef __APPLE__
    	printf("Apple system. Running CL as CPU-only for now...\n");
        err = clGetDeviceIDs(platform_ids[i], CL_DEVICE_TYPE_CPU, MAX_NUM_DEVICES, devices, &num_devices);
#else
        err = clGetDeviceIDs(platform_ids[i], CL_DEVICE_TYPE_ALL, MAX_NUM_DEVICES, devices, &num_devices);
#endif //__APPLE__
        //printf("found %d devices\n", num_devices);
        cl_data.device_id = NULL;
        for( dev = 0; dev < num_devices; dev++ ){
            char ext[2048];
            //Get info for this device.
            err = clGetDeviceInfo(devices[dev], CL_DEVICE_EXTENSIONS,
                    sizeof(ext),ext,NULL);
            CL_CHECK_SUCCESS(NULL,err != CL_SUCCESS,
                    "Error retrieving device extension list",continue, 0);
            //printf("Device %d supports: %s\n",dev,ext);
            
            //The kernels in VP8 require byte-addressable stores, which is an
            //extension. It's required in OpenCL 1.1, but not all devices
            //support it.
            if (strstr(ext,"cl_khr_byte_addressable_store")){
                //We found a valid device, so use it. But if we find a GPU
                //(maybe this is one), prefer that.
                cl_data.device_id = devices[dev];

                if ( device_type(devices[dev]) == CL_DEVICE_TYPE_GPU ){
                    //printf("Device %d is a GPU\n",dev);
                    break;
                }
            }
        }

        //If we've found a usable GPU, stop looking.
        if (cl_data.device_id != NULL && device_type(cl_data.device_id) == CL_DEVICE_TYPE_GPU )
            break;

    }

    if (cl_data.device_id == NULL){
    	printf("Error: Failed to find a valid OpenCL device. Using CPU paths\n");
    	return CL_TRIED_BUT_FAILED;
    }

    // Create the compute context
    cl_data.context = clCreateContext(0, 1, &cl_data.device_id, NULL, NULL, &err);
    if (!cl_data.context) {
        printf("Error: Failed to create a compute context!\n");
        return CL_TRIED_BUT_FAILED;
    }

    //Initialize programs to null value
    //Enables detection of if they've been initialized as well.
    cl_data.filter_program = NULL;
    cl_data.idct_program = NULL;
    cl_data.loop_filter_program = NULL;

    err = cl_init_filter();
    if (err != CL_SUCCESS)
        return err;

    err = cl_init_idct();
    if (err != CL_SUCCESS)
        return err;

    err = cl_init_loop_filter();
    if (err != CL_SUCCESS)
        return err;

    return CL_SUCCESS;
}

char *cl_read_file(const char* file_name) {
    long pos;
    char fullpath[1024];
    char *bytes;
    size_t amt_read;

    FILE *f;

    f = fopen(file_name, "rb");

    
    if (f == NULL) {
        printf("Couldn't find %s\n", file_name);

        //Disable until this no longer crashes on free(). Use static size for now
        //fullpath = malloc(strlen(vpx_codec_lib_dir() + strlen(file_name) + 2));
        //if (fullpath == NULL) {
        //    return NULL;
        //}

        strcpy(fullpath, vpx_codec_lib_dir());
        strcat(fullpath, "/");
        strcat(fullpath, file_name);

        printf("Looking in %s\n", fullpath);

        f = fopen(fullpath, "rb");
        if (f == NULL) {
            printf("Couldn't find CL source at %s or %s\n", file_name, fullpath);
            //free(fullpath);
            return NULL;
        }

        printf("Found cl source at %s\n", fullpath);
        //free(fullpath);
    }

    fseek(f, 0, SEEK_END);
    pos = ftell(f);
    fseek(f, 0, SEEK_SET);
    bytes = malloc(pos+1);

    if (bytes == NULL) {
        fclose(f);
        return NULL;
    }

    amt_read = fread(bytes, pos, 1, f);
    if (amt_read != 1) {
        free(bytes);
        fclose(f);
        return NULL;
    }

    bytes[pos] = '\0'; //null terminate the source string
    fclose(f);


    return bytes;
}

void show_build_log(cl_program *prog_ref){
    size_t len;
    char *buffer;
    int err = clGetProgramBuildInfo(*prog_ref, cl_data.device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);

    if (err != CL_SUCCESS){
        printf("Error: Could not get length of CL build log\n");
    }

    buffer = (char*) malloc(len);
    if (buffer == NULL) {
        printf("Error: Couldn't allocate compile output buffer memory\n");
    }

    err = clGetProgramBuildInfo(*prog_ref, cl_data.device_id, CL_PROGRAM_BUILD_LOG, len, buffer, NULL);
    if (err != CL_SUCCESS) {
        printf("Error: Could not get CL build log\n");

    } else {
        printf("Compile output: %s\n", buffer);
    }
    free(buffer);
}

int cl_load_program(cl_program *prog_ref, const char *file_name, const char *opts) {

    int err;
    char *buffer;
    size_t len;
    char *kernel_src = cl_read_file(file_name);
    
    *prog_ref = NULL;
    if (kernel_src != NULL) {
        *prog_ref = clCreateProgramWithSource(cl_data.context, 1, (const char**)&kernel_src, NULL, &err);
        free(kernel_src);
    } else {
        cl_destroy(NULL, CL_TRIED_BUT_FAILED);
        printf("Couldn't find OpenCL source files. \nUsing software path.\n");
        return CL_TRIED_BUT_FAILED;
    }

    if (*prog_ref == NULL) {
        printf("Error: Couldn't create program\n");
        return CL_TRIED_BUT_FAILED;
    }

    if (err != CL_SUCCESS) {
        printf("Error creating program: %d\n", err);
    }

    /* Build the program executable */
    err = clBuildProgram(*prog_ref, 0, NULL, opts, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("Error: Failed to build program executable for %s!\n", file_name);

        show_build_log(prog_ref);

        return CL_TRIED_BUT_FAILED;
    }
    //else show_build_log(prog_ref);

    return CL_SUCCESS;
}
