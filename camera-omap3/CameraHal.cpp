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
 */
/**
* @file CameraHal.cpp
*
* This file maps the Camera Hardware Interface to V4L2.
*
*/

#define LOG_TAG "CameraHal"

#include "CameraHal.h"
#include <poll.h>
#include "zoom_step.inc"

#include <math.h>

#include <cutils/properties.h>
#define UNLIKELY( exp ) (__builtin_expect( (exp) != 0, false ))
static int mDebugFps = 0;

#define RES_720P    1280

namespace android {
/*****************************************************************************/

/*
 * This is the overlay_t object, it is returned to the user and represents
 * an overlay. here we use a subclass, where we can store our own state.
 * This handles will be passed across processes and possibly given to other
 * HAL modules (for instance video decode modules).
 */
struct overlay_true_handle_t : public native_handle {
    /* add the data fields we need here, for instance: */
    int ctl_fd;
    int shared_fd;
    int width;
    int height;
    int format;
    int num_buffers;
    int shared_size;
};

/* Defined in liboverlay */
typedef struct {
    int fd;
    size_t length;
    uint32_t offset;
    void *ptr;
} mapping_data_t;

int CameraHal::camera_device = 0;
wp<CameraHardwareInterface> CameraHal::singleton;
const char CameraHal::supportedPictureSizes [] = "3264x2448,2560x2048,2048x1536,1600x1200,1280x1024,1152x968,1280x960,800x600,640x480,320x240";
const char CameraHal::supportedPreviewSizes [] = "1280x720,992x560,864x480,800x480,720x576,720x480,768x576,640x480,320x240,352x288,240x160,176x144,128x96";
const char CameraHal::supportedFPS [] = "33,30,25,24,20,15,10";
const char CameraHal::supportedThumbnailSizes []= "512x384,320x240,80x60,0x0";
const char CameraHal::PARAMS_DELIMITER []= ",";

const supported_resolution CameraHal::supportedPictureRes[] = { {3264, 2448} , {2560, 2048} ,
                                                     {2048, 1536} , {1600, 1200} ,
                                                     {1280, 1024} , {1152, 968} ,
                                                     {1280, 960} , {800, 600},
                                                     {640, 480}   , {320, 240} };

const supported_resolution CameraHal::supportedPreviewRes[] = { {1280, 720}, {800, 480},
                                                     {720, 576}, {720, 480},
                                                     {992, 560}, {864, 480}, {848, 480},
                                                     {768, 576}, {640, 480},
                                                     {320, 240}, {352, 288}, {240, 160},
                                                     {176, 144}, {128, 96}};

int camerahal_strcat(char *dst, const char *src, size_t size)
{
    size_t actual_size;

    actual_size = strlcat(dst, src, size);
    if(actual_size > size)
    {
        LOGE("Unexpected truncation from camerahal_strcat dst=%s src=%s", dst, src);
        return actual_size;
    }

    return 0;
}

CameraHal::CameraHal()
			:mParameters(),
			mOverlay(NULL),
			mPreviewRunning(0),
			mRecordingFrameSize(0),
			mVideoBufferCount(0),
			nOverlayBuffersQueued(0),
			nCameraBuffersQueued(0),
			mfirstTime(0),
			pictureNumber(0),
#ifdef FW3A
			fobj(NULL),
#endif
			file_index(0),
			mflash(2),
			mcapture_mode(1),
			mcaf(0),
			j(0)
{
#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
    gettimeofday(&ppm_start, NULL);
#endif

    isStart_FW3A = false;
    isStart_FW3A_AF = false;
    isStart_FW3A_CAF = false;
    isStart_FW3A_AEWB = false;
    isStart_VPP = false;
    isStart_JPEG = false;
    mPictureHeap = NULL;
    mIPPInitAlgoState = false;
    mIPPToEnable = false;
    mRecordEnabled = 0;
    mNotifyCb = 0;
    mDataCb = 0;
    mDataCbTimestamp = 0;
    mCallbackCookie = 0;
    mMsgEnabled = 0 ;
    mFalsePreview = false;  //Eclair HAL
    mZoomSpeed = 1;
    mZoomTargetIdx = 0;
    mZoomCurrentIdx = 0;
    mSmoothZoomStatus = SMOOTH_STOP;
    rotation = 0;

#ifdef HARDWARE_OMX

    gpsLocation = NULL;

#endif

    mShutterEnable = true;
    sStart_FW3A_CAF:mCAFafterPreview = false;
    ancillary_len = 8092;

#ifdef IMAGE_PROCESSING_PIPELINE

    pIPP.hIPP = NULL;

#endif

    int i = 0;
    for(i = 0; i < VIDEO_FRAME_COUNT_MAX; i++)
    {
        mVideoBuffer[i] = 0;
        buffers_queued_to_dss[i] = 0;
    }

    CameraCreate();

    initDefaultParameters();

    /* Avoiding duplicate call of cameraconfigure(). It is now called in previewstart() */
    //CameraConfigure();

#ifdef FW3A
    FW3A_Create();
    FW3A_Init();
#endif

    ICaptureCreate();

    mPreviewThread = new PreviewThread(this);
    mPreviewThread->run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);

    if( pipe(procPipe) != 0 ){
        LOGE("Failed creating pipe");
    }

    if( pipe(shutterPipe) != 0 ){
        LOGE("Failed creating pipe");
    }

    if( pipe(rawPipe) != 0 ){
        LOGE("Failed creating pipe");
    }

    if( pipe(snapshotPipe) != 0 ){
        LOGE("Failed creating pipe");
    }

    if( pipe(snapshotReadyPipe) != 0 ){
        LOGE("Failed creating pipe");
    }

    mPROCThread = new PROCThread(this);
    mPROCThread->run("CameraPROCThread", PRIORITY_URGENT_DISPLAY);
    LOGD("STARTING PROC THREAD \n");

    mShutterThread = new ShutterThread(this);
    mShutterThread->run("CameraShutterThread", PRIORITY_URGENT_DISPLAY);
    LOGD("STARTING Shutter THREAD \n");

    mRawThread = new RawThread(this);
    mRawThread->run("CameraRawThread", PRIORITY_URGENT_DISPLAY);
    LOGD("STARTING Raw THREAD \n");

    mSnapshotThread = new SnapshotThread(this);
    mSnapshotThread->run("CameraSnapshotThread", PRIORITY_URGENT_DISPLAY);
    LOGD("STARTING Snapshot THREAD \n");

    char value[PROPERTY_VALUE_MAX];
    property_get("debug.image.showfps", value, "0");
    mDebugFps = atoi(value);
    LOGD_IF(mDebugFps, "showfps enabled");


}

bool CameraHal::validateSize(size_t width, size_t height, const supported_resolution *supRes, size_t count)
{
    bool ret = false;
    status_t stat = NO_ERROR;
    unsigned int size;

    LOG_FUNCTION_NAME

    if ( NULL == supRes ) {
        LOGE("Invalid resolutions array passed");
        stat = -EINVAL;
    }

    if ( NO_ERROR == stat ) {
        for ( unsigned int i = 0 ; i < count; i++ ) {
            LOGD( "Validating %d, %d and %d, %d", supRes[i].width, width, supRes[i].height, height);
            if ( ( supRes[i].width == width ) && ( supRes[i].height == height ) ) {
                ret = true;
                break;
            }
        }
    }

    LOG_FUNCTION_NAME_EXIT

    return ret;
}

void CameraHal::initDefaultParameters()
{
    CameraParameters p;
    char tmpBuffer[PARAM_BUFFER], zoomStageBuffer[PARAM_BUFFER];
    unsigned int zoomStage;

    LOG_FUNCTION_NAME

    p.setPreviewSize(MIN_WIDTH, MIN_HEIGHT);
    p.setPreviewFrameRate(30);
    p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV422I);

    p.setPictureSize(PICTURE_WIDTH, PICTURE_HEIGHT);
    p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_JPEG_QUALITY, 100);

    //extended parameters

    memset(tmpBuffer, '\0', PARAM_BUFFER);
    for ( int i = 0 ; i < ZOOM_STAGES ; i++ ) {
        zoomStage =  (unsigned int ) ( zoom_step[i]*PARM_ZOOM_SCALE );
        snprintf(zoomStageBuffer, PARAM_BUFFER, "%d", zoomStage);

        if(camerahal_strcat((char*) tmpBuffer, (const char*) zoomStageBuffer, PARAM_BUFFER)) return;
        if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    }
    p.set(CameraParameters::KEY_ZOOM_RATIOS, tmpBuffer);
    p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
    p.set(CameraParameters::KEY_SMOOTH_ZOOM_SUPPORTED, "true");
    // zoom goes from 0..MAX_ZOOM so send array size minus one
    p.set(CameraParameters::KEY_MAX_ZOOM, ZOOM_STAGES-1);
    p.set(CameraParameters::KEY_ZOOM, 0);

    p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, COMPENSATION_MAX);
    p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, COMPENSATION_MIN);
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, COMPENSATION_STEP);
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, 0);

    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, CameraHal::supportedPictureSizes);
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, CameraHal::supportedPreviewSizes);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, CameraParameters::PIXEL_FORMAT_YUV422I);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, CameraHal::supportedFPS);
    p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, CameraHal::supportedThumbnailSizes);
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, STRINGIZE(DEFAULT_THUMB_WIDTH));
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, STRINGIZE(DEFAULT_THUMB_HEIGHT));

    p.set(CameraParameters::KEY_FOCAL_LENGTH, STRINGIZE(IMX046_FOCALLENGTH));
    p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, STRINGIZE(IMX046_HORZANGLE));
    p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, STRINGIZE(IMX046_VERTANGLE));

    memset(tmpBuffer, '\0', PARAM_BUFFER);
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_AUTO, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_INCANDESCENT, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_FLUORESCENT, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_DAYLIGHT, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_SHADE, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) WHITE_BALANCE_HORIZON, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) WHITE_BALANCE_TUNGSTEN, PARAM_BUFFER)) return;
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, tmpBuffer);
    p.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_NONE, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_MONO, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_NEGATIVE, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_SOLARIZE,  PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_SEPIA, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_WHITEBOARD,  PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_BLACKBOARD, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) EFFECT_COOL, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) EFFECT_EMBOSS, PARAM_BUFFER)) return;
    p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, tmpBuffer);
    p.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_AUTO, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_PORTRAIT, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_LANDSCAPE, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_ACTION, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_NIGHT_PORTRAIT, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_FIREWORKS, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_NIGHT, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_SNOW, PARAM_BUFFER)) return;
    p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, tmpBuffer);
    p.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_AUTO);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::FOCUS_MODE_AUTO, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::FOCUS_MODE_INFINITY, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::FOCUS_MODE_MACRO, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::FOCUS_MODE_FIXED, PARAM_BUFFER)) return;
    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, tmpBuffer);
    p.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::ANTIBANDING_50HZ, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::ANTIBANDING_60HZ, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER)) return;
    if(camerahal_strcat((char*) tmpBuffer, (const char*) CameraParameters::ANTIBANDING_OFF, PARAM_BUFFER)) return;
    p.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, tmpBuffer);
    p.set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);

    p.set(CameraParameters::KEY_ROTATION, 0);
    p.set(KEY_ROTATION_TYPE, ROTATION_PHYSICAL);

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }

    LOG_FUNCTION_NAME_EXIT

}


CameraHal::~CameraHal()
{
    int err = 0;
	int procMessage [1];
	sp<PROCThread> procThread;
	sp<RawThread> rawThread;
	sp<ShutterThread> shutterThread;
	sp<SnapshotThread> snapshotThread;

    LOG_FUNCTION_NAME


    if(mPreviewThread != NULL) {
        Message msg;
        msg.command = PREVIEW_KILL;
        previewThreadCommandQ.put(&msg);
        previewThreadAckQ.get(&msg);
    }

    sp<PreviewThread> previewThread;

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        mPreviewThread.clear();
    }

    procMessage[0] = PROC_THREAD_EXIT;
    write(procPipe[1], procMessage, sizeof(unsigned int));

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        procThread = mPROCThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (procThread != 0) {
        procThread->requestExitAndWait();
    }

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        mPROCThread.clear();
        close(procPipe[0]);
        close(procPipe[1]);
    }

    procMessage[0] = SHUTTER_THREAD_EXIT;
    write(shutterPipe[1], procMessage, sizeof(unsigned int));

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        shutterThread = mShutterThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (shutterThread != 0) {
        shutterThread->requestExitAndWait();
    }

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        mShutterThread.clear();
        close(shutterPipe[0]);
        close(shutterPipe[1]);
    }

    procMessage[0] = RAW_THREAD_EXIT;
    write(rawPipe[1], procMessage, sizeof(unsigned int));

	{ // scope for the lock
        Mutex::Autolock lock(mLock);
        rawThread = mRawThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (rawThread != 0) {
        rawThread->requestExitAndWait();
    }

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        mRawThread.clear();
        close(rawPipe[0]);
        close(rawPipe[1]);
    }

    procMessage[0] = SNAPSHOT_THREAD_EXIT;
    write(snapshotPipe[1], procMessage, sizeof(unsigned int));

    {
        Mutex::Autolock lock(mLock);
        snapshotThread = mSnapshotThread;
    }

    if (snapshotThread != 0 ) {
        snapshotThread->requestExitAndWait();
    }

    {
        Mutex::Autolock lock(mLock);
        mSnapshotThread.clear();
        close(snapshotPipe[0]);
        close(snapshotPipe[1]);
        close(snapshotReadyPipe[0]);
        close(snapshotReadyPipe[1]);
    }

    ICaptureDestroy();

#ifdef FW3A
    FW3A_Release();
    FW3A_Destroy();
#endif

    CameraDestroy(true);


#ifdef IMAGE_PROCESSING_PIPELINE

    if(pIPP.hIPP != NULL){
        err = DeInitIPP(mippMode);
        if( err )
            LOGE("ERROR DeInitIPP() failed");

        pIPP.hIPP = NULL;
    }

#endif

    if ( mOverlay.get() != NULL )
    {
        LOGD("Destroying current overlay");
        mOverlay->destroy();
    }

    singleton.clear();

    LOGD("<<< Release");
}

void CameraHal::previewThread()
{
    Message msg;
    int parm;
    bool  shouldLive = true;
    bool has_message;
    int err;
    struct pollfd pfd[2];

    LOG_FUNCTION_NAME

    while(shouldLive) {

        has_message = false;

        if( mPreviewRunning )
        {

            pfd[0].fd = previewThreadCommandQ.getInFd();
            pfd[0].events = POLLIN;
            pfd[1].fd = camera_device;
            pfd[1].events = POLLIN;

            if (poll(pfd, 2, 1000) == 0) {
                continue;
            }

            if (pfd[0].revents & POLLIN) {
                previewThreadCommandQ.get(&msg);
                has_message = true;
            }

            if (pfd[1].revents & POLLIN) {
                nextPreview();
            }

#ifdef FW3A

            if ( isStart_FW3A_AF ) {
                err = ICam_ReadStatus(fobj->hnd, &fobj->status);
                if ( (err == 0) && ( ICAM_AF_STATUS_RUNNING != fobj->status.af.status ) ) {

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS

                    PPM("AF Completed in ",&focus_before);

#endif

                    ICam_ReadMakerNote(fobj->hnd, &fobj->mnote);

                    if (FW3A_Stop_AF() < 0){
                        LOGE("ERROR FW3A_Stop_AF()");
                    }

                    bool focus_flag;
                    if ( fobj->status.af.status == ICAM_AF_STATUS_SUCCESS ) {
                        focus_flag = true;
                        LOGE("AF Success");
                    } else {
                        focus_flag = false;
                        LOGE("AF Fail");
                    }

                    if( msgTypeEnabled(CAMERA_MSG_FOCUS) )
                        mNotifyCb(CAMERA_MSG_FOCUS, focus_flag, 0, mCallbackCookie);
                }
            }
#endif

        }
        else
        {
            //block for message
            previewThreadCommandQ.get(&msg);
            has_message = true;
        }

        if( !has_message )
            continue;

        switch(msg.command)
        {
            case PREVIEW_START:
            {
                LOGD("Receive Command: PREVIEW_START");
                err = 0;

                if( ! mPreviewRunning ) {

                    if( CameraCreate() < 0){
                        LOGE("ERROR CameraCreate()");
                        err = -1;
                    }

                    PPM("CONFIGURING CAMERA TO RESTART PREVIEW");
                    if (CameraConfigure() < 0){
                        LOGE("ERROR CameraConfigure()");
                        err = -1;
                    }

#ifdef FW3A

                    if (FW3A_Start() < 0){
                        LOGE("ERROR FW3A_Start()");
                        err = -1;
                    }

                    if (FW3A_SetSettings() < 0){
                        LOGE("ERROR FW3A_SetSettings()");
                        err = -1;
                    }

#endif

                    if ( CorrectPreview() < 0 )
                        LOGE("Error during CorrectPreview()");

                    if ( CameraStart() < 0 ) {
                        LOGE("ERROR CameraStart()");
                        err = -1;
                    }

#ifdef FW3A
                    if ( mCAFafterPreview ) {
                        mCAFafterPreview = false;
                        if( FW3A_Start_CAF() < 0 )
                            LOGE("Error while starting CAF");
                    }
#endif

                    if(!mfirstTime){
                        PPM("Standby to first shot");
                        mfirstTime++;
                    } else {

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
                        PPM("Shot to Shot", &ppm_receiveCmdToTakePicture);

#endif

                    }
                } else {
                    err = -1;
                }

                LOGD("PREVIEW_START %s", err ? "NACK" : "ACK");
                msg.command = err ? PREVIEW_NACK : PREVIEW_ACK;

                if ( !err ) {
                    LOGD("Preview Started!");
                    mPreviewRunning = true;
                }

                previewThreadAckQ.put(&msg);
            }
            break;

            case PREVIEW_STOP:
            {
                LOGD("Receive Command: PREVIEW_STOP");
                err = 0;
                if( mPreviewRunning ) {

#ifdef FW3A

                    if( FW3A_Stop_AF() < 0){
                        LOGE("ERROR FW3A_Stop_AF()");
                        err= -1;
                    }
                    if( FW3A_Stop_CAF() < 0){
                        LOGE("ERROR FW3A_Stop_CAF()");
                        err= -1;
                    } else {
                        mcaf = 0;
                    }
                    if( FW3A_Stop() < 0){
                        LOGE("ERROR FW3A_Stop()");
                        err= -1;
                    }
                    if( FW3A_GetSettings() < 0){
                        LOGE("ERROR FW3A_GetSettings()");
                        err= -1;
                    }

#endif

                    if( CameraStop() < 0){
                        LOGE("ERROR CameraStop()");
                        err= -1;
                    }

                    if( CameraDestroy(false) < 0){
                        LOGE("ERROR CameraDestroy()");
                        err= -1;
                    }

                    if (err) {
                        LOGE("ERROR Cannot deinit preview.");
                    }

                    LOGD("PREVIEW_STOP %s", err ? "NACK" : "ACK");
                    msg.command = err ? PREVIEW_NACK : PREVIEW_ACK;
                }
                else
                {
                    msg.command = PREVIEW_NACK;
                }

                mPreviewRunning = false;

                previewThreadAckQ.put(&msg);
            }
            break;

            case START_SMOOTH_ZOOM:

                parm = ( int ) msg.arg1;

                LOGD("Receive Command: START_SMOOTH_ZOOM %d", parm);

                if ( ( parm >= 0 ) && ( parm < ZOOM_STAGES) ) {
                    mZoomTargetIdx = parm;
                    mZoomSpeed = 1;
                    mSmoothZoomStatus = SMOOTH_START;
                    msg.command = PREVIEW_ACK;
                } else {
                    msg.command = PREVIEW_NACK;
                }

                previewThreadAckQ.put(&msg);

                break;

            case STOP_SMOOTH_ZOOM:

                LOGD("Receive Command: STOP_SMOOTH_ZOOM");
                if(mSmoothZoomStatus == SMOOTH_START)
                {
                    mSmoothZoomStatus = SMOOTH_NOTIFY_AND_STOP;
                }
                msg.command = PREVIEW_ACK;

                previewThreadAckQ.put(&msg);

                break;

            case PREVIEW_AF_START:
            {
                LOGD("Receive Command: PREVIEW_AF_START");
				err = 0;

                if( !mPreviewRunning ){
                    LOGD("WARNING PREVIEW NOT RUNNING!");
                    msg.command = PREVIEW_NACK;
                } else {

#ifdef FW3A

                   if (isStart_FW3A_CAF!= 0){
                        if( FW3A_Stop_CAF() < 0){
                            LOGE("ERROR FW3A_Stop_CAF();");
                            err = -1;
                        }
                   }

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS

                    gettimeofday(&focus_before, NULL);

#endif
                    if (isStart_FW3A){
                    if (isStart_FW3A_AF == 0){
                        if( FW3A_Start_AF() < 0){
                            LOGE("ERROR FW3A_Start_AF()");
                            err = -1;
                         }

                    }
                    } else {
                        if(msgTypeEnabled(CAMERA_MSG_FOCUS))
                            mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
                    }

                    msg.command = err ? PREVIEW_NACK : PREVIEW_ACK;

#else

                    if( msgTypeEnabled(CAMERA_MSG_FOCUS) )
                        mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);

                    msg.command = PREVIEW_ACK;

#endif

                }
                LOGD("Receive Command: PREVIEW_AF_START %s", msg.command == PREVIEW_NACK ? "NACK" : "ACK");
                previewThreadAckQ.put(&msg);
            }
            break;

            case PREVIEW_CAF_START:
            {
                LOGD("Receive Command: PREVIEW_CAF_START");
                err=0;

                if( !mPreviewRunning ) {
                    msg.command = PREVIEW_ACK;
                    mCAFafterPreview = true;
                }
                else
                {
#ifdef FW3A
                if (isStart_FW3A_CAF == 0){
                    if( FW3A_Start_CAF() < 0){
                        LOGE("ERROR FW3A_Start_CAF()");
                        err = -1;
                    }
                }
#endif
                    msg.command = err ? PREVIEW_NACK : PREVIEW_ACK;
                }
                LOGD("Receive Command: PREVIEW_CAF_START %s", msg.command == PREVIEW_NACK ? "NACK" : "ACK");
                previewThreadAckQ.put(&msg);
            }
            break;

           case PREVIEW_CAF_STOP:
           {
                LOGD("Receive Command: PREVIEW_CAF_STOP");
                err = 0;

                if( !mPreviewRunning )
                    msg.command = PREVIEW_ACK;
                else
                {
#ifdef FW3A
                    if( FW3A_Stop_CAF() < 0){
                         LOGE("ERROR FW3A_Stop_CAF()");
                         err = -1;
                    }
#endif
                    msg.command = err ? PREVIEW_NACK : PREVIEW_ACK;
                }
                LOGD("Receive Command: PREVIEW_CAF_STOP %s", msg.command == PREVIEW_NACK ? "NACK" : "ACK");
                previewThreadAckQ.put(&msg);
           }
           break;

           case PREVIEW_CAPTURE:
           {
                int flg_AF = 0;
                int flg_CAF = 0;
                err = 0;

#ifdef DEBUG_LOG

                LOGD("ENTER OPTION PREVIEW_CAPTURE");

                PPM("RECEIVED COMMAND TO TAKE A PICTURE");

#endif

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
                gettimeofday(&ppm_receiveCmdToTakePicture, NULL);
#endif

                // Boost DSP OPP to highest level
                SetDSPHz(DSP3630_HZ_MAX);

                if( mPreviewRunning ) {
                    if( CameraStop() < 0){
                        LOGE("ERROR CameraStop()");
                        err = -1;
                    }

#ifdef FW3A
                   if( (flg_AF = FW3A_Stop_AF()) < 0){
                        LOGE("ERROR FW3A_Stop_AF()");
                        err = -1;
                   }
                   if( (flg_CAF = FW3A_Stop_CAF()) < 0){
                       LOGE("ERROR FW3A_Stop_CAF()");
                       err = -1;
                   } else {
                       mcaf = 0;
                   }

                   if ( ICam_ReadStatus(fobj->hnd, &fobj->status) < 0 ) {
                   LOGE("ICam_ReadStatus failed");
                   err = -1;
                   }

                   if ( ICam_ReadMakerNote(fobj->hnd, &fobj->mnote) < 0 ) {
                   LOGE("ICam_ReadMakerNote failed");
                   err = -1;
                   }

                   if( FW3A_Stop() < 0){
                       LOGE("ERROR FW3A_Stop()");
                       err = -1;
                   }
#endif

                   mPreviewRunning = false;

               }
#ifdef FW3A
               if( FW3A_GetSettings() < 0){
                   LOGE("ERROR FW3A_GetSettings()");
                   err = -1;
               }
#endif

#ifdef DEBUG_LOG

               PPM("STOPPED PREVIEW");

#endif

               if( ICapturePerform() < 0){
                   LOGE("ERROR ICapturePerform()");
                   err = -1;
               }

                if( err )
                   LOGE("Capture failed.");

               //restart the preview
#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
                gettimeofday(&ppm_restartPreview, NULL);
#endif

#ifdef DEBUG_LOG

               PPM("CONFIGURING CAMERA TO RESTART PREVIEW");

#endif

               if (CameraConfigure() < 0)
                   LOGE("ERROR CameraConfigure()");

#ifdef FW3A

               if (FW3A_Start() < 0)
                   LOGE("ERROR FW3A_Start()");

#endif

               if ( CorrectPreview() < 0 )
                   LOGE("Error during CorrectPreview()");

               if (CameraStart() < 0)
                   LOGE("ERROR CameraStart()");

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS

               PPM("Capture mode switch", &ppm_restartPreview);
               PPM("Shot to Shot", &ppm_receiveCmdToTakePicture);

#endif

#ifdef FW3A

                    if ( flg_CAF ) {
                        if( FW3A_Start_CAF() < 0 )
                            LOGE("Error while starting CAF");
                    }

#endif

               mPreviewRunning = true;

               msg.command = PREVIEW_ACK;
               previewThreadAckQ.put(&msg);
               LOGD("EXIT OPTION PREVIEW_CAPTURE");
           }
           break;

           case PREVIEW_CAPTURE_CANCEL:
           {
                LOGD("Receive Command: PREVIEW_CAPTURE_CANCEL");
                msg.command = PREVIEW_NACK;
                previewThreadAckQ.put(&msg);
            }
            break;

            case PREVIEW_KILL:
            {
                LOGD("Receive Command: PREVIEW_KILL");
                err = 0;

                if (mPreviewRunning) {
#ifdef FW3A
                   if( FW3A_Stop_AF() < 0){
                        LOGE("ERROR FW3A_Stop_AF()");
                        err = -1;
                   }
                   if( FW3A_Stop_CAF() < 0){
                        LOGE("ERROR FW3A_Stop_CAF()");
                        err = -1;
                   }
                   if( FW3A_Stop() < 0){
                        LOGE("ERROR FW3A_Stop()");
                        err = -1;
                   }
#endif
                   if( CameraStop() < 0){
                        LOGE("ERROR FW3A_Stop()");
                        err = -1;
                   }
                   mPreviewRunning = false;
                }

                msg.command = err ? PREVIEW_NACK : PREVIEW_ACK;
                LOGD("Receive Command: PREVIEW_CAF_STOP %s", msg.command == PREVIEW_NACK ? "NACK" : "ACK");

                previewThreadAckQ.put(&msg);
                shouldLive = false;
          }
          break;
        }
    }

   LOG_FUNCTION_NAME_EXIT
}

int CameraHal::CameraCreate()
{
    int err = 0;

    LOG_FUNCTION_NAME

    camera_device = open(VIDEO_DEVICE, O_RDWR);
    if (camera_device < 0) {
        LOGE ("Could not open the camera device: %s",  strerror(errno) );
        goto exit;
    }

    LOG_FUNCTION_NAME_EXIT
    return 0;

exit:
    return err;
}


int CameraHal::CameraDestroy(bool destroyOverlay)
{
    int err, buffer_count;

    LOG_FUNCTION_NAME

    close(camera_device);
    camera_device = -1;

    if ((mOverlay != NULL) && (destroyOverlay)) {
        buffer_count = mOverlay->getBufferCount();

        for ( int i = 0 ; i < buffer_count ; i++ )
        {
            // need to free buffers and heaps mapped using overlay fd before it is destroyed
            // otherwise we could create a resource leak
            // a segfault could occur if we try to free pointers after overlay is destroyed
            mPreviewBuffers[i].clear();
            mPreviewHeaps[i].clear();
            mVideoBuffer[i].clear();
            mVideoHeaps[i].clear();
            buffers_queued_to_dss[i] = 0;
        }
        mOverlay->destroy();
        mOverlay = NULL;
        nOverlayBuffersQueued = 0;

    }

    LOG_FUNCTION_NAME_EXIT
    return 0;
}

int CameraHal::CameraConfigure()
{
    int w, h, framerate;
    int image_width, image_height;
    int err;
    struct v4l2_format format;
    enum v4l2_buf_type type;
    struct v4l2_control vc;
    struct v4l2_streamparm parm;

    LOG_FUNCTION_NAME

    mParameters.getPreviewSize(&w, &h);

    /* Set preview format */
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = w;
    format.fmt.pix.height = h;
    format.fmt.pix.pixelformat = PIXEL_FORMAT;

    err = ioctl(camera_device, VIDIOC_S_FMT, &format);
    if ( err < 0 ){
        LOGE ("Failed to set VIDIOC_S_FMT.");
        goto s_fmt_fail;
    }

    LOGI("CameraConfigure PreviewFormat: w=%d h=%d", format.fmt.pix.width, format.fmt.pix.height);

    framerate = mParameters.getPreviewFrameRate();

    LOGD("CameraConfigure: framerate to set = %d",framerate);

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    err = ioctl(camera_device, VIDIOC_G_PARM, &parm);
    if(err != 0) {
        LOGD("VIDIOC_G_PARM ");
        return -1;
    }

    LOGD("CameraConfigure: Old frame rate is %d/%d  fps",
    parm.parm.capture.timeperframe.denominator,
    parm.parm.capture.timeperframe.numerator);

    parm.parm.capture.timeperframe.numerator = 1;
    //Framerate 0 does not make sense in kernel context
    if ( framerate!=0 ) parm.parm.capture.timeperframe.denominator = framerate;
    else ( parm.parm.capture.timeperframe.denominator ) = 30;
    err = ioctl(camera_device, VIDIOC_S_PARM, &parm);
    if(err != 0) {
        LOGE("VIDIOC_S_PARM ");
        return -1;
    }

    LOGI("CameraConfigure: New frame rate is %d/%d fps",parm.parm.capture.timeperframe.denominator,parm.parm.capture.timeperframe.numerator);

    LOG_FUNCTION_NAME_EXIT
    return 0;

s_fmt_fail:
    return -1;
}

int CameraHal::CameraStart()
{
    int w, h;
    int err;
    int nSizeBytes;
    int buffer_count;
    struct v4l2_format format;
    enum v4l2_buf_type type;
    struct v4l2_requestbuffers creqbuf;

    LOG_FUNCTION_NAME

    nCameraBuffersQueued = 0;

    mParameters.getPreviewSize(&w, &h);
    LOGD("**CaptureQBuffers: preview size=%dx%d", w, h);

    mPreviewFrameSize = w * h * 2;
    if (mPreviewFrameSize & 0xfff)
    {
        mPreviewFrameSize = (mPreviewFrameSize & 0xfffff000) + 0x1000;
    }
    LOGD("mPreviewFrameSize = 0x%x = %d", mPreviewFrameSize, mPreviewFrameSize);

    buffer_count = mOverlay->getBufferCount();
    LOGD("number of buffers = %d\n", buffer_count);

    creqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    creqbuf.memory = V4L2_MEMORY_USERPTR;
    creqbuf.count  =  buffer_count ;
    if (ioctl(camera_device, VIDIOC_REQBUFS, &creqbuf) < 0) {
        LOGE ("VIDIOC_REQBUFS Failed. %s", strerror(errno));
        goto fail_reqbufs;
    }

    for (int i = 0; i < (int)creqbuf.count; i++) {

        v4l2_cam_buffer[i].type = creqbuf.type;
        v4l2_cam_buffer[i].memory = creqbuf.memory;
        v4l2_cam_buffer[i].index = i;

        if (ioctl(camera_device, VIDIOC_QUERYBUF, &v4l2_cam_buffer[i]) < 0) {
            LOGE("VIDIOC_QUERYBUF Failed");
            goto fail_loop;
        }

        mapping_data_t* data = (mapping_data_t*) mOverlay->getBufferAddress((void*)i);
        if ( data == NULL ) {
            LOGE(" getBufferAddress returned NULL");
            goto fail_loop;
        }

        v4l2_cam_buffer[i].m.userptr = (unsigned long) data->ptr;
        v4l2_cam_buffer[i].length = data->length;

        strcpy((char *)v4l2_cam_buffer[i].m.userptr, "hello");
        if (strcmp((char *)v4l2_cam_buffer[i].m.userptr, "hello")) {
            LOGI("problem with buffer\n");
            goto fail_loop;
        }

        LOGD("User Buffer [%d].start = %p  length = %d\n", i,
             (void*)v4l2_cam_buffer[i].m.userptr, v4l2_cam_buffer[i].length);

        if (buffers_queued_to_dss[i] == 0)
        {
            if (ioctl(camera_device, VIDIOC_QBUF, &v4l2_cam_buffer[i]) < 0) {
                LOGE("CameraStart VIDIOC_QBUF Failed: %s", strerror(errno) );
                goto fail_loop;
            }else{
                nCameraBuffersQueued++;
            }
         }
         else LOGI("CameraStart::Could not queue buffer %d to Camera because it is being held by Overlay", i);

        // ensure we release any stale ref's to sp
        mPreviewBuffers[i].clear();
        mPreviewHeaps[i].clear();

        mPreviewHeaps[i] = new MemoryHeapBase(data->fd,mPreviewFrameSize, 0, data->offset);
        mPreviewBuffers[i] = new MemoryBase(mPreviewHeaps[i], 0, mPreviewFrameSize);

    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    err = ioctl(camera_device, VIDIOC_STREAMON, &type);
    if ( err < 0) {
        LOGE("VIDIOC_STREAMON Failed");
        goto fail_loop;
    }

    if( ioctl(camera_device, VIDIOC_G_CROP, &mInitialCrop) < 0 ){
        LOGE("[%s]: ERROR VIDIOC_G_CROP failed", strerror(errno));
        return -1;
    }

    LOGE("Initial Crop: crop_top = %d, crop_left = %d, crop_width = %d, crop_height = %d", mInitialCrop.c.top, mInitialCrop.c.left, mInitialCrop.c.width, mInitialCrop.c.height);

    if ( mZoomTargetIdx != mZoomCurrentIdx ) {

        if( ZoomPerform(zoom_step[mZoomTargetIdx]) < 0 )
            LOGE("Error while applying zoom");

        mZoomCurrentIdx = mZoomTargetIdx;
        mParameters.set("zoom", (int) mZoomCurrentIdx);
        mNotifyCb(CAMERA_MSG_ZOOM, (int) mZoomCurrentIdx, 1, mCallbackCookie);
    }

    LOG_FUNCTION_NAME_EXIT

    return 0;

fail_bufalloc:
fail_loop:
fail_reqbufs:

    return -1;
}

int CameraHal::CameraStop()
{

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    int ret;
    struct v4l2_requestbuffers creqbuf;
    struct v4l2_buffer cfilledbuffer;
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;

    while(nCameraBuffersQueued){
        nCameraBuffersQueued--;
    }

#ifdef DEBUG_LOG

	LOGD("Done dequeuing from Camera!");

#endif

    creqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_device, VIDIOC_STREAMOFF, &creqbuf.type) == -1) {
        LOGE("VIDIOC_STREAMOFF Failed");
        goto fail_streamoff;
    }

	//Force the zoom to be updated next time preview is started.
	mZoomCurrentIdx = 0;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return 0;

fail_streamoff:

    return -1;
}

static void debugShowFPS()
{
    static int mFrameCount = 0;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    if (!(mFrameCount & 0x1F)) {
        nsecs_t now = systemTime();
        nsecs_t diff = now - mLastFpsTime;
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
        LOGD("####### [%d] Frames, %f FPS", mFrameCount, mFps);
    }
}

void CameraHal::queueToOverlay(int index)
{
    int nBuffers_queued_to_dss = mOverlay->queueBuffer((void*)index);
    if (nBuffers_queued_to_dss < 0)
    {
        if (ioctl(camera_device, VIDIOC_QBUF, &v4l2_cam_buffer[index]) < 0) {
            LOGE("VIDIOC_QBUF Failed, line=%d",__LINE__);
        }
        else nCameraBuffersQueued++;
    }
    else
    {
        nOverlayBuffersQueued++;
        buffers_queued_to_dss[index] = 1; //queued
        if (nBuffers_queued_to_dss != nOverlayBuffersQueued)
        {
            LOGD("nBuffers_queued_to_dss = %d, nOverlayBuffersQueued = %d", nBuffers_queued_to_dss, nOverlayBuffersQueued);
            LOGD("buffers in DSS \n %d %d %d  %d %d %d", buffers_queued_to_dss[0], buffers_queued_to_dss[1],
            buffers_queued_to_dss[2], buffers_queued_to_dss[3], buffers_queued_to_dss[4], buffers_queued_to_dss[5]);
            //Queue all the buffers that were discarded by DSS upon STREAM OFF, back to camera.
            for(int k = 0; k < MAX_CAMERA_BUFFERS; k++)
            {
                if ((buffers_queued_to_dss[k] == 1) && (k != index))
                {
                    buffers_queued_to_dss[k] = 0;
                    nOverlayBuffersQueued--;
                    if (ioctl(camera_device, VIDIOC_QBUF, &v4l2_cam_buffer[k]) < 0) {
                        LOGE("VIDIOC_QBUF Failed, line=%d. Buffer lost forever.",__LINE__);
                    }else{
                        nCameraBuffersQueued++;
                        LOGD("Reclaiming buffer [%d] from Overlay", k);
                    }
                }
            }
        }
    }
}

int CameraHal::dequeueFromOverlay()
{
    overlay_buffer_t overlaybuffer;// contains the index of the buffer dque

    int dequeue_from_dss_failed = mOverlay->dequeueBuffer(&overlaybuffer);
    if(!dequeue_from_dss_failed){
        nOverlayBuffersQueued--;
        buffers_queued_to_dss[(int)overlaybuffer] = 0;
        lastOverlayBufferDQ = (int)overlaybuffer;
        return (int)overlaybuffer;
    }
    return -1;
}

void CameraHal::nextPreview()
{
    struct v4l2_buffer cfilledbuffer;
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;
    int w, h, ret;
    static int frame_count = 0;
    int zoom_inc, err;
    struct timeval lowLightTime;
    int overlaybufferindex = -1; //contains the last buffer dque or -1 if dque failed

    mParameters.getPreviewSize(&w, &h);

    //Zoom
    frame_count++;
    if( mZoomCurrentIdx != mZoomTargetIdx){

        if( mZoomCurrentIdx < mZoomTargetIdx)
            zoom_inc = 1;
        else
            zoom_inc = -1;

        if ( mZoomSpeed > 0 ){
            if( (frame_count % mZoomSpeed) == 0){
                mZoomCurrentIdx += zoom_inc;
            }
        } else {
            mZoomCurrentIdx = mZoomTargetIdx;
        }

        ZoomPerform(zoom_step[mZoomCurrentIdx]);
        mParameters.set("zoom", mZoomCurrentIdx);

        // Immediate zoom should not generate callbacks.
        if ( mSmoothZoomStatus == SMOOTH_START ||  mSmoothZoomStatus == SMOOTH_NOTIFY_AND_STOP)  {
            if(mSmoothZoomStatus == SMOOTH_NOTIFY_AND_STOP)
            {
                mZoomTargetIdx = mZoomCurrentIdx;
                mSmoothZoomStatus = SMOOTH_STOP;
            }
            if( mZoomCurrentIdx == mZoomTargetIdx )
                mNotifyCb(CAMERA_MSG_ZOOM, mZoomCurrentIdx, 1, mCallbackCookie);
            else
                mNotifyCb(CAMERA_MSG_ZOOM, mZoomCurrentIdx, 0, mCallbackCookie);
        }
    }

#ifdef FW3A
    if (isStart_FW3A != 0){
    //Low light notification
    if( ( fobj->settings.ae.framerate == 0 ) && ( ( frame_count % 10) == 0 ) ) {
#if ( PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS ) && DEBUG_LOG

        gettimeofday(&lowLightTime, NULL);

#endif
        err = ICam_ReadStatus(fobj->hnd, &fobj->status);
        if (err == 0) {
            if (fobj->status.ae.camera_shake == ICAM_SHAKE_HIGH_RISK2) {
                mParameters.set("low-light", "1");
            } else {
                mParameters.set("low-light", "0");
            }
         }

#if ( PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS ) && DEBUG_LOG

        PPM("Low-light delay", &lowLightTime);

#endif

    }
    }
#endif

    if (ioctl(camera_device, VIDIOC_DQBUF, &cfilledbuffer) < 0) {
        LOGE("VIDIOC_DQBUF Failed!!!");
        goto EXIT;
    }else{
        nCameraBuffersQueued--;
    }
    mCurrentTime[cfilledbuffer.index] = s2ns(cfilledbuffer.timestamp.tv_sec) + us2ns(cfilledbuffer.timestamp.tv_usec);

    //SaveFile(NULL, (char*)"yuv", (void *)cfilledbuffer.m.userptr, mPreviewFrameSize);

    if( msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME) )
        mDataCb(CAMERA_MSG_PREVIEW_FRAME, mPreviewBuffers[cfilledbuffer.index], mCallbackCookie);

    queueToOverlay(cfilledbuffer.index);
    overlaybufferindex = dequeueFromOverlay();

    mRecordingLock.lock();

    if(mRecordEnabled)
    {
        if(overlaybufferindex != -1) // dequeued a valid buffer from overlay
        {
            if (nCameraBuffersQueued == 0)
            {
                /* Drop the frame. Camera is starving. */
                if (ioctl(camera_device, VIDIOC_QBUF, &v4l2_cam_buffer[overlaybufferindex]) < 0) {
                    LOGE("VIDIOC_QBUF Failed, line=%d",__LINE__);
                }
                else nCameraBuffersQueued++;
                LOGD("Didnt queue this buffer to VE.");
                if (mPrevTime < mCurrentTime[overlaybufferindex])
                    mPrevTime = mCurrentTime[overlaybufferindex];
                else
                    mPrevTime += frameInterval;
            }
            else
            {
                buffers_queued_to_ve[overlaybufferindex] = 1;
                if (mPrevTime > mCurrentTime[overlaybufferindex])
                {
                    //LOGW("Had to adjust the timestamp. Clock went back in time. mCurrentTime = %lld, mPrevTime = %llu", mCurrentTime[overlaybufferindex], mPrevTime);
                    mCurrentTime[overlaybufferindex] = mPrevTime + frameInterval;
                }
#ifdef OMAP_ENHANCEMENT
                mDataCbTimestamp(mCurrentTime[overlaybufferindex], CAMERA_MSG_VIDEO_FRAME, mVideoBuffer[overlaybufferindex], mCallbackCookie, 0, 0);
#else
                mDataCbTimestamp(mCurrentTime[overlaybufferindex], CAMERA_MSG_VIDEO_FRAME, mVideoBuffer[overlaybufferindex], mCallbackCookie);
#endif
                mPrevTime = mCurrentTime[overlaybufferindex];
            }
        }
    }
    else
    {
        if (overlaybufferindex != -1) {	// dequeued a valid buffer from overlay
            if (ioctl(camera_device, VIDIOC_QBUF, &v4l2_cam_buffer[overlaybufferindex]) < 0) {
                LOGE("VIDIOC_QBUF Failed. line=%d",__LINE__);
            }else{
                nCameraBuffersQueued++;
            }
        }
    }

    mRecordingLock.unlock();


    if (UNLIKELY(mDebugFps)) {
        debugShowFPS();
    }

EXIT:

    return ;
}

#ifdef ICAP

int  CameraHal::ICapturePerform()
{

    int err;
    int status = 0;
    int jpegSize;
    uint32_t fixedZoom;
    void *outBuffer;
    unsigned long base, offset, jpeg_offset;
    unsigned int preview_width, preview_height;
    icap_buffer_t snapshotBuffer;
    unsigned int image_width, image_height, thumb_width, thumb_height;
    SICap_ManualParameters  manual_config;
    SICam_Settings config;
    icap_tuning_params_t cap_tuning;
    icap_resolution_cap_t spec_res;
    icap_buffer_t capture_buffer;
    icap_process_mode_e mode;
    unsigned int procMessage[PROC_THREAD_NUM_ARGS],
            shutterMessage[SHUTTER_THREAD_NUM_ARGS],
            rawMessage[RAW_THREAD_NUM_ARGS];
	int pixelFormat;
    exif_buffer *exif_buf;
    mode = ICAP_PROCESS_MODE_CONTINUOUS;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

    PPM("START OF ICapturePerform");

#endif

    mParameters.getPictureSize((int *) &image_width, (int *) &image_height);
    mParameters.getPreviewSize((int *) &preview_width,(int *) &preview_height);
    thumb_width = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    thumb_height = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);

#ifdef DEBUG_LOG

    LOGD("ICapturePerform image_width=%d image_height=%d",image_width,image_height);

#endif

    memset(&manual_config, 0 ,sizeof(manual_config));
    memset(&iobj->cfg, 0, sizeof(icap_configure_t));

#ifdef DEBUG_LOG

	LOGD("MakerNote 0x%x, size %d", ( unsigned int ) fobj->mnote.buffer,fobj->mnote.filled_size);

    LOGD("shutter_cap = %d ; again_cap = %d ; awb_index = %d; %d %d %d %d\n",
        (int)fobj->status.ae.shutter_cap, (int)fobj->status.ae.again_cap,
        (int)fobj->status.awb.awb_index, (int)fobj->status.awb.gain_Gr,
        (int)fobj->status.awb.gain_R, (int)fobj->status.awb.gain_B,
        (int)fobj->status.awb.gain_Gb);

#endif

    snapshotBuffer.buffer = NULL;
    snapshotBuffer.alloc_size = 0;
    LOGE("mBurstShots %d", mBurstShots);
    if ( 1 >= mBurstShots ) {
        if ( mcapture_mode == 1 ) {
            spec_res.capture_mode = ICAP_CAPTURE_MODE_HP;
            iobj->cfg.capture_mode = ICAP_CAPTURE_MODE_HP;
            iobj->cfg.notify.cb_capture = onSnapshot;
            iobj->cfg.notify.cb_snapshot_crop = onCrop;
            fixedZoom = (uint32_t) (zoom_step[0]*ZOOM_SCALE);

            iobj->cfg.zoom.enable = ICAP_ENABLE;
            iobj->cfg.zoom.vertical = fixedZoom;
            iobj->cfg.zoom.horizontal = fixedZoom;
        } else {
            snapshotBuffer.buffer = getLastOverlayAddress();
            snapshotBuffer.alloc_size = getLastOverlayLength();
            spec_res.capture_mode = ICAP_CAPTURE_MODE_HQ;
            iobj->cfg.capture_mode = ICAP_CAPTURE_MODE_HQ;
            iobj->cfg.notify.cb_snapshot = onGeneratedSnapshot;
            iobj->cfg.snapshot_width = preview_width;
            iobj->cfg.snapshot_height = preview_height;
            fixedZoom = (uint32_t) (zoom_step[mZoomTargetIdx]*ZOOM_SCALE);

            iobj->cfg.zoom.enable = ICAP_ENABLE;
            iobj->cfg.zoom.vertical = fixedZoom;
            iobj->cfg.zoom.horizontal = fixedZoom;
        }
        iobj->cfg.notify.cb_mknote = onMakernote;
        iobj->cfg.notify.cb_ipp = onIPP;
    } else {
        spec_res.capture_mode  = ICAP_CAPTURE_MODE_HP;
        iobj->cfg.capture_mode = ICAP_CAPTURE_MODE_HP;
        iobj->cfg.notify.cb_capture = onSnapshot;
        iobj->cfg.notify.cb_snapshot_crop = onCrop;
        fixedZoom = (uint32_t) (zoom_step[0]*ZOOM_SCALE);

        iobj->cfg.zoom.enable = ICAP_ENABLE;
        iobj->cfg.zoom.vertical = fixedZoom;
        iobj->cfg.zoom.horizontal = fixedZoom;
    }
    iobj->cfg.notify.cb_shutter = onShutter;
    spec_res.res.width = image_width;
    spec_res.res.height = image_height;
    spec_res.capture_format = ICAP_CAPTURE_FORMAT_UYVY;

    status = icap_query_resolution(iobj->lib_private, 0, &spec_res);

    if( ICAP_STATUS_FAIL == status){
        LOGE ("First icapture query resolution failed");

        int tmp_width, tmp_height;
        tmp_width = image_width;
        tmp_height = image_height;
        status = icap_query_enumerate(iobj->lib_private, 0, &spec_res);
        if ( ICAP_STATUS_FAIL == status ) {
            LOGE("icapture enumerate failed");
            goto fail_config;
        }

        if ( image_width  < spec_res.res_range.width_min )
            tmp_width = spec_res.res_range.width_min;

        if ( image_height < spec_res.res_range.height_min )
            tmp_height = spec_res.res_range.height_min;

        if ( image_width > spec_res.res_range.width_max )
            tmp_width = spec_res.res_range.width_max;

        if ( image_height > spec_res.res_range.height_max )
            tmp_height = spec_res.res_range.height_max;

        spec_res.res.width = tmp_width;
        spec_res.res.height = tmp_height;
        status = icap_query_resolution(iobj->lib_private, 0, &spec_res);
        if ( ICAP_STATUS_FAIL == status ) {
            LOGE("icapture query failed again, giving up");
            goto fail_config;
        }
    }

    iobj->cfg.notify.priv   = this;

    iobj->cfg.crop.enable = ICAP_ENABLE;
    iobj->cfg.crop.top = fobj->status.preview.top;
    iobj->cfg.crop.left = fobj->status.preview.left;
    iobj->cfg.crop.width = fobj->status.preview.width;
    iobj->cfg.crop.height = fobj->status.preview.height;

    iobj->cfg.capture_format = ICAP_CAPTURE_FORMAT_UYVY;
    iobj->cfg.width = spec_res.res.width;
    iobj->cfg.height = spec_res.res.height;
    mImageCropTop = 0;
    mImageCropLeft = 0;
    mImageCropWidth = spec_res.res.width;
    mImageCropHeight = spec_res.res.height;

    iobj->cfg.notify.cb_h3a  = NULL; //onSaveH3A;
    iobj->cfg.notify.cb_lsc  = NULL; //onSaveLSC;
    iobj->cfg.notify.cb_raw  = NULL; //onSaveRAW;

#if DEBUG_LOG

	PPM("Before ICapture Config");

#endif

    LOGE("Zoom set to %d", fixedZoom);

    status = icap_config_common(iobj->lib_private, &iobj->cfg);

    if( ICAP_STATUS_FAIL == status){
        LOGE ("ICapture Config function failed");
        goto fail_config;
    }

#if DEBUG_LOG

    PPM("ICapture config OK");

	LOGD("iobj->cfg.image_width = %d = 0x%x iobj->cfg.image_height=%d = 0x%x , iobj->cfg.sizeof_img_buf = %d", (int)iobj->cfg.width, (int)iobj->cfg.width, (int)iobj->cfg.height, (int)iobj->cfg.height, (int) spec_res.buffer_size);

#endif

    cap_tuning.icam_makernote = ( void * ) &fobj->mnote;
    cap_tuning.icam_settings = ( void * ) &fobj->settings;
    cap_tuning.icam_status = ( void * ) &fobj->status;

#ifdef DEBUG_LOG

    PPM("SETUP SOME 3A STUFF");

#endif

    status = icap_config_tuning(iobj->lib_private, &cap_tuning);
    if( ICAP_STATUS_FAIL == status){
        LOGE ("ICapture tuning function failed");
        goto fail_config;
    }

    allocatePictureBuffer(spec_res.buffer_size, mBurstShots);

    for ( int i = 0; i < mBurstShots; i++ ) {
        capture_buffer.buffer = mYuvBuffer[i];
        capture_buffer.alloc_size = spec_res.buffer_size;

        LOGE ("ICapture push buffer 0x%x, len %d", ( unsigned int ) mYuvBuffer[i], spec_res.buffer_size);
        status = icap_push_buffer(iobj->lib_private, &capture_buffer, &snapshotBuffer);
        if( ICAP_STATUS_FAIL == status){
            LOGE ("ICapture push buffer function failed");
            goto fail_config;
        }

    }

    for ( int i = 0; i < mBurstShots; i++ ) {

#if DEBUG_LOG

	PPM("BEFORE ICapture Process");

#endif

        status = icap_process(iobj->lib_private, mode, NULL);

    if( ICAP_STATUS_FAIL == status ) {
        LOGE("ICapture Process failed");
        goto fail_process;
    }

#if DEBUG_LOG

    else {
        PPM("ICapture process OK");
    }

    LOGD("iobj->proc.out_img_w = %d = 0x%x iobj->proc.out_img_h=%u = 0x%x", (int) iobj->cfg.width,(int) iobj->cfg.width, (int) iobj->cfg.height,(int)iobj->cfg.height);

#endif

    pixelFormat = PIX_YUV422I;

//block until snapshot is ready
    fd_set descriptorSet;
    int max_fd;
    unsigned int snapshotReadyMessage;

    max_fd = snapshotReadyPipe[0] + 1;

    FD_ZERO(&descriptorSet);
    FD_SET(snapshotReadyPipe[0], &descriptorSet);

#ifdef DEBUG_LOG

    LOGD("Waiting on SnapshotThread ready message");

#endif

    err = select(max_fd,  &descriptorSet, NULL, NULL, NULL);
    if (err < 1) {
       LOGE("Error in select");
    }

    if(FD_ISSET(snapshotReadyPipe[0], &descriptorSet))
        read(snapshotReadyPipe[0], &snapshotReadyMessage, sizeof(snapshotReadyMessage));

    }

    for ( int i = 0; i < mBurstShots; i++ ) {

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS

    PPM("SENDING MESSAGE TO PROCESSING THREAD");

#endif

    mExifParams.exposure = fobj->status.ae.shutter_cap;
    mExifParams.zoom = zoom_step[mZoomTargetIdx];
    exif_buf = get_exif_buffer(&mExifParams, gpsLocation);

    if( NULL != gpsLocation ) {
        free(gpsLocation);
        gpsLocation = NULL;
    }

    procMessage[0] = PROC_THREAD_PROCESS;
    procMessage[1] = iobj->cfg.width;
    procMessage[2] = iobj->cfg.height;
    procMessage[3] = image_width;
    procMessage[4] = image_height;
    procMessage[5] = pixelFormat;

#ifdef IMAGE_PROCESSING_PIPELINE

    procMessage[6]  = mIPPParams.EdgeEnhancementStrength;
    procMessage[7]  = mIPPParams.WeakEdgeThreshold;
    procMessage[8]  = mIPPParams.StrongEdgeThreshold;
    procMessage[9]  = mIPPParams.LowFreqLumaNoiseFilterStrength;
    procMessage[10] = mIPPParams.MidFreqLumaNoiseFilterStrength;
    procMessage[11] = mIPPParams.HighFreqLumaNoiseFilterStrength;
    procMessage[12] = mIPPParams.LowFreqCbNoiseFilterStrength;
    procMessage[13] = mIPPParams.MidFreqCbNoiseFilterStrength;
    procMessage[14] = mIPPParams.HighFreqCbNoiseFilterStrength;
    procMessage[15] = mIPPParams.LowFreqCrNoiseFilterStrength;
    procMessage[16] = mIPPParams.MidFreqCrNoiseFilterStrength;
    procMessage[17] = mIPPParams.HighFreqCrNoiseFilterStrength;
    procMessage[18] = mIPPParams.shadingVertParam1;
    procMessage[19] = mIPPParams.shadingVertParam2;
    procMessage[20] = mIPPParams.shadingHorzParam1;
    procMessage[21] = mIPPParams.shadingHorzParam2;
    procMessage[22] = mIPPParams.shadingGainScale;
    procMessage[23] = mIPPParams.shadingGainOffset;
    procMessage[24] = mIPPParams.shadingGainMaxValue;
    procMessage[25] = mIPPParams.ratioDownsampleCbCr;

#endif

    procMessage[26] = (unsigned int) mYuvBuffer[i];
    procMessage[27] = mPictureOffset[i];
    procMessage[28] = mPictureLength[i];
    procMessage[29] = rotation;

    if ( mcapture_mode == 1 ) {
        procMessage[30] = mZoomTargetIdx;
    } else {
        procMessage[30] = 0;
    }

    procMessage[31] = mippMode;
    procMessage[32] = mIPPToEnable;
    procMessage[33] = quality;
    procMessage[34] = (unsigned int) mDataCb;
    procMessage[35] = 0;
    procMessage[36] = (unsigned int) mCallbackCookie;
    procMessage[37] = 0;
    procMessage[38] = 0;
    procMessage[39] = iobj->cfg.width;
    procMessage[40] = iobj->cfg.height;
    procMessage[41] = thumb_width;
    procMessage[42] = thumb_height;

    procMessage[43] = (unsigned int) exif_buf;

    write(procPipe[1], &procMessage, sizeof(procMessage));

    mIPPToEnable = false; // reset ipp enable after sending to proc thread

#ifdef DEBUG_LOG

    LOGD("\n\n\n PICTURE NUMBER =%d\n\n\n",++pictureNumber);

    PPM("IMAGE CAPTURED");

#endif

    if( msgTypeEnabled(CAMERA_MSG_RAW_IMAGE) ) {

#ifdef DEBUG_LOG

        PPM("SENDING MESSAGE TO RAW THREAD");

#endif

        rawMessage[0] = RAW_THREAD_CALL;
        rawMessage[1] = (unsigned int) mDataCb;
        rawMessage[2] = (unsigned int) mCallbackCookie;
        rawMessage[3] = (unsigned int) NULL;
        write(rawPipe[1], &rawMessage, sizeof(rawMessage));
    }
    }
#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return 0;

fail_config :
fail_process:

    return -1;
}

#else

//TODO: Update the normal in according the PPM Changes
int CameraHal::ICapturePerform(){

    int image_width, image_height;
    int image_rotation;
    double image_zoom;
	int preview_width, preview_height;
    unsigned long base, offset;
    struct v4l2_buffer buffer;
    struct v4l2_format format;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_requestbuffers creqbuf;
    sp<MemoryBase> mPictureBuffer;
    sp<MemoryBase> memBase;
    int jpegSize;
    void * outBuffer;
    sp<MemoryHeapBase>  mJPEGPictureHeap;
    sp<MemoryBase>          mJPEGPictureMemBase;
	unsigned int vppMessage[3];
    int err, i;
    int snapshot_buffer_index;
    void* snapshot_buffer;
    int ipp_reconfigure=0;
    int ippTempConfigMode;
    int jpegFormat = PIX_YUV422I;
    v4l2_streamparm parm;

    LOG_FUNCTION_NAME

    LOGD("\n\n\n PICTURE NUMBER =%d\n\n\n",++pictureNumber);

	if( ( msgTypeEnabled(CAMERA_MSG_SHUTTER) ) && mShutterEnable)
		mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

    mParameters.getPictureSize(&image_width, &image_height);
    mParameters.getPreviewSize(&preview_width, &preview_height);

    LOGD("Picture Size: Width = %d \tHeight = %d", image_width, image_height);

    image_rotation = rotation;
    image_zoom = zoom_step[mZoomTargetIdx];

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = image_width;
    format.fmt.pix.height = image_height;
    format.fmt.pix.pixelformat = PIXEL_FORMAT;

    /* set size & format of the video image */
    if (ioctl(camera_device, VIDIOC_S_FMT, &format) < 0){
        LOGE ("Failed to set VIDIOC_S_FMT.");
        return -1;
    }

    //Set 10 fps for 8MP case
    if( ( image_height == CAPTURE_8MP_HEIGHT ) && ( image_width == CAPTURE_8MP_WIDTH ) ) {
        LOGE("8MP Capture setting framerate to 10");
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        err = ioctl(camera_device, VIDIOC_G_PARM, &parm);
        if(err != 0) {
            LOGD("VIDIOC_G_PARM ");
            return -1;
        }

        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = 10;
        err = ioctl(camera_device, VIDIOC_S_PARM, &parm);
        if(err != 0) {
            LOGE("VIDIOC_S_PARM ");
            return -1;
        }
    }

    /* Check if the camera driver can accept 1 buffer */
    creqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    creqbuf.memory = V4L2_MEMORY_USERPTR;
    creqbuf.count  = 1;
    if (ioctl(camera_device, VIDIOC_REQBUFS, &creqbuf) < 0){
        LOGE ("VIDIOC_REQBUFS Failed. errno = %d", errno);
        return -1;
    }

    yuv_len = image_width * image_height * 2;
    if (yuv_len & 0xfff)
    {
        yuv_len = (yuv_len & 0xfffff000) + 0x1000;
    }
    LOGD("pictureFrameSize = 0x%x = %d", yuv_len, yuv_len);
#define ALIGMENT 1
#if ALIGMENT
    mPictureHeap = new MemoryHeapBase(yuv_len);
#else
    // Make a new mmap'ed heap that can be shared across processes.
    mPictureHeap = new MemoryHeapBase(yuv_len + 0x20 + 256);
#endif

    base = (unsigned long)mPictureHeap->getBase();

#if ALIGMENT
	base = (base + 0xfff) & 0xfffff000;
#else
    /*Align buffer to 32 byte boundary */
    while ((base & 0x1f) != 0)
    {
        base++;
    }
    /* Buffer pointer shifted to avoid DSP cache issues */
    base += 128;
#endif

    offset = base - (unsigned long)mPictureHeap->getBase();

    buffer.type = creqbuf.type;
    buffer.memory = creqbuf.memory;
    buffer.index = 0;

    if (ioctl(camera_device, VIDIOC_QUERYBUF, &buffer) < 0) {
        LOGE("VIDIOC_QUERYBUF Failed");
        return -1;
    }

    yuv_len = buffer.length;

    buffer.m.userptr = base;
    mPictureBuffer = new MemoryBase(mPictureHeap, offset, yuv_len);
    LOGD("Picture Buffer: Base = %p Offset = 0x%x", (void *)base, (unsigned int)offset);

    if (ioctl(camera_device, VIDIOC_QBUF, &buffer) < 0) {
        LOGE("CAMERA VIDIOC_QBUF Failed");
        return -1;
    }

    /* turn on streaming */
    creqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_device, VIDIOC_STREAMON, &creqbuf.type) < 0) {
        LOGE("VIDIOC_STREAMON Failed");
        return -1;
    }

    LOGD("De-queue the next avaliable buffer");

    /* De-queue the next avaliable buffer */
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

#if ALIGMENT
    cfilledbuffer.memory = creqbuf.memory;
#else
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;
#endif
    while (ioctl(camera_device, VIDIOC_DQBUF, &cfilledbuffer) < 0) {
        LOGE("VIDIOC_DQBUF Failed");
    }

    PPM("AFTER CAPTURE YUV IMAGE");

    /* turn off streaming */
    creqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_device, VIDIOC_STREAMOFF, &creqbuf.type) < 0) {
        LOGE("VIDIOC_STREAMON Failed");
        return -1;
    }

	if( msgTypeEnabled(CAMERA_MSG_RAW_IMAGE) ){
		mDataCb(CAMERA_MSG_RAW_IMAGE, mPictureBuffer,mCallbackCookie);
	}

    yuv_buffer = (uint8_t*)buffer.m.userptr;
    LOGD("PictureThread: generated a picture, yuv_buffer=%p yuv_len=%d",yuv_buffer,yuv_len);

#ifdef HARDWARE_OMX
#if VPP
#if VPP_THREAD

    LOGD("SENDING MESSAGE TO VPP THREAD \n");
    vpp_buffer =  yuv_buffer;
    vppMessage[0] = VPP_THREAD_PROCESS;
    vppMessage[1] = image_width;
    vppMessage[2] = image_height;

    write(vppPipe[1],&vppMessage,sizeof(vppMessage));

#else

    mapping_data_t* data = (mapping_data_t*) mOverlay->getBufferAddress( (void*) (lastOverlayBufferDQ) );
    if ( data == NULL ) {
        LOGE(" getBufferAddress returned NULL skipping snapshot");
    } else{
        snapshot_buffer = data->ptr;

        scale_init(image_width, image_height, preview_width, preview_height, PIX_YUV422I, PIX_YUV422I);

        err = scale_process(yuv_buffer, image_width, image_height,
                             snapshot_buffer, preview_width, preview_height, 0, PIX_YUV422I, zoom_step[/*mZoomTargetIdx*/ 0], 0, 0, image_width, image_height);

#ifdef DEBUG_LOG
       PPM("After vpp downscales:");
       if( err )
            LOGE("scale_process() failed");
       else
            LOGD("scale_process() OK");
#endif

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
        PPM("Shot to Snapshot", &ppm_receiveCmdToTakePicture);
#endif
        scale_deinit();

        queueToOverlay(lastOverlayBufferDQ);
        dequeueFromOverlay();
    }
#endif
#endif
#endif

#ifdef IMAGE_PROCESSING_PIPELINE
#if 1

	if(mippMode ==-1 ){
		mippMode=IPP_EdgeEnhancement_Mode;
	}

#else

	if(mippMode ==-1){
		mippMode=IPP_CromaSupression_Mode;
	}
	if(mippMode == IPP_CromaSupression_Mode){
		mippMode=IPP_EdgeEnhancement_Mode;
	}
	else if(mippMode == IPP_EdgeEnhancement_Mode){
		mippMode=IPP_CromaSupression_Mode;
	}

#endif

	LOGD("IPPmode=%d",mippMode);
	if(mippMode == IPP_CromaSupression_Mode){
		LOGD("IPP_CromaSupression_Mode");
	}
	else if(mippMode == IPP_EdgeEnhancement_Mode){
		LOGD("IPP_EdgeEnhancement_Mode");
	}
	else if(mippMode == IPP_Disabled_Mode){
		LOGD("IPP_Disabled_Mode");
	}

	if(mippMode){

		if(mippMode != IPP_CromaSupression_Mode && mippMode != IPP_EdgeEnhancement_Mode){
			LOGE("ERROR ippMode unsupported");
			return -1;
		}
		PPM("Before init IPP");

        if(mIPPToEnable)
        {
            err = InitIPP(image_width,image_height, jpegFormat, mippMode);
            if( err ) {
                LOGE("ERROR InitIPP() failed");
                return -1;
            }
            PPM("After IPP Init");
            mIPPToEnable = false;
        }

		err = PopulateArgsIPP(image_width,image_height, jpegFormat, mippMode);
		if( err ) {
			LOGE("ERROR PopulateArgsIPP() failed");
			return -1;
		}
		PPM("BEFORE IPP Process Buffer");

		LOGD("Calling ProcessBufferIPP(buffer=%p , len=0x%x)", yuv_buffer, yuv_len);
	err = ProcessBufferIPP(yuv_buffer, yuv_len,
                    jpegFormat,
                    mippMode,
                    -1, // EdgeEnhancementStrength
                    -1, // WeakEdgeThreshold
                    -1, // StrongEdgeThreshold
                    -1, // LowFreqLumaNoiseFilterStrength
                    -1, // MidFreqLumaNoiseFilterStrength
                    -1, // HighFreqLumaNoiseFilterStrength
                    -1, // LowFreqCbNoiseFilterStrength
                    -1, // MidFreqCbNoiseFilterStrength
                    -1, // HighFreqCbNoiseFilterStrength
                    -1, // LowFreqCrNoiseFilterStrength
                    -1, // MidFreqCrNoiseFilterStrength
                    -1, // HighFreqCrNoiseFilterStrength
                    -1, // shadingVertParam1
                    -1, // shadingVertParam2
                    -1, // shadingHorzParam1
                    -1, // shadingHorzParam2
                    -1, // shadingGainScale
                    -1, // shadingGainOffset
                    -1, // shadingGainMaxValue
                    -1); // ratioDownsampleCbCr
		if( err ) {
			LOGE("ERROR ProcessBufferIPP() failed");
			return -1;
		}
		PPM("AFTER IPP Process Buffer");

  	if(!(pIPP.ippconfig.isINPLACE)){
		yuv_buffer = pIPP.pIppOutputBuffer;
	}

	#if ( IPP_YUV422P || IPP_YUV420P_OUTPUT_YUV422I )
		jpegFormat = PIX_YUV422I;
		LOGD("YUV422 !!!!");
	#else
		yuv_len=  ((image_width * image_height *3)/2);
        jpegFormat = PIX_YUV420P;
		LOGD("YUV420 !!!!");
    #endif

	}
	//SaveFile(NULL, (char*)"yuv", yuv_buffer, yuv_len);

#endif

	if ( msgTypeEnabled(CAMERA_MSG_COMPRESSED_IMAGE) )
	{

#ifdef HARDWARE_OMX
#if JPEG
        int jpegSize = (image_width * image_height) + 12288;
        mJPEGPictureHeap = new MemoryHeapBase(jpegSize+ 256);
        outBuffer = (void *)((unsigned long)(mJPEGPictureHeap->getBase()) + 128);

        exif_buffer *exif_buf = get_exif_buffer(&mExifParams, gpsLocation);

		PPM("BEFORE JPEG Encode Image");
		LOGE(" outbuffer = 0x%x, jpegSize = %d, yuv_buffer = 0x%x, yuv_len = %d, image_width = %d, image_height = %d, quality = %d, mippMode =%d", outBuffer , jpegSize, yuv_buffer, yuv_len, image_width, image_height, quality,mippMode);
		jpegEncoder->encodeImage((uint8_t *)outBuffer , jpegSize, yuv_buffer, yuv_len,
				image_width, image_height, quality, exif_buf, jpegFormat, DEFAULT_THUMB_WIDTH, DEFAULT_THUMB_HEIGHT, image_width, image_height,
				image_rotation, image_zoom, 0, 0, image_width, image_height);
		PPM("AFTER JPEG Encode Image");

		mJPEGPictureMemBase = new MemoryBase(mJPEGPictureHeap, 128, jpegEncoder->jpegSize);

	if ( msgTypeEnabled(CAMERA_MSG_COMPRESSED_IMAGE) ){
		mDataCb(CAMERA_MSG_COMPRESSED_IMAGE,mJPEGPictureMemBase,mCallbackCookie);
    }


		PPM("Shot to Save", &ppm_receiveCmdToTakePicture);
        if((exif_buf != NULL) && (exif_buf->data != NULL))
            exif_buf_free (exif_buf);

        if( NULL != gpsLocation ) {
            free(gpsLocation);
            gpsLocation = NULL;
        }
        mJPEGPictureMemBase.clear();
        mJPEGPictureHeap.clear();

#else

	if ( msgTypeEnabled(CAMERA_MSG_COMPRESSED_IMAGE) )
		mDataCb(CAMERA_MSG_COMPRESSED_IMAGE,NULL,mCallbackCookie);

#endif

#endif

    }

    // Release constraint to DSP OPP by setting lowest Hz
    SetDSPHz(DSP3630_HZ_MIN);

    mPictureBuffer.clear();
    mPictureHeap.clear();

#ifdef HARDWARE_OMX
#if VPP_THREAD
	LOGD("CameraHal thread before waiting increment in semaphore\n");
	sem_wait(&mIppVppSem);
	LOGD("CameraHal thread after waiting increment in semaphore\n");
#endif
#endif

    PPM("END OF ICapturePerform");
    LOG_FUNCTION_NAME_EXIT

    return NO_ERROR;

}

#endif

void *CameraHal::getLastOverlayAddress()
{
    void *res = NULL;

    mapping_data_t* data = (mapping_data_t*) mOverlay->getBufferAddress( (void*) (lastOverlayBufferDQ) );
    if ( data == NULL ) {
        LOGE(" getBufferAddress returned NULL");
    } else {
        res = data->ptr;
    }

    LOG_FUNCTION_NAME_EXIT

    return res;
}

size_t CameraHal::getLastOverlayLength()
{
    size_t res = 0;

    mapping_data_t* data = (mapping_data_t*) mOverlay->getBufferAddress( (void*) (lastOverlayBufferDQ) );
    if ( data == NULL ) {
        LOGE(" getBufferAddress returned NULL");
    } else {
        res = data->length;
    }

    LOG_FUNCTION_NAME_EXIT

    return res;
}

void CameraHal::snapshotThread()
{
    fd_set descriptorSet;
    int max_fd;
    int err, status;
    unsigned int snapshotMessage[9], snapshotReadyMessage;
    int image_width, image_height, pixelFormat, preview_width, preview_height;
    int crop_top, crop_left, crop_width, crop_height;
    void *yuv_buffer, *snapshot_buffer;
    double ZoomTarget;

    LOG_FUNCTION_NAME

    pixelFormat = PIX_YUV422I;
    max_fd = snapshotPipe[0] + 1;

    FD_ZERO(&descriptorSet);
    FD_SET(snapshotPipe[0], &descriptorSet);

    while(1) {
        err = select(max_fd,  &descriptorSet, NULL, NULL, NULL);

#ifdef DEBUG_LOG

       LOGD("SNAPSHOT THREAD SELECT RECEIVED A MESSAGE\n");

#endif

       if (err < 1) {
           LOGE("Snapshot: Error in select");
       }

       if(FD_ISSET(snapshotPipe[0], &descriptorSet)){

           read(snapshotPipe[0], &snapshotMessage, sizeof(snapshotMessage));

           if(snapshotMessage[0] == SNAPSHOT_THREAD_START){

#ifdef DEBUG_LOG

                LOGD("SNAPSHOT_THREAD_START RECEIVED\n");

#endif

                yuv_buffer = (void *) snapshotMessage[1];
                image_width = snapshotMessage[2];
                image_height = snapshotMessage[3];
                ZoomTarget = zoom_step[snapshotMessage[4]];
                crop_left = snapshotMessage[5];
                crop_top = snapshotMessage[6];
                crop_width = snapshotMessage[7];
                crop_height = snapshotMessage[8];

                mParameters.getPreviewSize(&preview_width, &preview_height);

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS

                PPM("Before vpp downscales:");

#endif

                snapshot_buffer = getLastOverlayAddress();
                if ( NULL == snapshot_buffer )
                    continue;

                LOGE("Snapshot buffer 0x%x, yuv_buffer = 0x%x, zoomTarget = %5.2f", ( unsigned int ) snapshot_buffer, ( unsigned int ) yuv_buffer, ZoomTarget);

                status = scale_init(image_width, image_height, preview_width, preview_height, PIX_YUV422I, PIX_YUV422I);

                if ( status < 0 ) {
                    LOGE("VPP init failed");
                    goto EXIT;
                }

                status = scale_process(yuv_buffer, image_width, image_height,
                         snapshot_buffer, preview_width, preview_height, 0, PIX_YUV422I, zoom_step[0], crop_top, crop_left, crop_width, crop_height);

#ifdef DEBUG_LOG

               PPM("After vpp downscales:");

               if( status )
                   LOGE("scale_process() failed");
               else
                   LOGD("scale_process() OK");

#endif

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS

               PPM("Shot to Snapshot", &ppm_receiveCmdToTakePicture);

#endif

                scale_deinit();

                queueToOverlay(lastOverlayBufferDQ);
                dequeueFromOverlay();

EXIT:

                write(snapshotReadyPipe[1], &snapshotReadyMessage, sizeof(snapshotReadyMessage));
          } else if (snapshotMessage[0] == SNAPSHOT_THREAD_START_GEN) {

                queueToOverlay(lastOverlayBufferDQ);
                mParameters.getPreviewSize(&preview_width, &preview_height);

#ifdef DUMP_SNAPSHOT
                SaveFile(NULL, (char*)"snp", getLastOverlayAddress(), preview_width*preview_height*2);
#endif

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS

               PPM("Shot to Snapshot", &ppm_receiveCmdToTakePicture);

#endif
                dequeueFromOverlay();
                write(snapshotReadyPipe[1], &snapshotReadyMessage, sizeof(snapshotReadyMessage));

          } else if (snapshotMessage[0] == SNAPSHOT_THREAD_EXIT) {
                LOGD("SNAPSHOT_THREAD_EXIT RECEIVED");

                break;
          }
        }
    }

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::rawThread()
{
    LOG_FUNCTION_NAME

    fd_set descriptorSet;
    int max_fd;
    int err;
    unsigned int rawMessage[RAW_THREAD_NUM_ARGS];
    data_callback RawCallback;
    void *PictureCallbackCookie;
    sp<MemoryBase> rawData;

    max_fd = rawPipe[0] + 1;

    FD_ZERO(&descriptorSet);
    FD_SET(rawPipe[0], &descriptorSet);

    while(1) {
        err = select(max_fd,  &descriptorSet, NULL, NULL, NULL);

#ifdef DEBUG_LOG

            LOGD("RAW THREAD SELECT RECEIVED A MESSAGE\n");

#endif

            if (err < 1) {
                LOGE("Raw: Error in select");
            }

            if(FD_ISSET(rawPipe[0], &descriptorSet)){

                read(rawPipe[0], &rawMessage, sizeof(rawMessage));

                if(rawMessage[0] == RAW_THREAD_CALL){

#ifdef DEBUG_LOG

                LOGD("RAW_THREAD_CALL RECEIVED\n");

#endif

                RawCallback = (data_callback) rawMessage[1];
                PictureCallbackCookie = (void *) rawMessage[2];
                rawData = (MemoryBase *) rawMessage[3];

                RawCallback(CAMERA_MSG_RAW_IMAGE, rawData, PictureCallbackCookie);

#ifdef DEBUG_LOG

                PPM("RAW CALLBACK CALLED");

#endif

            } else if (rawMessage[0] == RAW_THREAD_EXIT) {
                LOGD("RAW_THREAD_EXIT RECEIVED");

                break;
            }
        }
    }

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::shutterThread()
{
    LOG_FUNCTION_NAME

    fd_set descriptorSet;
    int max_fd;
    int err;
    unsigned int shutterMessage[SHUTTER_THREAD_NUM_ARGS];
    notify_callback ShutterCallback;
    void *PictureCallbackCookie;

    max_fd = shutterPipe[0] + 1;

    FD_ZERO(&descriptorSet);
    FD_SET(shutterPipe[0], &descriptorSet);

    while(1) {
        err = select(max_fd,  &descriptorSet, NULL, NULL, NULL);

#ifdef DEBUG_LOG

        LOGD("SHUTTER THREAD SELECT RECEIVED A MESSAGE\n");

#endif

        if (err < 1) {
            LOGE("Shutter: Error in select");
        }

        if(FD_ISSET(shutterPipe[0], &descriptorSet)){

           read(shutterPipe[0], &shutterMessage, sizeof(shutterMessage));

           if(shutterMessage[0] == SHUTTER_THREAD_CALL){

#ifdef DEBUG_LOG

                LOGD("SHUTTER_THREAD_CALL_RECEIVED\n");

#endif

                ShutterCallback = (notify_callback) shutterMessage[1];
                PictureCallbackCookie = (void *) shutterMessage[2];

                ShutterCallback(CAMERA_MSG_SHUTTER, 0, 0, PictureCallbackCookie);

#ifdef DEBUG_LOG

                PPM("CALLED SHUTTER CALLBACK");

#endif

            } else if (shutterMessage[0] == SHUTTER_THREAD_EXIT) {
                LOGD("SHUTTER_THREAD_EXIT RECEIVED");

                break;
            }
        }
    }

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::procThread()
{
    LOG_FUNCTION_NAME

    int status;
    int capture_width, capture_height, image_width, image_height;
    fd_set descriptorSet;
    int max_fd;
    int err;
    int pixelFormat;
    unsigned int procMessage [PROC_THREAD_NUM_ARGS];
    unsigned int jpegQuality, jpegSize, size, base, tmpBase, offset, yuv_offset, yuv_len, image_rotation, ippMode;
    unsigned int crop_top, crop_left, crop_width, crop_height;
    double image_zoom;
    bool ipp_to_enable;
    sp<MemoryHeapBase> JPEGPictureHeap;
    sp<MemoryBase> JPEGPictureMemBase;
    data_callback RawPictureCallback;
    data_callback JpegPictureCallback;
    void *yuv_buffer, *outBuffer, *PictureCallbackCookie;
    int thumb_width, thumb_height;

#ifdef HARDWARE_OMX

    exif_buffer *exif_buf;

#endif

    unsigned short EdgeEnhancementStrength, WeakEdgeThreshold, StrongEdgeThreshold,
                   LowFreqLumaNoiseFilterStrength, MidFreqLumaNoiseFilterStrength, HighFreqLumaNoiseFilterStrength,
                   LowFreqCbNoiseFilterStrength, MidFreqCbNoiseFilterStrength, HighFreqCbNoiseFilterStrength,
                   LowFreqCrNoiseFilterStrength, MidFreqCrNoiseFilterStrength, HighFreqCrNoiseFilterStrength,
                   shadingVertParam1, shadingVertParam2, shadingHorzParam1, shadingHorzParam2, shadingGainScale,
                   shadingGainOffset, shadingGainMaxValue, ratioDownsampleCbCr;

    void* input_buffer;
    unsigned int input_length;

    max_fd = procPipe[0] + 1;

    FD_ZERO(&descriptorSet);
    FD_SET(procPipe[0], &descriptorSet);

    mJPEGLength  = MAX_THUMB_WIDTH*MAX_THUMB_HEIGHT + PICTURE_WIDTH*PICTURE_HEIGHT + ((2*PAGE) - 1);
    mJPEGLength &= ~((2*PAGE) - 1);
    mJPEGLength  += 2*PAGE;
    JPEGPictureHeap = new MemoryHeapBase(mJPEGLength);

    while(1){

        err = select(max_fd,  &descriptorSet, NULL, NULL, NULL);

#ifdef DEBUG_LOG

        LOGD("PROCESSING THREAD SELECT RECEIVED A MESSAGE\n");

#endif

        if (err < 1) {
            LOGE("Proc: Error in select");
        }

        if(FD_ISSET(procPipe[0], &descriptorSet)){

            read(procPipe[0], &procMessage, sizeof(procMessage));

            if(procMessage[0] == PROC_THREAD_PROCESS){

#ifdef DEBUG_LOG

                LOGD("PROC_THREAD_PROCESS_RECEIVED\n");

#endif

                capture_width = procMessage[1];
                capture_height = procMessage[2];
                image_width = procMessage[3];
                image_height = procMessage[4];
                pixelFormat = procMessage[5];
                EdgeEnhancementStrength = procMessage[6];
                WeakEdgeThreshold = procMessage[7];
                StrongEdgeThreshold = procMessage[8];
                LowFreqLumaNoiseFilterStrength = procMessage[9];
                MidFreqLumaNoiseFilterStrength = procMessage[10];
                HighFreqLumaNoiseFilterStrength = procMessage[11];
                LowFreqCbNoiseFilterStrength = procMessage[12];
                MidFreqCbNoiseFilterStrength = procMessage[13];
                HighFreqCbNoiseFilterStrength = procMessage[14];
                LowFreqCrNoiseFilterStrength = procMessage[15];
                MidFreqCrNoiseFilterStrength = procMessage[16];
                HighFreqCrNoiseFilterStrength = procMessage[17];
                shadingVertParam1 = procMessage[18];
                shadingVertParam2 = procMessage[19];
                shadingHorzParam1 = procMessage[20];
                shadingHorzParam2 = procMessage[21];
                shadingGainScale = procMessage[22];
                shadingGainOffset = procMessage[23];
                shadingGainMaxValue = procMessage[24];
                ratioDownsampleCbCr = procMessage[25];
                yuv_buffer = (void *) procMessage[26];
                yuv_offset =  procMessage[27];
                yuv_len = procMessage[28];
                image_rotation = procMessage[29];
                image_zoom = zoom_step[procMessage[30]];
                ippMode = procMessage[31];
                ipp_to_enable = procMessage[32];
                jpegQuality = procMessage[33];
                JpegPictureCallback = (data_callback) procMessage[34];
                RawPictureCallback = (data_callback) procMessage[35];
                PictureCallbackCookie = (void *) procMessage[36];
                crop_left = procMessage[37];
                crop_top = procMessage[38];
                crop_width = procMessage[39];
                crop_height = procMessage[40];
                thumb_width = procMessage[41];
                thumb_height = procMessage[42];

#ifdef HARDWARE_OMX

                exif_buf = (exif_buffer *) procMessage[43];

#endif

                LOGD("JPEGPictureHeap->getStrongCount() = %d, base = 0x%x", JPEGPictureHeap->getStrongCount(), ( unsigned int ) JPEGPictureHeap->getBase());
                jpegSize = mJPEGLength;

                if(JPEGPictureHeap->getStrongCount() > 1 )
                {
                    JPEGPictureHeap.clear();
                    JPEGPictureHeap = new MemoryHeapBase(jpegSize);
                }

                base = (unsigned long) JPEGPictureHeap->getBase();
                base = (base + 0xfff) & 0xfffff000;
                offset = base - (unsigned long) JPEGPictureHeap->getBase();
                outBuffer = (void *) base;

                pixelFormat = PIX_YUV422I;

                input_buffer = yuv_buffer;
                input_length = yuv_len;

#ifdef IMAGE_PROCESSING_PIPELINE

#ifdef DEBUG_LOG

                LOGD("IPPmode=%d",ippMode);
                if(ippMode == IPP_CromaSupression_Mode){
                    LOGD("IPP_CromaSupression_Mode");
                }
                else if(ippMode == IPP_EdgeEnhancement_Mode){
                    LOGD("IPP_EdgeEnhancement_Mode");
                }
                else if(ippMode == IPP_Disabled_Mode){
                    LOGD("IPP_Disabled_Mode");
                }

#endif

               if( (ippMode == IPP_CromaSupression_Mode) || (ippMode == IPP_EdgeEnhancement_Mode) ){

                    if(ipp_to_enable) {

#ifdef DEBUG_LOG

                         PPM("Before init IPP");

#endif

                         err = InitIPP(capture_width, capture_height, pixelFormat, ippMode);
                         if( err )
                             LOGE("ERROR InitIPP() failed");

#ifdef DEBUG_LOG
                             PPM("After IPP Init");
#endif

                     }

                   err = PopulateArgsIPP(capture_width, capture_height, pixelFormat, ippMode);
                    if( err )
                         LOGE("ERROR PopulateArgsIPP() failed");

#ifdef DEBUG_LOG
                     PPM("BEFORE IPP Process Buffer");
                     LOGD("Calling ProcessBufferIPP(buffer=%p , len=0x%x)", yuv_buffer, yuv_len);
#endif
                    // TODO: Need to add support for new EENF 1.9 parameters from proc messages
                     err = ProcessBufferIPP(input_buffer, input_length,
                            pixelFormat,
                            ippMode,
                            EdgeEnhancementStrength,
                            WeakEdgeThreshold,
                            StrongEdgeThreshold,
                            LowFreqLumaNoiseFilterStrength,
                            MidFreqLumaNoiseFilterStrength,
                            HighFreqLumaNoiseFilterStrength,
                            LowFreqCbNoiseFilterStrength,
                            MidFreqCbNoiseFilterStrength,
                            HighFreqCbNoiseFilterStrength,
                            LowFreqCrNoiseFilterStrength,
                            MidFreqCrNoiseFilterStrength,
                            HighFreqCrNoiseFilterStrength,
                            shadingVertParam1,
                            shadingVertParam2,
                            shadingHorzParam1,
                            shadingHorzParam2,
                            shadingGainScale,
                            shadingGainOffset,
                            shadingGainMaxValue,
                            ratioDownsampleCbCr);
                    if( err )
                         LOGE("ERROR ProcessBufferIPP() failed");

#ifdef DEBUG_LOG
                    PPM("AFTER IPP Process Buffer");
#endif
                    pixelFormat = PIX_YUV422I; //output of IPP is always 422I
                    if(!(pIPP.ippconfig.isINPLACE)){
                        input_buffer = pIPP.pIppOutputBuffer;
                        input_length = pIPP.outputBufferSize;
                    }
               }
#endif

#if JPEG
                err = 0;

#ifdef DEBUG_LOG
                LOGD(" outbuffer = %p, jpegSize = %d, input_buffer = %p, yuv_len = %d, image_width = %d, image_height = %d, quality = %d, ippMode =%d", outBuffer , jpegSize, input_buffer/*yuv_buffer*/, input_length/*yuv_len*/, image_width, image_height, jpegQuality, ippMode);
#endif
                //workaround for thumbnail size  - it should be smaller than captured image
                if ((image_width<thumb_width) || (image_height<thumb_width) ||
                    (image_width<thumb_height) || (image_height<thumb_height)) {
                     thumb_width = MIN_THUMB_WIDTH;
                     thumb_height = MIN_THUMB_HEIGHT;
                }

#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
                PPM("BEFORE JPEG Encode Image");
#endif
                if (!( jpegEncoder->encodeImage((uint8_t *)outBuffer , jpegSize, input_buffer, input_length,
                                             capture_width, capture_height, jpegQuality, exif_buf, pixelFormat, thumb_width, thumb_height, image_width, image_height,
                                             image_rotation, image_zoom, crop_top, crop_left, crop_width, crop_height)))
                {
                    err = -1;
                    LOGE("JPEG Encoding failed");
                }
#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
                PPM("AFTER JPEG Encode Image");
                if ( 0 != image_rotation )
                    PPM("Shot to JPEG with %d deg rotation", &ppm_receiveCmdToTakePicture, image_rotation);
                else
                    PPM("Shot to JPEG", &ppm_receiveCmdToTakePicture);
#endif

                JPEGPictureMemBase = new MemoryBase(JPEGPictureHeap, offset, jpegEncoder->jpegSize);
#endif
                /* Disable the jpeg message enabled check for now */
                if(/*JpegPictureCallback*/ true) {

#if JPEG

                    JpegPictureCallback(CAMERA_MSG_COMPRESSED_IMAGE, JPEGPictureMemBase, PictureCallbackCookie);

#else

                    JpegPictureCallback(CAMERA_MSG_COMPRESSED_IMAGE, NULL, PictureCallbackCookie);

#endif

                }

#ifdef DEBUG_LOG

                LOGD("jpegEncoder->jpegSize=%d jpegSize=%d", jpegEncoder->jpegSize, jpegSize);

#endif

#ifdef HARDWARE_OMX

                if((exif_buf != NULL) && (exif_buf->data != NULL))
                    exif_buf_free(exif_buf);

#endif

                JPEGPictureMemBase.clear();
                free((void *) ( ((unsigned int) yuv_buffer) - yuv_offset) );

                // Release constraint to DSP OPP by setting lowest Hz
                SetDSPHz(DSP3630_HZ_MIN);

            } else if( procMessage[0] == PROC_THREAD_EXIT ) {
                LOGD("PROC_THREAD_EXIT_RECEIVED");
                JPEGPictureHeap.clear();
                break;
            }
        }
    }

    JPEGPictureHeap.clear();

    LOG_FUNCTION_NAME_EXIT
}

#ifdef ICAP_EXPERIMENTAL

int CameraHal::allocatePictureBuffer(size_t length, int burstCount)
{
    unsigned int base, tmpBase;

    length  += ((2*PAGE) - 1) + 10*PAGE;
    length &= ~((2*PAGE) - 1);
    length  += 2*PAGE;

    //allocate new buffers
    for ( int i = 0; i < burstCount; i++) {
        base = (unsigned int) malloc(length);
        if ( ((void *) base ) == NULL )
            return -1;

        tmpBase = base;
        base = (base + 0xfff) & 0xfffff000;
        mPictureOffset[i] = base - tmpBase;
        mYuvBuffer[i] = (uint8_t *) base;
        mPictureLength[i] = length - mPictureOffset[i];
    }

    return NO_ERROR;
}

#else

int CameraHal::allocatePictureBuffer(int width, int height, int burstCount)
{
    unsigned int base, tmpBase, length;

    length  = width*height*2 + ((2*PAGE) - 1) + 10*PAGE;
    length &= ~((2*PAGE) - 1);
    length  += 2*PAGE;

    //allocate new buffers
    for ( int i = 0; i < burstCount; i++) {
        base = (unsigned int) malloc(length);
        if ( ((void *) base ) == NULL )
            return -1;

    tmpBase = base;
    base = (base + 0xfff) & 0xfffff000;
        mPictureOffset[i] = base - tmpBase;
        mYuvBuffer[i] = (uint8_t *) base;
        mPictureLength[i] = length - mPictureOffset[i];
    }

    return NO_ERROR;
}

#endif

int CameraHal::ICaptureCreate(void)
{
    int res;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    res = 0;

#ifdef ICAP

    iobj = (libtest_obj *) malloc( sizeof( *iobj));
    if( NULL == iobj) {
        LOGE("libtest_obj malloc failed");
        goto exit;
    }

    memset(iobj, 0 , sizeof(*iobj));

    res = icap_create(&iobj->lib_private);
    if ( ICAP_STATUS_FAIL == res) {
        LOGE("ICapture Create function failed");
        goto fail_icapture;
    }

    LOGD("ICapture create OK");

#endif

#ifdef HARDWARE_OMX
#ifdef IMAGE_PROCESSING_PIPELINE

	mippMode = IPP_Disabled_Mode;

#endif

#if JPEG

    jpegEncoder = new JpegEncoder;

    if( NULL != jpegEncoder )
        isStart_JPEG = true;

#endif
#endif

    LOG_FUNCTION_NAME_EXIT
    return res;

fail_jpeg_buffer:
fail_yuv_buffer:
fail_init:

#ifdef HARDWARE_OMX
#if JPEG

    delete jpegEncoder;

#endif
#endif

#ifdef ICAP

   icap_destroy(iobj->lib_private);

#endif

fail_icapture:
exit:
    return -1;
}

int CameraHal::ICaptureDestroy(void)
{
    int err;

#ifdef HARDWARE_OMX
#if JPEG

    if( isStart_JPEG )
    {
        isStart_JPEG = false;
        delete jpegEncoder;
        jpegEncoder = NULL;
    }
#endif
#endif

#ifdef ICAP
    if(iobj != NULL)
    {
        err = icap_destroy(iobj->lib_private);
        if ( ICAP_STATUS_FAIL == err )
            LOGE("ICapture Delete failed");
        else
            LOGD("ICapture delete OK");

        free(iobj);
        iobj = NULL;
    }

#endif

    return 0;
}

status_t CameraHal::setOverlay(const sp<Overlay> &overlay)
{
    Mutex::Autolock lock(mLock);
    int w,h;

    LOGD("CameraHal setOverlay/1/%08lx/%08lx", (long unsigned int)overlay.get(), (long unsigned int)mOverlay.get());
    // De-alloc any stale data
    if ( mOverlay.get() != NULL )
    {
        LOGD("Destroying current overlay");

        int buffer_count = mOverlay->getBufferCount();
        for(int i =0; i < buffer_count ; i++){
            // need to free buffers and heaps mapped using overlay fd before it is destroyed
            // otherwise we could create a resource leak
            // a segfault could occur if we try to free pointers after overlay is destroyed
            mPreviewBuffers[i].clear();
            mPreviewHeaps[i].clear();
            mVideoBuffer[i].clear();
            mVideoHeaps[i].clear();
            buffers_queued_to_dss[i] = 0;
        }

        mOverlay->destroy();
        nOverlayBuffersQueued = 0;
    }

    mOverlay = overlay;
    if (mOverlay == NULL)
    {
        LOGE("Trying to set overlay, but overlay is null!, line:%d",__LINE__);
        return NO_ERROR;
    }

    mParameters.getPreviewSize(&w, &h);
    if ((w == RES_720P) || (h == RES_720P))
    {
        mOverlay->setParameter(CACHEABLE_BUFFERS, 1);
        mOverlay->setParameter(MAINTAIN_COHERENCY, 0);
        mOverlay->resizeInput(w, h);
    }

    if (mFalsePreview)   // Eclair HAL
    {
     // Restart the preview (Only for Overlay Case)
	//LOGD("In else overlay");
        mPreviewRunning = false;
        Message msg;
        msg.command = PREVIEW_START;
        previewThreadCommandQ.put(&msg);
        previewThreadAckQ.get(&msg);
    }	// Eclair HAL

    LOG_FUNCTION_NAME_EXIT

    return NO_ERROR;
}

status_t CameraHal::startPreview()
{
    LOG_FUNCTION_NAME

    if(mOverlay == NULL)	//Eclair HAL
    {
	    LOGD("Return from camera Start Preview");
	    mPreviewRunning = true;
	    mFalsePreview = true;
	    return NO_ERROR;
    }      //Eclair HAL

    mFalsePreview = false;   //Eclair HAL

    Message msg;
    msg.command = PREVIEW_START;
    previewThreadCommandQ.put(&msg);
    previewThreadAckQ.get(&msg);

    LOG_FUNCTION_NAME_EXIT
    return msg.command == PREVIEW_ACK ? NO_ERROR : INVALID_OPERATION;
}

void CameraHal::stopPreview()
{
    LOG_FUNCTION_NAME

    mFalsePreview = false;  //Eclair HAL
    Message msg;
    msg.command = PREVIEW_STOP;
    previewThreadCommandQ.put(&msg);
    previewThreadAckQ.get(&msg);
}


status_t CameraHal::autoFocus()
{
    LOG_FUNCTION_NAME

    Message msg;
    msg.command = PREVIEW_AF_START;
    previewThreadCommandQ.put(&msg);
    previewThreadAckQ.get(&msg);

    LOG_FUNCTION_NAME_EXIT
    return msg.command == PREVIEW_ACK ? NO_ERROR : INVALID_OPERATION;
}

bool CameraHal::previewEnabled()
{
    return mPreviewRunning;
}


status_t CameraHal::startRecording( )
{
    LOG_FUNCTION_NAME
    int w,h;
    int i = 0;

    mPrevTime = systemTime(SYSTEM_TIME_MONOTONIC);
    int framerate = mParameters.getPreviewFrameRate();
    frameInterval = 1000000000LL / framerate ;
    mParameters.getPreviewSize(&w, &h);
    mRecordingFrameSize = w * h * 2;
    overlay_handle_t overlayhandle = mOverlay->getHandleRef();
    overlay_true_handle_t true_handle;
    if ( overlayhandle == NULL ) {
        LOGD("overlayhandle is received as NULL. ");
        return UNKNOWN_ERROR;
    }

    memcpy(&true_handle,overlayhandle,sizeof(overlay_true_handle_t));
    int overlayfd = true_handle.ctl_fd;
    LOGD("#Overlay driver FD:%d ",overlayfd);

    mVideoBufferCount =  mOverlay->getBufferCount();

    if (mVideoBufferCount > VIDEO_FRAME_COUNT_MAX)
    {
        LOGD("Error: mVideoBufferCount > VIDEO_FRAME_COUNT_MAX");
        return UNKNOWN_ERROR;
    }

    mRecordingLock.lock();

    for(i = 0; i < mVideoBufferCount; i++)
    {
        mVideoHeaps[i].clear();
        mVideoBuffer[i].clear();
        buffers_queued_to_ve[i] = 0;
    }

    for(i = 0; i < mVideoBufferCount; i++)
    {
        mapping_data_t* data = (mapping_data_t*) mOverlay->getBufferAddress((void*)i);
        // make sure data if valid, if not clear all previously allocated memory and return
        if(data != NULL)
        {
            mVideoHeaps[i] = new MemoryHeapBase(data->fd,mPreviewFrameSize, 0, data->offset);
            mVideoBuffer[i] = new MemoryBase(mVideoHeaps[i], 0, mRecordingFrameSize);
            LOGV("mVideoHeaps[%d]: ID:%d,Base:[%p],size:%d", i, mVideoHeaps[i]->getHeapID(), mVideoHeaps[i]->getBase() ,mVideoHeaps[i]->getSize());
            LOGV("mVideoBuffer[%d]: Pointer[%p]", i, mVideoBuffer[i]->pointer());
        } else{
            for(int j = 0; j < i+1; j++)
            {
                mVideoHeaps[j].clear();
                mVideoBuffer[j].clear();
                buffers_queued_to_ve[j] = 0;
            }
            LOGD("Error: data from overlay returned null");
            return UNKNOWN_ERROR;
        }
    }

    mRecordEnabled =true;
    mRecordingLock.unlock();
    return NO_ERROR;
}

void CameraHal::stopRecording()
{
    LOG_FUNCTION_NAME
    mRecordingLock.lock();
    mRecordEnabled = false;

    for(int i = 0; i < MAX_CAMERA_BUFFERS; i ++)
    {
        if (buffers_queued_to_ve[i] == 1)
        {
            if (ioctl(camera_device, VIDIOC_QBUF, &v4l2_cam_buffer[i]) < 0) {
                LOGE("VIDIOC_QBUF Failed. line=%d",__LINE__);
            }
            else nCameraBuffersQueued++;
            buffers_queued_to_ve[i] = 0;
            LOGD("Buffer #%d was not returned by VE. Reclaiming it !!!!!!!!!!!!!!!!!!!!!!!!", i);
        }
    }

    mRecordingLock.unlock();
}

bool CameraHal::recordingEnabled()
{
    LOG_FUNCTION_NAME
    return (mRecordEnabled);
}
void CameraHal::releaseRecordingFrame(const sp<IMemory>& mem)
{
//LOG_FUNCTION_NAME
    int index;

    for(index = 0; index <mVideoBufferCount; index ++){
        if(mem->pointer() == mVideoBuffer[index]->pointer()) {
            break;
        }
    }

    if (index == mVideoBufferCount)
    {
        LOGD("Encoder returned wrong buffer address");
        return;
    }

    debugShowFPS();

    if (ioctl(camera_device, VIDIOC_QBUF, &v4l2_cam_buffer[index]) < 0)
    {
        LOGE("VIDIOC_QBUF Failed, index [%d] line=%d",index,__LINE__);
    }
    else
    {
        nCameraBuffersQueued++;
    }

    return;
}

sp<IMemoryHeap>  CameraHal::getRawHeap() const
{
    return mPictureHeap;
}


status_t CameraHal::takePicture( )
{
    LOG_FUNCTION_NAME

    Message msg;
    msg.command = PREVIEW_CAPTURE;
    previewThreadCommandQ.put(&msg);
    previewThreadAckQ.get(&msg);

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return NO_ERROR;
}

status_t CameraHal::cancelPicture( )
{
    LOG_FUNCTION_NAME
	disableMsgType(CAMERA_MSG_RAW_IMAGE);
	disableMsgType(CAMERA_MSG_COMPRESSED_IMAGE);
//	mCallbackCookie = NULL;   // done in destructor

    LOGE("Callbacks set to null");
    return -1;
}

status_t CameraHal::convertGPSCoord(double coord, int *deg, int *min, int *sec)
{
    double tmp;

    LOG_FUNCTION_NAME

    if ( coord == 0 ) {

        LOGE("Invalid GPS coordinate");

        return EINVAL;
    }

    *deg = (int) floor(coord);
    tmp = ( coord - floor(coord) )*60;
    *min = (int) floor(tmp);
    tmp = ( tmp - floor(tmp) )*60;
    *sec = (int) floor(tmp);

    if( *sec >= 60 ) {
        *sec = 0;
        *min += 1;
    }

    if( *min >= 60 ) {
        *min = 0;
        *deg += 1;
    }

    LOG_FUNCTION_NAME_EXIT

    return NO_ERROR;
}

status_t CameraHal::setParameters(const CameraParameters &params)
{
    LOG_FUNCTION_NAME

    int w, h;
    int w_orig, h_orig, rot_orig;
    int framerate;
    int zoom, compensation, saturation, sharpness;
    int zoom_save;
    int contrast, brightness, caf;
	int error;
	int base;
    const char *valstr;
    char *af_coord;
    Message msg;

    Mutex::Autolock lock(mLock);

    LOGD("PreviewFormat %s", params.getPreviewFormat());

    if ( params.getPreviewFormat() != NULL ) {
        if (strcmp(params.getPreviewFormat(), (const char *) CameraParameters::PIXEL_FORMAT_YUV422I) != 0) {
            LOGE("Only yuv422i preview is supported");
            return -EINVAL;
        }
    }

    LOGD("PictureFormat %s", params.getPictureFormat());
    if ( params.getPictureFormat() != NULL ) {
        if (strcmp(params.getPictureFormat(), (const char *) CameraParameters::PIXEL_FORMAT_JPEG) != 0) {
            LOGE("Only jpeg still pictures are supported");
            return -EINVAL;
        }
    }

    params.getPreviewSize(&w, &h);
    if ( validateSize(w, h, supportedPreviewRes, ARRAY_SIZE(supportedPreviewRes)) == false ) {
        LOGE("Preview size not supported");
        return -EINVAL;
    }
    LOGD("PreviewResolution by App %d x %d", w, h);

    params.getPictureSize(&w, &h);
    if (validateSize(w, h, supportedPictureRes, ARRAY_SIZE(supportedPictureRes)) == false ) {
        LOGE("Picture size not supported");
        return -EINVAL;
    }
    LOGD("Picture Size by App %d x %d", w, h);

#ifdef HARDWARE_OMX

    mExifParams.width = w;
    mExifParams.height = h;

#endif

    framerate = params.getPreviewFrameRate();
    LOGD("FRAMERATE %d", framerate);

    rot_orig = rotation;
    rotation = params.getInt(CameraParameters::KEY_ROTATION);

    mParameters.getPictureSize(&w_orig, &h_orig);
    zoom_save = mParameters.getInt(CameraParameters::KEY_ZOOM);
    mParameters = params;

#ifdef IMAGE_PROCESSING_PIPELINE

    if((mippMode != mParameters.getInt(KEY_IPP)) || (w != w_orig) || (h != h_orig) ||
            ((rot_orig != 180) && (rotation == 180)) ||
            ((rot_orig == 180) && (rotation != 180))) // in the current setup, IPP uses a different setup when rotation is 180 degrees
    {
        if(pIPP.hIPP != NULL){
            LOGD("pIPP.hIPP=%p", pIPP.hIPP);
            if(DeInitIPP(mippMode)) // deinit here to save time
                LOGE("ERROR DeInitIPP() failed");
            pIPP.hIPP = NULL;
        }

        mippMode = mParameters.getInt(KEY_IPP);
        LOGD("mippMode=%d", mippMode);

        mIPPToEnable = true;
    }

#endif

	mParameters.getPictureSize(&w, &h);
	LOGD("Picture Size by CamHal %d x %d", w, h);

	mParameters.getPreviewSize(&w, &h);
	LOGD("Preview Resolution by CamHal %d x %d", w, h);

    quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
    if ( ( quality < 0 ) || (quality > 100) ){
        quality = 100;
    }

    zoom = mParameters.getInt(CameraParameters::KEY_ZOOM);
    if( (zoom >= 0) && ( zoom < ZOOM_STAGES) ){
        // immediate zoom
        mZoomSpeed = 0;
        mZoomTargetIdx = zoom;
    } else if(zoom>= ZOOM_STAGES){
        mParameters.set(CameraParameters::KEY_ZOOM, zoom_save);
        return -EINVAL;
    } else {
        mZoomTargetIdx = 0;
    }
    LOGD("Zoom by App %d", zoom);

#ifdef HARDWARE_OMX

    mExifParams.zoom = zoom;

#endif

    if ( ( params.get(CameraParameters::KEY_GPS_LATITUDE) != NULL ) && ( params.get(CameraParameters::KEY_GPS_LONGITUDE) != NULL ) && ( params.get(CameraParameters::KEY_GPS_ALTITUDE) != NULL )) {

        double gpsCoord;
        struct tm* timeinfo;

#ifdef HARDWARE_OMX

        if( NULL == gpsLocation )
            gpsLocation = (gps_data *) malloc( sizeof(gps_data));

        if( NULL != gpsLocation ) {
            LOGE("initializing gps_data structure");

            memset(gpsLocation, 0, sizeof(gps_data));
            gpsLocation->datestamp[0] = '\0';

            gpsCoord = strtod( params.get(CameraParameters::KEY_GPS_LATITUDE), NULL);
            convertGPSCoord(gpsCoord, &gpsLocation->latDeg, &gpsLocation->latMin, &gpsLocation->latSec);
            gpsLocation->latRef = (gpsCoord < 0) ? (char*) "S" : (char*) "N";

            gpsCoord = strtod( params.get(CameraParameters::KEY_GPS_LONGITUDE), NULL);
            convertGPSCoord(gpsCoord, &gpsLocation->longDeg, &gpsLocation->longMin, &gpsLocation->longSec);
            gpsLocation->longRef = (gpsCoord < 0) ? (char*) "W" : (char*) "E";

            gpsCoord = strtod( params.get(CameraParameters::KEY_GPS_ALTITUDE), NULL);
            gpsLocation->altitude = gpsCoord;

            if ( NULL != params.get(CameraParameters::KEY_GPS_TIMESTAMP) ){
                gpsLocation->timestamp = strtol( params.get(CameraParameters::KEY_GPS_TIMESTAMP), NULL, 10);
                timeinfo = localtime((time_t*)&(gpsLocation->timestamp));
                if(timeinfo != NULL)
                    strftime(gpsLocation->datestamp, 11, "%Y:%m:%d", timeinfo);
            }

            gpsLocation->altitudeRef = params.getInt(KEY_GPS_ALTITUDE_REF);
            gpsLocation->mapdatum = (char *) params.get(KEY_GPS_MAPDATUM);
            gpsLocation->versionId = (char *) params.get(KEY_GPS_VERSION);
            gpsLocation->procMethod = (char *) params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);

        } else {
            LOGE("Not enough memory to allocate gps_data structure");
        }

#endif

    }

    if ( params.get(KEY_SHUTTER_ENABLE) != NULL ) {
        if ( strcmp(params.get(KEY_SHUTTER_ENABLE), (const char *) "true") == 0 )
            mShutterEnable = true;
        else if ( strcmp(params.get(KEY_SHUTTER_ENABLE), (const char *) "false") == 0 )
            mShutterEnable = false;
    }

#ifdef FW3A

    if ( params.get(KEY_CAPTURE_MODE) != NULL ) {
        if (strcmp(params.get(KEY_CAPTURE_MODE), (const char *) HIGH_QUALITY) == 0) {
            mcapture_mode = 2;
        } else if (strcmp(params.get(KEY_CAPTURE_MODE), (const char *) HIGH_PERFORMANCE) == 0) {
            mcapture_mode = 1;
        } else {
            mcapture_mode = 1;
        }
    } else {
        mcapture_mode = 1;
    }

    int burst_capture = params.getInt(KEY_BURST);
    if ( ( 0 >= burst_capture ) ){
        mBurstShots = 1;
    } else {
        // hardcoded in HP mode
        mcapture_mode = 1;
        mBurstShots = burst_capture;
    }

    LOGD("Capture Mode set %d, Burst Shots set %d", mcapture_mode, burst_capture);
    LOGD("mBurstShots %d", mBurstShots);

    if ( NULL != fobj ){

        if ( params.get(KEY_METER_MODE) != NULL ) {
            if (strcmp(params.get(KEY_METER_MODE), (const char *) METER_MODE_CENTER) == 0) {
                fobj->settings.af.spot_weighting = ICAM_FOCUS_SPOT_SINGLE_CENTER;
                fobj->settings.ae.spot_weighting = ICAM_EXPOSURE_SPOT_CENTER;

#ifdef HARDWARE_OMX

                mExifParams.metering_mode = EXIF_CENTER;

#endif

            } else if (strcmp(params.get(KEY_METER_MODE), (const char *) METER_MODE_AVERAGE) == 0) {
                fobj->settings.af.spot_weighting = ICAM_FOCUS_SPOT_MULTI_AVERAGE;
                fobj->settings.ae.spot_weighting = ICAM_EXPOSURE_SPOT_NORMAL;

#ifdef HARDWARE_OMX

                mExifParams.metering_mode = EXIF_AVERAGE;

#endif

            }
        }

        //Set 3A config to disable variable fps
        fobj->settings.ae.framerate = framerate;
        fobj->settings.general.view_finder_mode = ICAM_VFMODE_VIDEO_RECORD;

        if ( params.get(KEY_CAPTURE) != NULL ) {
            if (strcmp(params.get(KEY_CAPTURE), (const char *) CAPTURE_STILL) == 0) {
                //Set 3A config to enable variable fps
                fobj->settings.ae.framerate = 0;
                fobj->settings.general.view_finder_mode = ICAM_VFMODE_STILL_CAPTURE;
            }
        }

        if ( params.get(CameraParameters::KEY_SCENE_MODE) != NULL ) {
            if (strcmp(params.get(CameraParameters::KEY_SCENE_MODE), (const char *) CameraParameters::SCENE_MODE_AUTO) == 0) {
                fobj->settings.general.scene = ICAM_SCENE_MODE_MANUAL;

            } else if (strcmp(params.get(CameraParameters::KEY_SCENE_MODE), (const char *) CameraParameters::SCENE_MODE_PORTRAIT) == 0) {

                fobj->settings.general.scene = ICAM_SCENE_MODE_PORTRAIT;

            } else if (strcmp(params.get(CameraParameters::KEY_SCENE_MODE), (const char *) CameraParameters::SCENE_MODE_LANDSCAPE) == 0) {

                fobj->settings.general.scene = ICAM_SCENE_MODE_LANDSCAPE;

            } else if (strcmp(params.get(CameraParameters::KEY_SCENE_MODE), (const char *) CameraParameters::SCENE_MODE_NIGHT) == 0) {

                fobj->settings.general.scene = ICAM_SCENE_MODE_NIGHT;

            } else if (strcmp(params.get(CameraParameters::KEY_SCENE_MODE), (const char *) CameraParameters::SCENE_MODE_NIGHT_PORTRAIT) == 0) {

                fobj->settings.general.scene = ICAM_SCENE_MODE_NIGHT_PORTRAIT;

            } else if (strcmp(params.get(CameraParameters::KEY_SCENE_MODE), (const char *) CameraParameters::SCENE_MODE_FIREWORKS) == 0) {

                fobj->settings.general.scene = ICAM_SCENE_MODE_FIREWORKS;

            } else if (strcmp(params.get(CameraParameters::KEY_SCENE_MODE), (const char *) CameraParameters::SCENE_MODE_ACTION) == 0) {

                fobj->settings.general.scene = ICAM_SCENE_MODE_SPORT;

            }
              else if (strcmp(params.get(CameraParameters::KEY_SCENE_MODE), (const char *) CameraParameters::SCENE_MODE_SNOW) == 0) {

                fobj->settings.general.scene = ICAM_SCENE_MODE_SNOW_BEACH;

            }
        }

        if ( params.get(CameraParameters::KEY_WHITE_BALANCE) != NULL ) {
            if (strcmp(params.get(CameraParameters::KEY_WHITE_BALANCE), (const char *) CameraParameters::WHITE_BALANCE_AUTO ) == 0) {

                fobj->settings.awb.mode = ICAM_WHITE_BALANCE_MODE_WB_AUTO;

#ifdef HARDWARE_OMX

                mExifParams.wb = EXIF_WB_AUTO;

#endif

            } else if (strcmp(params.get(CameraParameters::KEY_WHITE_BALANCE), (const char *) CameraParameters::WHITE_BALANCE_INCANDESCENT) == 0) {

                fobj->settings.awb.mode = ICAM_WHITE_BALANCE_MODE_WB_INCANDESCENT;

#ifdef HARDWARE_OMX

                mExifParams.wb = EXIF_WB_MANUAL;

#endif

            } else if (strcmp(params.get(CameraParameters::KEY_WHITE_BALANCE), (const char *) CameraParameters::WHITE_BALANCE_FLUORESCENT) == 0) {

                fobj->settings.awb.mode = ICAM_WHITE_BALANCE_MODE_WB_FLUORESCENT;

#ifdef HARDWARE_OMX

                mExifParams.wb = EXIF_WB_MANUAL;

#endif

            } else if (strcmp(params.get(CameraParameters::KEY_WHITE_BALANCE), (const char *) CameraParameters::WHITE_BALANCE_DAYLIGHT) == 0) {

                fobj->settings.awb.mode = ICAM_WHITE_BALANCE_MODE_WB_DAYLIGHT;

#ifdef HARDWARE_OMX

                mExifParams.wb = EXIF_WB_MANUAL;

#endif

            } else if (strcmp(params.get(CameraParameters::KEY_WHITE_BALANCE), (const char *) CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT) == 0) {

                fobj->settings.awb.mode = ICAM_WHITE_BALANCE_MODE_WB_CLOUDY;

#ifdef HARDWARE_OMX

                mExifParams.wb = EXIF_WB_MANUAL;

#endif

            } else if (strcmp(params.get(CameraParameters::KEY_WHITE_BALANCE), (const char *) CameraParameters::WHITE_BALANCE_SHADE) == 0) {

                fobj->settings.awb.mode = ICAM_WHITE_BALANCE_MODE_WB_SHADOW;

#ifdef HARDWARE_OMX

                mExifParams.wb = EXIF_WB_MANUAL;

#endif

            } else if (strcmp(params.get(CameraParameters::KEY_WHITE_BALANCE), (const char *) WHITE_BALANCE_HORIZON) == 0) {

                fobj->settings.awb.mode = ICAM_WHITE_BALANCE_MODE_WB_HORIZON;

#ifdef HARDWARE_OMX

                mExifParams.wb = EXIF_WB_MANUAL;

#endif
            } else if (strcmp(params.get(CameraParameters::KEY_WHITE_BALANCE), (const char *) WHITE_BALANCE_TUNGSTEN) == 0) {

                fobj->settings.awb.mode = ICAM_WHITE_BALANCE_MODE_WB_TUNGSTEN;

#ifdef HARDWARE_OMX

                mExifParams.wb = EXIF_WB_MANUAL;

#endif
            }

        }

        if ( params.get(CameraParameters::KEY_EFFECT) != NULL ) {
            if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) CameraParameters::EFFECT_NONE ) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_NORMAL;

            } else if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) CameraParameters::EFFECT_MONO) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_GRAYSCALE;

            } else if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) CameraParameters::EFFECT_NEGATIVE) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_NEGATIVE;

            } else if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) CameraParameters::EFFECT_SOLARIZE) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_SOLARIZE;

            } else if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) CameraParameters::EFFECT_SEPIA) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_SEPIA;

            } else if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) CameraParameters::EFFECT_WHITEBOARD) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_WHITEBOARD;

            } else if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) CameraParameters::EFFECT_BLACKBOARD) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_BLACKBOARD;

            } else if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) EFFECT_COOL) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_COOL;

            } else if (strcmp(params.get(CameraParameters::KEY_EFFECT), (const char *) EFFECT_EMBOSS) == 0) {

                fobj->settings.general.effects = ICAM_EFFECT_EMBOSS;

            }
        }

        if ( params.get(CameraParameters::KEY_ANTIBANDING) != NULL ) {
            if (strcmp(params.get(CameraParameters::KEY_ANTIBANDING), (const char *) CameraParameters::ANTIBANDING_50HZ ) == 0) {

                fobj->settings.general.flicker_avoidance = ICAM_FLICKER_AVOIDANCE_50HZ;

            } else if (strcmp(params.get(CameraParameters::KEY_ANTIBANDING), (const char *) CameraParameters::ANTIBANDING_60HZ) == 0) {

                fobj->settings.general.flicker_avoidance = ICAM_FLICKER_AVOIDANCE_60HZ;

            } else if (strcmp(params.get(CameraParameters::KEY_ANTIBANDING), (const char *) CameraParameters::ANTIBANDING_OFF) == 0) {

                fobj->settings.general.flicker_avoidance = ICAM_FLICKER_AVOIDANCE_NO;

            }
        }

        if ( params.get(CameraParameters::KEY_FOCUS_MODE) != NULL ) {
            if (strcmp(params.get(CameraParameters::KEY_FOCUS_MODE), (const char *) CameraParameters::FOCUS_MODE_AUTO ) == 0) {

                fobj->settings.af.focus_mode = ICAM_FOCUS_MODE_AF_AUTO;

            } else if (strcmp(params.get(CameraParameters::KEY_FOCUS_MODE), (const char *) CameraParameters::FOCUS_MODE_INFINITY) == 0) {

                fobj->settings.af.focus_mode = ICAM_FOCUS_MODE_AF_INFINY;

            } else if (strcmp(params.get(CameraParameters::KEY_FOCUS_MODE), (const char *) CameraParameters::FOCUS_MODE_MACRO) == 0) {

                fobj->settings.af.focus_mode = ICAM_FOCUS_MODE_AF_MACRO;

            } else if (strcmp(params.get(CameraParameters::KEY_FOCUS_MODE), (const char *) CameraParameters::FOCUS_MODE_FIXED) == 0) {

                fobj->settings.af.focus_mode = ICAM_FOCUS_MODE_AF_MANUAL;

            }
        }

        valstr = mParameters.get(KEY_TOUCH_FOCUS);
        if(NULL != valstr) {
            char *valstr_copy = (char *)malloc(ARRAY_SIZE(valstr) + 1);
            if(NULL != valstr_copy){
                if ( strcmp(valstr, (const char *) TOUCH_FOCUS_DISABLED) != 0) {

                    //make a copy of valstr, because strtok() overrides the whole mParameters structure
                    strcpy(valstr_copy, valstr);
                    int af_x = 0;
                    int af_y = 0;

                    af_coord = strtok((char *) valstr_copy, PARAMS_DELIMITER);

                    if( NULL != af_coord){
                        af_x = atoi(af_coord);
                    }

                    af_coord = strtok(NULL, PARAMS_DELIMITER);

                    if( NULL != af_coord){
                        af_y = atoi(af_coord);
                    }

                    fobj->settings.general.face_tracking.enable = 1;
                    fobj->settings.general.face_tracking.count = 1;
                    fobj->settings.general.face_tracking.update = 1;
                    fobj->settings.general.face_tracking.faces[0].top = af_y;
                    fobj->settings.general.face_tracking.faces[0].left = af_x;
                    fobj->settings.af.focus_mode = ICAM_FOCUS_MODE_AF_EXTENDED;

                    LOGD("NEW PARAMS: af_x = %d, af_y = %d", af_x, af_y);
                }
                free(valstr_copy);
                valstr_copy = NULL;
            }
        }

        if ( params.get(KEY_ISO) != NULL ) {
            if (strcmp(params.get(KEY_ISO), (const char *) ISO_AUTO ) == 0) {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_AUTO;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_AUTO;

#endif

            } else if (strcmp(params.get(KEY_ISO), (const char *) ISO_100 ) == 0) {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_100;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_100;

#endif

            } else if (strcmp(params.get(KEY_ISO), (const char *) ISO_200 ) == 0) {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_200;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_200;

#endif

            } else if (strcmp(params.get(KEY_ISO), (const char *) ISO_400 ) == 0) {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_400;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_400;

#endif

            } else if (strcmp(params.get(KEY_ISO), (const char *) ISO_800 ) == 0) {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_800;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_800;

#endif

            } else if (strcmp(params.get(KEY_ISO), (const char *) ISO_1000 ) == 0) {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_1000;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_1000;

#endif

            } else if (strcmp(params.get(KEY_ISO), (const char *) ISO_1200 ) == 0) {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_1600;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_1200;

#endif

            } else if (strcmp(params.get(KEY_ISO), (const char *) ISO_1600 ) == 0) {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_1600;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_1600;

#endif

            } else {

                fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_AUTO;

#ifdef HARDWARE_OMX

                mExifParams.iso = EXIF_ISO_AUTO;

#endif

            }
        } else {

            fobj->settings.ae.iso = ICAM_EXPOSURE_ISO_AUTO;

#ifdef HARDWARE_OMX

            mExifParams.iso = EXIF_ISO_AUTO;

#endif

        }

        if ( params.get(KEY_EXPOSURE_MODE) != NULL ) {
            if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_AUTO ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_AUTO;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_MACRO ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_MACRO;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_PORTRAIT ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_PORTRAIT;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_LANDSCAPE ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_LANDSCAPE;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_SPORTS ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_SPORTS;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_NIGHT ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_NIGHT;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_NIGHT_PORTRAIT ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_NIGHT_PORTRAIT;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_BACKLIGHTING ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_BACKLIGHTING;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_MANUAL ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_MANUAL;

            } else if (strcmp(params.get(KEY_EXPOSURE_MODE), (const char *) EXPOSURE_VERYLONG ) == 0) {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_VERYLONG;

            } else {

                fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_AUTO;

            }
        } else {

            fobj->settings.ae.mode = ICAM_EXPOSURE_MODE_EXP_AUTO;

        }

        compensation = mParameters.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
        saturation = mParameters.getInt(KEY_SATURATION);
        sharpness = mParameters.getInt(KEY_SHARPNESS);
        contrast = mParameters.getInt(KEY_CONTRAST);
        brightness = mParameters.getInt(KEY_BRIGHTNESS);
        caf = mParameters.getInt(KEY_CAF);

        if(contrast != -1) {
            contrast -= CONTRAST_OFFSET;
            fobj->settings.general.contrast = contrast;
        }

        if(brightness != -1) {
            brightness -= BRIGHTNESS_OFFSET;
            fobj->settings.general.brightness = brightness;
            LOGE("Brightness passed to 3A %d", brightness);
        }

        if(saturation!= -1) {
            saturation -= SATURATION_OFFSET;
            fobj->settings.general.saturation = saturation;
            LOGE("Saturation passed to 3A %d", saturation);
        }
        if(sharpness != -1)
            fobj->settings.general.sharpness = sharpness;

        fobj->settings.ae.compensation = compensation;

        FW3A_SetSettings();

        if(mParameters.getInt(KEY_ROTATION_TYPE) == ROTATION_EXIF) {
            mExifParams.rotation = rotation;
            rotation = 0; // reset rotation so encoder doesn't not perform any rotation
        } else {
            mExifParams.rotation = -1;
        }

        if ((caf != -1) && (mcaf != caf)){
            mcaf = caf;
            Message msg;
            msg.command = mcaf ? PREVIEW_CAF_START : PREVIEW_CAF_STOP;
            previewThreadCommandQ.put(&msg);
            //unlock in order to read the message correctly
            mLock.unlock();
            previewThreadAckQ.get(&msg);
            return msg.command == PREVIEW_ACK ? NO_ERROR : INVALID_OPERATION;
        }
    }

#endif

    LOG_FUNCTION_NAME_EXIT
    return NO_ERROR;
}

CameraParameters CameraHal::getParameters() const
{
    CameraParameters params;

    LOG_FUNCTION_NAME

    {
        Mutex::Autolock lock(mLock);
        params = mParameters;
    }

#ifdef FW3A

    //check if fobj is created in order to get settings from 3AFW
    if ( NULL != fobj ) {

        if( FW3A_GetSettings() < 0 ) {
            LOGE("ERROR FW3A_GetSettings()");
            goto exit;
        }

        switch ( fobj->settings.general.scene ) {
            case ICAM_SCENE_MODE_MANUAL:
                params.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_AUTO);
                break;
            case ICAM_SCENE_MODE_PORTRAIT:
                params.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_PORTRAIT);
                break;
            case ICAM_SCENE_MODE_LANDSCAPE:
                params.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_LANDSCAPE);
                break;
            case ICAM_SCENE_MODE_NIGHT:
                params.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_NIGHT);
                break;
            case ICAM_SCENE_MODE_FIREWORKS:
                params.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_FIREWORKS);
                break;
            case ICAM_SCENE_MODE_SPORT:
                params.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_ACTION);
                break;
            //TODO: Extend support for those
            case ICAM_SCENE_MODE_CLOSEUP:
                break;
            case ICAM_SCENE_MODE_UNDERWATER:
                break;
            case ICAM_SCENE_MODE_SNOW_BEACH:
                params.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_SNOW);
                break;
            case ICAM_SCENE_MODE_MOOD:
                break;
            case ICAM_SCENE_MODE_NIGHT_INDOOR:
                break;
            case ICAM_SCENE_MODE_NIGHT_PORTRAIT:
                params.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_NIGHT_PORTRAIT);
                break;
            case ICAM_SCENE_MODE_INDOOR:
                break;
            case ICAM_SCENE_MODE_AUTO:
                break;
        };

        switch ( fobj->settings.ae.mode ) {
            case ICAM_EXPOSURE_MODE_EXP_AUTO:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_AUTO);
                break;
            case ICAM_EXPOSURE_MODE_EXP_MACRO:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_MACRO);
                break;
            case ICAM_EXPOSURE_MODE_EXP_PORTRAIT:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_PORTRAIT);
                break;
            case ICAM_EXPOSURE_MODE_EXP_LANDSCAPE:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_LANDSCAPE);
                break;
            case ICAM_EXPOSURE_MODE_EXP_SPORTS:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_SPORTS);
                break;
            case ICAM_EXPOSURE_MODE_EXP_NIGHT:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_NIGHT);
                break;
            case ICAM_EXPOSURE_MODE_EXP_NIGHT_PORTRAIT:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_NIGHT_PORTRAIT);
                break;
            case ICAM_EXPOSURE_MODE_EXP_BACKLIGHTING:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_BACKLIGHTING);
                break;
            case ICAM_EXPOSURE_MODE_EXP_MANUAL:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_MANUAL);
                break;
            case ICAM_EXPOSURE_MODE_EXP_VERYLONG:
                params.set(KEY_EXPOSURE_MODE, EXPOSURE_VERYLONG);
                break;
        };

        switch ( fobj->settings.ae.iso ) {
            case ICAM_EXPOSURE_ISO_AUTO:
                params.set(KEY_ISO, ISO_AUTO);
                break;
            case ICAM_EXPOSURE_ISO_100:
                params.set(KEY_ISO, ISO_100);
                break;
            case ICAM_EXPOSURE_ISO_200:
                params.set(KEY_ISO, ISO_200);
                break;
            case ICAM_EXPOSURE_ISO_400:
                params.set(KEY_ISO, ISO_400);
                break;
            case ICAM_EXPOSURE_ISO_800:
                params.set(KEY_ISO, ISO_800);
                break;
            case ICAM_EXPOSURE_ISO_1000:
                params.set(KEY_ISO, ISO_1000);
                break;
            case ICAM_EXPOSURE_ISO_1200:
                params.set(KEY_ISO, ISO_1200);
                break;
            case ICAM_EXPOSURE_ISO_1600:
                params.set(KEY_ISO, ISO_1600);
                break;
        };

        switch ( fobj->settings.af.focus_mode ) {
            case ICAM_FOCUS_MODE_AF_AUTO:
                params.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
                break;
            case ICAM_FOCUS_MODE_AF_INFINY:
                params.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_INFINITY);
                break;
            case ICAM_FOCUS_MODE_AF_MACRO:
                params.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_MACRO);
                break;
            case ICAM_FOCUS_MODE_AF_MANUAL:
                params.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_FIXED);
                break;
            //TODO: Extend support for those
            case ICAM_FOCUS_MODE_AF_CONTINUOUS:
                break;
            case ICAM_FOCUS_MODE_AF_CONTINUOUS_NORMAL:
                break;
            case ICAM_FOCUS_MODE_AF_PORTRAIT:
                break;
            case ICAM_FOCUS_MODE_AF_HYPERFOCAL:
                break;
            case ICAM_FOCUS_MODE_AF_EXTENDED:
                break;
            case ICAM_FOCUS_MODE_AF_CONTINUOUS_EXTENDED:
                break;
        };

        switch ( fobj->settings.general.flicker_avoidance ) {
            case ICAM_FLICKER_AVOIDANCE_50HZ:
                params.set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_50HZ);
                break;
            case ICAM_FLICKER_AVOIDANCE_60HZ:
                params.set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_60HZ);
                break;
            case ICAM_FLICKER_AVOIDANCE_NO:
                params.set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
                break;
        };

        switch ( fobj->settings.general.effects ) {
            case ICAM_EFFECT_EMBOSS:
                params.set(CameraParameters::KEY_EFFECT, EFFECT_EMBOSS);
                break;
            case ICAM_EFFECT_COOL:
                params.set(CameraParameters::KEY_EFFECT, EFFECT_COOL);
                break;
            case ICAM_EFFECT_BLACKBOARD:
                params.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_BLACKBOARD);
                break;
            case ICAM_EFFECT_WHITEBOARD:
                params.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_WHITEBOARD);
                break;
            case ICAM_EFFECT_SEPIA:
                params.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_SEPIA);
                break;
            case ICAM_EFFECT_SOLARIZE:
                params.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_SOLARIZE);
                break;
            case ICAM_EFFECT_NEGATIVE:
                params.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NEGATIVE);
                break;
            case ICAM_EFFECT_GRAYSCALE:
                params.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_MONO);
                break;
            case ICAM_EFFECT_NORMAL:
                params.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);
                break;
            //TODO: Add support for those
            case ICAM_EFFECT_NATURAL:
                break;
            case ICAM_EFFECT_VIVID:
                break;
            case ICAM_EFFECT_COLORSWAP:
                break;
            case ICAM_EFFECT_OUT_OF_FOCUS:
                break;
            case ICAM_EFFECT_NEGATIVE_SEPIA:
                break;
        };

        switch ( fobj->settings.awb.mode ) {
            case ICAM_WHITE_BALANCE_MODE_WB_SHADOW:
                params.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_SHADE);
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_CLOUDY:
                params.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT);
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_DAYLIGHT:
                params.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_DAYLIGHT);
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_FLUORESCENT:
                params.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_FLUORESCENT);
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_INCANDESCENT:
                params.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_INCANDESCENT);
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_AUTO:
                params.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_HORIZON:
                params.set(CameraParameters::KEY_WHITE_BALANCE, WHITE_BALANCE_HORIZON);
                break;
            //TODO: Extend support for those
            case ICAM_WHITE_BALANCE_MODE_WB_MANUAL:
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_TUNGSTEN:
                params.set(CameraParameters::KEY_WHITE_BALANCE, WHITE_BALANCE_TUNGSTEN);
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_OFFICE:
                break;
            case ICAM_WHITE_BALANCE_MODE_WB_FLASH:
                break;
        };

        switch ( fobj->settings.ae.spot_weighting ) {
            case ICAM_EXPOSURE_SPOT_NORMAL:
                params.set(KEY_METER_MODE, METER_MODE_AVERAGE);
                break;
            case ICAM_EXPOSURE_SPOT_CENTER:
                params.set(KEY_METER_MODE, METER_MODE_CENTER);
                break;
            //TODO: support this also
            case ICAM_EXPOSURE_SPOT_WIDE:
                break;
        };

        params.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, fobj->settings.ae.compensation);
        params.set(KEY_SATURATION, ( fobj->settings.general.saturation + SATURATION_OFFSET ));
        params.set(KEY_SHARPNESS, fobj->settings.general.sharpness);
        params.set(KEY_CONTRAST, ( fobj->settings.general.contrast + CONTRAST_OFFSET ));
        params.set(KEY_BRIGHTNESS, ( fobj->settings.general.brightness + BRIGHTNESS_OFFSET ));
        params.setPreviewFrameRate(fobj->settings.ae.framerate);
    }
#endif

exit:

    LOG_FUNCTION_NAME_EXIT

    return params;
}

status_t  CameraHal::dump(int fd, const Vector<String16>& args) const
{
    return 0;
}

void CameraHal::dumpFrame(void *buffer, int size, char *path)
{
    FILE* fIn = NULL;

    fIn = fopen(path, "w");
    if ( fIn == NULL ) {
        LOGE("\n\n\n\nError: failed to open the file %s for writing\n\n\n\n", path);
        return;
    }

    fwrite((void *)buffer, 1, size, fIn);
    fclose(fIn);

}

void CameraHal::release()
{
}

#ifdef FW3A

void CameraHal::onIPP(void *priv, icap_ipp_parameters_t *ipp_params)
{
    CameraHal* camHal = reinterpret_cast<CameraHal*>(priv);

    LOG_FUNCTION_NAME

#ifdef IMAGE_PROCESSING_PIPELINE

    if ( ipp_params->type == ICAP_IPP_PARAMETERS_VER1_9 ) {
        camHal->mIPPParams.EdgeEnhancementStrength = ipp_params->ipp19.EdgeEnhancementStrength;
        camHal->mIPPParams.WeakEdgeThreshold = ipp_params->ipp19.WeakEdgeThreshold;
        camHal->mIPPParams.StrongEdgeThreshold = ipp_params->ipp19.StrongEdgeThreshold;
        camHal->mIPPParams.LowFreqLumaNoiseFilterStrength = ipp_params->ipp19.LowFreqLumaNoiseFilterStrength;
        camHal->mIPPParams.MidFreqLumaNoiseFilterStrength = ipp_params->ipp19.MidFreqLumaNoiseFilterStrength;
        camHal->mIPPParams.HighFreqLumaNoiseFilterStrength = ipp_params->ipp19.HighFreqLumaNoiseFilterStrength;
        camHal->mIPPParams.LowFreqCbNoiseFilterStrength = ipp_params->ipp19.LowFreqCbNoiseFilterStrength;
        camHal->mIPPParams.MidFreqCbNoiseFilterStrength = ipp_params->ipp19.MidFreqCbNoiseFilterStrength;
        camHal->mIPPParams.HighFreqCbNoiseFilterStrength = ipp_params->ipp19.HighFreqCbNoiseFilterStrength;
        camHal->mIPPParams.LowFreqCrNoiseFilterStrength = ipp_params->ipp19.LowFreqCrNoiseFilterStrength;
        camHal->mIPPParams.MidFreqCrNoiseFilterStrength = ipp_params->ipp19.MidFreqCrNoiseFilterStrength;
        camHal->mIPPParams.HighFreqCrNoiseFilterStrength = ipp_params->ipp19.HighFreqCrNoiseFilterStrength;
        camHal->mIPPParams.shadingVertParam1 = ipp_params->ipp19.shadingVertParam1;
        camHal->mIPPParams.shadingVertParam2 = ipp_params->ipp19.shadingVertParam2;
        camHal->mIPPParams.shadingHorzParam1 = ipp_params->ipp19.shadingHorzParam1;
        camHal->mIPPParams.shadingHorzParam2 = ipp_params->ipp19.shadingHorzParam2;
        camHal->mIPPParams.shadingGainScale = ipp_params->ipp19.shadingGainScale;
        camHal->mIPPParams.shadingGainOffset = ipp_params->ipp19.shadingGainOffset;
        camHal->mIPPParams.shadingGainMaxValue = ipp_params->ipp19.shadingGainMaxValue;
        camHal->mIPPParams.ratioDownsampleCbCr = ipp_params->ipp19.ratioDownsampleCbCr;
    } else if ( ipp_params->type == ICAP_IPP_PARAMETERS_VER1_8 ) {
        camHal->mIPPParams.EdgeEnhancementStrength = ipp_params->ipp18.ee_q;
        camHal->mIPPParams.WeakEdgeThreshold = ipp_params->ipp18.ew_ts;
        camHal->mIPPParams.StrongEdgeThreshold = ipp_params->ipp18.es_ts;
        camHal->mIPPParams.LowFreqLumaNoiseFilterStrength = ipp_params->ipp18.luma_nf;
        camHal->mIPPParams.MidFreqLumaNoiseFilterStrength = ipp_params->ipp18.luma_nf;
        camHal->mIPPParams.HighFreqLumaNoiseFilterStrength = ipp_params->ipp18.luma_nf;
        camHal->mIPPParams.LowFreqCbNoiseFilterStrength = ipp_params->ipp18.chroma_nf;
        camHal->mIPPParams.MidFreqCbNoiseFilterStrength = ipp_params->ipp18.chroma_nf;
        camHal->mIPPParams.HighFreqCbNoiseFilterStrength = ipp_params->ipp18.chroma_nf;
        camHal->mIPPParams.LowFreqCrNoiseFilterStrength = ipp_params->ipp18.chroma_nf;
        camHal->mIPPParams.MidFreqCrNoiseFilterStrength = ipp_params->ipp18.chroma_nf;
        camHal->mIPPParams.HighFreqCrNoiseFilterStrength = ipp_params->ipp18.chroma_nf;
        camHal->mIPPParams.shadingVertParam1 = -1;
        camHal->mIPPParams.shadingVertParam2 = -1;
        camHal->mIPPParams.shadingHorzParam1 = -1;
        camHal->mIPPParams.shadingHorzParam2 = -1;
        camHal->mIPPParams.shadingGainScale = -1;
        camHal->mIPPParams.shadingGainOffset = -1;
        camHal->mIPPParams.shadingGainMaxValue = -1;
        camHal->mIPPParams.ratioDownsampleCbCr = -1;
    } else {
        camHal->mIPPParams.EdgeEnhancementStrength = 220;
        camHal->mIPPParams.WeakEdgeThreshold = 8;
        camHal->mIPPParams.StrongEdgeThreshold = 200;
        camHal->mIPPParams.LowFreqLumaNoiseFilterStrength = 5;
        camHal->mIPPParams.MidFreqLumaNoiseFilterStrength = 10;
        camHal->mIPPParams.HighFreqLumaNoiseFilterStrength = 15;
        camHal->mIPPParams.LowFreqCbNoiseFilterStrength = 20;
        camHal->mIPPParams.MidFreqCbNoiseFilterStrength = 30;
        camHal->mIPPParams.HighFreqCbNoiseFilterStrength = 10;
        camHal->mIPPParams.LowFreqCrNoiseFilterStrength = 10;
        camHal->mIPPParams.MidFreqCrNoiseFilterStrength = 25;
        camHal->mIPPParams.HighFreqCrNoiseFilterStrength = 15;
        camHal->mIPPParams.shadingVertParam1 = 10;
        camHal->mIPPParams.shadingVertParam2 = 400;
        camHal->mIPPParams.shadingHorzParam1 = 10;
        camHal->mIPPParams.shadingHorzParam2 = 400;
        camHal->mIPPParams.shadingGainScale = 128;
        camHal->mIPPParams.shadingGainOffset = 2048;
        camHal->mIPPParams.shadingGainMaxValue = 16384;
        camHal->mIPPParams.ratioDownsampleCbCr = 1;
    }

#endif

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::onGeneratedSnapshot(void *priv, icap_image_buffer_t *buf)
{
    unsigned int snapshotMessage[1];

    CameraHal* camHal = reinterpret_cast<CameraHal*>(priv);

    LOG_FUNCTION_NAME

    snapshotMessage[0] = SNAPSHOT_THREAD_START_GEN;
    write(camHal->snapshotPipe[1], &snapshotMessage, sizeof(snapshotMessage));

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::onSnapshot(void *priv, icap_image_buffer_t *buf)
{
    unsigned int snapshotMessage[9];

    CameraHal* camHal = reinterpret_cast<CameraHal*>(priv);

    LOG_FUNCTION_NAME

    snapshotMessage[0] = SNAPSHOT_THREAD_START;
    snapshotMessage[1] = (unsigned int) buf->buffer.buffer;
    snapshotMessage[2] = buf->width;
    snapshotMessage[3] = buf->height;
    snapshotMessage[4] = camHal->mZoomTargetIdx;
    snapshotMessage[5] = camHal->mImageCropLeft;
    snapshotMessage[6] = camHal->mImageCropTop;
    snapshotMessage[7] = camHal->mImageCropWidth;
    snapshotMessage[8] = camHal->mImageCropHeight;

    write(camHal->snapshotPipe[1], &snapshotMessage, sizeof(snapshotMessage));

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::onShutter(void *priv, icap_image_buffer_t *image_buf)
{
    LOG_FUNCTION_NAME

    unsigned int shutterMessage[3];

    CameraHal* camHal = reinterpret_cast<CameraHal*>(priv);

    if( ( camHal->msgTypeEnabled(CAMERA_MSG_SHUTTER) ) && (camHal->mShutterEnable) ) {

#ifdef DEBUG_LOG

        camHal->PPM("SENDING MESSAGE TO SHUTTER THREAD");

#endif

        shutterMessage[0] = SHUTTER_THREAD_CALL;
        shutterMessage[1] = (unsigned int) camHal->mNotifyCb;
        shutterMessage[2] = (unsigned int) camHal->mCallbackCookie;
        write(camHal->shutterPipe[1], &shutterMessage, sizeof(shutterMessage));
    }

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::onCrop(void *priv,  icap_crop_rect_t *crop_rect)
{
    LOG_FUNCTION_NAME

    CameraHal *camHal = reinterpret_cast<CameraHal *> (priv);

    camHal->mImageCropTop = crop_rect->top;
    camHal->mImageCropLeft = crop_rect->left;
    camHal->mImageCropWidth = crop_rect->width;
    camHal->mImageCropHeight = crop_rect->height;

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::onMakernote(void *priv, void *mknote_ptr)
{
    LOG_FUNCTION_NAME

    CameraHal *camHal = reinterpret_cast<CameraHal *>(priv);
    SICam_MakerNote *makerNote = reinterpret_cast<SICam_MakerNote *>(mknote_ptr);

#ifdef DUMP_MAKERNOTE

    camHal->SaveFile(NULL, (char*)"mnt", makerNote->buffer, makerNote->filled_size);

#endif

    LOG_FUNCTION_NAME_EXIT
}

void CameraHal::onSaveH3A(void *priv, icap_image_buffer_t *buf)
{
    CameraHal* camHal = reinterpret_cast<CameraHal*>(priv);

    LOGD("Observer onSaveH3A\n");
    camHal->SaveFile(NULL, (char*)"h3a", buf->buffer.buffer, buf->buffer.filled_size);
}

void CameraHal::onSaveLSC(void *priv, icap_image_buffer_t *buf)
{
    CameraHal* camHal = reinterpret_cast<CameraHal*>(priv);

    LOGD("Observer onSaveLSC\n");
    camHal->SaveFile(NULL, (char*)"lsc", buf->buffer.buffer, buf->buffer.filled_size);
}

void CameraHal::onSaveRAW(void *priv, icap_image_buffer_t *buf)
{
    CameraHal* camHal = reinterpret_cast<CameraHal*>(priv);

    LOGD("Observer onSaveRAW\n");
    camHal->SaveFile(NULL, (char*)"raw", buf->buffer.buffer, buf->buffer.filled_size);
}

#endif

int CameraHal::SaveFile(char *filename, char *ext, void *buffer, int size)
{
    LOG_FUNCTION_NAME
    //Store image
    char fn [512];

    if (filename) {
      strcpy(fn,filename);
    } else {
      if (ext==NULL) ext = (char*)"tmp";
      sprintf(fn, PHOTO_PATH, file_index, ext);
    }
    file_index++;
    LOGD("Writing to file: %s", fn);
    int fd = open(fn, O_RDWR | O_CREAT | O_SYNC);
    if (fd < 0) {
        LOGE("Cannot open file %s : %s", fn, strerror(errno));
        return -1;
    } else {

        int written = 0;
        int page_block, page = 0;
        int cnt = 0;
        int nw;
        char *wr_buff = (char*)buffer;
        LOGD("Jpeg size %d buffer 0x%x", size, ( unsigned int ) buffer);
        page_block = size / 20;
        while (written < size ) {
          nw = size - written;
          nw = (nw>512*1024)?8*1024:nw;

          nw = ::write(fd, wr_buff, nw);
          if (nw<0) {
              LOGD("write fail nw=%d, %s", nw, strerror(errno));
            break;
          }
          wr_buff += nw;
          written += nw;
          cnt++;

          page    += nw;
          if (page>=page_block){
              page = 0;
              LOGD("Percent %6.2f, wn=%5d, total=%8d, jpeg_size=%8d",
                  ((float)written)/size, nw, written, size);
          }
        }

        close(fd);

        return 0;
    }
}


sp<IMemoryHeap> CameraHal::getPreviewHeap() const
{
    LOG_FUNCTION_NAME
    return 0;
}

sp<CameraHardwareInterface> CameraHal::createInstance()
{
    LOG_FUNCTION_NAME

    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }

    sp<CameraHardwareInterface> hardware(new CameraHal());

    singleton = hardware;
    return hardware;
}

/*--------------------Eclair HAL---------------------------------------*/
void CameraHal::setCallbacks(notify_callback notify_cb,
                                      data_callback data_cb,
                                      data_callback_timestamp data_cb_timestamp,
                                      void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void CameraHal::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void CameraHal::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool CameraHal::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}

status_t CameraHal::cancelAutoFocus()
{
    return NO_ERROR;
}

/*--------------------Eclair HAL---------------------------------------*/

status_t CameraHal::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2)
{
    Message msg;
    status_t ret = NO_ERROR;

    LOG_FUNCTION_NAME

    switch(cmd) {
        case CAMERA_CMD_START_SMOOTH_ZOOM:
            msg.command = START_SMOOTH_ZOOM;
            msg.arg1 = ( void * ) arg1;
            previewThreadCommandQ.put(&msg);
            previewThreadAckQ.get(&msg);

            if ( PREVIEW_ACK != msg.command ) {
                ret = -EINVAL;
            }

            break;
        case CAMERA_CMD_STOP_SMOOTH_ZOOM:
            msg.command = STOP_SMOOTH_ZOOM;
            previewThreadCommandQ.put(&msg);
            previewThreadAckQ.get(&msg);

            if ( PREVIEW_ACK != msg.command ) {
                ret = -EINVAL;
            }

            break;
        default:
            break;
    };

    LOG_FUNCTION_NAME_EXIT

    return ret;
}


extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    LOGD("opening ti camera hal\n");
    return CameraHal::createInstance();
}


}; // namespace android

