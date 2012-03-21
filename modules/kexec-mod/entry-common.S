/*
 *  linux/arch/arm/kernel/entry-common.S
 *
 *  Copyright (C) 2000 Russell King
 * This Edition is maintained by Matthew Veety (aliasxerog) <mveety@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/unistd.h>
#include <asm/ftrace.h>
#include <mach/entry-macro.S>

#include "entry-header.S"

ENTRY(pabort_ifar)
		mrc	p15, 0, r0, cr6, cr0, 2
ENTRY(pabort_noifar)
		mov	pc, lr
ENDPROC(pabort_ifar)
ENDPROC(pabort_noifar)

