/*
 * arch/arm/mach-tegra/board-grouper.c
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/spi/spi.h>
#include <linux/tegra_uart.h>
#include <linux/memblock.h>
#include <linux/spi-tegra.h>
#include <linux/nfc/pn544.h>
#include <linux/skbuff.h>
#include <linux/ti_wilink_st.h>
#include <linux/regulator/consumer.h>
#include <linux/smb349-charger.h>
#include <linux/max17048_battery.h>
#include <linux/leds.h>
#include <linux/i2c/at24.h>
#include <linux/of_platform.h>

#include <asm/hardware/gic.h>

#include <mach/clk.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/pinmux-tegra30.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <mach/io_dpd.h>
#include <mach/i2s.h>
#include <mach/tegra_asoc_pdata.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/usb_phy.h>
#include <mach/thermal.h>
#include <mach/gpio-tegra.h>
#include <mach/tegra_fiq_debugger.h>
 
#include "board.h"
#include "clock.h"
#include "board-grouper.h"
#include "board-touch.h"
#include "devices.h"
#include "gpio-names.h"
#include "fuse.h"
#include "pm.h"
#include "wdt-recovery.h"
#include "common.h"

static struct balanced_throttle throttle_list[] = {
	{
		.tegra_cdev = {
			.id = CDEV_BTHROT_ID_TJ,
		}
		.throt_tab_size = 10,
		.throt_tab = {
			{      0, 1000 },
			{ 640000, 1000 },
			{ 640000, 1000 },
			{ 640000, 1000 },
			{ 640000, 1000 },
			{ 640000, 1000 },
			{ 760000, 1000 },
			{ 760000, 1050 },
			{1000000, 1050 },
			{1000000, 1100 },
		},
	},
};

/* All units are in millicelsius */
static struct tegra_thermal_bind thermal_binds[] = {
	{
		.tdev_id = THERMAL_DEVICE_ID_NCT_EXT,
		.cdev_id = CDEV_BTHROT_ID_TJ,
		.type = THERMAL_TRIP_PASSIVE,
		.passive = {
			.trip_temp = 85000,
			.tc1 = 0,
			.tc2 = 1,
			.passive_delay = 2000,
		}
	},
	{
		.tdev_id = THERMAL_DEVICE_ID_NULL,
	},
};

static unsigned long retry_suspend;

static int plat_kim_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct kim_data_s *kim_gdata;
	struct st_data_s *core_data;
	kim_gdata = dev_get_drvdata(&pdev->dev);
	core_data = kim_gdata->core_data;
	if (st_ll_getstate(core_data) != ST_LL_INVALID) {
		/* Prevent suspend until sleep indication from chip */
		while (st_ll_getstate(core_data) != ST_LL_ASLEEP &&
			(retry_suspend++ < 5)) {
				return -1;
		}
	}
	return 0;
}

static int plat_kim_resume(struct platform_device *pdev)
{
	retry_suspend = 0;
	return 0;
}


/* wl128x BT, FM, GPS connectivity chip */
struct ti_st_plat_data wilink_pdata = {
	.nshutdown_gpio	= TEGRA_GPIO_PU0,
	.dev_name	= BLUETOOTH_UART_DEV_NAME,
	.flow_cntrl	= 1,
	.baud_rate	= 3000000,
	.suspend	= plat_kim_suspend,
	.resume		= plat_kim_resume,
};

static struct platform_device wl128x_device = {
	.name		= "kim",
	.id		= -1,
	.dev.platform_data = &wilink_pdata,
};

static struct platform_device btwilink_device = {
	.name = "btwilink",
	.id = -1,
};

static noinline void __init grouper_bt_st(void)
{
	pr_info("grouper_bt_st");

	platform_device_register(&wl128x_device);
	platform_device_register(&btwilink_device);
	return;
}

static __initdata struct tegra_clk_init_table grouper_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "pll_m",	NULL,		0,		false},
	{ "hda",	"pll_p",	108000000,	false},
	{ "hda2codec_2x", "pll_p",	48000000,	false},
	{ "pwm",	"pll_p",	3187500,	false},
	{ "blink",	"clk_32k",	32768,		true},
	{ "i2s1",	"pll_a_out0",	0,		false},
	{ "i2s3",	"pll_a_out0",	0,		false},
	{ "i2s4",	"pll_a_out0",	0,		false},
	{ "spdif_out",	"pll_a_out0",	0,		false},
	{ "d_audio",	"clk_m",	12000000,	false},
	{ "dam0",	"clk_m",	12000000,	false},
	{ "dam1",	"clk_m",	12000000,	false},
	{ "dam2",	"clk_m",	12000000,	false},
	{ "audio1",	"i2s1_sync",	0,		false},
	{ "audio3",	"i2s3_sync",	0,		false},
	{ "vi_sensor",	"pll_p",	150000000,	false},
	{ "i2c1",	"pll_p",	3200000,	false},
	{ "i2c2",	"pll_p",	3200000,	false},
	{ "i2c3",	"pll_p",	3200000,	false},
	{ "i2c4",	"pll_p",	3200000,	false},
	{ "i2c5",	"pll_p",	3200000,	false},
	{ NULL,		NULL,		0,		0},
};

static struct pn544_i2c_platform_data nfc_pdata = {
	.irq_gpio = TEGRA_GPIO_PX0,
	.ven_gpio = TEGRA_GPIO_PS7,
	.firm_gpio = TEGRA_GPIO_PR3,
};

static struct i2c_board_info __initdata grouper_nfc_board_info[] = {
	{
		I2C_BOARD_INFO("pn544", 0x28),
		.platform_data = &nfc_pdata,
	},
};

static struct tegra_i2c_platform_data grouper_i2c1_platform_data = {
	.adapter_nr	= 0,
	.bus_count	= 1,
	.bus_clk_rate	= { 100000, 0 },
	.scl_gpio		= {TEGRA_GPIO_PC4, 0},
	.sda_gpio		= {TEGRA_GPIO_PC5, 0},
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data grouper_i2c2_platform_data = {
	.adapter_nr	= 1,
	.bus_count	= 1,
	.bus_clk_rate	= { 100000, 0 },
	.is_clkon_always = true,
	.scl_gpio		= {TEGRA_GPIO_PT5, 0},
	.sda_gpio		= {TEGRA_GPIO_PT6, 0},
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data grouper_i2c3_platform_data = {
	.adapter_nr	= 2,
	.bus_count	= 1,
	.bus_clk_rate	= { 100000, 0 },
	.scl_gpio		= {TEGRA_GPIO_PBB1, 0},
	.sda_gpio		= {TEGRA_GPIO_PBB2, 0},
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data grouper_i2c4_platform_data = {
	.adapter_nr	= 3,
	.bus_count	= 1,
	.bus_clk_rate	= { 10000, 0 },
	.scl_gpio		= {TEGRA_GPIO_PV4, 0},
	.sda_gpio		= {TEGRA_GPIO_PV5, 0},
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data grouper_i2c5_platform_data = {
	.adapter_nr	= 4,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.scl_gpio		= {TEGRA_GPIO_PZ6, 0},
	.sda_gpio		= {TEGRA_GPIO_PZ7, 0},
	.arb_recovery = arb_lost_recovery,
};

struct max17048_battery_model max17048_mdata = {
	.rcomp          = 170,
	.soccheck_A     = 252,
	.soccheck_B     = 254,
	.bits           = 19,
	.alert_threshold = 0x00,
	.one_percent_alerts = 0x40,
	.alert_on_reset = 0x40,
	.rcomp_seg      = 0x0800,
	.hibernate      = 0x3080,
	.vreset         = 0x9696,
	.valert         = 0xD4AA,
	.ocvtest        = 55600,
};


static struct at24_platform_data eeprom_info = {
	.byte_len	= (256*1024)/8,
	.page_size	= 64,
	.flags		= AT24_FLAG_ADDR16,
	.setup		= get_mac_addr,
};

static struct i2c_board_info grouper_i2c4_max17048_board_info[] = {
	{
		I2C_BOARD_INFO("max17048", 0x36),
		.platform_data = &max17048_mdata,
	},
};

static struct i2c_board_info grouper_eeprom_mac_add = {
	I2C_BOARD_INFO("at24", 0x56),
	.platform_data = &eeprom_info,
};

static struct regulator_consumer_supply smb349_vbus_supply[] = {
	REGULATOR_SUPPLY("usb_bat_chg", NULL),
};

static struct regulator_consumer_supply smb349_otg_vbus_supply[] = {
	REGULATOR_SUPPLY("usb_vbus", "tegra-ehci.0"),
};

static struct smb349_charger_platform_data smb349_charger_pdata = {
	.max_charge_current_mA = 1000,
	.charging_term_current_mA = 100,
	.consumer_supplies = smb349_vbus_supply,
	.num_consumer_supplies = ARRAY_SIZE(smb349_vbus_supply),
	.otg_consumer_supplies = smb349_otg_vbus_supply,
	.num_otg_consumer_supplies = ARRAY_SIZE(smb349_otg_vbus_supply),
};

static struct i2c_board_info grouper_i2c4_smb349_board_info[] = {
	{
		I2C_BOARD_INFO("smb349", 0x1B),
		.platform_data = &smb349_charger_pdata,
	},
};

static struct i2c_board_info __initdata rt5640_board_info = {
	I2C_BOARD_INFO("rt5640", 0x1c),
};

static struct i2c_board_info __initdata rt5639_board_info = {
	I2C_BOARD_INFO("rt5639", 0x1c),
};

static void grouper_i2c_init(void)
{
	struct board_info board_info;

	tegra_get_board_info(&board_info);

	tegra_i2c_device1.dev.platform_data = &grouper_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &grouper_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &grouper_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &grouper_i2c4_platform_data;
	tegra_i2c_device5.dev.platform_data = &grouper_i2c5_platform_data;

	platform_device_register(&tegra_i2c_device5);
	platform_device_register(&tegra_i2c_device4);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device1);

	i2c_register_board_info(4, grouper_i2c4_smb349_board_info,
		ARRAY_SIZE(grouper_i2c4_smb349_board_info));

	rt5639_board_info.irq = rt5640_board_info.irq =
		gpio_to_irq(TEGRA_GPIO_CDC_IRQ);
	if (board_info.fab == BOARD_FAB_A00)
		i2c_register_board_info(4, &rt5640_board_info, 1);
	else
		i2c_register_board_info(4, &rt5639_board_info, 1);

	i2c_register_board_info(4, &grouper_eeprom_mac_add, 1);

	i2c_register_board_info(4, grouper_i2c4_max17048_board_info,
		ARRAY_SIZE(grouper_i2c4_max17048_board_info));

	grouper_nfc_board_info[0].irq = gpio_to_irq(TEGRA_GPIO_PX0);
	i2c_register_board_info(0, grouper_nfc_board_info, 1);
}

static struct platform_device *grouper_uart_devices[] __initdata = {
	&tegra_uarta_device,
	&tegra_uartb_device,
	&tegra_uartc_device,
	&tegra_uartd_device,
	&tegra_uarte_device,
};
static struct uart_clk_parent uart_parent_clk[] = {
	[0] = {.name = "clk_m"},
	[1] = {.name = "pll_p"},
#ifndef CONFIG_TEGRA_PLLM_RESTRICTED
	[2] = {.name = "pll_m"},
#endif
};

static struct tegra_uart_platform_data grouper_uart_pdata;
static struct tegra_uart_platform_data grouper_loopback_uart_pdata;

static void __init uart_debug_init(void)
{
	int debug_port_id;

	debug_port_id = get_tegra_uart_debug_port_id();
	if (debug_port_id < 0)
		debug_port_id = 3;

	switch (debug_port_id) {
	case 0:
		/* UARTA is the debug port. */
		pr_info("Selecting UARTA as the debug console\n");
		grouper_uart_devices[0] = &debug_uarta_device;
		debug_uart_clk = clk_get_sys("serial8250.0", "uarta");
		debug_uart_port_base = ((struct plat_serial8250_port *)(
			debug_uarta_device.dev.platform_data))->mapbase;
		break;

	case 1:
		/* UARTB is the debug port. */
		pr_info("Selecting UARTB as the debug console\n");
		grouper_uart_devices[1] = &debug_uartb_device;
		debug_uart_clk = clk_get_sys("serial8250.0", "uartb");
		debug_uart_port_base = ((struct plat_serial8250_port *)(
			debug_uartb_device.dev.platform_data))->mapbase;
		break;

	case 2:
		/* UARTC is the debug port. */
		pr_info("Selecting UARTC as the debug console\n");
		grouper_uart_devices[2] = &debug_uartc_device;
		debug_uart_clk = clk_get_sys("serial8250.0", "uartc");
		debug_uart_port_base = ((struct plat_serial8250_port *)(
			debug_uartc_device.dev.platform_data))->mapbase;
		break;

	case 3:
		/* UARTD is the debug port. */
		pr_info("Selecting UARTD as the debug console\n");
		grouper_uart_devices[3] = &debug_uartd_device;
		debug_uart_clk = clk_get_sys("serial8250.0", "uartd");
		debug_uart_port_base = ((struct plat_serial8250_port *)(
			debug_uartd_device.dev.platform_data))->mapbase;
		break;

	case 4:
		/* UARTE is the debug port. */
		pr_info("Selecting UARTE as the debug console\n");
		grouper_uart_devices[4] = &debug_uarte_device;
		debug_uart_clk = clk_get_sys("serial8250.0", "uarte");
		debug_uart_port_base = ((struct plat_serial8250_port *)(
			debug_uarte_device.dev.platform_data))->mapbase;
		break;

	default:
		pr_info("The debug console id %d is invalid, Assuming UARTA",
			debug_port_id);
		grouper_uart_devices[0] = &debug_uarta_device;
		debug_uart_clk = clk_get_sys("serial8250.0", "uarta");
		debug_uart_port_base = ((struct plat_serial8250_port *)(
			debug_uarta_device.dev.platform_data))->mapbase;
		break;
	}
	return;
}

static void __init grouper_uart_init(void)
{
	struct clk *c;
	int i;

	for (i = 0; i < ARRAY_SIZE(uart_parent_clk); ++i) {
		c = tegra_get_clock_by_name(uart_parent_clk[i].name);
		if (IS_ERR_OR_NULL(c)) {
			pr_err("Not able to get the clock for %s\n",
						uart_parent_clk[i].name);
			continue;
		}
		uart_parent_clk[i].parent_clk = c;
		uart_parent_clk[i].fixed_clk_rate = clk_get_rate(c);
	}
	grouper_uart_pdata.parent_clk_list = uart_parent_clk;
	grouper_uart_pdata.parent_clk_count = ARRAY_SIZE(uart_parent_clk);
	grouper_loopback_uart_pdata.parent_clk_list = uart_parent_clk;
	grouper_loopback_uart_pdata.parent_clk_count =
						ARRAY_SIZE(uart_parent_clk);
	grouper_loopback_uart_pdata.is_loopback = true;
	tegra_uarta_device.dev.platform_data = &grouper_uart_pdata;
	tegra_uartb_device.dev.platform_data = &grouper_uart_pdata;
	tegra_uartc_device.dev.platform_data = &grouper_uart_pdata;
	tegra_uartd_device.dev.platform_data = &grouper_uart_pdata;
	/* UARTE is used for loopback test purpose */
	tegra_uarte_device.dev.platform_data = &grouper_loopback_uart_pdata;

	/* Register low speed only if it is selected */
	if (!is_tegra_debug_uartport_hs()) {
		uart_debug_init();
		/* Clock enable for the debug channel */
		if (!IS_ERR_OR_NULL(debug_uart_clk)) {
			pr_info("The debug console clock name is %s\n",
						debug_uart_clk->name);
			c = tegra_get_clock_by_name("pll_p");
			if (IS_ERR_OR_NULL(c))
				pr_err("Not getting the parent clock pll_p\n");
			else
				clk_set_parent(debug_uart_clk, c);

			clk_enable(debug_uart_clk);
			clk_set_rate(debug_uart_clk, clk_get_rate(c));
		} else {
			pr_err("Not getting the clock %s for debug console\n",
					debug_uart_clk->name);
		}
	}

	platform_add_devices(grouper_uart_devices,
				ARRAY_SIZE(grouper_uart_devices));
}

static struct platform_device tegra_camera = {
	.name = "tegra_camera",
	.id = -1,
};

static struct platform_device *grouper_spi_devices[] __initdata = {
	&tegra_spi_device4,
	&tegra_spi_device1,
};

static struct spi_clk_parent spi_parent_clk[] = {
	[0] = {.name = "pll_p"},
#ifndef CONFIG_TEGRA_PLLM_RESTRICTED
	[1] = {.name = "pll_m"},
	[2] = {.name = "clk_m"},
#else
	[1] = {.name = "clk_m"},
#endif
};

static struct tegra_spi_platform_data grouper_spi4_pdata = {
	.is_dma_based		= true,
	.max_dma_buffer		= (16 * 1024),
	.is_clkon_always	= false,
	.max_rate		= 100000000,
};

static struct tegra_spi_platform_data grouper_spi1_pdata = {
		.is_dma_based           = true,
		.max_dma_buffer         = (128),
		.is_clkon_always        = false,
		.max_rate               = 100000000,
};

static void __init grouper_spi_init(void)
{
	int i;
	struct clk *c;

	for (i = 0; i < ARRAY_SIZE(spi_parent_clk); ++i) {
		c = tegra_get_clock_by_name(spi_parent_clk[i].name);
		if (IS_ERR_OR_NULL(c)) {
			pr_err("Not able to get the clock for %s\n",
						spi_parent_clk[i].name);
			continue;
		}
		spi_parent_clk[i].parent_clk = c;
		spi_parent_clk[i].fixed_clk_rate = clk_get_rate(c);
	}
	grouper_spi4_pdata.parent_clk_list = spi_parent_clk;
	grouper_spi4_pdata.parent_clk_count = ARRAY_SIZE(spi_parent_clk);
	tegra_spi_device4.dev.platform_data = &grouper_spi4_pdata;

	grouper_spi1_pdata.parent_clk_list = spi_parent_clk;
	grouper_spi1_pdata.parent_clk_count = ARRAY_SIZE(spi_parent_clk);
	tegra_spi_device1.dev.platform_data = &grouper_spi1_pdata;
	platform_add_devices(grouper_spi_devices,
				ARRAY_SIZE(grouper_spi_devices));

}

static struct resource tegra_rtc_resources[] = {
	[0] = {
		.start = TEGRA_RTC_BASE,
		.end = TEGRA_RTC_BASE + TEGRA_RTC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = INT_RTC,
		.end = INT_RTC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device tegra_rtc_device = {
	.name = "tegra_rtc",
	.id   = -1,
	.resource = tegra_rtc_resources,
	.num_resources = ARRAY_SIZE(tegra_rtc_resources),
};

static struct tegra_asoc_platform_data grouper_audio_pdata = {
	.gpio_spkr_en		= TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det		= TEGRA_GPIO_HP_DET,
	.gpio_hp_mute		= -1,
	.gpio_int_mic_en	= TEGRA_GPIO_INT_MIC_EN,
	.gpio_ext_mic_en	= TEGRA_GPIO_EXT_MIC_EN,
	.gpio_ldo1_en           = TEGRA_GPIO_PX2,
	.i2s_param[HIFI_CODEC]  = {
		.audio_port_id	= 0,
		.is_i2s_master	= 1,
		.i2s_mode	= TEGRA_DAIFMT_I2S,
	},
	.i2s_param[BASEBAND]	= {
		.audio_port_id	= -1,
	},
	.i2s_param[BT_SCO]	= {
		.audio_port_id	= 3,
		.is_i2s_master	= 1,
		.i2s_mode	= TEGRA_DAIFMT_DSP_A,
	},
};

static struct platform_device grouper_audio_device = {
	.name	= "tegra-snd-rt5640",
	.id	= 0,
	.dev	= {
		.platform_data = &grouper_audio_pdata,
	},
};

static struct gpio_led grouper_led_info[] = {
	{
		.name			= "statled",
		.default_trigger	= "default-on",
		.gpio			= TEGRA_GPIO_STAT_LED,
		.active_low		= 1,
		.retain_state_suspended	= 0,
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
};

static struct gpio_led_platform_data grouper_leds_pdata = {
	.leds		= grouper_led_info,
	.num_leds	= ARRAY_SIZE(grouper_led_info),
};

static struct platform_device grouper_leds_gpio_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &grouper_leds_pdata,
	},
};

static struct platform_device *grouper_devices[] __initdata = {
	&tegra_pmu_device,
	&tegra_rtc_device,
	&tegra_udc_device,
	&tegra_wdt0_device,
	&tegra_wdt1_device,
	&tegra_wdt2_device,
#if defined(CONFIG_TEGRA_AVP)
	&tegra_avp_device,
#endif
	&tegra_camera,
#if defined(CONFIG_CRYPTO_DEV_TEGRA_SE)
	&tegra_se_device,
#endif
	&tegra_ahub_device,
	&tegra_dam_device0,
	&tegra_dam_device1,
	&tegra_dam_device2,
	&tegra_i2s_device1,
	&tegra_i2s_device3,
	&tegra_i2s_device4,
	&tegra_spdif_device,
	&spdif_dit_device,
	&bluetooth_dit_device,
	&tegra_pcm_device,
	&grouper_audio_device,
	&grouper_leds_gpio_device,
	&tegra_hda_device,
#if defined(CONFIG_CRYPTO_DEV_TEGRA_AES)
	&tegra_aes_device,
#endif
};

static __initdata struct tegra_clk_init_table spi_clk_init_table[] = {
	/* name         parent          rate            enabled */
	{ "sbc1",       "pll_p",        52000000,       true},
	{ NULL,         NULL,           0,              0},
};

static __initdata struct tegra_clk_init_table touch_clk_init_table[] = {
	/* name         parent          rate            enabled */
	{ "extern3",    "pll_p",        41000000,       true},
	{ "clk_out_3",  "extern3",      40800000,       true},
	{ NULL,         NULL,           0,              0},
};

static int __init grouper_touch_init(void)
{
	int touch_id;

	gpio_request(GROUPER_TS_ID1, "touch-id1");
	gpio_direction_input(GROUPER_TS_ID1);

	gpio_request(GROUPER_TS_ID2, "touch-id2");
	gpio_direction_input(GROUPER_TS_ID2);

	touch_id = gpio_get_value(GROUPER_TS_ID1) << 1;
	touch_id |= gpio_get_value(GROUPER_TS_ID2);

	pr_info("touch-id %d\n", touch_id);

	/* Disable TS_ID GPIO to save power */
	gpio_direction_output(GROUPER_TS_ID1, 0);
	tegra_pinmux_set_pullupdown(GROUPER_TS_ID1_PG, TEGRA_PUPD_NORMAL);
	tegra_pinmux_set_tristate(GROUPER_TS_ID1_PG, TEGRA_TRI_TRISTATE);
	gpio_direction_output(GROUPER_TS_ID2, 0);
	tegra_pinmux_set_pullupdown(GROUPER_TS_ID2_PG, TEGRA_PUPD_NORMAL);
	tegra_pinmux_set_tristate(GROUPER_TS_ID2_PG, TEGRA_TRI_TRISTATE);

	switch (touch_id) {
	case 0:
		pr_info("Raydium PCB based touch init\n");
		tegra_clk_init_from_table(spi_clk_init_table);
		touch_init_raydium(TEGRA_GPIO_PZ3, TEGRA_GPIO_PN5, 0);
		break;
	case 1:
		pr_info("Raydium On-Board touch init\n");
		tegra_clk_init_from_table(spi_clk_init_table);
		tegra_clk_init_from_table(touch_clk_init_table);
		clk_enable(tegra_get_clock_by_name("clk_out_3"));

		touch_init_raydium(TEGRA_GPIO_PZ3, TEGRA_GPIO_PN5, 1);
		break;
	case 3:
		pr_info("Synaptics PCB based touch init\n");
		touch_init_synaptics_grouper();
		break;
	default:
		pr_err("touch_id error, no touch %d\n", touch_id);
	}
	return 0;
}

static struct tegra_usb_platform_data tegra_udc_pdata = {
	.port_otg = true,
	.has_hostpc = true,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_DEVICE,
	.u_data.dev = {
		.vbus_pmu_irq = 0,
 		.vbus_gpio = -1,
		.charging_supported = false,
 		.remote_wakeup_supported = false,
	},
	.u_cfg.utmi = {
		.hssync_start_delay = 0,
		.elastic_limit = 16,
		.idle_wait_delay = 17,
		.term_range_adj = 6,
		.xcvr_setup = 8,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
		.xcvr_setup_offset = 0,
		.xcvr_use_fuses = 1,
	},
};

static struct tegra_usb_platform_data tegra_ehci1_utmi_pdata = {
	.port_otg = true,
	.has_hostpc = true,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_HOST,
	.u_data.host = {
		.vbus_gpio = -1,
		.hot_plug = true,
		.remote_wakeup_supported = true,
		.power_off_on_suspend = true,
	},
	.u_cfg.utmi = {
		.hssync_start_delay = 0,
		.elastic_limit = 16,
		.idle_wait_delay = 17,
		.term_range_adj = 6,
		.xcvr_setup = 15,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
		.xcvr_setup_offset = 0,
		.xcvr_use_fuses = 1,
	},
};

static struct tegra_usb_platform_data tegra_ehci2_utmi_pdata = {
	.port_otg = false,
	.has_hostpc = true,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode	= TEGRA_USB_OPMODE_HOST,
	.u_data.host = {
		.vbus_gpio = -1,
		.hot_plug = false,
		.remote_wakeup_supported = true,
		.power_off_on_suspend = true,
	},
	.u_cfg.utmi = {
		.hssync_start_delay = 0,
		.elastic_limit = 16,
		.idle_wait_delay = 17,
		.term_range_adj = 6,
		.xcvr_setup = 8,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
		.xcvr_setup_offset = 0,
		.xcvr_use_fuses = 1,
	},
};

static struct tegra_usb_otg_data tegra_otg_pdata = {
	.ehci_device = &tegra_ehci1_device,
	.ehci_pdata = &tegra_ehci1_utmi_pdata,
};

#if CONFIG_USB_SUPPORT
static void grouper_usb_init(void)
{
	tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	platform_device_register(&tegra_otg_device);

	/* Setup the udc platform data */
	tegra_udc_device.dev.platform_data = &tegra_udc_pdata;

	tegra_ehci2_device.dev.platform_data = &tegra_ehci2_utmi_pdata;
	platform_device_register(&tegra_ehci2_device);
}

static void grouper_modem_init(void)
{
	int ret;

	ret = gpio_request(TEGRA_GPIO_W_DISABLE, "w_disable_gpio");
	if (ret < 0)
		pr_err("%s: gpio_request failed for gpio %d\n",
			__func__, TEGRA_GPIO_W_DISABLE);
	else
		gpio_direction_output(TEGRA_GPIO_W_DISABLE, 1);


	ret = gpio_request(TEGRA_GPIO_MODEM_RSVD1, "Port_V_PIN_0");
	if (ret < 0)
		pr_err("%s: gpio_request failed for gpio %d\n",
			__func__, TEGRA_GPIO_MODEM_RSVD1);
	else
		gpio_direction_input(TEGRA_GPIO_MODEM_RSVD1);


	ret = gpio_request(TEGRA_GPIO_MODEM_RSVD2, "Port_H_PIN_7");
	if (ret < 0)
		pr_err("%s: gpio_request failed for gpio %d\n",
			__func__, TEGRA_GPIO_MODEM_RSVD2);
	else
		gpio_direction_output(TEGRA_GPIO_MODEM_RSVD2, 1);

}

#else
static void grouper_usb_init(void) { }
static void grouper_modem_init(void) { }
#endif

static void grouper_audio_init(void)
{
	struct board_info board_info;

	tegra_get_board_info(&board_info);

	if (board_info.fab == BOARD_FAB_A01) {
		grouper_audio_pdata.codec_name = "rt5639.4-001c";
		grouper_audio_pdata.codec_dai_name = "rt5639-aif1";
	} else if (board_info.fab == BOARD_FAB_A00) {
		grouper_audio_pdata.codec_name = "rt5640.4-001c";
		grouper_audio_pdata.codec_dai_name = "rt5640-aif1";
	}
}

/* This needs to be inialized later hand */
static int __init grouper_throttle_list_init(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(throttle_list); i++)
		if (balanced_throttle_register(&throttle_list[i]))
			return -ENODEV;

	return 0;
}
late_initcall(grouper_throttle_list_init);

static void __init tegra_grouper_init(void)
{
	tegra_thermal_init(thermal_binds);
	tegra_clk_init_from_table(grouper_clk_init_table);
	tegra_enable_pinmux();
	tegra_smmu_init()
	tegra_soc_device_init("grouper");
	grouper_pinmux_init();
	grouper_i2c_init();
	grouper_spi_init();
	grouper_usb_init();
#ifdef CONFIG_TEGRA_EDP_LIMITS
	grouper_edp_init();
#endif
	grouper_uart_init();
	grouper_audio_init();
	platform_add_devices(grouper_devices, ARRAY_SIZE(grouper_devices));
	tegra_ram_console_debug_init();
	tegra_io_dpd_init();
	grouper_sdhci_init();
	grouper_regulator_init();
	grouper_suspend_init();
	grouper_touch_init();
	grouper_keys_init();
	grouper_panel_init();
	grouper_bt_st();
	grouper_sensors_init();
	grouper_pins_state_init();
	grouper_emc_init();
	tegra_release_bootloader_fb();
	grouper_modem_init();
#ifdef CONFIG_TEGRA_WDT_RECOVERY
	tegra_wdt_recovery_init();
#endif
	tegra_serial_debug_init(TEGRA_UARTD_BASE, INT_WDT_CPU, NULL, -1, -1);
}

static void __init grouper_ramconsole_reserve(unsigned long size)
{
	tegra_ram_console_debug_reserve(SZ_1M);
}

static void __init tegra_grouper_dt_init(void)
{
	tegra_grouper_init();

	of_platform_populate(NULL,
		of_default_bus_match_table, NULL, NULL);
}

static void __init tegra_grouper_reserve(void)
{
#if defined(CONFIG_NVMAP_CONVERT_CARVEOUT_TO_IOVMM)
	/* support 1920X1200 with 24bpp */
	tegra_reserve(0, SZ_8M + SZ_1M, SZ_8M + SZ_1M);
#else
	tegra_reserve(SZ_128M, SZ_8M, SZ_8M);
#endif
	grouper_ramconsole_reserve(SZ_1M);
}

static const char * const grouper_dt_board_compat[] = {
	"nvidia,grouper",
	NULL
};

MACHINE_START(GROUPER, "grouper")
	.atag_offset	= 0x100
	.soc            = &tegra_soc_desc,
	.map_io		= tegra_map_common_io,
	.reserve	= tegra_grouper_reserve,
	.init_early	= tegra30_init_early,
	.init_irq	= tegra_init_irq,
	.handle_irq	= gic_handle_irq,
	.timer		= &tegra_timer,
	.init_machine	= tegra_grouper_dt_init,
	.restart	= tegra_assert_system_reset,
	.dt_compat      = kai_dt_board_compat,
MACHINE_END
