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
rm -f $REPACK/ota/boot.img

# add an empty script to prevent logcat errors (moto init.rc)
touch $REPACK/ota/system/bin/mount_ext3.sh
chmod +x $REPACK/ota/system/bin/mount_ext3.sh

# Copy custom update-script
cp -f $DEVICE_COMMON/updater-script $REPACK/ota/META-INF/com/google/android/updater-script

# Copy kernel & ramdisk
cp -f $DEVICE_OUT/kernel $REPACK/ota/system/bootmenu/2nd-boot/zImage
cp -f $DEVICE_OUT/ramdisk.img $REPACK/ota/system/bootmenu/2nd-boot/ramdisk

# use the static busybox as bootmenu shell, and some static utilities
mkdir -p $REPACK/ota/system/etc/terminfo/x
cp -f $DEVICE_OUT/utilities/busybox $REPACK/ota/system/bootmenu/binary/busybox
cp -f $DEVICE_OUT/utilities/lsof $REPACK/ota/system/bootmenu/binary/lsof
cp $REPACK/ota/system/etc/terminfo/l/linux $REPACK/ota/system/etc/terminfo/x/xterm

# ril fix
cp -f $REPACK/ota/system/lib/hw/audio.a2dp.default.so $REPACK/ota/system/lib/liba2dp.so

# Media profiles with HD rec.
cp -f $DEVICE_TOP/media_profiles.xml $REPACK/ota/system/etc/media_profiles.xml
