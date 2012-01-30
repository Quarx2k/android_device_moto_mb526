
#ifndef GRALLOC_PRIV_H_
#define GRALLOC_PRIV_H_

#include <stdint.h>
#include <limits.h>
#include <sys/cdefs.h>
#include <hardware/gralloc.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <cutils/native_handle.h>

#define DL_HMI_SIZE 404
//#define DL_HMI_SIZE 448
#define DL_HMI_USED 76
#define DL_HMI_LEFT DL_HMI_SIZE-DL_HMI_USED

struct private_handle_t;

struct t_private_module_t {
    gralloc_module_t base;
    
    struct private_handle_t* framebuffer;
    uint32_t fbFormat;
    uint32_t flags;
    uint32_t numBuffers;
    uint32_t bufferMask;
};

struct private_module_t {
    gralloc_module_t base;

    char reserved[DL_HMI_LEFT];
};

#ifdef __cplusplus
struct private_handle_t : public native_handle {
#else
struct private_handle_t {
    struct native_handle nativeHandle;
#endif

    // file-descriptors
    int     fd;
    // ints
    int     magic;
    int     flags;
    int     size;
    int     offset;

    // FIXME: the attributes below should be out-of-line
    int     base;
    int     pid;
};

#endif