/* FPSensor Touch sensor driver
 *
 * Copyright (c) 2013 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#ifndef LINUX_SPI_FPSensor_H
#define LINUX_SPI_FPSensor_H

#define FPSENSOR_DEV_NAME            "fpsensor"

#define FPSENSOR_IOC_HW_PREPARE			_IOW('K', 0, int)
#define FPSENSOR_IOC_HW_UNPREPARE		_IOW('K', 1, int)
#define FPSENSOR_IOC_GET_INTERRUPT		_IOR('K', 2, int)
#define FPSENSOR_IOC_GET_MODULE_NAME	_IOR('K', 3, int)
#define FPSENSOR_IOC_MASK_INTERRUPT		_IOW('K', 4, int)
#define FPSENSOR_IOC_HW_RESET			_IOW('K', 5, int)
#define FPSENSOR_IOC_GET_SERIAL_NUM		_IOR('K', 6, int)
#define FPSENSOR_IOC_SENDKEY			_IOW('K', 7, int)
#define FPSENSOR_IOC_SETCLKRATE			_IOW('K', 8, int)
#define FPSENSOR_IOC_COOLBOOT			_IOW('K', 9, int)
#define FPSENSOR_IOC_GET_SENSOR_NAME	_IOR('K', 10, int)
#define FPSENSOR_IOC_WAKEUP_POLL		_IOW('K', 11, int)
#define FPSENSOR_IOC_WAKELOCK			_IOW('K', 12, int)
#define FPSENSOR_IOC_CMD_WAKELOCK               _IOW('K', 13, int)
#define FPSENSOR_IOC_GET_IRQ_STATUS     _IOW('K', 18, int)
#define FPSENSOR_IOC_RESET_STATE		  	_IOW('K', 19, int)
#define FPSENSOR_IOC_POWER_CTRL		    	_IOW('K', 20, int)
#define FPSENSOR_IOC_GET_BOOT_IRQ_STATUS		    	_IOW('K', 21, int)
enum key_index {
	FPSENSOR_REL_WHEEL,
	FPSENSOR_KEY_LEFT,
	FPSENSOR_KEY_RIGHT,
	FPSENSOR_KEY_UP,
	FPSENSOR_KEY_DOWN,
	FPSENSOR_KEY_CLICK,
	FPSENSOR_KEY_DBLCLICK,
	FPSENSOR_KEY_LONG,
	FPSENSOR_KEY_MAX
};

struct fpsensor_key {
	enum key_index keyID;
	int value;
};



#endif
