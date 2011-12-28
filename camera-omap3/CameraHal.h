/*
 * Copyright (C) Texas Instruments - http://www.ti.com/
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * This file contains material that is available under the Apache License
 */


#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utils/Log.h>
#include <utils/threads.h>
#include <linux/videodev2.h>
#include "binder/MemoryBase.h"
#include "binder/MemoryHeapBase.h"
#include <utils/threads.h>
#include <ui/Overlay.h>
#include <camera/CameraHardwareInterface.h>
#include "MessageQueue.h"
#include "overlay_common.h"
#include "CameraHalParams.h"

#ifdef HARDWARE_OMX
#include <JpegEncoderEXIF.h>
#endif

#define RESIZER 1
#define JPEG 1
#define VPP 1

#define PPM_INSTRUMENTATION 1
#define ICAP_EXPERIMENTAL 1

#define DEBUG_LOG 1

//#undef FW3A
//#undef ICAP
//#undef IMAGE_PROCESSING_PIPELINE

#ifdef FW3A
extern "C" {
#include "icam_icap/icamera.h"
#include "icam_icap/icapture_v2.h"
}
#endif

#ifdef HARDWARE_OMX
#include "JpegEncoder.h"
#endif

#ifdef IMAGE_PROCESSING_PIPELINE
#include "imageprocessingpipeline.h"
#include "ipp_algotypes.h"
#include "capdefs.h"

#define MAXIPPDynamicParams 10

#endif
#define MAX_BURST 15
#define DSP_CACHE_ALIGNMENT 128
#define BUFF_MAP_PADDING_TEST 256
#define DSP_CACHE_ALIGN_MEM_ALLOC(__size__) \
    memalign(DSP_CACHE_ALIGNMENT, __size__ + BUFF_MAP_PADDING_TEST)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define VIDEO_DEVICE        "/dev/video5"
#define MIN_WIDTH           128
#define MIN_HEIGHT          96
#define PICTURE_WIDTH   3264 /* 5mp - 2560. 8mp - 3280 */ /* Make sure it is a multiple of 16. */
#define PICTURE_HEIGHT  2448 /* 5mp - 2048. 8mp - 2464 */ /* Make sure it is a multiple of 16. */
#define PREVIEW_WIDTH 176
#define PREVIEW_HEIGHT 144
#define CAPTURE_8MP_WIDTH        3280
#define CAPTURE_8MP_HEIGHT       2464
#define PIXEL_FORMAT           V4L2_PIX_FMT_UYVY
#define LOG_FUNCTION_NAME    LOGD("%d: %s() ENTER", __LINE__, __FUNCTION__);
#define LOG_FUNCTION_NAME_EXIT    LOGD("%d: %s() EXIT", __LINE__, __FUNCTION__);
#define VIDEO_FRAME_COUNT_MAX    NUM_OVERLAY_BUFFERS_REQUESTED
#define MAX_CAMERA_BUFFERS    NUM_OVERLAY_BUFFERS_REQUESTED
#define COMPENSATION_OFFSET 20
#define CONTRAST_OFFSET 100
#define BRIGHTNESS_OFFSET 100
#define STRINGIZE_(x) #x
#define STRINGIZE(x) STRINGIZE_(x)
#define DEFAULT_THUMB_WIDTH     320
#define DEFAULT_THUMB_HEIGHT    240
#define MAX_THUMB_WIDTH     512
#define MAX_THUMB_HEIGHT    384
#define MIN_THUMB_WIDTH    80
#define MIN_THUMB_HEIGHT   60
#define IMX046_FOCALLENGTH 4.68
#define IMX046_HORZANGLE 62.9
#define IMX046_VERTANGLE 24.8

#define ZOOM_SCALE (1<<16)

#define PIX_YUV422I 0
#define PIX_YUV420P 1
#define KEY_ROTATION_TYPE       "rotation-type"
#define ROTATION_PHYSICAL       0
#define ROTATION_EXIF           1

#define DSP3630_HZ_MIN 260000000
#define DSP3630_HZ_MAX 800000000

#define __ALIGN(x,a) ( (x) & (~((a) - 1)))

#define NONNEG_ASSIGN(x,y) \
    if(x > -1) \
        y = x

namespace android {

#ifdef IMAGE_PROCESSING_PIPELINE
	#define INPLACE_ON	1
	#define INPLACE_OFF	0
	#define IPP_Disabled_Mode 0
	#define IPP_CromaSupression_Mode 1
	#define IPP_EdgeEnhancement_Mode 2
typedef struct OMX_IPP
{
    IPP_Handle hIPP;
    IPP_ConfigurationTypes ippconfig;
    IPP_CRCBSAlgoCreateParams CRCBptr;
    IPP_EENFAlgoCreateParams EENFcreate;
    IPP_YUVCAlgoCreateParams YUVCcreate;
    void* dynStar;
    void* dynCRCB;
    void* dynEENF;
    IPP_StarAlgoInArgs* iStarInArgs;
    IPP_StarAlgoOutArgs* iStarOutArgs;
    IPP_CRCBSAlgoInArgs* iCrcbsInArgs;
    IPP_CRCBSAlgoOutArgs* iCrcbsOutArgs;
    IPP_EENFAlgoInArgs* iEenfInArgs;
    IPP_EENFAlgoOutArgs* iEenfOutArgs;
    IPP_YUVCAlgoInArgs*  iYuvcInArgs1;
    IPP_YUVCAlgoOutArgs* iYuvcOutArgs1;
    IPP_YUVCAlgoInArgs* iYuvcInArgs2;
    IPP_YUVCAlgoOutArgs* iYuvcOutArgs2;
    IPP_StarAlgoStatus starStatus;
    IPP_CRCBSAlgoStatus CRCBSStatus;
    IPP_EENFAlgoStatus EENFStatus;
    IPP_StatusDesc statusDesc;
    IPP_ImageBufferDesc iInputBufferDesc;
    IPP_ImageBufferDesc iOutputBufferDesc;
    IPP_ProcessArgs iInputArgs;
    IPP_ProcessArgs iOutputArgs;
	int outputBufferSize;
	unsigned char* pIppOutputBuffer;
} OMX_IPP;

typedef struct {
    // Edge Enhancement Parameters
    uint16_t  EdgeEnhancementStrength;
    uint16_t  WeakEdgeThreshold;
    uint16_t  StrongEdgeThreshold;

    // Noise filter parameters
    // LumaNoiseFilterStrength
    uint16_t  LowFreqLumaNoiseFilterStrength;
    uint16_t  MidFreqLumaNoiseFilterStrength;
    uint16_t  HighFreqLumaNoiseFilterStrength;

    // CbNoiseFilterStrength
    uint16_t  LowFreqCbNoiseFilterStrength;
    uint16_t  MidFreqCbNoiseFilterStrength;
    uint16_t  HighFreqCbNoiseFilterStrength;

    // CrNoiseFilterStrength
    uint16_t  LowFreqCrNoiseFilterStrength;
    uint16_t  MidFreqCrNoiseFilterStrength;
    uint16_t  HighFreqCrNoiseFilterStrength;

    uint16_t  shadingVertParam1;
    uint16_t  shadingVertParam2;
    uint16_t  shadingHorzParam1;
    uint16_t  shadingHorzParam2;
    uint16_t  shadingGainScale;
    uint16_t  shadingGainOffset;
    uint16_t  shadingGainMaxValue;

    uint16_t  ratioDownsampleCbCr;
} IPP_PARAMS;

#endif

//icapture
#define DTP_FILE_NAME 	    "/data/dyntunn.enc"
#define EEPROM_FILE_NAME    "/data/eeprom.hex"
#define LIBICAPTURE_NAME    "libicapture.so"

// 3A FW
#define LIB3AFW             "libMMS3AFW.so"

#define PHOTO_PATH          "/tmp/photo_%02d.%s"

#define PROC_THREAD_PROCESS         0x5
#define PROC_THREAD_EXIT            0x6
#define PROC_THREAD_NUM_ARGS        45
#define SHUTTER_THREAD_CALL         0x1
#define SHUTTER_THREAD_EXIT         0x2
#define SHUTTER_THREAD_NUM_ARGS     3
#define RAW_THREAD_CALL             0x1
#define RAW_THREAD_EXIT             0x2
#define RAW_THREAD_NUM_ARGS         4
#define SNAPSHOT_THREAD_START       0x1
#define SNAPSHOT_THREAD_EXIT        0x2
#define SNAPSHOT_THREAD_START_GEN   0x3

#define PAGE                    0x1000
#define PARAM_BUFFER            512

#ifdef FW3A

typedef struct {
    TICam_Handle hnd;

    /* hold 2A settings */
    SICam_Settings settings;

    /* hold 2A status */
    SICam_Status   status;

    /* hold MakerNote */
    SICam_MakerNote mnote;
} lib3atest_obj;
#endif

#ifdef ICAP
typedef struct{
    void           *lib_private;
    icap_configure_t    cfg;
    icap_tuning_params_t tuning_pars;
    SICap_ManualParameters manual;
    icap_buffer_t   img_buf;
    icap_buffer_t   lsc_buf;
} libtest_obj;
#endif

typedef struct {
    size_t width;
    size_t height;
} supported_resolution;

class CameraHal : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getRawHeap() const;
    virtual void stopRecording();

	virtual status_t startRecording();  // Eclair HAL
    virtual bool recordingEnabled();
    virtual void releaseRecordingFrame(const sp<IMemory>& mem);
    virtual sp<IMemoryHeap> getPreviewHeap() const ;

	virtual status_t startPreview();   //  Eclair HAL
    virtual bool useOverlay() { return true; }
    virtual status_t setOverlay(const sp<Overlay> &overlay);
    virtual void stopPreview();
    virtual bool previewEnabled();

	virtual status_t autoFocus();  // Eclair HAL


	virtual status_t takePicture();		// Eclair HAL

	virtual status_t cancelPicture();	// Eclair HAL
	virtual status_t cancelAutoFocus();

    virtual status_t dump(int fd, const Vector<String16>& args) const;
    void dumpFrame(void *buffer, int size, char *path);
    virtual status_t setParameters(const CameraParameters& params);
    virtual CameraParameters getParameters() const;
    virtual void release();
    void initDefaultParameters();

/*--------------------Eclair HAL---------------------------------------*/
     virtual void        setCallbacks(notify_callback notify_cb,
                                     data_callback data_cb,
                                     data_callback_timestamp data_cb_timestamp,
                                     void* user);

    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);
/*--------------------Eclair HAL---------------------------------------*/
    static sp<CameraHardwareInterface> createInstance();

    virtual status_t sendCommand(int32_t cmd, int32_t arg1, int32_t arg2);

    class PreviewThread : public Thread {
        CameraHal* mHardware;
    public:
        PreviewThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }

        virtual bool threadLoop() {
            mHardware->previewThread();

            return false;
        }
    };

    class SnapshotThread : public Thread {
        CameraHal* mHardware;
    public:
        SnapshotThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }

        virtual bool threadLoop() {
            mHardware->snapshotThread();
            return false;
        }
    };

    class PROCThread : public Thread {
        CameraHal* mHardware;
    public:
        PROCThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }

        virtual bool threadLoop() {
            mHardware->procThread();
            return false;
        }
    };

    class ShutterThread : public Thread {
        CameraHal* mHardware;
    public:
        ShutterThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }

        virtual bool threadLoop() {
            mHardware->shutterThread();
            return false;
        }
    };

    class RawThread : public Thread {
        CameraHal* mHardware;
    public:
        RawThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }

        virtual bool threadLoop() {
            mHardware->rawThread();
            return false;
        }
    };

   CameraHal();
    virtual ~CameraHal();
    void previewThread();
    bool validateSize(size_t width, size_t height, const supported_resolution *supRes, size_t count);
    void procThread();
    void shutterThread();
    void rawThread();
    void snapshotThread();
    void *getLastOverlayAddress();
	size_t getLastOverlayLength();

#ifdef IMAGE_PROCESSING_PIPELINE

    IPP_PARAMS mIPPParams;

#endif

#ifdef FW3A

    static void onIPP(void *priv, icap_ipp_parameters_t *ipp_params);
    static void onMakernote(void *priv, void *mknote_ptr);
    static void onShutter(void *priv, icap_image_buffer_t *image_buf);
    static void onSaveH3A(void *priv, icap_image_buffer_t *buf);
    static void onSaveLSC(void *priv, icap_image_buffer_t *buf);
    static void onSaveRAW(void *priv, icap_image_buffer_t *buf);
    static void onSnapshot(void *priv, icap_image_buffer_t *buf);
    static void onGeneratedSnapshot(void *priv, icap_image_buffer_t *buf);
    static void onCrop(void *priv,  icap_crop_rect_t *crop_rect);

    int FW3A_Create();
    int FW3A_Init();
    int FW3A_Release();
    int FW3A_Destroy();
    int FW3A_Start();
    int FW3A_Stop();
    int FW3A_Start_CAF();
    int FW3A_Stop_CAF();
    int FW3A_Start_AF();
    int FW3A_Stop_AF();
    int FW3A_GetSettings() const;
    int FW3A_SetSettings();

#endif

    int CorrectPreview();
    int ZoomPerform(float zoom);
    void nextPreview();
    void queueToOverlay(int index);
    int dequeueFromOverlay();
    int ICapturePerform();
    int ICaptureCreate(void);
    int ICaptureDestroy(void);
    void SetDSPHz(unsigned int Hz);
	void PPM(const char *);
	void PPM(const char *, struct timeval*, ...);
    status_t convertGPSCoord(double coord, int *deg, int *min, int *sec);

#ifdef IMAGE_PROCESSING_PIPELINE

    int DeInitIPP(int ippMode);
    int InitIPP(int w, int h, int fmt, int ippMode);
    int PopulateArgsIPP(int w, int h, int fmt, int ippMode);
    int ProcessBufferIPP(void *pBuffer, long int nAllocLen, int fmt, int ippMode,
                       int EdgeEnhancementStrength, int WeakEdgeThreshold, int StrongEdgeThreshold,
                        int LowFreqLumaNoiseFilterStrength, int MidFreqLumaNoiseFilterStrength, int HighFreqLumaNoiseFilterStrength,
                        int LowFreqCbNoiseFilterStrength, int MidFreqCbNoiseFilterStrength, int HighFreqCbNoiseFilterStrength,
                        int LowFreqCrNoiseFilterStrength, int MidFreqCrNoiseFilterStrength, int HighFreqCrNoiseFilterStrength,
                        int shadingVertParam1, int shadingVertParam2, int shadingHorzParam1, int shadingHorzParam2,
                        int shadingGainScale, int shadingGainOffset, int shadingGainMaxValue,
                        int ratioDownsampleCbCr);
    OMX_IPP pIPP;

#endif   

    int CameraCreate();
    int CameraDestroy(bool destroyOverlay);
    int CameraConfigure();
	int CameraSetFrameRate();
    int CameraStart();
    int CameraStop();

#ifdef ICAP_EXPERIMENTAL

    int allocatePictureBuffer(size_t length, int burstCount);

#else

    int allocatePictureBuffer(int width, int height, int burstCount, int len);

#endif

    int SaveFile(char *filename, char *ext, void *buffer, int jpeg_size);
    
    int isStart_FW3A;
    int isStart_FW3A_AF;
    int isStart_FW3A_CAF;
    int isStart_FW3A_AEWB;
    int isStart_VPP;
    int isStart_JPEG;
    int FW3A_AF_TimeOut;

    mutable Mutex mLock;
    int mBurstShots;
    struct v4l2_crop mInitialCrop;

#ifdef HARDWARE_OMX

    gps_data *gpsLocation;
    exif_params mExifParams;

#endif

    bool mShutterEnable;
    bool mCAFafterPreview;
    CameraParameters mParameters;
    sp<MemoryHeapBase> mPictureHeap, mJPEGPictureHeap;
    int mPictureOffset[MAX_BURST];
    int mJPEGOffset, mJPEGLength;
    int mPictureLength[MAX_BURST];
    void *mYuvBuffer[MAX_BURST];
    void *mJPEGBuffer;
    int  mPreviewFrameSize;
    sp<Overlay>  mOverlay;
    sp<PreviewThread>  mPreviewThread;
	sp<PROCThread>  mPROCThread;
	sp<ShutterThread> mShutterThread;
	sp<RawThread> mRawThread;
	sp<SnapshotThread> mSnapshotThread;
    bool mPreviewRunning;
    bool mIPPInitAlgoState;
    bool mIPPToEnable;
    Mutex mRecordingLock;
    int mRecordingFrameSize;
    // Video Frame Begin
    int mVideoBufferCount;
    sp<MemoryHeapBase> mVideoHeaps[VIDEO_FRAME_COUNT_MAX];
    sp<MemoryBase> mVideoBuffer[VIDEO_FRAME_COUNT_MAX];

    // ...
    int nOverlayBuffersQueued;
    int nCameraBuffersQueued;
    struct v4l2_buffer v4l2_cam_buffer[MAX_CAMERA_BUFFERS];
    int buffers_queued_to_dss[MAX_CAMERA_BUFFERS];
    int buffers_queued_to_ve[MAX_CAMERA_BUFFERS];
    sp<MemoryHeapBase> mPreviewHeaps[MAX_CAMERA_BUFFERS];
    sp<MemoryBase> mPreviewBuffers[MAX_CAMERA_BUFFERS];
	int mfirstTime;
    static wp<CameraHardwareInterface> singleton;
    static int camera_device;
    static const supported_resolution supportedPreviewRes[];
    static const supported_resolution supportedPictureRes[];
    static const char supportedPictureSizes[];
    static const char supportedPreviewSizes[];
    static const char supportedFPS[];
    static const char supportedThumbnailSizes[];
    static const char PARAMS_DELIMITER[];
	int procPipe[2], shutterPipe[2], rawPipe[2], snapshotPipe[2], snapshotReadyPipe[2];
	int mippMode;
	int pictureNumber;
	int rotation;
#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
    struct timeval ppm;
	struct timeval ppm_start;
	struct timeval ppm_receiveCmdToTakePicture;
	struct timeval ppm_restartPreview;
    struct timeval focus_before, focus_after;
    struct timeval ppm_before, ppm_after;
    struct timeval ipp_before, ipp_after;
#endif
	int lastOverlayBufferDQ;

/*--------------Eclair Camera HAL---------------------*/

	notify_callback mNotifyCb;
	data_callback   mDataCb;
	data_callback_timestamp mDataCbTimestamp;
	void                 *mCallbackCookie;

	int32_t             mMsgEnabled;
	bool                mRecordEnabled;
	nsecs_t             mCurrentTime[MAX_CAMERA_BUFFERS];
       nsecs_t             mPrevTime;
       nsecs_t             frameInterval;
	bool mFalsePreview;


/*--------------Eclair Camera HAL---------------------*/

#ifdef HARDWARE_OMX
    JpegEncoder*    jpegEncoder;
#endif    
    
#ifdef FW3A
  	lib3atest_obj *fobj;
#endif

#ifdef ICAP
    libtest_obj   *iobj;
#endif

    int file_index;
    int cmd;
    int quality;
    unsigned int sensor_width;
    unsigned int sensor_height;
    unsigned int zoom_width;
    unsigned int zoom_height;
    int32_t mImageCropTop, mImageCropLeft, mImageCropWidth, mImageCropHeight;
    int mflash;
    int mred_eye;
    int mcapture_mode;
    int mZoomCurrentIdx, mZoomTargetIdx, mZoomSpeed;
    int mcaf;
    int j;

    enum SmoothZoomStatus {
        SMOOTH_START = 0,
        SMOOTH_NOTIFY_AND_STOP,
        SMOOTH_STOP
    } mSmoothZoomStatus;

    enum PreviewThreadCommands {

        // Comands        
        PREVIEW_START,
        PREVIEW_STOP,
        PREVIEW_AF_START,
        PREVIEW_AF_STOP,
        PREVIEW_CAPTURE,
        PREVIEW_CAPTURE_CANCEL,
        PREVIEW_KILL,
        PREVIEW_CAF_START,
        PREVIEW_CAF_STOP,
		PREVIEW_FPS,
        START_SMOOTH_ZOOM,
        STOP_SMOOTH_ZOOM,
        // ACKs        
        PREVIEW_ACK,
        PREVIEW_NACK,

    };

    enum ProcessingThreadCommands {

        // Comands        
        PROCESSING_PROCESS,
        PROCESSING_CANCEL,
        PROCESSING_KILL,

        // ACKs        
        PROCESSING_ACK,
        PROCESSING_NACK,
    };    

    MessageQueue    previewThreadCommandQ;
    MessageQueue    previewThreadAckQ;    
    MessageQueue    processingThreadCommandQ;
    MessageQueue    processingThreadAckQ;

    mutable Mutex takephoto_lock;
    uint8_t *yuv_buffer, *jpeg_buffer, *vpp_buffer, *ancillary_buffer;
    
    int yuv_len, jpeg_len, ancillary_len;

    FILE *foutYUV;
    FILE *foutJPEG;
};

}; // namespace android

extern "C" {

    int aspect_ratio_calc(
        unsigned int sens_width,  unsigned int sens_height,
        unsigned int pix_width,   unsigned int pix_height,
        unsigned int src_width,   unsigned int src_height,
        unsigned int dst_width,   unsigned int dst_height,
        unsigned int align_crop_width, unsigned int align_crop_height,
        unsigned int align_pos_width,  unsigned int align_pos_height,
        int *crop_src_left,  int *crop_src_top,
        int *crop_src_width, int *crop_src_height,
        int *pos_dst_left,   int *pos_dst_top,
        int *pos_dst_width,  int *pos_dst_height,
        unsigned int flags);

    int scale_init(int inWidth, int inHeight, int outWidth, int outHeight, int inFmt, int outFmt);
    int scale_deinit();
    int scale_process(void* inBuffer, int inWidth, int inHeight, void* outBuffer, int outWidth, int outHeight, int rotation, int fmt, float zoom, int crop_top, int crop_left, int crop_width, int crop_height);
}

#endif
