/*
 *
 * FocalTech TouchScreen driver.
 * 
 * Copyright (c) 2010-2016, FocalTech Systems, Ltd., all rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __FOCALTECH_COMM_H__
#define __FOCALTECH_COMM_H__
 /*******************************************************************************
*
* File Name: focaltech_comm.h
*
*     Author: Xu YF & ZR, Software Department, FocalTech
*
*   Created: 2016-03-16
*
*  Abstract: the implement of global function and structure variable
*
* Reference:
*
*******************************************************************************/

/*******************************************************************************
* 1.Included header files
*******************************************************************************/
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/firmware.h>
#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>

/*******************************************************************************
* Private constant and macro definitions using #define
*******************************************************************************/
#define FTS_COMMON_LIB_INFO  "Common_Lib_Version  V1.0.0 2016-02-24"


/*******************************************************************************
* Private enumerations, structures and unions using typedef
*******************************************************************************/


/*******************************************************************************
* Static variables
*******************************************************************************/


/*******************************************************************************
* Global variable or extern global variabls/functions
*******************************************************************************/
//Base functions
int fts_i2c_read(unsigned char *writebuf, int writelen, unsigned char *readbuf, int readlen);
int fts_i2c_write(unsigned char *writebuf, int writelen);
int fts_read_reg(u8 addr, u8 *val);
int fts_write_reg(u8 addr, const u8 val);

/*******************************************************************************
* Define and initiate in focaltech_flash.c
*******************************************************************************/
extern struct fts_Upgrade_Info fts_upgrade_info_curr;

int fts_flash_init(struct i2c_client *client);
int fts_flash_exit(void);
int fts_flash_upgrade_with_bin_file( char *firmware_name);
int fts_flash_upgrade_with_i_file(void);
int fts_flash_get_i_file_version(void);
int fts_ctpm_auto_clb(void);
	
int fts_test_init(void);

//Apk and functions
int fts_create_apk_debug_channel(struct i2c_client * client);
void fts_release_apk_debug_channel(void);

//ADB functions
int fts_create_sysfs(struct i2c_client *client);
int fts_remove_sysfs(struct i2c_client *client);

//char device for old apk
int fts_rw_iic_drv_init(struct i2c_client *client);
void  fts_rw_iic_drv_exit(void);


//Getstre functions
int fts_Gesture_init(struct input_dev *input_dev);
int fts_read_Gestruedata(void);

/************************************ Static function prototypes ************************************/
#define FTS_DBG_EN_COMMON

#ifdef FTS_DBG_EN_COMMON	
#define FTS_COMMON_DBG(fmt, args...)	do {printk("[FTS] %s. line: %d.  "fmt"\n", __FUNCTION__, __LINE__, ##args);} while (0)
#else
#define FTS_COMMON_DBG(fmt, args...) do{}while(0)
#endif

#define ERROR_COMMON_FTS(fmt, arg...)	printk(KERN_ERR"[FTS] ERROR. %s. line: %d.  "fmt"  !!!\n", __FUNCTION__, __LINE__, ##arg)

#endif

