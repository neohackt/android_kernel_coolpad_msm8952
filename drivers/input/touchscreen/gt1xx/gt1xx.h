/* drivers/input/touchscreen/gt813_827_828.h
 * 
 * 2010 - 2012 Goodix Technology.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be a reference 
 * to you, when you are integrating the GOODiX's CTP IC into your system, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
 * General Public License for more details.
 * 
 * Version:1.0
 *      V1.0:2012/08/31,first release.
 */

#ifndef _LINUX_GOODIX_TOUCH_H
#define	_LINUX_GOODIX_TOUCH_H

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
//#include <mach/gpio.h>
#include <linux/gpio.h>
#include <linux/slab.h>

struct goodix_ts_data {
    spinlock_t irq_lock;
    struct i2c_client *client;
    struct input_dev  *input_dev;
    struct hrtimer timer;
    struct work_struct  work;
#if defined(CONFIG_HAS_EARLYSUSPEND)
    struct early_suspend early_suspend;
#elif defined(CONFIG_FB)
    struct notifier_block fb_notif;
    struct work_struct fb_notify_work;
#endif
    s32 irq_is_disable;
    s32 use_irq;
    u16 abs_x_max;
    u16 abs_y_max;
    u8  max_touch_num;
    u8  int_trigger_type;
    u8  green_wake_mode;
    u8  chip_type;
    u8  enter_update;
    u8  gtp_is_suspend;
    u8  gtp_is_sleep_suspend;
    u8  gtp_rawdiff_mode;
    u8  gtp_cfg_len;
    u8  fixed_cfg;
    spinlock_t esd_lock;
    u8  esd_running;
    s32 clk_tick_cnt;
    u8  fw_error;
    struct tw_platform_data *pdata;
	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
};

extern u16 show_len;
extern u16 total_len;

/******************* gt9xx_config.h start**********************/

//***************************PART1:ON/OFF define*******************************
#define GTP_CUSTOM_CFG        0
#define GTP_DRIVER_SEND_CFG   1
#define GTP_HAVE_TOUCH_KEY    0
#define GTP_POWER_CTRL_SLEEP  0
#define GTP_AUTO_UPDATE       1
#define GTP_CHANGE_X2Y        0
#define GTP_ESD_PROTECT       0
#define GTP_CREATE_WR_NODE    1
#define GTP_ICS_SLOT_REPORT   0

#define GUP_USE_HEADER_FILE   0
#ifdef CONFIG_TOUCHSCREEN_GESTURE_WAKEUP
#define GTP_SLIDE_WAKEUP      1
#else
#define GTP_SLIDE_WAKEUP      0
#endif

#define GTP_MAX_TOUCH         10
#define GTP_ESD_CHECK_CIRCLE  2 /* esd: s //Hz*/


#if GTP_HAVE_TOUCH_KEY
#define GTP_KEY_TAB	 {KEY_MENU, KEY_HOME, KEY_BACK}
#endif
#define GTP_IRQ_TAB  {IRQ_TYPE_EDGE_RISING, IRQ_TYPE_EDGE_FALLING, IRQ_TYPE_LEVEL_LOW, IRQ_TYPE_LEVEL_HIGH}

/******************* gt9xx_config.h end ********************************/

//***************************PART3:OTHER define*********************************
#define GTP_DRIVER_VERSION    "V1.0<2015/01/28>"
#define GTP_I2C_NAME          "GT1XX-TS"
#define GTP_POLL_TIME         10     // jiffy: ms
#define GTP_ADDR_LENGTH       2
#define GTP_CONFIG_MIN_LENGTH 186
#define GTP_CONFIG_MAX_LENGTH 240
#define GTP_MAX_I2C_XFER_LEN        250
#define FAIL                  0
#define SUCCESS               1
#define SWITCH_OFF            0
#define SWITCH_ON             1

#define GTP_REG_MATRIX_DRVNUM           0x8069
#define GTP_REG_MATRIX_SENNUM           0x806A
#define GTP_REG_RQST                    0x8044
#define GTP_REG_BAK_REF                 0x90EC
#define GTP_REG_MAIN_CLK                0x8020
#define GTP_REG_HAVE_KEY                0x8057
#define GTP_REG_HN_STATE                0x8800

#define GTP_REG_WAKEUP_GESTURE         0x814C
#define GTP_REG_WAKEUP_GESTURE_DETAIL  0xA2A0	// need change

#define GTP_BAK_REF_PATH                "/data/gt1x_ref.bin"
#define GTP_MAIN_CLK_PATH               "/data/gt1x_clk.bin"

/* request type */
#define GTP_RQST_CONFIG                 0x01
#define GTP_RQST_BAK_REF                0x02
#define GTP_RQST_RESET                  0x03
#define GTP_RQST_MAIN_CLOCK             0x04
#define GTP_RQST_HOTKNOT_CODE           0x20
#define GTP_RQST_RESPONDED              0x00
#define GTP_RQST_IDLE                   0xFF

#define HN_DEVICE_PAIRED                0x80
#define HN_MASTER_DEPARTED              0x40
#define HN_SLAVE_DEPARTED               0x20
#define HN_MASTER_SEND                  0x10
#define HN_SLAVE_RECEIVED               0x08
//Register define
#define GTP_READ_COOR_ADDR    0x814E
#define GTP_REG_CMD         0x8040
#define GTP_REG_SENSOR_ID     0x814A
#define GTP_REG_CONFIG_DATA   0x8050
#define GTP_REG_CONFIG_RESOLUTION   0x8051
#define GTP_REG_CONFIG_TRIGGER      0x8056
#define GTP_REG_CONFIG_CHECKSUM     0x813C
#define GTP_REG_CONFIG_UPDATE       0x813E
#define GTP_REG_VERSION       0x8140
#define GTP_REG_HW_INFO             0x4220
#define GTP_REG_REFRESH_RATE	    0x8056
#define GTP_REG_ESD_CHECK           0x8043
#define GTP_REG_FLASH_PASSBY        0x8006
#define GTP_REG_HN_PAIRED           0x81AA
#define GTP_REG_HN_MODE             0x81A8
#define GTP_REG_MODULE_SWITCH3      0x8058

#define GTP_I2C_RETRY_3		  3
#define GTP_I2C_RETRY_5		  5
#define GTP_I2C_RETRY_10	  10

#define set_reg_bit(reg,pos,val)	 ((reg)=((reg) & (~(1<<(pos))))|((val)<<(pos)))

/* cmd define */
#define GTP_CMD_SLEEP               0x05
#define GTP_CMD_CHARGER_ON          0x06
#define GTP_CMD_CHARGER_OFF         0x07
#define GTP_CMD_GESTURE_WAKEUP      0x08
#define GTP_CMD_CLEAR_CFG           0x10
#define GTP_CMD_ESD                 0xAA
#define GTP_CMD_HN_TRANSFER         0x22
#define GTP_CMD_HN_EXIT_SLAVE       0x28
/* define offset in the config*/
#define RESOLUTION_LOC              (GTP_REG_CONFIG_RESOLUTION - GTP_REG_CONFIG_DATA)
#define TRIGGER_LOC                 (GTP_REG_CONFIG_TRIGGER - GTP_REG_CONFIG_DATA)
#define MODULE_SWITCH3_LOC	     	(GTP_REG_MODULE_SWITCH3 - GTP_REG_CONFIG_DATA)

extern unsigned char GTP_DEBUG_ON;
extern struct i2c_client * i2c_connect_client;

#define GTP_DEBUG_ARRAY_ON    0
#define GTP_DEBUG_FUNC_ON     0
#define IS_NUM_OR_CHAR(x)    (((x) > 'A' && (x) < 'Z') || ((x) > '0' && (x) < '9'))

//Log define
#define GTP_INFO(fmt,arg...)           printk("<<-GTP-INFO->> "fmt"\n",##arg)
#define GTP_ERROR(fmt,arg...)          printk("<<-GTP-ERROR->> "fmt"\n",##arg)
#define GTP_DEBUG(fmt,arg...)          do{\
                                         if(GTP_DEBUG_ON)\
                                         printk("<<-GTP-DEBUG->> [%d]"fmt"\n",__LINE__, ##arg);\
                                       }while(0)
#define GTP_DEBUG_ARRAY(array, num)    do{\
                                         s32 i;\
                                         u8* a = array;\
                                         if(GTP_DEBUG_ARRAY_ON)\
                                         {\
                                            printk("<<-GTP-DEBUG-ARRAY->>\n");\
                                            for (i = 0; i < (num); i++)\
                                            {\
                                                printk("%02x   ", (a)[i]);\
                                                if ((i + 1 ) %10 == 0)\
                                                {\
                                                    printk("\n");\
                                                }\
                                            }\
                                            printk("\n");\
                                        }\
                                       }while(0)
#define GTP_DEBUG_FUNC()               do{\
                                         if(GTP_DEBUG_FUNC_ON)\
                                         printk("<<-GTP-FUNC->> Func:%s@Line:%d\n",__func__,__LINE__);\
                                       }while(0)
#define GTP_SWAP(x, y)                 do{\
                                         typeof(x) z = x;\
                                         x = y;\
                                         y = z;\
                                       }while (0)

//****************************PART4:UPDATE define*******************************
#define _ERROR(e)      ((0x01 << e) | (0x01 << (sizeof(s32) * 8 - 1)))
#define ERROR          _ERROR(1)	//for common use
//system relevant
#define ERROR_IIC      _ERROR(2)	//IIC communication error.
#define ERROR_MEM      _ERROR(3)	//memory error.

//system irrelevant
#define ERROR_HN_VER   _ERROR(10)	//HotKnot version error.
#define ERROR_CHECK    _ERROR(11)	//Compare src and dst error.
#define ERROR_RETRY    _ERROR(12)	//Too many retries.
#define ERROR_PATH     _ERROR(13)	//Mount path error
#define ERROR_FW       _ERROR(14)
#define ERROR_FILE     _ERROR(15)
#define ERROR_VALUE    _ERROR(16)	//Illegal value of variables

/* bit operation */
#define SET_BIT(data, flag)	((data) |= (flag))
#define CLR_BIT(data, flag)	((data) &= ~(flag))
#define CHK_BIT(data, flag)	((data) & (flag))

/* touch states */
#define BIT_TOUCH			0x01
#define BIT_TOUCH_KEY		0x02
#define BIT_STYLUS			0x04
#define BIT_STYLUS_KEY		0x08
#define BIT_HOVER			0x10

//Error no
#define ERROR_NO_FILE				2   //ENOENT
#define ERROR_FILE_READ				23  //ENFILE
#define ERROR_FILE_TYPE				21  //EISDIR
#define ERROR_GPIO_REQUEST			4   //EINTR
#define ERROR_I2C_TRANSFER			5   //EIO
#define ERROR_NO_RESPONSE			16  //EBUSY
#define ERROR_TIMEOUT				110 //ETIMEDOUT
//--------------------------------------------------------
/* GTP CM_HEAD RW flags */
#define GTP_RW_READ					0
#define GTP_RW_WRITE				1
#define GTP_RW_READ_IC_TYPE			2
#define GTP_RW_WRITE_IC_TYPE		3
#define GTP_RW_FILL_INFO			4
#define GTP_RW_NO_WRITE				5
#define GTP_RW_READ_ERROR			6
#define GTP_RW_DISABLE_IRQ			7
#define GTP_RW_READ_VERSION			8
#define GTP_RW_ENABLE_IRQ			9
#define GTP_RW_ENTER_UPDATE_MODE	11
#define GTP_RW_LEAVE_UPDATE_MODE	13
#define GTP_RW_UPDATE_FW			15
#define GTP_RW_CHECK_RAWDIFF_MODE	17

/* GTP need flag or interrupt */
#define GTP_NO_NEED					0
#define GTP_NEED_FLAG				1
#define GTP_NEED_INTERRUPT			2
//--------------------------------------------------------
//*****************************End of Part III********************************
#pragma pack(1)
struct gt1x_version_info {
	u8 product_id[5];
	u32 patch_id;
	u32 mask_id;
	u8 sensor_id;
	u8 match_opt;
};
#pragma pack()

struct fw_subsystem_info {
	int type;
	int length;
	u32 address;
	int offset;
};

#pragma pack(1)
struct fw_info {
	u32 length;
	u16 checksum;
	u8 target_mask[6];
	u8 target_mask_version[3];
	u8 pid[6];
	u8 version[3];
	u8 subsystem_count;
	u8 chip_type;
	u8 reserved[6];
	struct fw_subsystem_info subsystem[12];
};
#pragma pack()

struct fw_update_info {
	int update_type;
	int status;
	int progress;
	int max_progress;
	struct fw_info *firmware;
	u32 fw_length;

	// file update
	char *fw_name;
	u8 *buffer;
	mm_segment_t old_fs;
	struct file *fw_file;

	// header update
	u8 *fw_data;
};

/* Export form gt1xx.c */

extern s32 gt1x_i2c_read(u16 addr, u8 * buf, s32 len);
extern s32 gt1x_i2c_write(u16 addr, u8 * buf, s32 len);
extern s32 gt1x_i2c_read_dbl_check(u16 addr, u8 * buffer, s32 len);
extern s32 gt1x_read_version(struct gt1x_version_info * ver_info);

#if GTP_ESD_PROTECT
extern void gt1x_esd_switch(s32 state);
#endif
extern void gt1x_irq_disable(void);
extern void gt1x_irq_enable(void);
extern s32 gt1x_reset_guitar(void);
extern s32 gt1x_init_panel(void);


extern u8 gt1x_rawdiff_mode;
extern struct fw_update_info update_info;

extern u8 gt1x_default_FW[];
extern int gt1x_hold_ss51_dsp(void);
extern int gt1x_auto_update_proc(void *data);
extern int gt1x_update_firmware(char *filename);
extern void gt1x_enter_update_mode(void);
extern void gt1x_leave_update_mode(void);
extern int gt1x_hold_ss51_dsp_no_reset(void);
extern int gt1x_load_patch(u8 * patch, u32 patch_size, int offset, int bank_size);
extern int gt1x_startup_patch(void);
extern int gt1x_update_prepare(char *filename);
extern int gt1x_check_firmware(void);
extern int gt1x_update_judge(void);

#endif /* _LINUX_GOODIX_TOUCH_H */
