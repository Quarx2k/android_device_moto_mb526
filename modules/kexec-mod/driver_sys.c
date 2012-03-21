/*
 * sys.c - pseudo-bus for system 'devices' (cpus, PICs, timers, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 *               2002-3 Open Source Development Lab
 * This Edition is maintained by Matthew Veety (aliasxerog) <mveety@gmail.com>
 *
 * This file is released under the GPLv2
 *
 * This exports a 'system' bus type.
 * By default, a 'sys' bus gets added to the root of the system. There will
 * always be core system devices. Devices can use sysdev_register() to
 * add themselves as children of the system bus.
 */

#include <linux/sysdev.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/smp_lock.h>


#define to_sysdev(k) container_of(k, struct sys_device, kobj)
#define to_sysdev_attr(a) container_of(a, struct sysdev_attribute, attr)

static struct sysdev_class dummy_sysclass = {
         .name="dummy",
};


/**
 *	sysdev_shutdown - Shut down all system devices.
 *
 *	Loop over each class of system devices, and the devices in each
 *	of those classes. For each device, we call the shutdown method for
 *	each driver registered for the device - the auxillaries,
 *	and the class driver.
 *
 *	Note: The list is iterated in reverse order, so that we shut down
 *	child devices before we shut down thier parents. The list ordering
 *	is guaranteed by virtue of the fact that child devices are registered
 *	after their parents.
 */
void sysdev_shutdown(void)
{
	struct sysdev_class * cls;
	struct kset *system_kset;

	pr_debug("Shutting Down System Devices\n");

	lock_kernel();

	sysdev_class_register(&dummy_sysclass);
	cls=&dummy_sysclass;
	system_kset=cls->kset.kobj.kset;
	sysdev_class_unregister(&dummy_sysclass);

	list_for_each_entry_reverse(cls, &system_kset->list, kset.kobj.entry) {
		struct sys_device * sysdev;

		pr_debug("Shutting down type '%s':\n",
			 kobject_name(&cls->kset.kobj));

		list_for_each_entry(sysdev, &cls->kset.list, kobj.entry) {
			struct sysdev_driver * drv;
			pr_debug(" %s\n", kobject_name(&sysdev->kobj));

			/* Call auxillary drivers first */
			list_for_each_entry(drv, &cls->drivers, entry) {
				if (drv->shutdown)
					drv->shutdown(sysdev);
			}

			/* Now call the generic one */
			if (cls->shutdown)
				cls->shutdown(sysdev);
		}
	}
	unlock_kernel();
}
