
#define LOG_TAG "GrallocWrapper"
#define LOG_NDEBUG 0

#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <cutils/log.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include "gralloc_priv.h"

struct g_gralloc_context_t {
    alloc_device_t  device;
};

struct gralloc_context_t {
    alloc_device_t  device;
    /* our private data here */
    int is_gpu;
    int need_workaround;
    g_gralloc_context_t *g_ctx;
};

struct g_fb_context_t {
    framebuffer_device_t  device;
};

struct fb_context_t {
    framebuffer_device_t  device;
    /* our private data here */
    g_fb_context_t *g_ctx;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void *g_libHandle = NULL;

static private_module_t *g_hw_module = NULL;

#define G_GMT_SZ sizeof(gralloc_module_t)

#define DUMP_PRIV_MODULE(x) \
    { \
        t_private_module_t* tm = (t_private_module_t*)(x); \
        \
        LOGI(   "%s: t_private_module_t dump\n" \
                "framebuffer_ptr= %p\n" \
                "fbFormat       = %d\n" \
                "flags          = %p\n" \
                "numBuffers     = %d\n" \
                "bufferMask     = %d\n", \
                __func__, \
                tm->framebuffer, \
                tm->fbFormat, \
                tm->flags, \
                tm->numBuffers, \
                tm->bufferMask \
        ); \    
    }

static void ensureLibOpened();

static int gralloc_alloc(alloc_device_t* dev,
        int w, int h, int format, int usage,
        buffer_handle_t* pHandle, int* pStride)
{
    gralloc_context_t* ctx = reinterpret_cast<gralloc_context_t*>(dev);
    
    LOGV("%s: dev = %p, is_gpu = %d, w = %d, h = %d, format = %d, usage = %d, pHandle = %p, *pHandle = %p, pStride = %p, *pStride = %d",
         __func__, dev, ctx->is_gpu, w, h, format, usage, pHandle, *pHandle, pStride, *pStride);
    
    if (usage & 0x800) {
        usage = (usage & ~0x800);
        LOGV("%s: new usage = %d", __func__, usage);
    }
    
    //*pHandle = 0;
    //*pStride = 0;
    
    /*if (*pHandle) {
        private_handle_t* hnd = (private_handle_t*)*pHandle;
        if (hnd)
            LOGV("%s: in: fd = %d, magic = %d, flags = %d, size = %d, offset = %d, base = %d",
                __func__, hnd->fd, hnd->magic, hnd->flags, hnd->size, hnd->offset, hnd->base);
    }*/
    
    //pHandle = (buffer_handle_t *)(1);
    int ret = ctx->g_ctx->device.alloc(dev, w, h, format, usage, pHandle, pStride);
    if (ret == 0) {
        private_handle_t* hnd = (private_handle_t*)*pHandle;
        LOGV("%s: result: fd = %d, magic = %d, flags = %d, size = %d, offset = %d, base = %d",
             __func__, hnd->fd, hnd->magic, hnd->flags, hnd->size, hnd->offset, hnd->base);
    }
    
    LOGV("%s: OUT: *pHandle = %p, *pStride = %d",
         __func__, *pHandle, *pStride);
    
    DUMP_PRIV_MODULE(dev->common.module)
    
    return ret;
    
    /*if (ctx->need_workaround) {
        return ctx->g_ctx->device.alloc(dev, w, h, format, usage & 0xf7ff, pHandle, pStride);
    }
    
    int ret = ctx->g_ctx->device.alloc(dev, w, h, format, usage, pHandle, pStride);
    if (ret == -22) {
        ctx->need_workaround = true;
        return ctx->g_ctx->device.alloc(dev, w, h, format, usage & 0xf7ff, pHandle, pStride);
    }
    
    return ret;*/
}

static int gralloc_free(alloc_device_t* dev,
        buffer_handle_t handle)
{
    LOGV("%s: ", __func__);
    gralloc_context_t* ctx = reinterpret_cast<gralloc_context_t*>(dev);
    return ctx->g_ctx->device.free(dev, handle);
}

static int gralloc_close(struct hw_device_t *dev)
{
    LOGV("%s: ", __func__);
    gralloc_context_t* ctx = reinterpret_cast<gralloc_context_t*>(dev);
    if (ctx) {
        if (ctx->g_ctx) {
            ctx->g_ctx->device.common.close(&ctx->g_ctx->device.common);
        }
        free(ctx);
    }
    return 0;
}

static int fb_setSwapInterval(struct framebuffer_device_t* dev,
            int interval)
{
    LOGV("%s: interval = %d", __func__, interval);
    fb_context_t* ctx = reinterpret_cast<fb_context_t*>(dev);
    
    int ret = ctx->g_ctx->device.setSwapInterval(&ctx->g_ctx->device, interval);
    LOGV("%s: ret = %d", __func__, ret);
    return ret;
}

static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    LOGV("%s: buffer = %p", __func__, buffer);
    fb_context_t* ctx = reinterpret_cast<fb_context_t*>(dev);
    
    int ret = ctx->g_ctx->device.post(&ctx->g_ctx->device, buffer);
    LOGV("%s: ret = %d", __func__, ret);
    return ret;
}

static int fb_compositionComplete(struct framebuffer_device_t* dev)
{
    LOGV("%s: ", __func__);
    fb_context_t* ctx = reinterpret_cast<fb_context_t*>(dev);
    
    int ret = ctx->g_ctx->device.compositionComplete(&ctx->g_ctx->device);
    LOGV("%s: ret = %d", __func__, ret);
    return ret;
}

static int fb_close(struct hw_device_t *dev)
{
    LOGV("%s: ", __func__);
    fb_context_t* ctx = reinterpret_cast<fb_context_t*>(dev);
    if (ctx) {
        if (ctx->g_ctx) {
            ctx->g_ctx->device.common.close(&ctx->g_ctx->device.common);
        }
        free(ctx);
    }
    return 0;
}

static int gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    LOGV("%s: %s", __func__, name);
    int status = -EINVAL;
    
    ensureLibOpened();
    
    status = g_hw_module->base.common.methods->open(module, name, device);
    if (status == 0 && !strcmp(name, GRALLOC_HARDWARE_GPU0)) {
        gralloc_context_t *dev;
        dev = (gralloc_context_t*)malloc(sizeof(*dev));
        
        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));
        
        dev->g_ctx = reinterpret_cast<g_gralloc_context_t*>(*device);
	dev->is_gpu = 1;
        
        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = gralloc_close;

        dev->device.alloc   = gralloc_alloc;
        dev->device.free    = gralloc_free;

        *device = &dev->device.common;
    } else {
        fb_context_t *dev;
        dev = (fb_context_t*)malloc(sizeof(*dev));
        
        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));
        
        dev->g_ctx = reinterpret_cast<g_fb_context_t*>(*device);
        
        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = fb_close;
        
        LOGI(   "%s: fb_open\n"
                "flags                  = %d\n"
                "width                  = %d\n"
                "height                 = %d\n"
                "stride                 = %d\n"
                "format                 = %d\n"
                "xdpi                   = %f\n"
                "ydpi                   = %f\n"
                "fps                    = %f\n"
                "minSwapInterval        = %d\n"
                "maxSwapInterval        = %d\n\n"
                
                "setSwapInterval        = %p\n"
                "setUpdateRect          = %p\n"
                "post                   = %p\n"
                "compositionComplete    = %p\n",
                __func__,
                const_cast<uint32_t&>(dev->device.flags)        = dev->g_ctx->device.flags,
                const_cast<uint32_t&>(dev->device.width)        = dev->g_ctx->device.width,
                const_cast<uint32_t&>(dev->device.height)       = dev->g_ctx->device.height,
                const_cast<int&>(dev->device.stride)            = dev->g_ctx->device.stride,
                const_cast<int&>(dev->device.format)            = dev->g_ctx->device.format,
                const_cast<float&>(dev->device.xdpi)            = dev->g_ctx->device.xdpi,
                const_cast<float&>(dev->device.ydpi)            = dev->g_ctx->device.ydpi,
                const_cast<float&>(dev->device.fps)             = dev->g_ctx->device.fps,
                const_cast<int&>(dev->device.minSwapInterval)   = dev->g_ctx->device.minSwapInterval,
                const_cast<int&>(dev->device.maxSwapInterval)   = dev->g_ctx->device.maxSwapInterval,
                
                dev->g_ctx->device.setSwapInterval,
                dev->g_ctx->device.setUpdateRect,
                dev->g_ctx->device.post,
                dev->g_ctx->device.compositionComplete
        );
        
        dev->device.setSwapInterval = fb_setSwapInterval;
        dev->device.post = fb_post;
        dev->device.compositionComplete = fb_compositionComplete;
        
        memcpy(dev->device.reserved, dev->g_ctx->device.reserved, sizeof(dev->device.reserved));
        memcpy(dev->device.reserved_proc, dev->g_ctx->device.reserved_proc, sizeof(dev->device.reserved_proc));
        
        *device = &dev->device.common;
    }
    
    DUMP_PRIV_MODULE(module)

    return status;
}

static int gralloc_register_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    LOGV("%s: ", __func__);
    ensureLibOpened();
    return g_hw_module->base.registerBuffer(module, handle);
}

static int gralloc_unregister_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    LOGV("%s: ", __func__);
    ensureLibOpened();
    return g_hw_module->base.unregisterBuffer(module, handle);
}

static int gralloc_lock(gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr)
{
    LOGV("%s: ", __func__);
    ensureLibOpened();
    return g_hw_module->base.lock(module, handle, usage, l, t, w, h, vaddr);
}

static int gralloc_unlock(gralloc_module_t const* module, 
        buffer_handle_t handle)
{
    LOGV("%s: ", __func__);
    ensureLibOpened();
    return g_hw_module->base.unlock(module, handle);
}

static int gralloc_perform(struct gralloc_module_t const* module,
            int operation, ... )
{
    LOGV("%s: ", __func__);
    ensureLibOpened();
    return 0;
    //return g_gralloc_perform(module, operation);
}

static struct hw_module_methods_t gralloc_module_methods = {
        open: gralloc_device_open
};

struct private_module_t HAL_MODULE_INFO_SYM = {
    base: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: GRALLOC_HARDWARE_MODULE_ID,
            name: "Graphics Memory Allocator Module",
            author: "The Android Open Source Project",
            methods: &gralloc_module_methods
        },
        registerBuffer: gralloc_register_buffer,
        unregisterBuffer: gralloc_unregister_buffer,
        lock: gralloc_lock,
        unlock: gralloc_unlock,
        perform: gralloc_perform,
    },
};

static void ensureLibOpened()
{
    pthread_mutex_lock(&lock);
    if (g_libHandle == NULL) {
        g_libHandle = ::dlopen("libgralloc.so", RTLD_NOW | RTLD_GLOBAL);
        if (g_libHandle == NULL) {
            assert(0);
            LOGE("dlopen() error: %s\n", dlerror());
        } else {
            LOGV("GrallocWrapper is loaded...", __func__);
            LOGV("%s: initialize...", __func__);
            g_hw_module = (private_module_t *) ::dlsym(g_libHandle, HAL_MODULE_INFO_SYM_AS_STR);
            LOGV("%s: g_hw_module = %p", __func__, g_hw_module);
            memcpy(HAL_MODULE_INFO_SYM.reserved, g_hw_module->reserved, DL_HMI_LEFT);
            HAL_MODULE_INFO_SYM.base.perform = g_hw_module->base.perform;
        }
    }
    pthread_mutex_unlock(&lock);
}
