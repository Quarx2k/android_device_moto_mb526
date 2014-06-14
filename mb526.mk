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
$(call inherit-product, device/moto/jordan-common/jordan.mk)

device_path = device/moto/mb526
DEVICE_PACKAGE_OVERLAYS += device/moto/mb526/overlay

PRODUCT_PACKAGES += Torch

PRODUCT_PROPERTY_OVERRIDES += \
	ro.media.capture.maxres=5m \
	ro.media.capture.flash=led \
	ro.media.capture.flashIntensity=41 \
	ro.media.capture.torchIntensity=25 \
	ro.media.capture.classification=classE

PRODUCT_COPY_FILES += \
	${device_path}/media_profiles_mb526.xml:system/etc/media_profiles.xml \
	${device_path}/devtree:system/bootstrap/2nd-boot/devtree \
	${device_path}/devtree:system/bootstrap/2nd-boot/devtree-recovery

#${device_path}/media_profiles_mb525.xml:system/etc/media_profiles_mb525.xml
# Include non-opensource parts
$(call inherit-product, vendor/motorola/jordan-common/jordan-vendor.mk)

