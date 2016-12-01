/* FPC1020 Touch sensor driver
 *
 * Copyright (c) 2013,2014 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#ifndef FPC1020_COMMON_H
#define FPC1020_COMMON_H

#include <linux/spi/spi.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/pinctrl/pinctrl.h>

#include <linux/spi/fpc1020.h>


typedef enum {
	FPC1020_CHIP_NONE  = 0,
	FPC1020_CHIP_1020A = 1,
	FPC1020_CHIP_1021A = 2,
	FPC1020_CHIP_1021B = 3,
	FPC1020_CHIP_1021F = 4,
	FPC1020_CHIP_1150A = 5,
	FPC1020_CHIP_1150B = 6,
	FPC1020_CHIP_1150F = 7,
	FPC1020_CHIP_1155X = 8
} fpc1020_chip_t;

typedef struct fpc1020_chip_info {
	fpc1020_chip_t         type;
	u8                     revision;
	u8                     pixel_rows;
	u8                     pixel_columns;
	u8                     adc_group_size;
	u16                    spi_max_khz;
}fpc1020_chip_info_t;


typedef struct {
	struct spi_device      *spi;
	struct class           *class;
	struct device          *device;
	struct cdev            cdev;
	dev_t                  devno;
	fpc1020_chip_info_t    chip;
	u32                    reset_gpio;
	u32                    irq_gpio;
	int                    irq;
	bool				   spi_use_pinctrl;
#ifdef CONFIG_PINCTRL	
	struct pinctrl		   *pinctrl;
	struct pinctrl_state   *pins_active;
	struct pinctrl_state   *pins_sleep;
#endif	
	u32                    spi_gpio_clk;
	u32                    spi_gpio_miso;
	u32                    spi_gpio_mosi;
	u32                    spi_gpio_cs;
	struct clk			   *clk;
	struct clk			   *pclk;
	wait_queue_head_t      wq_irq_return;
	bool                   interrupt_done;
	struct semaphore       mutex;
	bool                   soft_reset_enabled;
	struct regulator       *vcc_spi;
	struct regulator       *vdd_ana;
	struct regulator       *vdd_io;
	struct regulator       *vdd_tx;
	bool                   power_enabled;
	int                    vddtx_mv;
	bool                   txout_boost;
	u16                    force_hwid;
	bool                   use_regulator_for_bezel;
	u16                    spi_freq_khz;
	struct work_struct	   vsync_work;
	int					   state;
	int						irq_state;
} fpc1020_data_t;

#endif /*  FPC1020_COMMON_H */
