# Copyright 2011 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	battd2.c 

LOCAL_MODULE:= battdsock
LOCAL_MODULE_TAGS := optional
#LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_STATIC_LIBRARIES := libcutils libc

include $(BUILD_EXECUTABLE)

