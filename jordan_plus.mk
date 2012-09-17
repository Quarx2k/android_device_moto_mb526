#
# Copyright (C) 2011 The Android Open Source Project
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

#
# This is the product configuration for a generic Motorola Defy (jordan)
#

## (1) First, inherit common config
$(call inherit-product, device/motorola/jordan-common/jordan-common.mk)
## (2) Also get non-open-source files if available
$(call inherit-product-if-exists, vendor/motorola/jordan_plus/jordan_plus-vendor.mk)

## (3)  Finally, the least specific parts, i.e. the non-GSM-specific aspects
PRODUCT_PROPERTY_OVERRIDES += \
	ro.media.capture.maxres=5m \
	ro.media.capture.flash=led \
	ro.media.capture.flashIntensity=41 \
	ro.media.capture.torchIntensity=25 \
	ro.media.capture.classification=classE \
	ro.url.safetylegal=http://www.motorola.com/staticfiles/Support/legal/?model=MB525

# copy all vendor (motorola) kernel modules to system/lib/modules
PRODUCT_COPY_FILES += $(shell test -d vendor/motorola/jordan_plus/lib/modules &&  \
	find vendor/motorola/jordan_plus/lib/modules -name '*.ko' \
	-printf '%p:system/lib/modules/%f ')

# List of files to keep against rom upgrade (baseband config, overclock settings)
ifdef CYANOGEN_RELEASE
    PRODUCT_COPY_FILES += device/motorola/jordan_plus/releasetools/custom_backup_release.txt:system/etc/custom_backup_list.txt
else
    PRODUCT_COPY_FILES += device/motorola/jordan_plus/releasetools/custom_backup_list.txt:system/etc/custom_backup_list.txt
endif

PRODUCT_NAME := generic_jordan_plus
PRODUCT_DEVICE := MB526

