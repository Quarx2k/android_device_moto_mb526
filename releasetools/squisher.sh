# This script is included in squisher
# It is the final build step (after OTA package)

# set in squisher
# DEVICE_OUT=$ANDROID_BUILD_TOP/out/target/product/jordan_plus
# DEVICE_TOP=$ANDROID_BUILD_TOP/device/motorola/jordan_plus
# VENDOR_TOP=$ANDROID_BUILD_TOP/vendor/motorola/jordan_plus

# Delete unwanted apps
rm -f $REPACK/ota/system/app/RomManager.apk
rm -f $REPACK/ota/system/app/FOTAKill.apk
rm -f $REPACK/ota/system/xbin/irssi

# these scripts are not required
rm $REPACK/ota/system/etc/init.d/03firstboot
rm $REPACK/ota/system/etc/init.d/04modules

# add an empty script to prevent logcat errors (moto init.rc)
touch $REPACK/ota/system/bin/mount_ext3.sh
chmod +x $REPACK/ota/system/bin/mount_ext3.sh

mkdir -p $REPACK/ota/system/etc/terminfo/x
cp $REPACK/ota/system/etc/terminfo/l/linux $REPACK/ota/system/etc/terminfo/x/xterm

# add files needed by bootmenu that normally live in initrd
cp -f $DEVICE_OUT/utilities/busybox $REPACK/ota/system/bootmenu/binary/busybox
cp -f $DEVICE_OUT/root/sbin/adbd $REPACK/ota/system/bootmenu/binary/adbd
cp -f $DEVICE_OUT/recovery/root/sbin/tune2fs $REPACK/ota/system/bootmenu/recovery/sbin/tune2fs

# prebuilt boot, devtree, logo & updater-script
rm -f $REPACK/ota/boot.img
cp -f $DEVICE_COMMON/releasetools/updater-script $REPACK/ota/META-INF/com/google/android/updater-script

# keep multiboot specific files, if installed
cat $DEVICE_TOP/releasetools/multiboot_backup_list.txt >> $REPACK/ota/system/etc/custom_backup_list.txt

# release builds contains a kernel, and do not backup kernel modules
if [ -n "$CYANOGEN_RELEASE" ]; then
  cat $DEVICE_COMMON/releasetools/updater-script-rel >> $REPACK/ota/META-INF/com/google/android/updater-script
  cp -f $VENDOR_TOP/boot-234-134.smg $REPACK/ota/boot.img
  cp -f $VENDOR_TOP/devtree-234-134.smg $REPACK/ota/devtree.img
  cp -f $VENDOR_TOP/logo-moto.raw $REPACK/ota/logo.img
fi

cp -f $DEVICE_OUT/root/init $REPACK/ota/system/bootmenu/2nd-init/init
cp -f $DEVICE_OUT/root/init.rc $REPACK/ota/system/bootmenu/2nd-init/init.rc

