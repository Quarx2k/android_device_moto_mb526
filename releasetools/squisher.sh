# This script is included in squisher
# It is the final build step (after OTA package)

# set in squisher
# DEVICE_OUT=$ANDROID_BUILD_TOP/out/target/product/mb526
# DEVICE_TOP=$ANDROID_BUILD_TOP/device/moto/mb526
# VENDOR_TOP=$ANDROID_BUILD_TOP/vendor/motorola/jordan_plus

# Delete unwanted files
rm -f $REPACK/ota/system/app/RomManager.apk
rm -f $REPACK/ota/system/app/VideoEditor.apk
rm -f $REPACK/ota/system/lib/libvideoeditor*
rm -f $REPACK/ota/system/lib/hw/*goldfish*
rm -f $REPACK/ota/system/media/video/*
rm -f $REPACK/ota/system/etc/init.d/04modules

