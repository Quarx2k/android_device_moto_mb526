LOCAL_PATH:= $(call my-dir)

ifneq ($(USE_CAMERA_STUB),true)
#
# libcamera
#
include $(CLEAR_VARS)

ifeq ($(TARGET_PRODUCT), flashboard)
LOCAL_CFLAGS += -DCONFIG_FLASHBOARD
endif

LOCAL_SRC_FILES:= \
    CameraHardware.cpp \
    V4L2Camera.cpp \
    converter.cpp

LOCAL_SHARED_LIBRARIES:= \
    libcutils \
    libui \
    libutils \
    libbinder \
    libjpeg \
    libcamera_client \
    libsurfaceflinger_client

LOCAL_C_INCLUDES += \
    external/jpeg

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= libsoccamera

include $(BUILD_SHARED_LIBRARY)
endif

include $(all-subdir-makefiles)
