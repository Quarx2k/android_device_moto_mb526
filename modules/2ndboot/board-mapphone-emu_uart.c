/*
 * board-mapphone-emu_uart.c
 *
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Date         Author          Comment
 * ===========  ==============  ==============================================
 * Jun-26-2009  Motorola	Initial revision.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <linux/spi/spi.h>
#include <mach/system.h>
#include <linux/irq.h>

#include <plat/dma.h>
#include <plat/clock.h>
#include <plat/board-mapphone-emu_uart.h>
#include <plat/hardware.h>
#include <plat/omap34xx.h>

#define OMAP2_MCSPI_MAX_FREQ		48000000

#define OMAP2_MCSPI_REVISION		0x00
#define OMAP2_MCSPI_SYSCONFIG		0x10
#define OMAP2_MCSPI_SYSSTATUS		0x14
#define OMAP2_MCSPI_IRQSTATUS		0x18
#define OMAP2_MCSPI_IRQENABLE		0x1c
#define OMAP2_MCSPI_WAKEUPENABLE	0x20
#define OMAP2_MCSPI_SYST		0x24
#define OMAP2_MCSPI_MODULCTRL		0x28

#define OMAP2_MCSPI_CHCONF0		0x2c
#define OMAP2_MCSPI_CHSTAT0		0x30
#define OMAP2_MCSPI_CHCTRL0		0x34
#define OMAP2_MCSPI_TX0			0x38
#define OMAP2_MCSPI_RX0			0x3c

#define OMAP2_MCSPI_SYSCONFIG_AUTOIDLE	(1 << 0)
#define OMAP2_MCSPI_SYSCONFIG_SOFTRESET	(1 << 1)
#define OMAP2_AFTR_RST_SET_MASTER	(0 << 2)

#define OMAP2_MCSPI_SYSSTATUS_RESETDONE	(1 << 0)
#define OMAP2_MCSPI_SYS_CON_LVL_1 1
#define OMAP2_MCSPI_SYS_CON_LVL_2 2

#define OMAP2_MCSPI_MODULCTRL_SINGLE	(1 << 0)
#define OMAP2_MCSPI_MODULCTRL_MS	(1 << 2)
#define OMAP2_MCSPI_MODULCTRL_STEST	(1 << 3)

#define OMAP2_MCSPI_CHCONF_PHA		(1 << 0)
#define OMAP2_MCSPI_CHCONF_POL		(1 << 1)
#define OMAP2_MCSPI_CHCONF_CLKD_MASK	(0x0f << 2)
#define OMAP2_MCSPI_CHCONF_EPOL		(1 << 6)
#define OMAP2_MCSPI_CHCONF_WL_MASK	(0x1f << 7)
#define OMAP2_MCSPI_CHCONF_TRM_RX_ONLY	(0x01 << 12)
#define OMAP2_MCSPI_CHCONF_TRM_TX_ONLY	(0x02 << 12)
#define OMAP2_MCSPI_CHCONF_TRM_MASK	(0x03 << 12)
#define OMAP2_MCSPI_CHCONF_TRM_TXRX	(~OMAP2_MCSPI_CHCONF_TRM_MASK)
#define OMAP2_MCSPI_CHCONF_DMAW		(1 << 14)
#define OMAP2_MCSPI_CHCONF_DMAR		(1 << 15)
#define OMAP2_MCSPI_CHCONF_DPE0		(1 << 16)
#define OMAP2_MCSPI_CHCONF_DPE1		(1 << 17)
#define OMAP2_MCSPI_CHCONF_IS		(1 << 18)
#define OMAP2_MCSPI_CHCONF_TURBO	(1 << 19)
#define OMAP2_MCSPI_CHCONF_FORCE	(1 << 20)

#define OMAP2_MCSPI_SYSCFG_WKUP		(1 << 2)
#define OMAP2_MCSPI_SYSCFG_IDL		(2 << 3)
#define OMAP2_MCSPI_SYSCFG_CLK		(2 << 8)
#define OMAP2_MCSPI_WAKEUP_EN		(1 << 1)
#define OMAP2_MCSPI_IRQ_WKS		(1 << 16)
#define OMAP2_MCSPI_CHSTAT_RXS		(1 << 0)
#define OMAP2_MCSPI_CHSTAT_TXS		(1 << 1)
#define OMAP2_MCSPI_CHSTAT_EOT		(1 << 2)

#define OMAP2_MCSPI_CHCTRL_EN		(1 << 0)
#define OMAP2_MCSPI_MODE_IS_MASTER	0
#define OMAP2_MCSPI_MODE_IS_SLAVE	1
#define OMAP_MCSPI_WAKEUP_ENABLE	1

#define OMAP_MCSPI_BASE                 0xd8098000

#define WORD_LEN            32
#define CLOCK_DIV           12

#define LEVEL1              1
#define LEVEL2              2
#define WRITE_CPCAP         1
#define READ_CPCAP          0

#define CM_ICLKEN1_CORE  0xd8004A10
#define CM_FCLKEN1_CORE  0xd8004A00
#define OMAP2_MCSPI_EN_MCSPI1   (1 << 18)

#define  RESET_FAIL      1
#define RAW_MOD_REG_BIT(val, mask, set) do { \
    if (set) \
		val |= mask; \
    else \
		 val &= ~mask; \
} while (0)

struct cpcap_dev {
	u16 address;
	u16 value;
	u32 result;
	int access_flag;
};

static char tx[4];
static bool emu_uart_is_active = FALSE;

static inline void raw_writel_reg(u32 value, u32 reg)
{
	__raw_writel(value, OMAP_MCSPI_BASE + reg);
}

static inline u32 raw_readl_reg(u32 reg)
{
	u32 result;
	unsigned int absolute_reg;

	absolute_reg = OMAP_MCSPI_BASE + reg;
	result = __raw_readl(absolute_reg);
	return result;
}

static void raw_omap_mcspi_wakeup_enable(int level)
{
	u32 result;

	if (level == LEVEL1) {
		result = raw_readl_reg(OMAP2_MCSPI_SYSCONFIG);
		result =
		    result | OMAP2_MCSPI_SYSCFG_WKUP |
		    OMAP2_MCSPI_SYSCFG_IDL | OMAP2_MCSPI_SYSCFG_CLK |
		    OMAP2_MCSPI_SYSCONFIG_AUTOIDLE;
		raw_writel_reg(result, OMAP2_MCSPI_SYSCONFIG);
	}

	if (level == LEVEL2) {
		result = raw_readl_reg(OMAP2_MCSPI_SYSCONFIG);
		result =
		    result | OMAP2_MCSPI_SYSCFG_WKUP |
		    OMAP2_MCSPI_SYSCFG_IDL |
		    OMAP2_MCSPI_SYSCONFIG_AUTOIDLE;
		RAW_MOD_REG_BIT(result, OMAP2_MCSPI_SYSCFG_CLK, 0);
		raw_writel_reg(result, OMAP2_MCSPI_SYSCONFIG);
	}

	raw_writel_reg(OMAP2_MCSPI_WAKEUP_EN, OMAP2_MCSPI_WAKEUPENABLE);

	result = raw_readl_reg(OMAP2_MCSPI_IRQENABLE);
	result = result | OMAP2_MCSPI_IRQ_WKS;
	raw_writel_reg(result, OMAP2_MCSPI_IRQENABLE);
}

static void raw_omap2_mcspi_set_master_mode(void)
{
	u32 result;

	result = raw_readl_reg(OMAP2_MCSPI_MODULCTRL);

	RAW_MOD_REG_BIT(result, OMAP2_MCSPI_MODULCTRL_STEST, 0);
	RAW_MOD_REG_BIT(result, OMAP2_MCSPI_MODULCTRL_MS,
			OMAP2_MCSPI_MODE_IS_MASTER);
	RAW_MOD_REG_BIT(result, OMAP2_MCSPI_MODULCTRL_SINGLE, 1);

	raw_writel_reg(result, OMAP2_MCSPI_MODULCTRL);
}

static void raw_omap2_mcspi_channel_config(void)
{
	u32 result;

	result = raw_readl_reg(OMAP2_MCSPI_CHCONF0);

	result &= ~OMAP2_MCSPI_CHCONF_IS;
	result &= ~OMAP2_MCSPI_CHCONF_DPE1;
	result |= OMAP2_MCSPI_CHCONF_DPE0;

	result &= ~OMAP2_MCSPI_CHCONF_WL_MASK;
	result |= (WORD_LEN - 1) << 7;

	result &= ~OMAP2_MCSPI_CHCONF_EPOL;

	result &= ~OMAP2_MCSPI_CHCONF_CLKD_MASK;
	result |= CLOCK_DIV << 2;

	result &= ~OMAP2_MCSPI_CHCONF_POL;
	result &= ~OMAP2_MCSPI_CHCONF_PHA;

	raw_writel_reg(result, OMAP2_MCSPI_CHCONF0);

}

static void raw_mcspi_setup(void)
{
	raw_omap_mcspi_wakeup_enable(LEVEL1);
	raw_omap2_mcspi_set_master_mode();
	raw_omap2_mcspi_channel_config();
	raw_omap_mcspi_wakeup_enable(LEVEL2);
}

static int raw_mcspi_reset(void)
{
	u32 tmp;

	raw_omap_mcspi_wakeup_enable(LEVEL1);

	raw_writel_reg(OMAP2_MCSPI_SYSCONFIG_SOFTRESET,
		      OMAP2_MCSPI_SYSCONFIG);

	do {
		tmp = raw_readl_reg(OMAP2_MCSPI_SYSSTATUS);
	} while (!(tmp & OMAP2_MCSPI_SYSSTATUS_RESETDONE));

	raw_writel_reg(OMAP2_AFTR_RST_SET_MASTER, OMAP2_MCSPI_MODULCTRL);

	raw_omap_mcspi_wakeup_enable(LEVEL1);
	raw_omap_mcspi_wakeup_enable(LEVEL2);

	return 0;
}

static void raw_omap2_mcspi_force_cs(int enable_tag)
{
	u32 result;
	result = raw_readl_reg(OMAP2_MCSPI_CHCONF0);
	RAW_MOD_REG_BIT(result, OMAP2_MCSPI_CHCONF_FORCE, enable_tag);
	raw_writel_reg(result, OMAP2_MCSPI_CHCONF0);
}

static void raw_omap2_mcspi_set_enable(int enable)
{
	u32 result;

	result = enable ? OMAP2_MCSPI_CHCTRL_EN : 0;
	raw_writel_reg(result, OMAP2_MCSPI_CHCTRL0);
}


static int raw_mcspi_wait_for_reg_bit(unsigned long reg, unsigned long bit)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(1000);

	while (!(raw_readl_reg(reg) & bit)) {
		if (time_after(jiffies, timeout))
			return -1;
	}

	return 0;
}

static void parser_cpcap(struct cpcap_dev *dev)
{
	if (dev->access_flag == WRITE_CPCAP) {
		tx[3] = ((dev->address >> 6) & 0x000000FF) | 0x80;
		tx[2] = (dev->address << 2) & 0x000000FF;
		tx[1] = (dev->value >> 8) & 0x000000FF;
		tx[0] = dev->value & 0x000000FF;
	} else {
		tx[3] = ((dev->address >> 6) & 0x000000FF);
		tx[2] = (dev->address << 2) & 0x000000FF;
		tx[1] = 1;
		tx[0] = 1;
	}
}

static void raw_omap2_mcspi_txrx_pio(struct cpcap_dev *dev)
{
	u32 result;
	u32 tx_32bit;

	result = raw_readl_reg(OMAP2_MCSPI_CHCONF0);
	result &= ~OMAP2_MCSPI_CHCONF_TRM_MASK;
	raw_writel_reg(result, OMAP2_MCSPI_CHCONF0);

	raw_omap2_mcspi_set_enable(1);

	parser_cpcap(dev);
	memcpy((void *)&tx_32bit, (void *)tx, 4);

	if (raw_mcspi_wait_for_reg_bit(OMAP2_MCSPI_CHSTAT0,
				       OMAP2_MCSPI_CHSTAT_TXS) < 0) {
		goto out;
	}
	raw_writel_reg(tx_32bit, OMAP2_MCSPI_TX0);

	if (raw_mcspi_wait_for_reg_bit(OMAP2_MCSPI_CHSTAT0,
				       OMAP2_MCSPI_CHSTAT_RXS) < 0) {
		goto out;
	}

	result = raw_readl_reg(OMAP2_MCSPI_RX0);

	dev->result = result;

out:
	raw_omap2_mcspi_set_enable(0);
}

static void raw_mcspi_run(struct cpcap_dev *dev)
{
	raw_omap_mcspi_wakeup_enable(LEVEL1);
	raw_omap2_mcspi_set_master_mode();
	raw_omap2_mcspi_channel_config();
	raw_omap2_mcspi_force_cs(1);
	raw_omap2_mcspi_txrx_pio(dev);
	raw_omap2_mcspi_force_cs(0);
	raw_omap_mcspi_wakeup_enable(LEVEL2);
}

static void raw_omap_mcspi_enable_IFclock(void)
{
	u32 result;

	result = __raw_readl(CM_FCLKEN1_CORE);
	RAW_MOD_REG_BIT(result, OMAP2_MCSPI_EN_MCSPI1, 1);
	__raw_writel(result, CM_FCLKEN1_CORE);

	result = __raw_readl(CM_ICLKEN1_CORE);
	RAW_MOD_REG_BIT(result, OMAP2_MCSPI_EN_MCSPI1, 1);
	__raw_writel(result, CM_ICLKEN1_CORE);

}

static int write_cpcap_register_raw(u16 addr, u16 val)
{
	int result;
	struct cpcap_dev cpcap_write;

	raw_omap_mcspi_enable_IFclock();

	result = raw_mcspi_reset();
	if (result < 0) {
		return result;
	}

	raw_mcspi_setup();

	cpcap_write.address = addr;
	cpcap_write.value = val;
	cpcap_write.access_flag = WRITE_CPCAP;
	raw_mcspi_run(&cpcap_write);

	return result;
}

static int read_cpcap_register_raw(u16 addr, u16 *val)
{
	int result;
	struct cpcap_dev cpcap_read;

	raw_omap_mcspi_enable_IFclock();

	result = raw_mcspi_reset();
	if (result < 0) {
		return result;
	}

	raw_mcspi_setup();

	cpcap_read.address = addr;
	cpcap_read.access_flag = READ_CPCAP;
	raw_mcspi_run(&cpcap_read);
	*val = cpcap_read.result;

	return result;
}

int is_emu_uart_iomux_reg(unsigned short offset)
{
	if ((emu_uart_is_active) && \
	    ((offset >= 0x1A2 && offset < 0x1BA) || (offset == 0x19E)))
		return 1;
	else
		return 0;
}

bool is_emu_uart_active(void)
{
	return emu_uart_is_active;
}

static void write_omap_mux_register(u16 offset, u8 mode, u8 input_en)
{
	u16 tmp_val, reg_val;
	u32 reg = OMAP343X_CTRL_BASE + offset;

	reg_val = mode | (input_en << 8);
	tmp_val = omap_readw(reg) & ~(0x0007 | (1 << 8));
	reg_val = reg_val | tmp_val;
	omap_writew(reg_val, reg);
}

void activate_emu_uart(void)
{
	int i;

	for (i = 0; i < 0x18; i += 2)
		write_omap_mux_register(0x1A2 + i, 7, 0);

	write_cpcap_register_raw(897, 0x0101);
	write_cpcap_register_raw(411, 0x014C);

	write_omap_mux_register(0x19E, 7, 0);
	write_omap_mux_register(0x1AA, 2, 0);
	write_omap_mux_register(0x1AC, 2, 1);

	emu_uart_is_active = TRUE;
}
