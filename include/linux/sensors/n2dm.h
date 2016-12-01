
/******************** (C) COPYRIGHT 2013 STMicroelectronics ********************
*
* File Name	: n2dm.h
* Authors	: MH - C&I BU - Application Team
*		: Matteo Dameno (matteo.dameno@st.com)
*		: Denis Ciocca (denis.ciocca@st.com)
* Version	: V.1.0.12
* Date		: 2013/Nov/12
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
*******************************************************************************/
/*******************************************************************************
Version History.

 Revision 1.0.10: 2011/Aug/16

 Revision 1.0.11: 2012/Jan/09
  moved under input/misc
 Revision 1.0.12: 2012/Feb/29
  moved use_smbus inside status struct; modified:-update_fs_range;-set_range
  input format; allows gpio_intX to be passed as parameter at insmod time;
  renamed field g_range to fs_range in n2dm_acc_platform_data;
  replaced defines SA0L and SA0H with N2DM_SAD0x
*******************************************************************************/

#ifndef	__N2DM_H__
#define	__N2DM_H__

/*YuLong add start 20150514*/
#include <linux/sensors/accel_common.h>

#define N2DM_DEVICE_NAME	"yl_acc_sensor"
#define	N2DM_ACC_DEV_NAME	"n2dm"

#define N2DM_IOCTL_APP_SET_AFLAG	ACCEL_IOCTL_ACTIVE
#define N2DM_IOCTL_APP_SET_DELAY	ACCEL_IOCTL_SET_DELAY
#define N2DM_IOCTL_APP_CALIBRATE	ACCEL_IOCTL_CALIBRATE
#define N2DM_IOCTL_APP_OFFSET		ACCEL_IOCTL_SETOFFSET

struct accel_cal {
	short x;
	short y;
	short z;
} ;
/*add end*/

#define	N2DM_ACC_MIN_POLL_PERIOD_MS	1


#ifdef __KERNEL__

#define N2DM_SAD0L			(0x00)
#define N2DM_SAD0H			(0x01)
#define N2DM_ACC_I2C_SADROOT		(0x04)

/* I2C address if acc SA0 pin to GND */
#define N2DM_ACC_I2C_SAD_L		((N2DM_ACC_I2C_SADROOT<<1)| \
						N2DM_SAD0L)

/* I2C address if acc SA0 pin to Vdd */
#define N2DM_ACC_I2C_SAD_H		((N2DM_ACC_I2C_SADROOT<<1)| \
						N2DM_SAD0H)

#define N2DM_ACC_DEFAULT_INT1_GPIO		(-EINVAL)
#define N2DM_ACC_DEFAULT_INT2_GPIO		(-EINVAL)

/* Accelerometer Sensor Full Scale */
#define	N2DM_ACC_FS_MASK		(0x30)
#define N2DM_ACC_G_2G			(0x00)
#define N2DM_ACC_G_4G			(0x10)
#define N2DM_ACC_G_8G			(0x20)
#define N2DM_ACC_G_16G			(0x30)

struct n2dm_acc_platform_data {
	unsigned int poll_interval;
	unsigned int min_interval;

	u8 fs_range;

	u8 axis_map_x;
	u8 axis_map_y;
	u8 axis_map_z;

	u8 negate_x;
	u8 negate_y;
	u8 negate_z;

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

	/* set gpio_int[1,2] either to the choosen gpio pin number or to -EINVAL
	 * if leaved unconnected
	 */
	int gpio_int1;
	int gpio_int2;
};

#endif	/* __KERNEL__ */

#endif	/* __N2DM_H__ */



