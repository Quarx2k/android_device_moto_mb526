LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := hbootuser.c
LOCAL_MODULE := hbootuser
LOCAL_SHARED_LIBRARIES:= libcutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
