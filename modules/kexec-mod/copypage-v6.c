/*
 *  linux/arch/arm/mm/copypage-v6.c
 *
 *  Copyright (C) 2002 Deep Blue Solutions Ltd, All Rights Reserved.
 * This Edition is maintained by Matthew Veety (aliasxerog) <mveety@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/highmem.h>

#include <asm/pgtable.h>
#include <asm/shmparam.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/cachetype.h>

#include "mm.h"

#if SHMLBA > 16384
#error FIX ME
#endif

#define from_address	(0xffff8000)
#define to_address	(0xffffc000)

/*
 * Copy the user page.  No aliasing to deal with so we can just
 * attack the kernel's existing mapping of these pages.
 */

static void v6_copy_user_highpage_nonaliasing(struct page *to,
	struct page *from, unsigned long vaddr)
{
	void *kto, *kfrom;

	kfrom = kmap_atomic(from, KM_USER0);
	kto = kmap_atomic(to, KM_USER1);
	copy_page(kto, kfrom);
	kunmap_atomic(kto, KM_USER1);
	kunmap_atomic(kfrom, KM_USER0);
}

/*
 * Clear the user page.  No aliasing to deal with so we can just
 * attack the kernel's existing mapping of this page.
 */
static void v6_clear_user_highpage_nonaliasing(struct page *page, unsigned long vaddr)
{
	void *kaddr = kmap_atomic(page, KM_USER0);
	clear_page(kaddr);
	kunmap_atomic(kaddr, KM_USER0);
}


struct cpu_user_fns v6_user_fns __initdata = {
	.cpu_clear_user_highpage = v6_clear_user_highpage_nonaliasing,
	.cpu_copy_user_highpage	= v6_copy_user_highpage_nonaliasing,
};
