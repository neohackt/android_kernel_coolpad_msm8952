/* FPC1020 Touch sensor driver
 *
 * Copyright (c) 2013 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#ifndef LINUX_SPI_FPC1020_H
#define LINUX_SPI_FPC1020_H

#define FPC_HW_PREPARE			_IOW('K', 0, int)
#define FPC_HW_UNPREPARE		_IOW('K', 1, int)
#define FPC_GET_INTERRUPT 		_IOR('K', 2, int)
#define FPC_GET_MODULE_NAME		_IOR('K', 3, int)
#define FPC_MASK_INTERRUPT		_IOW('K', 4, int)
#define FPC_HW_RESET			_IOW('K', 5, int)
#define FPC_GET_SERIAL_NUM		_IOR('K', 6, int)

struct fpc1020_platform_data {
	int irq_gpio;
	int reset_gpio;
	int cs_gpio;
	int external_supply_mv;
	int txout_boost;
	int force_hwid;
	int use_regulator_for_bezel;
};

#endif

