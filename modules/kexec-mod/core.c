/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2006 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2006 Novell, Inc.
 * This Edition is maintained by Matthew Veety (aliasxerog) <mveety@gmail.com>
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/list.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/notifier.h>
#include <linux/genhd.h>
#include <linux/kallsyms.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>

/**
 * device_shutdown - call ->shutdown() on each device to shutdown.
 */
void device_shutdown(void)
{
	struct device *dev, *devn, *dummy_device;
	struct kset *devices_kset;

	dummy_device = kzalloc(sizeof(*dummy_device), GFP_KERNEL);

	/* init a dummy device to get to the devices kset */
	device_initialize(dummy_device);
	devices_kset=dummy_device->kobj.kset;
	put_device(dummy_device);

	list_for_each_entry_safe_reverse(dev, devn, &devices_kset->list,
				kobj.entry) {
		if (dev->bus && dev->bus->shutdown) {
			dev_dbg(dev, "shutdown\n");
			dev->bus->shutdown(dev);
		} else if (dev->driver && dev->driver->shutdown) {
			dev_dbg(dev, "shutdown\n");
			dev->driver->shutdown(dev);
		}
	}
}
