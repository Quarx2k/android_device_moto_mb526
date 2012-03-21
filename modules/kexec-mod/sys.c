/*
 *  linux/kernel/sys.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/mman.h>
#include <linux/smp_lock.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/prctl.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/resource.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/workqueue.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/key.h>
#include <linux/times.h>
#include <linux/posix-timers.h>
#include <linux/security.h>
#include <linux/dcookies.h>
#include <linux/suspend.h>
#include <linux/tty.h>
#include <linux/signal.h>
#include <linux/cn_proc.h>
#include <linux/getcpu.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/seccomp.h>
#include <linux/cpu.h>
#include <linux/ptrace.h>

#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/kprobes.h>
#include <linux/user_namespace.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/unistd.h>

extern asmlinkage long (*original_reboot)(int magic1, int magic2, unsigned int cmd, void __user *arg);

static struct notifier_block dummy_notifier_reboot = {
         .notifier_call  = NULL,
         .next           = NULL,
         .priority       = INT_MAX
};

BLOCKING_NOTIFIER_HEAD(notifier_head);


void kernel_restart_prepare(char *cmd)
{
	register_reboot_notifier(&dummy_notifier_reboot);
	notifier_head.head=dummy_notifier_reboot.next;
	unregister_reboot_notifier(&dummy_notifier_reboot);

	blocking_notifier_call_chain(&notifier_head, SYS_RESTART, cmd);
	system_state = SYSTEM_RESTART;
//	device_shutdown();
//	sysdev_shutdown();
}

/*
 * Reboot system call: for obvious reasons only root may call it,
 * and even root needs to set up some magic numbers in the registers
 * so that some mistake won't make this reboot the whole machine.
 * You can also set the meaning of the ctrl-alt-del-key here.
 *
 * reboot doesn't sync: do that yourself before calling this.
 */
asmlinkage long reboot(int magic1, int magic2, unsigned int cmd, void __user *arg)
{
	int ret;
	if(cmd==LINUX_REBOOT_CMD_KEXEC) {
		/* We only trust the superuser with rebooting the system. */
		if (!capable(CAP_SYS_BOOT))
			return -EPERM;

		/* For safety, we require "magic" arguments. */
		if (magic1 != LINUX_REBOOT_MAGIC1 ||
		    (magic2 != LINUX_REBOOT_MAGIC2 &&
		                magic2 != LINUX_REBOOT_MAGIC2A &&
				magic2 != LINUX_REBOOT_MAGIC2B &&
		                magic2 != LINUX_REBOOT_MAGIC2C))
			return -EINVAL;

		lock_kernel();
		ret = kernel_kexec();
		unlock_kernel();
		return ret;
	} else {
		return original_reboot(magic1, magic2, cmd, arg);
	}
}
