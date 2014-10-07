# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# This file sets variables that control the way modules are built
# thorughout the system. It should not be used to conditionally
# disable makefiles (the proper mechanism to control what gets
# included in a build is to use PRODUCT_PACKAGES in a product
# definition file).
#

# WARNING: This line must come *before* including the proprietary
# variant, so that it gets overwritten by the parent (which goes
# against the traditional rules of inheritance).

# inherit from common jordan
include device/moto/jordan-common/BoardConfig.mk
LOCAL_PATH := device/moto/jordan-common

# Assert
TARGET_OTA_ASSERT_DEVICE := mb526,mb520

# Init
TARGET_INIT_VENDOR_LIB := libinit_omap3
TARGET_LIBINIT_DEFINES_FILE := $(LOCAL_PATH)/init/init_omap3.c
TARGET_UNIFIED_DEVICE := true


TARGET_KERNEL_CONFIG  := mb526_cm10.1_defconfig


