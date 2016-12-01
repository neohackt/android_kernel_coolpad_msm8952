/*
 * Driver for sharp touch screen controller
 *
 * Copyright (c) 2013 Sharp Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/input/mt.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>

#include <linux/input/touchscreen_yl.h>
//#include <generated/uapi/linux/version.h>

//#define DP_8074_DEMO // old board
#define DP_8084_EVA // new board
#ifdef DP_8074_DEMO
#elif defined DP_8084_EVA
#include <linux/regulator/consumer.h> // 2015.04.09 added by Y.Nakamura for initialize regulator (pma8084)
#endif /* DP_8074_DEMO */

// should be defined automatically
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif /* KERNEL_VERSION */

#ifndef LINUX_VERSION_CODE
#ifdef DP_8074_DEMO
#define LINUX_VERSION_CODE (KERNEL_VERSION(3,4,0)) //dp qualcomm
#elif defined DP_8084_EVA
#define LINUX_VERSION_CODE (KERNEL_VERSION(3,10,0)) //dp new qualcomm
#endif /* DP_8074_DEMO */
#endif /* LINUX_VERSION_CODE */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
#define __devinit
#define __devexit
#define __devexit_p
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */


//2014.10.16 added
#include <linux/string.h>
#include <linux/of_gpio.h>

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>

#include <linux/mutex.h>

#include <linux/i2c.h>

#include "shtsc_ioctl.h"
#include "shtsc.h"

// device code
#define DEVICE_CODE_LR388K5 2

char VersionYear = 15;
char VersionMonth = 12;
char VersionDay = 23;
char VersionSerialNumber = 0; // reset on another day
char VersionModelCode = DEVICE_CODE_LR388K5;
#define DRIVER_VERSION_LEN (5) // do not change

#define SHTSC_I2C_P_MAX		((1<<16)-1) //8000 // depends on firmware
#define SHTSC_I2C_SIZE_MAX		((1<<6)-1)

/* DEBUG */
#ifndef DEBUG_SHTSC
//#define DEBUG_SHTSC
#endif//DEBUG_SHTSC

#define DBGLOG(format, args...)  printk(KERN_INFO format, ## args)
#define ERRLOG(format, args...)  printk(KERN_ERR format, ## args)

//#define D_DBG_LEVEL
static unsigned long s_debug_level = 0; //2015.12.21
#define D_DBG_IRQ         (1 << 1)
#define D_DBG_WRITE_N     (1 << 2)
#define D_DBG_READ_N      (1 << 3)
#define D_DBG_FUNC        (1 << 4) //2015.12.23
#define D_DBG_TOUCH_CLEAR (1 << 5) //2015.12.24
#define D_DBG_WRITE_ARRAY (1 << 6)
#define D_DBG_READ_ARRAY  (1 << 7)
#define D_DBG_TEST_MODE   (1 << 16) /* bit16 : 3byte bit0 */
#define D_DBG_TOUCH_IRQ_DISABLE (1 << 17)
#define D_DBG_TOUCH_IRQ_CLEAR   (1 << 18)
#define D_DBG_TOUCH_IRQ_ENABLE  (1 << 19)

static unsigned long s_test_mode = 0;
#define D_ERR_I2C_ERROR (1<<24)
#define D_ERR_CHIP_ID (2<<24)

/* INT STATUS */
#define SHTSC_STATUS_TOUCH_READY    (1<<0)
#define SHTSC_STATUS_POWER_UP       (1<<1)
#define SHTSC_STATUS_RESUME_PROX    (1<<2)
#define SHTSC_STATUS_WDT            (1<<3)
#define SHTSC_STATUS_DCMAP_READY    (1<<4)
#define SHTSC_STATUS_COMMAND_RESULT (1<<5)
#define SHTSC_STATUS_KNOCK_CODE     (1<<6)
#define SHTSC_STATUS_FLASH_LOAD_ERROR    (1<<8)
#define SHTSC_STATUS_PLL_UNLOCK     (1<<9)
#define SHTSC_STATUS_UNDEFINED_BIT  (0xFC80) // add by misaki on 12/15
#define SHTSC_STATUS_LEGAL 	    (0x037F)

/* DONE IND */
#define SHTSC_IND_CMD   0x20
#define SHTSC_IND_TOUCH 0x01

/* BANK Address */
#define SHTSC_BANK_TOUCH_REPORT   0x00
#define SHTSC_BANK_COMMAND        0x02
#define SHTSC_BANK_COMMAND_RESULT 0x03
#define SHTSC_BANK_DCMAP          0x05

/* Common Register Address */
#define SHTSC_ADDR_INT0  0x00
#define SHTSC_ADDR_INTMASK0 0x01
#define SHTSC_ADDR_BANK 0x02
#define SHTSC_ADDR_IND  0x03
#define SHTSC_ADDR_INT1  0x04
#define SHTSC_ADDR_INTMASK1 0x05

/* Touch Report Register Address */
#define SHTSC_ADDR_TOUCH_NUM 0x08
#define SHTSC_ADDR_RESUME_PROX 0x09
#define SHTSC_ADDR_TOUCH_REPORT 0x10

/* Touch Parmeters */
#define SHTSC_MAX_FINGERS 10
#define SHTSC_MAX_TOUCH_1PAGE 10
#define SHTSC_LENGTH_OF_TOUCH 8

/* Touch Status */
#define SHTSC_F_TOUCH ((u8)0x01)
#define SHTSC_F_TOUCH_OUT ((u8)0x03)
//#define SHTSC_P_TOUCH ((u8)0x02)
//#define SHTSC_P_TOUCH_OUT ((u8)0x04)

#define SHTSC_TOUCHOUT_STATUS ((u8)0x80)

#define SHTSC_ADDR_COMMAND 0x08

typedef enum _cmd_state_e {
	CMD_STATE_SLEEP         = 0x00,
	CMD_STATE_IDLE          = 0x03,
	CMD_STATE_DEEP_IDLE     = 0x04,
	CMD_STATE_HOVER         = 0x06,  // if applicable
	CMD_STATE_PROX          = 0x07,  // if applicable
	CMD_STATE_GLOVE         = 0x08,  // if applicable
	CMD_STATE_COVER         = 0x0D,  // if applicable
	CMD_STATE_MAX
} dCmdState_e ;

typedef enum _cmd_CalibratioMode_e
{
	CMD_CALIB_MANUAL	  = 0x00,
	CMD_CALIB_START_OF_FORCE= 0x01,
	CMD_CALIB_END_OF_FORCE  = 0x02,
	CMD_CALIB_MAX
} dCmdCalibMode_e;

#define CMD_INIT              0x01
#define CMD_SETSYSTEM_STATE   0x02
#define CMD_EXEC_CALIBRATION  0x0F

#define CMD_PAYLOAD_LENGTH(X)              \
	X == CMD_INIT		?	0x04 : \
X == CMD_SETSYSTEM_STATE?	0x04 : \
X == CMD_EXEC_CALIBRATION?	0x04 : \
0x0

#define SHTSC_ICON_KEY_NUM 3
#ifdef SHTSC_ICON_KEY_NUM
#define USE_APPSELECT /* KEY_APPSELECT instead of KEY_MENU */
#ifdef USE_APPSELECT
const int shtsc_keyarray[SHTSC_ICON_KEY_NUM]={158,102,0x244}; /* KEY_BACK,KEY_HOME,KEY_APPSELECT */
#else /* USE_APPSELECT */
const int shtsc_keyarray[SHTSC_ICON_KEY_NUM]={158,102,139}; /* KEY_BACK,KEY_HOME,KEY_MENU */
#endif /* USE_APPSELECT */
#endif

//2014.11.20 added
#define CMD_DELAY             1

#define WAIT_NONE   (0)
#define WAIT_CMD    (1)
#define WAIT_RESET  (2)

#define SYNC_TO_JIFFIES (1500) //(HZ*3/2 =1000*3/2 ) //2015.12.21

#define MAX_16BIT			0xffff
#define MAX_12BIT			0xfff

#define MAX_DCMAP_SIZE (37*37*2)

#define LOW_LEVEL 0
#define HIGH_LEVEL 1


/* ======== EEPROM command ======== */
#define	FLASH_CMD_WRITE_EN		(0x06)		/* Write enable command */
#define	FLASH_CMD_PAGE_WR			(0x02)		/* Page write command */
#define	FLASH_CMD_READ_ST			(0x05)		/* Read status command */
#define	FLASH_CMD_SECTOR_ERASE	(0xD7)		/* Sector erase command */
#define	FLASH_CMD_READ			(0x03)		/* Read command */

/* ======== EEPROM status ======== */
#define	FLASH_ST_BUSY		(1 << 0)	/* Busy with a write operation */


#define SHTSC_COORDS_ARR_SIZE	4 //2014.10.17 added
/* Software reset delay */
#define SHTSC_RESET_TIME	10	/* msec */
//#define SHTSC_SLEEP_TIME	100	/* msec */

#define FLASH_CHECK // experimental code - not checked yet

#ifdef DP_8074_DEMO
#elif defined DP_8084_EVA
// 2015.04.09 added by Y.Nakamura for initialize regulator (pma8084)
#define SHTSC_VTG_MIN_UV       2800000 // not used
#define SHTSC_VTG_MAX_UV       2800000 // not used
#define SHTSC_ACTIVE_LOAD_UA     15000
#define SHTSC_I2C_VTG_MIN_UV       1800000
#define SHTSC_I2C_VTG_MAX_UV       1800000
#define SHTSC_I2C_LOAD_UA        10000
// ---
#endif /* DP_8074_DEMO */

/*
 * The touch driver structure.
 */
struct shtsc_touch {
	u8 status;
	u8 id;
	u8 size;
	u8 type;
	u16 x;
	u16 y;
	u16 z;
};

/* force flash reprogramming */
#define FORCE_FIRM_UPDATE  // enable if required
#define CGID_CHECK  // enable if required

#include "shtsc_firmware.h"  // yulong add to include TW firmware;---2015.09.10
#ifdef FORCE_FIRM_UPDATE
//#include "firm/LR388K5_fFFF0102A_p1028_150601_ext_h.h" // change to your firmware
#ifdef CGID_CHECK
//#include "firm/LR388K5_fFFF1102A_p1028_150601_ext_h.h" // change to your firmware
#define CGID_ADDRESS 0xF200
#endif /* CGID_CHECK */
int update_flash(void *, unsigned char *, unsigned int);
int CheckingFirmwareVersion = 0;
int FlashUpdateByNoFirm = 0;
#endif /* FORCE_FIRM_UPDATE */
static unsigned char cgid = 0xff;
static int current_working_mode = MODE_NORMAL;
static int custom_setting_mode = MODE_NORMAL;

//#define CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
extern void touch_register_charger_notify(void (*fn)(void));
extern unsigned int touch_get_charger_status(void);
void yl_chg_status_changed(void);
#endif

#define CMD_GETPROPERTY "\xE0\x00\x00\x11"
#define CMD_GETPROPERTY_LEN 4
#define CMD_SETSYSTEMSTATE_SLEEP "\x02\x00\x01\x00\x00"
#define CMD_SETSYSTEMSTATE_SLEEP_LEN 5

/* gesture in suspend */
#define ENABLE_SUSPEND_GESTURE // enable if required
//#define FORCE_DISPLAY_ON
//#define READ_GESTURE_IN_RESUME

#define CMD_SETSYSTEMSTATE_DEEPIDLE "\x02\x00\x01\x00\x04"
#define CMD_SETSYSTEMSTATE_DEEPIDLE_LEN 5
#define CMD_SETSYSTEMSTATE_IDLE "\x02\x00\x01\x00\x03"
#define CMD_SETSYSTEMSTATE_IDLE_LEN 5
#define CMD_GETSYSTEMSTATE "\x03\x00\x00\x01"
#define CMD_GETSYSTEMSTATE_LEN 4
#define CMD_READRAM "\xD3\x00\x04\x20\x14\x68\x00\x00"
#define CMD_READRAM_LEN 8

#ifdef ENABLE_SUSPEND_GESTURE
#if 0
/* user must define these value for their purpose */
#define KEY_GESTURE_DOUBLE_TAP 0x01
#define KEY_GESTURE_SLIDE_UP 0x02
#define	KEY_GESTURE_SLIDE_DOWN 0x03
#define KEY_GESTURE_SLIDE_RIGHT 0x04
#define KEY_GESTURE_SLIDE_LEFT 0x05
#define KEY_GESTURE_CHAR_C 0x63
#define KEY_GESTURE_CHAR_E 0x65
#define KEY_GESTURE_CHAR_M 0x6D
#define KEY_GESTURE_CHAR_O 0x6F
#define KEY_GESTURE_CHAR_V 0x76
#define KEY_GESTURE_CHAR_W 0x77
#define KEY_GESTURE_UNKNOWN 0x7F
#endif /* 0 */

//yulong add start
/*typedef enum
{
	DOZE_DISABLED = 0,
	DOZE_ENABLED = 1,
	DOZE_WAKEUP = 2,
    DOZE_FREEZE = 3,
}DOZE_T;
static DOZE_T doze_status = DOZE_DISABLED;*///yangmingjin delete 2016.1.16
static struct mutex gesture_lock;

enum support_gesture_e {
	TW_SUPPORT_NONE_SLIDE_WAKEUP = 0x0,
	TW_SUPPORT_UP_SLIDE_WAKEUP = 0x1,
	TW_SUPPORT_DOWN_SLIDE_WAKEUP = 0x2,
	TW_SUPPORT_LEFT_SLIDE_WAKEUP = 0x4,
	TW_SUPPORT_RIGHT_SLIDE_WAKEUP = 0x8,
	TW_SUPPORT_E_SLIDE_WAKEUP = 0x10,
	TW_SUPPORT_O_SLIDE_WAKEUP = 0x20,
	TW_SUPPORT_W_SLIDE_WAKEUP = 0x40,
	TW_SUPPORT_C_SLIDE_WAKEUP = 0x80,
	TW_SUPPORT_M_SLIDE_WAKEUP = 0x100,
	TW_SUPPORT_DOUBLE_CLICK_WAKEUP = 0x200,

	TW_SUPPORT_GESTURE_IN_ALL = ( TW_SUPPORT_UP_SLIDE_WAKEUP |
			TW_SUPPORT_DOWN_SLIDE_WAKEUP |
			TW_SUPPORT_LEFT_SLIDE_WAKEUP |
			TW_SUPPORT_RIGHT_SLIDE_WAKEUP |
			TW_SUPPORT_E_SLIDE_WAKEUP |
			TW_SUPPORT_O_SLIDE_WAKEUP |
			TW_SUPPORT_W_SLIDE_WAKEUP |
			TW_SUPPORT_C_SLIDE_WAKEUP |
			TW_SUPPORT_M_SLIDE_WAKEUP |
			TW_SUPPORT_DOUBLE_CLICK_WAKEUP)
};

struct slide_wakeup
{
	unsigned char code;
	unsigned int mask;
	char *evp[2];
	char *gesture;
};

char wakeup_slide[32];
u32 support_gesture = TW_SUPPORT_NONE_SLIDE_WAKEUP;

extern struct device *touchscreen_get_dev(void);
static void enable_gesture(bool enable);

struct slide_wakeup tp_ges_wakeup[]={
	{0x01,TW_SUPPORT_DOUBLE_CLICK_WAKEUP,{"GESTURE=DOUBLE_CLICK",NULL}, "double_click"},
	{0x6F,TW_SUPPORT_O_SLIDE_WAKEUP,{"GESTURE=O",NULL}, "o"},
	{0x63,TW_SUPPORT_C_SLIDE_WAKEUP,{"GESTURE=C",NULL}, "c"},
	{0x65,TW_SUPPORT_E_SLIDE_WAKEUP,{"GESTURE=E",NULL}, "e"},
	{0x6D,TW_SUPPORT_M_SLIDE_WAKEUP,{"GESTURE=M",NULL}, "m"},
	{0x77,TW_SUPPORT_W_SLIDE_WAKEUP,{"GESTURE=W",NULL}, "w"},
	{0x04,TW_SUPPORT_RIGHT_SLIDE_WAKEUP,{"GESTURE=RIGHT",NULL}, "right"},
	{0x05,TW_SUPPORT_LEFT_SLIDE_WAKEUP,{"GESTURE=LEFT",NULL}, "left"},
	{0x02,TW_SUPPORT_UP_SLIDE_WAKEUP,{"GESTURE=UP",NULL}, "up"},
	{0x03,TW_SUPPORT_DOWN_SLIDE_WAKEUP,{"GESTURE=DOWN",NULL}, "down"},
};
//yulong add end

#endif /* ENABLE_SUSPEND_GESTURE */


/*******************************************************************************************************/
//yulong add start 2015.08.12
unsigned char SHTSC_DEBUG_ON= 0;
static struct mutex shtsc_mutex; //Add shtsc global mutex lock to avoid conflict ;2015-08-11,lijiakan


//yulong add start 2015.08.12
/*******************************************************************************************************/

#if defined(CONFIG_TOUCHSCREEN_SHTSC_I2C)
struct shtsc_i2c {
#ifdef DP_8074_DEMO
#elif defined DP_8084_EVA
	struct regulator *vdd;	// 2015.04.09 added by Y.Nakamura (not used)
	struct regulator *vcc_i2c;	// 2015.04.09 added by Y.Nakamura
#endif /* DP_8074_DEMO */
	u8 cmd;//2014.11.19 added
	u8 wait_state;//2014.11.19 added
	bool wait_result;//2014.11.19 added
	bool disabled;		/* interrupt status */ //2014.11.6 added
	struct input_dev *input;
	struct shtsc_i2c_pdata *pdata;//2014.10.16 added
	char phys[32];
	struct i2c_client *client;
	int reset_pin;
	int vcc_i2c_supply_en;
	int irq_pin;
	struct shtsc_touch touch[SHTSC_MAX_FINGERS];
	struct mutex mutex;
#if defined(CONFIG_FB)
	struct notifier_block fb_notif;
	bool dev_sleep;
	struct work_struct fb_notify_work;
#endif
#ifdef FORCE_FIRM_UPDATE
	struct work_struct workstr;
	struct work_struct workstr2;
	struct workqueue_struct *workqueue;
#endif /* FORCE_FIRM_UPDATE */

	bool enter_update;
	unsigned int            max_num_touch;
	int                     min_x;
	int                     min_y;
	int                     max_x;
	int                     max_y;
	int                     pressure_max;
	int                     touch_num_max;
	bool                    flip_x;
	bool                    flip_y;
	bool                    swap_xy;

	bool irq_wake_enable_status;
  	struct completion sync_completion;
	bool setting_gesture;//yangmingjin add 2016.1.15
};
#endif // (CONFIG_TOUCHSCREEN_SHTSC_UNKNOWNIF??)

struct shtsc_i2c *g_ts;


#define SHTSC_DEVBUF_SIZE 1500

volatile static int buf_pos;
static u8 devbuf[SHTSC_DEVBUF_SIZE];

static int WriteMultiBytes(void *ts, u8 u8Addr, u8 *data, int len);
static int WriteOneByte(void *ts, u8 u8Addr, u8 u8Val);
static u8 ReadOneByte(void *ts, u8 u8Addr);
static void ReadMultiBytes(void *ts, u8 u8Addr, u16 u16Len, u8 *u8Buf);
static int issue_command(void*, unsigned char *, unsigned int);
static int shtsc_system_init(void *ts);
int flash_access_start_shtsc(void *_ts);
int flash_access_end_shtsc(void *_ts);
int flash_erase_page_shtsc(void *_ts, int page);
int flash_write_page_shtsc(void *_ts, int page, unsigned char *data);
int flash_verify_page_shtsc(void *_ts, int page, unsigned char *data);
int flash_read(void *_ts, unsigned address, unsigned length, unsigned char *data);
static void shtsc_reset_delay(void);
static void shtsc_reset(void *_ts, bool reset);
static void shtsc_reset_L_H(void *ts );
static void touch_enable (struct shtsc_i2c *ts);
static void touch_disable (struct shtsc_i2c *ts);

u8 s_shtsc_addr;
u8 s_shtsc_buf[4*4096];
u8 s_shtsc_i2c_data[4*4096];
unsigned s_shtsc_len;

#define D_TPC_STATE_LEN 8
static unsigned char s_tpc_state[D_TPC_STATE_LEN] = {0,0,0,0,0,0,0,0}; //2015.12.21 
#define D_STATE_RESET   (0)  //2015.12.23
#define D_STATE_RESET_POWER_UP (1)
#define D_STATE_RESET_FL_ERR (2)
#define D_STATE_RESET_WDT (3)

#define D_STATE_WAIT    (1)  //2015.12.23
#define D_STATE_RESULT  (2)  //2015.12.23
#define D_STATE_UPFIRM  (3)  //2015.12.23
#define D_STATE_SUSPEND (4)  //2015.12.23

#define MAX_COMMAND_RESULT_LEN (64-8)
static unsigned char CommandResultBuf[MAX_COMMAND_RESULT_LEN]; //2015.12.21 add static 

//int	G_reset_done = false;
u16 G_Irq_Mask = 0xffff;

pid_t pid = 0;
unsigned char resumeStatus; // bank 0, address 9

#ifdef D_DBG_LEVEL
static int shtsc_write_print(unsigned char *data, unsigned short len, unsigned char addr, int ret)
{
	int i;
	if ((s_debug_level & D_DBG_WRITE_ARRAY) == D_DBG_WRITE_ARRAY) {

		printk(KERN_INFO "shtsc_read_regs:ADR:0x%02x r:%d :", addr, ret);
		for (i = 0; i < len; i++) {
			printk(KERN_INFO "%0X", data[i] & 0xFF);
		}
		printk(KERN_INFO "\n");
	}
	else if ((s_debug_level & D_DBG_WRITE_N) == D_DBG_WRITE_N) {
	
		printk(KERN_INFO "shtsc_write_regs:ADR:0x%02x l:%2d r:%d :", addr, len, ret);
		for (i = 0; i < 16; i++) {
			printk(KERN_INFO "%02X", data[i] & 0xFF);
		}
		printk(KERN_INFO "\n");
	}
	return 0;
}
#endif

#if defined(CONFIG_TOUCHSCREEN_SHTSC_I2C)
static int shtsc_write_regs(struct shtsc_i2c *tsc, unsigned char addr,
		unsigned char len, unsigned char *value)
{
	struct i2c_client *client = tsc->client;
	int ret;

	s_shtsc_i2c_data[0] = addr;
	memcpy(s_shtsc_i2c_data + 1, value, len);

	ret = i2c_master_send(client, s_shtsc_i2c_data, len + 1);

#ifdef D_DBG_LEVEL
	shtsc_write_print(value, len, addr, ret);
#endif /* #ifdef D_DBG_LEVEL */

	return ret;
}

#ifdef D_DBG_LEVEL
static int shtsc_read_print(unsigned char *data, unsigned short len, unsigned char addr, int ret)
{
	int i;
	if ((s_debug_level & D_DBG_READ_ARRAY) == D_DBG_READ_ARRAY) {

		printk(KERN_INFO "shtsc_read_regs:ADR:0x%02x r:%d :", addr, ret);
		for (i = 0; i < len; i++) {
			printk(KERN_INFO "%0X", data[i] & 0xFF);
		}
		printk(KERN_INFO "\n");
	}
	else if ((s_debug_level & D_DBG_READ_N) == D_DBG_READ_N) {
		
		printk(KERN_INFO "shtsc_read_regs:ADR:0x%02x l:%2d r:%d :", addr, len, ret);
		for (i = 0; i < 16; i++) {
			printk(KERN_INFO "%02X", data[i] & 0xFF);
		}
		printk(KERN_INFO "\n");
	}
	return 0;
}
#endif

static int shtsc_read_regs(struct shtsc_i2c *tsc,
		unsigned char *data, unsigned short len, unsigned char addr)
{
	struct i2c_client *client = tsc->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg[2];
	int ret;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len = 1;
	msg[0].buf = &addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags & I2C_M_TEN;
	msg[1].flags |= I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	ret = i2c_transfer(adap, msg, 2);

#ifdef D_DBG_LEVEL
	shtsc_read_print(data, len, addr, ret);
#endif /* #ifdef D_DBG_LEVEL */

	if( ret < 2 ){
		dev_err(&client->dev, "Unable to read i2c bus (adr=%02x)\n",addr);
		goto out;
	}

	return 0;
out:
	return -1;
}
#endif // (CONFIG_TOUCHSCREEN_SHTSC_UNKNOWNIF??)

static int WriteMultiBytes(void *_ts, u8 u8Addr, u8 *data, int len)
{
	struct shtsc_i2c *ts = _ts;

	return shtsc_write_regs(ts, u8Addr, len, data);
}

static int WriteOneByte(void *_ts, u8 u8Addr, u8 u8Val)
{
	struct shtsc_i2c *ts = _ts;
	u8 wData[1];
	wData[0] = u8Val;

	return shtsc_write_regs(ts, u8Addr, 1, wData);
}

static u8 ReadOneByte(void *_ts, u8 u8Addr)
{  
	struct shtsc_i2c *ts = _ts;
	u8 rData[1+1]; //requires one more byte to hold

	shtsc_read_regs(ts, rData, 1, u8Addr);
	return rData[0];
}

static u8 shtsc_read_reg_1byte(void *_ts, u8 u8Addr, u8 *u8Buf)
{
  u8 r;
  struct shtsc_i2c *ts = _ts;
  u8 rData[2]; //requires one more byte to hold
  
  r = shtsc_read_regs(ts, rData, 1, u8Addr);

  *u8Buf = rData[0];

  return r;
}

static void ReadMultiBytes(void *_ts, u8 u8Addr, u16 u16Len, u8 *u8Buf)
{
	struct shtsc_i2c *ts = _ts;
	shtsc_read_regs(ts, u8Buf, u16Len, u8Addr);
}

static int WaitAsync(void *_ts)
{
	struct shtsc_i2c *ts = _ts;
	unsigned long starttime = jiffies;
	unsigned long elapsetime;
	unsigned long timeout;

	for(;;) {
		elapsetime = (long)(jiffies-starttime);
		if( SYNC_TO_JIFFIES <= elapsetime ){
			return -EIO;
		}
		timeout = wait_for_completion_timeout(&ts->sync_completion,SYNC_TO_JIFFIES-elapsetime);
		if (0 == timeout)
			goto TIMEOUT_ERR;

		switch(ts->wait_state) {
			case WAIT_RESET:
				break;
			case WAIT_CMD:
				break;
			case WAIT_NONE:
				if (ts->wait_result == true) {
					SHTSC_INFO("wait state change: success\n");
					return 0;
				}
				else
					return -EIO;
			default:
				break;
		}
	}

TIMEOUT_ERR:
	shtsc_reset(ts, false);
	G_Irq_Mask = 0xffff;
	#ifdef GET_REPORT
	reportBuf[0] = (unsigned char)0;
	#endif
	shtsc_reset(ts, true);
	shtsc_system_init(ts);
	msleep(200);
	SHTSC_ERROR("wait state change: timeout failure reset\n");

	return 0;
}

#ifndef DEBUG_IRQ_DISABLE
static int SetBankAddr(void *_ts, u8 u8Bank)
{
	int r;
	struct shtsc_i2c *ts = _ts;

	r = WriteOneByte(ts, SHTSC_ADDR_BANK, u8Bank);
	if (r < 0) {
		SHTSC_ERROR("SetBankAddr: WriteOneByte error %d.", r);
	}

	return r;
}

static void ClearInterrupt(void *_ts, u16 u16Val)
{
	struct shtsc_i2c *ts = _ts;

	if(u16Val & 0x00FF)
		WriteOneByte(ts, SHTSC_ADDR_INT0, (u16Val & 0x00FF));
	if((u16Val & 0xFF00) >> 8)
		WriteOneByte(ts, SHTSC_ADDR_INT1, ((u16Val & 0xFF00) >> 8));
}

static void SetIndicator(void *_ts, u8 u8Val)
{
	struct shtsc_i2c *ts = _ts;

	WriteOneByte(ts, SHTSC_ADDR_IND, u8Val);
}
#endif /* DEBUG_IRQ_DISABLE */

static int shtsc_system_init(void *ts)
{
	unsigned int cnt = 0;
	struct input_dev *input_dev = ((struct shtsc_i2c *)ts)->input;

	/* PLL unlock - not used*/
	//  WriteOneByte(ts, SHTSC_ADDR_INTMASK1, (unsigned char)0x02);
	//  WriteOneByte(ts, (unsigned char)0x04, (unsigned char)0x02);
	for(cnt=0;cnt<SHTSC_MAX_FINGERS;cnt++){
		input_mt_slot(input_dev, cnt);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
#if defined(DEBUG_SHTSC)
		printk(KERN_INFO "shtsc_system_init() clear mt_slot(%d)\n",cnt);
#endif
	}
	input_report_key(input_dev, BTN_TOUCH, false );

#ifdef SHTSC_ICON_KEY_NUM
	for(cnt=0;cnt<SHTSC_ICON_KEY_NUM;cnt++){
		input_report_key( input_dev, shtsc_keyarray[cnt] , false );
#if defined(DEBUG_SHTSC)
		printk(KERN_INFO "shtsc_system_init() clear report_key(%d)\n",cnt);
#endif
	}
#endif
	input_sync(input_dev);
	return 0;
}


#ifndef DEBUG_IRQ_DISABLE
static void GetTouchReport(void *ts, u8 u8Num, u8 *u8Buf)
{
	struct shtsc_touch *touch = ((struct shtsc_i2c *)ts)->touch;
	struct input_dev *input_dev = ((struct shtsc_i2c *)ts)->input;

	int i;
	u8 ID,Status;
	int touchNum = 0;//2014.11.12 added

	if( u8Num > SHTSC_MAX_TOUCH_1PAGE ){
#if defined(DEBUG_SHTSC)
		printk(KERN_INFO "shtsc touch number erro (num=%d)\n",u8Num);
#endif
		return;
	}

	for(i = 0;i < u8Num;i++){
		Status = (u8Buf[i * SHTSC_LENGTH_OF_TOUCH] & 0xF0);
		touch[i].id = ID = u8Buf[i * SHTSC_LENGTH_OF_TOUCH] & 0x0F;    
		touch[i].size = u8Buf[i * SHTSC_LENGTH_OF_TOUCH + 1] & 0x3F;
		touch[i].type = (u8Buf[i * SHTSC_LENGTH_OF_TOUCH + 1] & 0xC0) >> 6;
		touch[i].x =
			(u8Buf[i * SHTSC_LENGTH_OF_TOUCH + 2] << 0) |
			(u8Buf[i * SHTSC_LENGTH_OF_TOUCH + 3] << 8);
		touch[i].y =
			(u8Buf[i * SHTSC_LENGTH_OF_TOUCH + 4] << 0) |
			(u8Buf[i * SHTSC_LENGTH_OF_TOUCH + 5] << 8);
		touch[i].z =
			(u8Buf[i * SHTSC_LENGTH_OF_TOUCH + 6] << 0) |
			(u8Buf[i * SHTSC_LENGTH_OF_TOUCH + 7] << 8);

#if defined(DEBUG_SHTSC)
		printk(KERN_INFO "shtsc ID=%2d, Status=%02x, Size=%3d, X=%5d, Y=%5d, z=%5d, num=%2d\n"
				,ID
				,Status
				,touch[i].size
				,touch[i].x
				,touch[i].y
				,touch[i].z
				,u8Num);
#endif

		input_mt_slot(input_dev, ID);
		if(Status & SHTSC_TOUCHOUT_STATUS){
			touch[i].status = SHTSC_F_TOUCH_OUT;
			input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false); // the 2nd parameter is DON'T CARE
			continue;
		}

		if(((struct shtsc_i2c *)ts)->swap_xy){//2014.11.12 added
			int tmp;
			tmp = touch[i].x;
			touch[i].x = touch[i].y;
			touch[i].y = tmp;
		}
		if(((struct shtsc_i2c *)ts)->flip_x){
			touch[i].x = (((struct shtsc_i2c *)ts)->max_x - touch[i].x) < 0 ? 0 : ((struct shtsc_i2c *)ts)->max_x - touch[i].x;
		}
		if(((struct shtsc_i2c *)ts)->flip_y){
			touch[i].y = (((struct shtsc_i2c *)ts)->max_y - touch[i].y) < 0 ? 0 : ((struct shtsc_i2c *)ts)->max_y - touch[i].y;
		}
		touchNum++;

		touch[i].status = SHTSC_F_TOUCH;
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);

		input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, touch[i].size);
		input_report_abs(input_dev, ABS_MT_POSITION_X, touch[i].x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, touch[i].y);
		//input_report_abs(input_dev, ABS_MT_PRESSURE  , touch[i].z);
	}
	input_report_key(input_dev, BTN_TOUCH, touchNum?true:false );//2014.11.12 added
}
#endif /* DEBUG_IRQ_DISABLE */


#ifdef FORCE_FIRM_UPDATE
void update_flash_func(struct work_struct *work)
{
	int retf = 0;
	int retp = 0;
	unsigned char *firmverbin,*paramverbin,*firmbin;
	unsigned int firmlen;
	unsigned char irqmask[2];

	mutex_lock(&shtsc_mutex);
	g_ts->enter_update = true;
	if (FlashUpdateByNoFirm) {
		{
			u8 TPC_start;
			SetBankAddr(g_ts, 0x18);
			TPC_start = ReadOneByte(g_ts, 0x14) & 0x20;
			SHTSC_ERROR("flash load error, TPC_start:%04x",TPC_start);
			if(TPC_start){
				SHTSC_ERROR("flash load error for calibration, don't update firmware");
				WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK1, (unsigned char)0x03);
				g_ts->enter_update = false;
				mutex_unlock(&shtsc_mutex);
				return ;
			}
		 }
		// no firmware found in flash
#ifdef CGID_CHECK
		CheckingFirmwareVersion = 2; /* no more update */
		//flash_access_start_shtsc(g_ts);
		irqmask[0] = ReadOneByte(g_ts,(unsigned char)SHTSC_ADDR_INTMASK0);
		irqmask[1] = ReadOneByte(g_ts,(unsigned char)SHTSC_ADDR_INTMASK1);
		WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK0, (unsigned char)0xff);
		WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK1, (unsigned char)0x03);
		flash_read(g_ts,CGID_ADDRESS,1,&cgid);
		WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK0, irqmask[0]);
		WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK1, irqmask[1]);
		if( (cgid == CG_VENDOR_OFILM)||(cgid == 0xff) ){
			firmverbin = (unsigned char *)FIRM_VERSION;
			paramverbin = (unsigned char *)PARAM_VERSION;
			firmbin = (unsigned char *)FIRM_IMAGE;
			firmlen = FIRM_SIZE;
		}
		else if( cgid == CG_VENDOR_BIEL ){
			firmverbin = (unsigned char *)FIRM_VERSION2;
			paramverbin = (unsigned char *)PARAM_VERSION2;
			firmbin = (unsigned char *)FIRM_IMAGE2;
			firmlen = FIRM_SIZE2;
		}
		else{
			firmverbin = NULL;
			paramverbin = NULL;
			firmbin = NULL;
			firmlen = 0;
		}
		printk(KERN_INFO "shtsc: CG Vendor ID : %02x\n",cgid);
#else /* CGID_CHECK */
		CheckingFirmwareVersion = 2; /* no more update */
		firmverbin = (unsigned char *)FIRM_VERSION;
		paramverbin = (unsigned char *)PARAM_VERSION;
		firmbin = (unsigned char *)FIRM_IMAGE;
		firmlen = FIRM_SIZE;
#endif /* CGID_CHECK */
		FlashUpdateByNoFirm = 0;

		/* force flash reprogramming */
		if ( firmverbin ) {
			update_flash(g_ts, firmbin, firmlen);
			printk(KERN_INFO "shtsc: force updating flash by no valid firmware.... done\n");
		} else {
			firmbin = (unsigned char *)FIRM_IMAGE;
			firmlen = FIRM_SIZE;
			update_flash(g_ts, firmbin, firmlen);
			printk(KERN_INFO "shtsc: no valid CGID found. so write firmware for INDEX 1.... done\n");
		}

	} else {
		if (CheckingFirmwareVersion == 0) {
			CheckingFirmwareVersion = 1;

			/* issue GetProperty command to read out the firmware/parameter version */
			issue_command(g_ts, CMD_GETPROPERTY, CMD_GETPROPERTY_LEN);

			printk(KERN_INFO "shtsc: issue_command in workqueue... done\n");
		}

		if (CheckingFirmwareVersion == 1) {
#ifdef CGID_CHECK
			CheckingFirmwareVersion = 2; /* no more update */
			//flash_access_start_shtsc(g_ts);
			irqmask[0] = ReadOneByte(g_ts,(unsigned char)SHTSC_ADDR_INTMASK0);
			irqmask[1] = ReadOneByte(g_ts,(unsigned char)SHTSC_ADDR_INTMASK1);
			WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK0, (unsigned char)0xff);
			WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK1, (unsigned char)0x03);
			flash_read(g_ts,CGID_ADDRESS,1,&cgid);
			WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK0, irqmask[0]);
			WriteOneByte(g_ts, (unsigned char)SHTSC_ADDR_INTMASK1, irqmask[1]);
			printk(KERN_INFO "shtsc: CG Vendor ID : %02x\n",cgid);
			if( (cgid == CG_VENDOR_OFILM)||(cgid == 0xff) ){
				firmverbin = (unsigned char *)FIRM_VERSION;
				paramverbin = (unsigned char *)PARAM_VERSION;
				firmbin = (unsigned char *)FIRM_IMAGE;
				firmlen = FIRM_SIZE;
			}
			else if( cgid == CG_VENDOR_BIEL ){
				firmverbin = (unsigned char *)FIRM_VERSION2;
				paramverbin = (unsigned char *)PARAM_VERSION2;
				firmbin = (unsigned char *)FIRM_IMAGE2;
				firmlen = FIRM_SIZE2;
			}
			else{
				firmverbin = NULL;
				paramverbin = NULL;
				firmbin = NULL;
				firmlen = 0;
			}
#else /* CGID_CHECK */
			CheckingFirmwareVersion = 2; /* no more update */
			firmverbin = (unsigned char *)FIRM_VERSION;
			paramverbin = (unsigned char *)PARAM_VERSION;
			firmbin = (unsigned char *)FIRM_IMAGE;
			firmlen = FIRM_SIZE;
#endif /* CGID_CHECK */
			touch_disable(g_ts);
			if(firmverbin != NULL ){
				retf = strncmp(firmverbin, (unsigned char *)&(CommandResultBuf[0x0a-0x08]),4);
				retp = strncmp(paramverbin, (unsigned char *)&(CommandResultBuf[0x12-0x08]),4);
				printk(KERN_INFO "shtsc: compare firmware Result f%d p%d \n", retf,retp);

				printk(KERN_INFO "shtsc: compare firmware K5: f%02X%02X%02X%02X p%02X%02X%02X%02X local: f%02X%02X%02X%02X p%02X%02X%02X%02X\n", 
					CommandResultBuf[0x0D-0x08], // current firm version
					CommandResultBuf[0x0C-0x08],
					CommandResultBuf[0x0B-0x08],
					CommandResultBuf[0x0A-0x08],
					CommandResultBuf[0x15-0x08], // current param version
					CommandResultBuf[0x14-0x08],
					CommandResultBuf[0x13-0x08],
					CommandResultBuf[0x12-0x08],
					firmverbin[3], // target firm version
					firmverbin[2],
					firmverbin[1],
					firmverbin[0],
					paramverbin[3], // target param version
					paramverbin[2],
					paramverbin[1],
					paramverbin[0]);
			}

			if( 0 == strncmp("\xff", (unsigned char *)&(CommandResultBuf[0x0D-0x08]),1)){
				retf = 1; 
				SHTSC_INFO("shtsc: firmware is test version, force to update \n");
			}

			if ( (retf > 0) || ((retf == 0) && (retp > 0)) )
			{
				issue_command(g_ts, CMD_SETSYSTEMSTATE_SLEEP, CMD_SETSYSTEMSTATE_SLEEP_LEN);
				update_flash(g_ts, firmbin, firmlen);
			} 
			touch_enable(g_ts);
		}
	}
	g_ts->enter_update = false;
	mutex_unlock(&shtsc_mutex);

	return;
}
#endif /* FORCE_FIRM_UPDATE */


//#define GET_REPORT  // experimental

#ifdef GET_REPORT
#include <linux/time.h>

#define REP_SIZE (4+4+4+120)
#define MAX_REPORTS 14

unsigned char reportBuf[4+ MAX_REPORTS*REP_SIZE];
volatile unsigned char Mutex_GetReport = false;

#endif /* GET_REPORT */

static void shtsc_print_irq(int u16status) {
#ifdef D_DBG_LEVEL
  if ((s_debug_level & D_DBG_IRQ) == D_DBG_IRQ) {
    printk(KERN_INFO "shtsc:[IRQ] \n");
    if (u16status & SHTSC_STATUS_TOUCH_READY)
      printk(KERN_INFO "TOUCH_READY \n");
    if (u16status & SHTSC_STATUS_RESUME_PROX)
      printk(KERN_INFO "RESUME_PROX \n");
    if (u16status & SHTSC_STATUS_WDT)
      printk(KERN_INFO "WDT \n");
    if (u16status & SHTSC_STATUS_DCMAP_READY)
      printk(KERN_INFO "DCMAP_READY \n");
    if (u16status & SHTSC_STATUS_COMMAND_RESULT)
      printk(KERN_INFO "COMMAND_RESULT \n");
    if (u16status & SHTSC_STATUS_KNOCK_CODE)
      printk(KERN_INFO "KNOCK_CODE \n");
    if (u16status & SHTSC_STATUS_FLASH_LOAD_ERROR)
      printk(KERN_INFO "FLASH_LOAD_ERROR \n");
    if (u16status & SHTSC_STATUS_PLL_UNLOCK)
      printk(KERN_INFO "PLL_UNLOCK \n");
    printk(KERN_INFO "\n");
  }
#endif
  return ;
}


static u8 dcmapBuf[MAX_DCMAP_SIZE+128];
static u8 dcmap[31*18*2+128]; // greater than 17*32*2

#ifndef DEBUG_IRQ_DISABLE
static void shtsc_gesture_report(struct work_struct *work)
//static void shtsc_gesture_report(void)
{
	int i;
	char **envp = NULL;
	struct device *touchscreen_dev;
	bool is_support = false;

	touchscreen_dev = touchscreen_get_dev();

	printk(KERN_ERR "SHTSC:YLLOG: gesture code: %d\n", resumeStatus);
	for(i = 0; i < ARRAY_SIZE(tp_ges_wakeup); i++) {
		if((resumeStatus == tp_ges_wakeup[i].code) && \
				(support_gesture & tp_ges_wakeup[i].mask)) {
			//doze_status = DOZE_WAKEUP;//yangmingjin delete 2016.1.16
			is_support = true;
			sprintf(wakeup_slide, tp_ges_wakeup[i].gesture);
			envp = tp_ges_wakeup[i].evp;
			break;
		}
	}

	//if(doze_status == DOZE_WAKEUP) {//yangmingjin delete 2016.1.16
	if(is_support) //yangmingjin add 2016.1.16
	{
		kobject_uevent_env(&touchscreen_dev->kobj, KOBJ_CHANGE, envp);
		printk(KERN_INFO "SHTSC:YLLOG:send <%s> kobject uevent!\n", wakeup_slide);
		if((resumeStatus == 0x04)
				|| (resumeStatus == 0x05))
		{
			printk(KERN_INFO "SHTSC:YLLOG:gesture support,lcd off.\n");
			enable_gesture(true);
		}
	} else {
		printk(KERN_INFO "SHTSC:YLLOG:gesture not support.\n");
		//Goto gesture mode
		enable_gesture(true);
	}


	return;
}

static irqreturn_t shtsc_irq_thread(int irq, void *_ts)
{
	struct shtsc_i2c *ts = _ts;

	u16 u16status;
	u8 u8Num = 0;
	u8 tmpbuf[128];
	u8 numDriveLine2, numSenseLine2;
	u8 num_adc_dmy[3];
	u8 regcommonbuf[11];

	SHTSC_DEBUG("[ENTER] shtsc_irq .");

	/* Get Interrupt State */
	ReadMultiBytes(ts,SHTSC_ADDR_INT0,11,regcommonbuf);
	u16status = ((regcommonbuf[SHTSC_ADDR_INT1] << 8) | regcommonbuf[SHTSC_ADDR_INT0]);
	G_Irq_Mask = ~((regcommonbuf[SHTSC_ADDR_INTMASK1] << 8) | regcommonbuf[SHTSC_ADDR_INTMASK0]);
	u16status &= G_Irq_Mask;

#if defined(DEBUG_SHTSC)
	if ((u16status != 0x0001) && (u16status != 0x0000)) {
		SHTSC_INFO("[IRQ] shtsc_irq: %04X !", u16status);
	}
#endif

	while(u16status != 0){
		shtsc_print_irq(u16status);

		if ( u16status&(~SHTSC_STATUS_LEGAL)) {
			SHTSC_ERROR("SHTSC:[IRQ] shtsc_irq illegal");
			shtsc_reset(g_ts, false);
			G_Irq_Mask = 0xffff;
		#ifdef GET_REPORT
			reportBuf[0] = (unsigned char)0;
		#endif /* GET_REPORT */
			shtsc_reset(g_ts, true);
			shtsc_system_init(g_ts);
			msleep(200);
			return IRQ_HANDLED;
		}

		if (u16status & (SHTSC_STATUS_WDT | SHTSC_STATUS_UNDEFINED_BIT)) { //modified by misaki on 12/15
			ClearInterrupt(ts, SHTSC_STATUS_WDT);
			u16status = 0;//modified by misaki on 12/15 // u16status &=~SHTSC_STATUS_WDT;
			SHTSC_ERROR("[IRQ] resetting by WDT");
			shtsc_reset(g_ts, false);
			G_Irq_Mask = 0xffff;
			msleep(10); 
		#ifdef GET_REPORT
			reportBuf[0] = (unsigned char)0;
		#endif /* GET_REPORT */
			shtsc_reset(g_ts, true);
			shtsc_system_init(g_ts);
			msleep(200);
			s_tpc_state[D_STATE_RESET] = D_STATE_RESET_WDT;
			break;
		}

		if (u16status == SHTSC_STATUS_FLASH_LOAD_ERROR) { // only this occasion
			//
			// FLASH_LOAD_ERROR
			// occurs when flash is erased
			// nothing can be done
			//
			SHTSC_ERROR("[IRQ] shtsc flash load error !");

		#ifdef FORCE_FIRM_UPDATE
			FlashUpdateByNoFirm = 1;
			//queue_work(ts->workqueue, &ts->workstr);
		#endif
			// from now on, no int for this
			G_Irq_Mask &= ~SHTSC_STATUS_FLASH_LOAD_ERROR;

			/* Clear Interrupt */
			ClearInterrupt(ts, SHTSC_STATUS_FLASH_LOAD_ERROR);
			u16status &= ~SHTSC_STATUS_FLASH_LOAD_ERROR;

			if (ts->wait_state == WAIT_RESET) {//2014.11.20 added
				ts->wait_state = WAIT_NONE;
				ts->wait_result = true;
				complete(&ts->sync_completion); //2015.12.21
			} 
      // mask it
			WriteOneByte(ts, (unsigned char)SHTSC_ADDR_INTMASK1, (unsigned char)(regcommonbuf[SHTSC_ADDR_INTMASK1] | 0x01));//add by misaki on 8/4
			regcommonbuf[SHTSC_ADDR_INTMASK1] |= 0x01; //2015.12.25

			s_tpc_state[D_STATE_RESET] = D_STATE_RESET_FL_ERR;
			return IRQ_HANDLED;
		}

		if (u16status & SHTSC_STATUS_PLL_UNLOCK) {
			//
			// PLL unlock
			//

			SHTSC_DEBUG("[IRQ] shtsc_irq PLL_UNLOCK: %04X !", u16status);
			// mask it
			WriteOneByte(ts, (unsigned char)SHTSC_ADDR_INTMASK1, (unsigned char)(regcommonbuf[SHTSC_ADDR_INTMASK1] | 0x02)); // 2015.12.25
			regcommonbuf[SHTSC_ADDR_INTMASK1] |= 0x02; //2015.12.25
			// from now on, no int for this
			G_Irq_Mask &= ~SHTSC_STATUS_PLL_UNLOCK;

			/* Clear Interrupt */
			ClearInterrupt(ts, SHTSC_STATUS_PLL_UNLOCK);
			u16status &= ~SHTSC_STATUS_PLL_UNLOCK;
		}

		if (u16status & SHTSC_STATUS_POWER_UP) {
			//
			// Power-up
			//

			SHTSC_DEBUG("[IRQ] shtsc power-up !");

			/* Clear Interrupt */
			ClearInterrupt(ts, SHTSC_STATUS_POWER_UP);
			u16status &= ~SHTSC_STATUS_POWER_UP;

			if (ts->wait_state == WAIT_RESET) {//2014.11.20 added
				ts->wait_state = WAIT_NONE;
				ts->wait_result = true;
				complete(&ts->sync_completion); //2015.12.21
			} 
			s_tpc_state[D_STATE_RESET] = D_STATE_RESET_POWER_UP;
		}

		if (u16status & SHTSC_STATUS_KNOCK_CODE) {
			ClearInterrupt(ts, SHTSC_STATUS_KNOCK_CODE);
			u16status &= ~SHTSC_STATUS_KNOCK_CODE;
			SHTSC_ERROR("[IRQ] shtsc_irq KNOCK_CODE");
		}

		if (u16status & SHTSC_STATUS_RESUME_PROX) {
			//
			// Resume from DeepIdle
			// or
			// PROXIMITY
			//

#if defined(DEBUG_SHTSC)
			SHTSC_DEBUG("[IRQ] shtsc resume from DeepIdle or prox\n");
#endif

			SetBankAddr(ts, SHTSC_BANK_TOUCH_REPORT);
			resumeStatus = ReadOneByte(ts, SHTSC_ADDR_RESUME_PROX);

#ifdef ENABLE_SUSPEND_GESTURE
			SHTSC_INFO("shtsc: entering gesture interrupt !");

			//shtsc_gesture_report();	//yulong add use yl code
			queue_work(ts->workqueue, &ts->workstr2);

#else /* ENABLE_SUSPEND_GESTURE */
			// throw interrupt to userland
			{
				struct siginfo info;
				struct task_struct *task;

				memset(&info, 0, sizeof(struct siginfo));
				info.si_signo = SHTSC_SIGNAL;
				info.si_code = SI_QUEUE;
				info.si_int = 0;

				rcu_read_lock();
				task = find_task_by_vpid(pid);
				rcu_read_unlock();

				if (task) {
					send_sig_info(SHTSC_SIGNAL, &info, task); //send signal to user land
					printk(KERN_INFO "[SIGNAL] sending shtsc signal (resume_prox)\n");
				}
			}
#endif /* ENABLE_SUSPEND_GESTURE */

			/* Clear Interrupt */
			ClearInterrupt(ts, SHTSC_STATUS_RESUME_PROX);
			u16status &= ~SHTSC_STATUS_RESUME_PROX;

#ifdef ENABLE_SUSPEND_GESTURE

#ifdef FORCE_DISPLAY_ON
			/* set display on without any control of the system */
			/* the system must control the display without this */
			if((doze_status == DOZE_WAKEUP) 
					&& (resumeStatus != 0x04)
					&& (resumeStatus != 0x05))
				//&& (resumeStatus != TW_SUPPORT_LEFT_SLIDE_WAKEUP)
				//&& (resumeStatus != TW_SUPPORT_RIGHT_SLIDE_WAKEUP))
			{ 
				input_report_key( ts->input, KEY_POWER, true );
				input_sync(ts->input);
				input_report_key( ts->input, KEY_POWER, false );
				input_sync(ts->input);
				msleep(200); /* wait HSYNC becomes stable */
			}
#endif

#endif
		}

		if (u16status & SHTSC_STATUS_TOUCH_READY) {
			//
			// Touch report
			//
			if ((s_test_mode & D_DBG_TOUCH_IRQ_DISABLE) == D_DBG_TOUCH_IRQ_DISABLE) { //2015.12.24
				printk(KERN_INFO "[IRQ]:D_DBG_TOUCH_IRQ_DISABLE\n");
				WriteOneByte(ts, (unsigned char)SHTSC_ADDR_INTMASK0, (unsigned char)(regcommonbuf[SHTSC_ADDR_INTMASK0] | SHTSC_STATUS_TOUCH_READY)); // 2015.12.25
				regcommonbuf[SHTSC_ADDR_INTMASK0] |= SHTSC_STATUS_TOUCH_READY; //2015.12.25
				u16status &= ~SHTSC_STATUS_TOUCH_READY;
				break;
			}
			else if ((s_test_mode & D_DBG_TOUCH_IRQ_CLEAR) == D_DBG_TOUCH_IRQ_CLEAR) { //2015.12.24
				if ((s_debug_level & D_DBG_TOUCH_CLEAR) == D_DBG_TOUCH_CLEAR)
					printk(KERN_INFO "[IRQ]:D_DBG_TOUCH_IRQ_CLEAR mask(0x%x)\n", regcommonbuf[SHTSC_ADDR_INTMASK0] & SHTSC_STATUS_TOUCH_READY);
				if (regcommonbuf[SHTSC_ADDR_INTMASK0] & SHTSC_STATUS_TOUCH_READY) {
					WriteOneByte(ts, (unsigned char)SHTSC_ADDR_INTMASK0, (unsigned char)(regcommonbuf[SHTSC_ADDR_INTMASK0] & (~SHTSC_STATUS_TOUCH_READY))); 
					regcommonbuf[SHTSC_ADDR_INTMASK0] &= ~SHTSC_STATUS_TOUCH_READY;//2015.12.25
				} // 2015.12.25
				ClearInterrupt(ts, SHTSC_STATUS_TOUCH_READY);
				WriteOneByte(ts, (unsigned char)SHTSC_ADDR_IND, (unsigned char)SHTSC_IND_TOUCH);
				u16status &= ~SHTSC_STATUS_TOUCH_READY;
				break;
			}

			SHTSC_DEBUG("[IRQ] shtsc touch-ready cleared !");

#ifdef GET_REPORT
			{
				/*
header:
1: counts of report (not a number of touch)
3: reserved

repeated contents if "counts of report"  is more than one.
1: cyclic counter (from 0 to 255. return to zero on next to 255)
3: reserved
4: report time (sec)
4: report time (usec)
120: bank0 report
				 */

				static unsigned char cyclic = 255;
				volatile unsigned char count;
				unsigned char bank0[120];
				struct timeval tv;

				while (Mutex_GetReport)
					;

				Mutex_GetReport = true;

				count = reportBuf[0];

				if (cyclic == 255) {
					cyclic = 0;
				} else {
					cyclic++;
				}

#if defined(DEBUG_SHTSC)
				//	printk(KERN_INFO "touch report: count %d, cyclic %d\n", count, cyclic);
#endif
				if (count == MAX_REPORTS) {
					//	  printk(KERN_INFO "touch report buffer full\n");
					;
				} else {	
					do_gettimeofday(&tv);
#if defined(DEBUG_SHTSC)
					//	  printk(KERN_INFO "touch time: %ld.%ld, bank0pos:%d\n", (long)tv.tv_sec, (long)tv.tv_usec, (1+3+ count*REP_SIZE + 12));
#endif

					reportBuf[1+3+ count*REP_SIZE +0] = cyclic;
					reportBuf[1+3+ count*REP_SIZE +1] = 0;
					reportBuf[1+3+ count*REP_SIZE +2] = 0;
					reportBuf[1+3+ count*REP_SIZE +3] = 0;

					reportBuf[1+3+ count*REP_SIZE +4+0] = (tv.tv_sec & 0xff);
					reportBuf[1+3+ count*REP_SIZE +4+1] = ((tv.tv_sec >> 8) & 0xff);
					reportBuf[1+3+ count*REP_SIZE +4+2] = ((tv.tv_sec >> 16) & 0xff);
					reportBuf[1+3+ count*REP_SIZE +4+3] = ((tv.tv_sec >> 24) & 0xff);

					reportBuf[1+3+ count*REP_SIZE +4+4+0] = (tv.tv_usec & 0xff);
					reportBuf[1+3+ count*REP_SIZE +4+4+1] = ((tv.tv_usec >> 8) & 0xff);
					reportBuf[1+3+ count*REP_SIZE +4+4+2] = ((tv.tv_usec >> 16) & 0xff);
					reportBuf[1+3+ count*REP_SIZE +4+4+3] = ((tv.tv_usec >> 24) & 0xff);

					SetBankAddr(ts, SHTSC_BANK_TOUCH_REPORT);
					ReadMultiBytes(ts, 0x08, 120, bank0);

					memcpy((unsigned char *)(&reportBuf[1+3+ count*REP_SIZE + 12]), (unsigned char *)bank0, 120);
					reportBuf[0] = (unsigned char)(count+1);
				}

				Mutex_GetReport = false;
			}
#endif /* GET_REPORT */

			/* Get number of touches */
			{
				u8 u8Buf[128];
				if( regcommonbuf[SHTSC_ADDR_BANK] != SHTSC_BANK_TOUCH_REPORT ){
					WriteOneByte(ts, SHTSC_ADDR_BANK, SHTSC_BANK_TOUCH_REPORT);
					ReadMultiBytes(ts,SHTSC_ADDR_TOUCH_NUM,3,regcommonbuf+SHTSC_ADDR_TOUCH_NUM);
				}
				u8Num = regcommonbuf[SHTSC_ADDR_TOUCH_NUM];

#if defined(DEBUG_SHTSC)
				printk(KERN_INFO "shtsc touch num=%d\n", u8Num);
#endif

#ifdef SHTSC_ICON_KEY_NUM
				{
					unsigned int buttonBit,cnt;
					buttonBit = ReadOneByte(ts, 0x0A);
#if defined(DEBUG_SHTSC)
					printk(KERN_INFO "shtsc(k5) icon key bitfield = %02x\n", buttonBit );
#endif
					for(cnt=0;cnt<SHTSC_ICON_KEY_NUM;cnt++){
						if(buttonBit&(0x01<<cnt)){
							input_report_key( ts->input, shtsc_keyarray[cnt] , true );
						}
						else{
							input_report_key( ts->input, shtsc_keyarray[cnt] , false );
						}
					}
				}
#endif
				/* Retrieve touch report */
				if (u8Num > 0){
					ReadMultiBytes((struct shtsc_i2c *)ts, SHTSC_ADDR_TOUCH_REPORT, SHTSC_LENGTH_OF_TOUCH * u8Num, u8Buf);
				}
				/* Clear Interrupt */
				u16status &= ~SHTSC_STATUS_TOUCH_READY;
				regcommonbuf[SHTSC_ADDR_INT0] = SHTSC_STATUS_TOUCH_READY;
				regcommonbuf[SHTSC_ADDR_BANK] = SHTSC_BANK_TOUCH_REPORT;
				regcommonbuf[SHTSC_ADDR_IND] = SHTSC_IND_TOUCH;
				WriteMultiBytes(ts,SHTSC_ADDR_INT0,regcommonbuf,4);
				if (u8Num > 0){
					GetTouchReport(ts, u8Num, u8Buf);
				}

				input_sync(ts->input);
			}
		}

		if (u16status & SHTSC_STATUS_COMMAND_RESULT) {
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "[IRQ] shtsc command result\n");
#endif
			SetBankAddr(ts,SHTSC_BANK_COMMAND_RESULT);
			ReadMultiBytes(ts, 0x08, MAX_COMMAND_RESULT_LEN, CommandResultBuf); // always read entire result. TEGAPP will handle it.

#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "[IRQ] shtsc command result, %02X, %02X, %02X\n", CommandResultBuf[0], CommandResultBuf[1], CommandResultBuf[2]);
#endif

			/* Clear Interrupt */
			ClearInterrupt(ts, SHTSC_STATUS_COMMAND_RESULT);
			u16status &= ~SHTSC_STATUS_COMMAND_RESULT;

			if (ts->wait_state == WAIT_CMD) {
				if ((CommandResultBuf[0] != ts->cmd) || (CommandResultBuf[1] != 0)) {
					ts->wait_state = WAIT_NONE;
					ts->wait_result = false;
				} else {
					ts->wait_state = WAIT_NONE;
					ts->wait_result = true;
				}
				complete(&ts->sync_completion); //2015.12.21
			}
		}

		if (u16status & SHTSC_STATUS_DCMAP_READY) {
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "[IRQ] shtsc DCMAP READY\n");
#endif
			/* Clear Interrupt */
			ClearInterrupt(ts, SHTSC_STATUS_DCMAP_READY);
			u16status &= ~SHTSC_STATUS_DCMAP_READY;

			{ // L2
				unsigned char dsFlag, readingSenseNum, ram_addr[2];
				unsigned vramAddr;
				unsigned readingSize;

				// get SD/DS and size
				ram_addr[0] = 0x58;
				ram_addr[1] = 0xBF;
				WriteMultiBytes(ts,(unsigned char)0x06,ram_addr,2);

				SetBankAddr(ts,SHTSC_BANK_DCMAP);
				ReadMultiBytes(ts, 0x08, 5, tmpbuf);

				numSenseLine2 = tmpbuf[0];
				numDriveLine2 = tmpbuf[1];

				dsFlag = tmpbuf[4]; // 1 for DS, 0 for SD
				vramAddr = ((tmpbuf[3]<<8) | tmpbuf[2]);
				// readingSenseNum is greater or equal to itself, but a multiply of 4
				readingSenseNum = (unsigned char)((numSenseLine2+3)/4); // not a double but int
				readingSenseNum *= 4;

				readingSize = readingSenseNum * numDriveLine2 * 2; /* 2:16bit */

				num_adc_dmy[0] = num_adc_dmy[2] = 0; //top and left have no 0-filled lines
				num_adc_dmy[1] = readingSenseNum - numSenseLine2; //right side

				//	      printk(KERN_INFO "%s(%d): num_adc_dmy[1]:%d\n", __FILE__, __LINE__, num_adc_dmy[1]);
				//	      printk(KERN_INFO "%s(%d):Sense:%d, Drive:%d\n", __FILE__, __LINE__, numSenseLine2, numDriveLine2);
				//	      printk(KERN_INFO "%s(%d): dsFlag:%d\n", __FILE__, __LINE__, dsFlag);

				// read DCmap values from register
				// store it to read buffer memory for read action
				{ // L1
					/* read 120 bytes from Bank5, address 8 */
					/* read loop required */
					int bytes = readingSize;
					int size;
					int index = 0;
					//SetBankAddr(ts,SHTSC_BANK_DCMAP);

					//	      printk(KERN_INFO "%s(%d):readingSize:%d\n", __FILE__, __LINE__, readingSize);
					while (bytes > 0) {
						ram_addr[0] = (unsigned char)(vramAddr&0xff); // address to read (Lower)
						ram_addr[1] = (unsigned char)((vramAddr&0xff00)>>8); // address to read (Higher)
						WriteMultiBytes(ts,(unsigned char)0x06,ram_addr,2);

						size = ((bytes >= 120) ? 120 : bytes);
						//		printk(KERN_INFO "%s(%d):bytes:%d, size:%d, index:%d, vramAddr:%x\n", __FILE__, __LINE__, bytes, size, index, vramAddr);

						ReadMultiBytes(ts, 0x08, size, &(dcmapBuf[index]));
						index += size;
						bytes -= size;
						vramAddr += size;
					} // while
				} // L1


				{ //L3
					int sindex = 0, dindex = 0;
					int l, x, y;
					// dcmap header
					// [0]: horizontal data num (in short, not byte)
					// [1]: vertical data num

					x = dcmap[dindex++] = numSenseLine2;
					y = dcmap[dindex++] = numDriveLine2;
					dcmap[dindex++] = dsFlag;
#if 0 // debug
					dcmap[dindex++] = frm_count++; // just for debug
#else /* 1 */
					dcmap[dindex++] = 0x00; // reserved
#endif /* 1 */

					//top
					sindex = (num_adc_dmy[0] + x + num_adc_dmy[1]) * num_adc_dmy[2] * 2;

					// contents line
					for (l = 0; l < y; l++) {
						// left
						sindex += (num_adc_dmy[0] * 2);

						// contents
						memcpy((u8 *)&(dcmap[dindex]), (u8 *)&(dcmapBuf[sindex]), (x*2));
						dindex += (x*2);
						sindex += (x*2);

						// right
						sindex += (num_adc_dmy[1] * 2);
					}

					// for read()
					//	      printk(KERN_INFO "check buf_pos: %d\n", buf_pos);
					if (buf_pos == 0) {
						memcpy((u8 *)devbuf, (u8 *)dcmap, (4+x*y*2));
						//		printk(KERN_INFO "setting buf_pos: %d\n", buf_pos);
						buf_pos = (4+x*y*2);
						//		printk(KERN_INFO "set buf_pos: %d\n", buf_pos);
					}

				} //L3
			} // L2 DEVICE_LR388K5 block
			//*************************
		}
		if (u16status != 0) {
			printk(KERN_INFO "[IRQ] shtsc unknown interrupt status %04X\n", u16status);
			u16status = 0;

			shtsc_reset(g_ts, false);
			G_Irq_Mask = 0xffff;
#ifdef GET_REPORT
			reportBuf[0] = (unsigned char)0;
#endif /* GET_REPORT */
			//      g_ts->wait_state = WAIT_RESET;
			//      g_ts->wait_result = false;
			shtsc_reset(g_ts, true);
			// wait
			//      WaitAsync(g_ts);
			shtsc_system_init(g_ts);
			msleep(200);
		}
	}

	if (u8Num != 0 ) {
#if defined(DEBUG_SHTSC)
		printk(KERN_INFO "shtsc flush touch input(%d)\n", u8Num);
#endif
		/*
		   input_report_key(ts->input, BTN_TOUCH, touchNum?true:false );

		   input_sync(ts->input);
		 */
	}

	return IRQ_HANDLED;
}
#endif /* DEBUG_IRQ_DISABLE */

static ssize_t dev_read( struct file* filp, char* buf, size_t count, loff_t* pos )
{
	int copy_len;
	int i;

	//	printk( KERN_INFO "shtsc : read()  called, buf_pos: %d, count: %d\n", buf_pos, count);
	if ( count > buf_pos )
		copy_len = buf_pos;
	else
		copy_len = count;

	//	printk( KERN_INFO "shtsc : copy_len = %d\n", copy_len );
	if ( copy_to_user( buf, devbuf, copy_len ) ) {
		printk( KERN_INFO "shtsc : copy_to_user failed\n" );
		return -EFAULT;
	}

	*pos += copy_len;

	for ( i = copy_len; i < buf_pos; i ++ )
		devbuf[ i - copy_len ] = devbuf[i];

	buf_pos -= copy_len;

	//	printk( KERN_INFO "shtsc : buf_pos = %d\n", buf_pos );
	return copy_len;
}

static void shtsc_reset(void *_ts, bool reset)
{
	struct shtsc_i2c *ts = _ts;
	if (ts->reset_pin) {
		if (reset) {
			shtsc_reset_delay();		
		}
		printk(KERN_INFO "shtsc: shtsc_reset: %d\n", reset);
		gpio_direction_output(ts->reset_pin, reset);
		//G_reset_done = reset;
		if (! reset) {
			G_Irq_Mask = 0xffff;
		}
	}
	s_tpc_state[D_STATE_RESET] = 0;//2015.12.24
}

static void shtsc_reset_L_H(void *_ts )
{
        struct shtsc_i2c *ts = _ts;
	shtsc_reset(g_ts, false);

	G_Irq_Mask = 0xffff;
#ifdef GET_REPORT
	reportBuf[0] = (unsigned char)0;
#endif /* GET_REPORT */

	g_ts->wait_state = WAIT_RESET;
	g_ts->wait_result = false;
	INIT_COMPLETION(ts->sync_completion); //yangmingjin add 2016.1.15
	shtsc_reset(g_ts, true);
	// wait
	WaitAsync(g_ts);
	shtsc_system_init(g_ts);
}

int flash_access_start_shtsc(void *_ts)
{
	struct shtsc_i2c *ts = _ts;
	// mask everything
	WriteOneByte(ts, (unsigned char)SHTSC_ADDR_INTMASK0, (unsigned char)0xff);
	WriteOneByte(ts, (unsigned char)SHTSC_ADDR_INTMASK1, (unsigned char)0x03);
	msleep(100);

	/* TRIM_OSC = 0 */
	WriteOneByte(ts, (unsigned char)0x02, (unsigned char)0x18); // bank change reg
	WriteOneByte(ts, (unsigned char)0x11, (unsigned char)0x19);
	return 0;
}

int flash_access_end_shtsc(void *_ts)
{
	struct shtsc_i2c *ts = _ts;
	msleep(100);
	shtsc_reset_L_H(ts);
#if 0
	shtsc_reset(ts, false);
	msleep(100);
	shtsc_reset(ts, true);
	msleep(10);
	shtsc_system_init(ts);

	msleep(100);
#endif

	return 0;
}

#define RETRY_COUNT (2000*10) //experimental
int flash_erase_page_shtsc(void *_ts, int page)
{
	struct shtsc_i2c *ts = _ts;
	//  int chan= 0;
	volatile unsigned char readData;
	int retry=0;

	/* BankChange */
	//SPI_ArrieRegWrite(0x02,0x1C);
	WriteOneByte(ts, (unsigned char)0x02, (unsigned char)0x1C); // bank change reg

	/* CLKON_CTL0 */
	//SPI_ArrieRegWrite(0x14,0x22);	/* CLKON_FLC,CLKON_REGBUS */
	WriteOneByte(ts, (unsigned char)0x14, (unsigned char)0x22);

	/* FLC_CTL CS_HIGH,WP_DISABLE */
	//	SPI_ArrieRegWrite(0x3C,0x14);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x14);

	/* FLC_CTL CS_LOW,WP_DISABLE */
	//	SPI_ArrieRegWrite(0x3C,0x16);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x16);
	/* FLC_TxDATA WRITE_ENABLE */
	//SPI_ArrieRegWrite(0x3D,SPI_CMD_WRITE_EN);
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)FLASH_CMD_WRITE_EN);
	/* FLC_CTL CS_HIGH,WP_DISABLE */
	//	SPI_ArrieRegWrite(0x3C,0x14);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x14);

	/* FLC_CTL CS_LOW,WP_DISABLE */
	//	SPI_ArrieRegWrite(0x3C,0x16);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x16);


	/* FLC_TxDATA CHIP_ERASE_COMMAND */
	//	SPI_ArrieRegWrite(0x3D,SPI_CMD_CHIP_ERASE);
	// not a chip erase, but a sector erase for the backward compatibility!!
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)FLASH_CMD_SECTOR_ERASE);
	// 24bit address. 4kByte=001000H. 00x000H:x=0-f -> 4kB*16=64kB
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)0x00);
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)((page<<4)&0xFF));
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)0x00);

	/* FLC_CTL CS_HIGH,WP_DISABLE */
	//	SPI_ArrieRegWrite(0x3C,0x14);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x14);


	/* wait until 'BUSY = LOW' */
	do {
		retry++;
		////		msleep(10);
		if (retry > RETRY_COUNT)
			goto RETRY_ERROR;

		/* FLC_CTL CS_LOW,WP_DISABLE */
		//		SPI_ArrieRegWrite(0x3C,0x16);
		WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x16);
		/* FLC_TxDATA READ_STATUS_COMMAND*/
		//		SPI_ArrieRegWrite(0x3D,SPI_CMD_READ_ST);
		WriteOneByte(ts, (unsigned char)0x3D, FLASH_CMD_READ_ST);
		/* Dummy data */
		//		SPI_ArrieRegWrite(0x3D,0);
		WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)0x00);
		/* FLC_RxDATA */
		//		readData = SPI_ArrieRegRead(0x3F);
		readData = ReadOneByte(ts, 0x3F);
		/* FLC_CTL CS_HIGH,WP_DISABLE */
		//		SPI_ArrieRegWrite(0x3C,0x14);
		WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x14);
	} while (readData & FLASH_ST_BUSY); 		/* check busy bit */

	return 0;

RETRY_ERROR:
	printk(KERN_INFO "FATAL: flash_erase_page_shtsc retry %d times for page %d - FAILED!\n", retry, page);
	return 1;
}
#define FLASH_PAGE_SIZE (4<<10) // 4k block for each page
#define FLASH_PHYSICAL_PAGE_SIZE (256) // can write 256bytes at a time.

int flash_write_page_shtsc(void *_ts, int page, unsigned char *data)
{
	struct shtsc_i2c *ts = _ts;
	//  int chan = 0;
	int retry = 0;
	//  unsigned addr; // in-page address (from 0 to FLASH_PAGE_SIZE-1)
	unsigned paddr; // address (32 or 64kB area)
	volatile unsigned char readData;
	int cnt, idx;

	/* BankChange */
	//	SPI_ArrieRegWrite(0x02,0x1C);
	WriteOneByte(ts, (unsigned char)0x02, (unsigned char)0x1C); // bank change reg

	/* CLKON_CTL0 */
	//	SPI_ArrieRegWrite(0x14,0x22);	/* CLKON_FLC,CLKON_REGBUS */
	WriteOneByte(ts, (unsigned char)0x14, (unsigned char)0x22);

	/* FLC_CTL */
	//	SPI_ArrieRegWrite(0x3C,0x14);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x14);

	/* 256 bytes / Flash write page, 4kByte / logical(virtual) flash page */
	for (cnt = 0; cnt < (FLASH_PAGE_SIZE / FLASH_PHYSICAL_PAGE_SIZE); cnt++) {
		paddr = (page * FLASH_PAGE_SIZE) + (cnt * FLASH_PHYSICAL_PAGE_SIZE);
		// 4k page offset + in-page offset. 4k*n+256*m

		/* FLC_CTL CS_LOW,WP_DISABLE */
		//		SPI_ArrieRegWrite(0x3C,0x16);
		WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x16);
		/* FLC_TxDATA WRITE_ENABLE */
		//		SPI_ArrieRegWrite(0x3D,SPI_CMD_WRITE_EN);
		WriteOneByte(ts, (unsigned char)0x3D, FLASH_CMD_WRITE_EN);
		/* FLC_CTL CS_HIGH,WP_DISABLE */
		//		SPI_ArrieRegWrite(0x3C,0x14);
		WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x14);

		/* FLC_CTL CS_LOW,WP_DISABLE */
		//		SPI_ArrieRegWrite(0x3C,0x16);
		WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x16);

		/* FLC_TxDATA PAGE_PROGRAM_COMMAND */
		//		SPI_ArrieRegWrite(0x3D,SPI_CMD_PAGE_WR);
		WriteOneByte(ts, (unsigned char)0x3D, FLASH_CMD_PAGE_WR);
		/* FLC_TxDATA Address(bit16~23) */
		//		SPI_ArrieRegWrite(0x3D,((address>>16)&0xFF));
		WriteOneByte(ts, (unsigned char)0x3D, ((paddr>>16)&0xFF));
		/* FLC_TxDATA Address(bit8~15) */
		//		SPI_ArrieRegWrite(0x3D,((address>>8)&0xFF));
		WriteOneByte(ts, (unsigned char)0x3D, ((paddr>>8)&0xFF));
		/* FLC_TxDATA Address(bit0~7) */
		//		SPI_ArrieRegWrite(0x3D,(address&0xFF));
		WriteOneByte(ts, (unsigned char)0x3D, (paddr&0xFF));
		/* Data write 1page = 256byte */
		for(idx=0;idx<256;idx++){
			//			SPI_ArrieRegWrite(0x3D,*pData++);
			// addr=in-page(virtual, in 4k block) 256xN
			WriteOneByte(ts, (unsigned char)0x3D, data[(cnt*FLASH_PHYSICAL_PAGE_SIZE) +idx]);
		}
		/* FLC_CTL CS_HIGH,WP_DISABLE */
		//		SPI_ArrieRegWrite(0x3C,0x14);
		WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x14);


		/* wait until 'BUSY = LOW' */
		do {
			retry++;
			////		  msleep(10);
			if (retry > RETRY_COUNT)
				goto RETRY_ERROR;

			/* FLC_CTL CS_LOW,WP_DISABLE */
			//		SPI_ArrieRegWrite(0x3C,0x16);
			WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x16);
			/* FLC_TxDATA READ_STATUS_COMMAND*/
			//		SPI_ArrieRegWrite(0x3D,SPI_CMD_READ_ST);
			WriteOneByte(ts, (unsigned char)0x3D, FLASH_CMD_READ_ST);
			/* Dummy data */
			//		SPI_ArrieRegWrite(0x3D,0);
			WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)0x00);
			/* FLC_RxDATA */
			//		readData = SPI_ArrieRegRead(0x3F);
			readData = ReadOneByte(ts, 0x3F);
			/* FLC_CTL CS_HIGH,WP_DISABLE */
			//		SPI_ArrieRegWrite(0x3C,0x14);
			WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x14);
		} while (readData & FLASH_ST_BUSY); 		/* check busy bit */
	}

	return 0;

RETRY_ERROR:
	printk(KERN_INFO "FATAL: flash_write_page_shtsc retry %d times for page %d, addr %04X - FAILED!\n", retry, page, paddr);
	return 1;
}

#define FLASH_VERIFY_SIZE 512
unsigned char readBuf[FLASH_VERIFY_SIZE];

int flash_verify_page_shtsc(void *_ts, int page, unsigned char *data)
{
	struct shtsc_i2c *ts = _ts;
	unsigned addr; // in-page address (from 0 to FLASH_PAGE_SIZE-1)
	int cnt;

	/* BankChange */
	//	SPI_ArrieRegWrite(0x02,0x1C);
	WriteOneByte(ts, (unsigned char)0x02, (unsigned char)0x1C); // bank change reg

	/* CLKON_CTL0 */
	//	SPI_ArrieRegWrite(0x14,0x22);	/* CLKON_FLC,CLKON_REGBUS */
	WriteOneByte(ts, (unsigned char)0x14, (unsigned char)0x22);

	/* FLC_CTL CS_HIGH,WP_ENABLE */
	//	SPI_ArrieRegWrite(0x3C,0x10);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x10);

	/* FLC_CTL CS_LOW,WP_ENABLE */
	//	SPI_ArrieRegWrite(0x3C,0x12);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x12);

	/* FLC_TxDATA READ_COMMAND*/
	//	SPI_ArrieRegWrite(0x3D,SPI_CMD_READ);
	WriteOneByte(ts, (unsigned char)0x3D, FLASH_CMD_READ);
	/* FLC_TxDATA Address(bit16~23) */
	//	SPI_ArrieRegWrite(0x3D,((address>>16)&0xFF));
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)0x00);
	/* FLC_TxDATA Address(bit8~15) */
	//	SPI_ArrieRegWrite(0x3D,((address>>8)&0xFF));
	WriteOneByte(ts, (unsigned char)0x3D, ((page<<4)&0xFF));
	/* FLC_TxDATA Address(bit0~7) */
	//	SPI_ArrieRegWrite(0x3D,(address&0xFF));
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)0x00);
	/* FLC_TxDATA Dummy data */
	//	SPI_ArrieRegWrite(0x3D,0);
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)0x00);

	for (addr = 0; addr < FLASH_PAGE_SIZE; addr += FLASH_VERIFY_SIZE) {
		for(cnt=0; cnt<FLASH_VERIFY_SIZE; cnt++){
			/* FLC_RxDATA */
			//		*pData++ = SPI_ArrieRegRead(0x3F);
			readBuf[cnt] = ReadOneByte(ts, 0x3F);
		}
		if (memcmp((unsigned char *)&(data[addr]), (unsigned char *)readBuf, FLASH_VERIFY_SIZE)) {
			goto VERIFY_ERROR;
		}
	}

	/* FLC_CTL CS_LOW,WP_ENABLE */
	//	SPI_ArrieRegWrite(0x3C,0x10);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x10);

	return 0;

VERIFY_ERROR:
	/* FLC_CTL CS_LOW,WP_ENABLE */
	//	SPI_ArrieRegWrite(0x3C,0x10);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x10);
	// verify error
	printk(KERN_INFO "FATAL: flash_verify_page_shtsc for page %d - FAILED!\n", page);

	return 1;
}

int flash_read(void *_ts, unsigned address, unsigned length, unsigned char *data)
{
	struct shtsc_i2c *ts = _ts;
	int cnt;

	/* BankChange */
	//	SPI_ArrieRegWrite(0x02,0x1C);
	WriteOneByte(ts, (unsigned char)0x02, (unsigned char)0x1C); // bank change reg

	/* CLKON_CTL0 */
	//	SPI_ArrieRegWrite(0x14,0x22);	/* CLKON_FLC,CLKON_REGBUS */
	WriteOneByte(ts, (unsigned char)0x14, (unsigned char)0x22);

	/* FLC_CTL CS_HIGH,WP_ENABLE */
	//	SPI_ArrieRegWrite(0x3C,0x10);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x10);

	/* FLC_CTL CS_LOW,WP_ENABLE */
	//	SPI_ArrieRegWrite(0x3C,0x12);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x12);

	/* FLC_TxDATA READ_COMMAND*/
	//	SPI_ArrieRegWrite(0x3D,SPI_CMD_READ);
	WriteOneByte(ts, (unsigned char)0x3D, FLASH_CMD_READ);
	/* FLC_TxDATA Address(bit16~23) */
	//	SPI_ArrieRegWrite(0x3D,((address>>16)&0xFF));
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)((address>>16)&0xFF));
	/* FLC_TxDATA Address(bit8~15) */
	//	SPI_ArrieRegWrite(0x3D,((address>>8)&0xFF));
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)((address>>8)&0xFF));
	/* FLC_TxDATA Address(bit0~7) */
	//	SPI_ArrieRegWrite(0x3D,(address&0xFF));
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)(address&0xFF));
	/* FLC_TxDATA Dummy data */
	//	SPI_ArrieRegWrite(0x3D,0);
	WriteOneByte(ts, (unsigned char)0x3D, (unsigned char)0x00);

	for(cnt=0; cnt<length; cnt++){
		/* FLC_RxDATA */
		//		*pData++ = SPI_ArrieRegRead(0x3F);
		data[cnt] = ReadOneByte(ts, 0x3F);
	}

	/* FLC_CTL CS_LOW,WP_ENABLE */
	//	SPI_ArrieRegWrite(0x3C,0x10);
	WriteOneByte(ts, (unsigned char)0x3C, (unsigned char)0x10);

	return 0;
}

#ifdef FORCE_FIRM_UPDATE
#define FLASH_WAIT 100
/*
 * force re-programming the firm image held in the driver
 */
int update_flash(void *_ts, unsigned char *data, unsigned int len)
{
	int page;

	printk(KERN_INFO "shtsc: force updating K5 firmware....\n");
	flash_access_start_shtsc(_ts);

	for (page = 0; page < (len/FLASH_PAGE_SIZE); page++) {
		msleep(FLASH_WAIT);
		flash_erase_page_shtsc(_ts, page);
		printk(KERN_INFO "shtsc: flash_erase_page_shtsc done: page %d\n",  page);
		msleep(FLASH_WAIT);
		flash_write_page_shtsc(_ts, page, &(data[page*FLASH_PAGE_SIZE]));
		printk(KERN_INFO "shtsc: flash_write_page_shtsc done: page %d\n",  page);
		msleep(FLASH_WAIT);
		flash_verify_page_shtsc(_ts, page, &(data[page*FLASH_PAGE_SIZE]));
		printk(KERN_INFO "shtsc: flash_verify_page_shtsc done: page %d\n",  page);
	}

	flash_access_end_shtsc(_ts);

	printk(KERN_INFO "shtsc: force updating K5 firmware....done\n");

	return 0;
}
#endif /* FORCE_FIRM_UPDATE */

int issue_command(void *ts, unsigned char *cmd, unsigned int len)
{
	int err = 0;
	struct shtsc_i2c * touchscreen = (struct shtsc_i2c *)ts;

	touch_disable(touchscreen);
	SetBankAddr(touchscreen, SHTSC_BANK_COMMAND);
	// set command
	WriteMultiBytes(touchscreen, SHTSC_ADDR_COMMAND, cmd, len);
	SHTSC_DEBUG("set command (%x)\n",cmd[0]);

	// prepare waiting
	touchscreen->cmd = cmd[0];
	touchscreen->wait_state = WAIT_CMD;
	touchscreen->wait_result = false;

	INIT_COMPLETION(touchscreen->sync_completion); //2015.12.21
	// do it
	SetIndicator(touchscreen, SHTSC_IND_CMD);
	touch_enable(touchscreen);
	// wait
	err = WaitAsync(touchscreen);

	return err;
}

#define USE_SETSYSTEMSTATE_CMD

#ifdef USE_SETSYSTEMSTATE_CMD
#define CMD_SETSYSTEMSTATE_LEN 5
#define CMD_SETSYSTEMSTATE "\x02\x00\x01\x00\xff"
int shtsc_CMD_SetSystemState(void *ts, dCmdState_e eState)
{
	char cmdArray[CMD_SETSYSTEMSTATE_LEN];
	int ret;

	memcpy(cmdArray, CMD_SETSYSTEMSTATE, CMD_SETSYSTEMSTATE_LEN);
	cmdArray[4] = eState;
	SHTSC_DEBUG("YLLOG:%s lr388k5 state:%x.", __func__, eState);
	ret =  issue_command(ts, cmdArray, CMD_SETSYSTEMSTATE_LEN);
	return ret;
}
#endif /* USE_SETSYSTEMSTATE_CMD */



#define USE_SETPARAM_CMD
#ifdef USE_SETPARAM_CMD
#define CMD_SETPARAM_LEN 9
#define CMD_SETPARAM "\x10\x00\x05\x00\xff\xff\xff\xff\xff"

typedef enum _cmd_param_e {
	CMD_PARAM_NOTUSED       = 0x00,
	CMD_PARAM_LEFT_COVER    = 0x01,
	CMD_PARAM_TOP_COVER     = 0x02,
	CMD_PARAM_RIGHT_COVER   = 0x03,
	CMD_PARAM_BOTTOM_COVER  = 0x04,
	CMD_PARAM_MAX
} dCmdParam_e ;

int shtsc_CMD_SetParam(void *ts, dCmdParam_e eParam, unsigned paramVal)
{
	char cmdArray[CMD_SETPARAM_LEN];

	memcpy(cmdArray, CMD_SETPARAM, CMD_SETPARAM_LEN);
	cmdArray[4] = eParam;
	cmdArray[5] = (paramVal & 0x000000ff) >> 0; // little endian 4 byte // LSB
	cmdArray[6] = (paramVal & 0x0000ff00) >> 8;
	cmdArray[7] = (paramVal & 0x00ff0000) >> 16;
	cmdArray[8] = (paramVal & 0xff000000) >> 24; // MSB
	return issue_command(ts, cmdArray, CMD_SETPARAM_LEN);
}
#endif /* USE_SETPARAM_CMD */

int detectDevice(void)
{
	return DEVICE_CODE_LR388K5;
}

static void shtsc_reset_delay(void)
{
	msleep(SHTSC_RESET_TIME);
}

static void touch_enable (struct shtsc_i2c *ts)
{
	SHTSC_DEBUG("%s: ts->disabled = %d \n", __func__, ts->disabled);
	if(ts->disabled)
	{
		if(ts->client->irq) enable_irq(ts->client->irq);
		ts->disabled = false;
	}
}

static	void touch_disable(struct shtsc_i2c *ts)
{
	SHTSC_DEBUG("%s: ts->disabled = %d \n", __func__, ts->disabled);
	if(!ts->disabled)
	{
		if(ts->client->irq)	disable_irq(ts->client->irq);
		ts->disabled = true;
	}
}

static int shtsc_input_open(struct input_dev *dev)
{
	struct shtsc_i2c *ts = input_get_drvdata(dev);

	touch_enable(ts);

	printk("%s\n", __func__);

	return 0;
}

static void shtsc_input_close(struct input_dev *dev)
{
	struct shtsc_i2c *ts = input_get_drvdata(dev);

	touch_disable(ts);

	printk("%s\n", __func__);
}

#ifdef CONFIG_OF
static int shtsc_get_dt_coords(struct device *dev, char *name,
		struct shtsc_i2c_pdata *pdata)
{
	u32 coords[SHTSC_COORDS_ARR_SIZE];
	struct property *prop;
	struct device_node *np = dev->of_node;
	int coords_size, rc;

	prop = of_find_property(np, name, NULL);
	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;

	coords_size = prop->length / sizeof(u32);
	if (coords_size != SHTSC_COORDS_ARR_SIZE) {
		dev_err(dev, "invalid %s\n", name);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(np, name, coords, coords_size);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read %s\n", name);
		return rc;
	}

	if (strncmp(name, "sharp,panel-coords",
				sizeof("sharp,panel-coords")) == 0) {
		pdata->panel_minx = coords[0];
		pdata->panel_miny = coords[1];
		pdata->panel_maxx = coords[2];
		pdata->panel_maxy = coords[3];
	} else if (strncmp(name, "sharp,display-coords",
				sizeof("sharp,display-coords")) == 0) {
		pdata->disp_minx = coords[0];
		pdata->disp_miny = coords[1];
		pdata->disp_maxx = coords[2];
		pdata->disp_maxy = coords[3];
	} else {
		dev_err(dev, "unsupported property %s\n", name);
		return -EINVAL;
	}

	return 0;
}

#ifdef DP_8074_DEMO
#elif defined DP_8084_EVA
//   2015.04.09 added by Y.Nakamura
static int reg_set_optimum_mode_check(struct regulator *reg, int load_uA)
{
	return (regulator_count_voltages(reg) > 0) ?
		regulator_set_optimum_mode(reg, load_uA) : 0;
}

//   2015.04.09 added by Y.Nakamura
static int shtsc_regulator_configure(struct shtsc_i2c *ts, bool on )
{
	int retval;

	if( on==false )
		goto hw_shutdown;

	
	   ts->vdd = regulator_get(&ts->client->dev, "vdd");
	   if( IS_ERR(ts->vdd) ){
	   dev_err(&ts->client->dev, "%s: failed to get vdd regulator\n", __func__);
	   return PTR_ERR(ts->vdd);
	   }
/*
	   if (regulator_count_voltages(ts->vdd) > 0) {
	   	retval = regulator_set_voltage(ts->vdd,
	   	SHTSC_VTG_MIN_UV, SHTSC_VTG_MAX_UV);//3300000, 3300000);
	   	if (retval) {
	   		dev_err(&ts->client->dev,"regulator set_vtg failed retval =%d\n",retval);
	   		goto err_set_vtg_vdd;
	   	}
	   }*/
	 

	if (ts->pdata->i2c_pull_up) {
		ts->vcc_i2c = regulator_get(&ts->client->dev, "vcc_i2c");
		if (IS_ERR(ts->vcc_i2c)) {
			dev_err(&ts->client->dev, "%s: Failed to get i2c regulator\n",
					__func__);
			retval = PTR_ERR(ts->vcc_i2c);
			goto err_get_vtg_i2c;
		}

		if (regulator_count_voltages(ts->vcc_i2c) > 0) {
			retval = regulator_set_voltage(ts->vcc_i2c,
					SHTSC_I2C_VTG_MIN_UV, SHTSC_I2C_VTG_MAX_UV);//1800000, 1800000);
			if (retval) {
				dev_err(&ts->client->dev, "reg set i2c vtg failed retval =%d\n",
						retval);
				goto err_set_vtg_i2c;
			}
		}
	}
	return 0;

err_set_vtg_i2c:
	if (ts->pdata->i2c_pull_up)
		regulator_put(ts->vcc_i2c);
err_get_vtg_i2c:
	//	if (regulator_count_voltages(ts->vdd) > 0)
	//		regulator_set_voltage(ts->vdd, 0, SHTSC_VTG_MAX_UV);
//err_set_vtg_vdd:
	//	regulator_put(ts->vdd);
	return retval;

hw_shutdown:
	//	if (regulator_count_voltages(ts->vdd) > 0)
	//		regulator_set_voltage(ts->vdd, 0, SHTSC_VTG_MAX_UV);
	//	regulator_put(ts->vdd);
	if (ts->pdata->i2c_pull_up) {
		if (regulator_count_voltages(ts->vcc_i2c) > 0)
			regulator_set_voltage(ts->vcc_i2c, 0, SHTSC_I2C_VTG_MAX_UV);
		regulator_put(ts->vcc_i2c);
	}
	pr_info("%s: shutdown \n", __func__);

	return 0;
}

//   2015.04.09 added by Y.Nakamura
static int shtsc_power_on(struct shtsc_i2c *ts, bool on)
{
	int retval;

	if (on == false)
		goto power_off;

        if (ts->vcc_i2c_supply_en > 0) {
		if (on) {
                	printk(KERN_INFO "shtsc: vcc_i2c_supply_en: set 1\n");
                	gpio_direction_output(ts->vcc_i2c_supply_en, 1);
                	//G_reset_done = reset;
		}else{	
			gpio_direction_output(ts->vcc_i2c_supply_en, 0);
                	printk(KERN_INFO "shtsc: vcc_i2c_supply_en: set 0\n");
		}
		
        }


	/*	
	   retval = reg_set_optimum_mode_check(ts->vdd,
	   SHTSC_ACTIVE_LOAD_UA);
	   if (retval < 0) {
	   dev_err(&ts->client->dev, "Regulator vdd set_opt failed rc=%d\n", retval);
	   return retval;
	   }*/

	   retval = regulator_enable(ts->vdd);
	   if (retval) {
	   dev_err(&ts->client->dev, "Regulator vdd enable failed rc=%d\n",retval);
	   goto error_reg_en_vdd;
	   }
	 

	if (ts->pdata->i2c_pull_up) {
		retval = reg_set_optimum_mode_check(ts->vcc_i2c,
				SHTSC_I2C_LOAD_UA);
		if (retval < 0) {
			dev_err(&ts->client->dev, "Regulator vcc_i2c set_opt failed rc=%d\n", retval);
			goto error_reg_opt_i2c;
		}

		retval = regulator_enable(ts->vcc_i2c);
		if (retval) {
			dev_err(&ts->client->dev, "Regulator vcc_i2c enable failed rc=%d\n", retval);
			goto error_reg_en_vcc_i2c;
		}
	}
	return 0;

error_reg_en_vcc_i2c:
	if (ts->pdata->i2c_pull_up)
		reg_set_optimum_mode_check(ts->vcc_i2c, 0);
error_reg_opt_i2c:
	//	regulator_disable(ts->vdd);
error_reg_en_vdd:
	//	reg_set_optimum_mode_check(ts->vdd, 0);
	return retval;

power_off:
	//	reg_set_optimum_mode_check(ts->vdd, 0);
	//	regulator_disable(ts->vdd);
	if (ts->pdata->i2c_pull_up) {
		reg_set_optimum_mode_check(ts->vcc_i2c, 0);
		regulator_disable(ts->vcc_i2c);
	}
	pr_info("%s: power off \n", __func__);
	return 0;

}
// ---
#endif /* DP_8074_DEMO */

static int shtsc_parse_dt(struct device *dev, struct shtsc_i2c_pdata *pdata)
{
	struct device_node *np = dev->of_node;
	int rc;
	u32 temp_val;

	rc = shtsc_get_dt_coords(dev, "sharp,panel-coords", pdata);
	if (rc)
		return rc;

	rc = shtsc_get_dt_coords(dev, "sharp,display-coords", pdata);
	if (rc)
		return rc;

	/* regulator info */
	pdata->i2c_pull_up = of_property_read_bool(np, "sharp,i2c-pull-up");

	/* reset, irq gpio info */
	pdata->reset_gpio = of_get_named_gpio_flags(np, "sharp,reset-gpio",0, &pdata->reset_gpio_flags);
	pdata->irq_gpio = of_get_named_gpio_flags(np, "sharp,irq-gpio",0, &pdata->irq_gpio_flags);
	pdata->vcc_i2c_supply_en = of_get_named_gpio_flags(np, "sharp,vcc-i2c-supply-en",0, &pdata->vcc_i2c_supply_en_flags);

	rc = of_property_read_u32(np, "ts_touch_num_max", &temp_val);
	if( !rc ) pdata->ts_touch_num_max = temp_val;
	rc = of_property_read_u32(np, "ts_pressure_max", &temp_val);
	if( !rc ) pdata->ts_pressure_max = temp_val;
	rc = of_property_read_u32(np, "ts_flip_x", &temp_val);
	if( !rc ) pdata->ts_flip_x = temp_val;
	rc = of_property_read_u32(np, "ts_flip_y", &temp_val);
	if( !rc ) pdata->ts_flip_y = temp_val;
	rc = of_property_read_u32(np, "ts_swap_xy", &temp_val);
	if( !rc ) pdata->ts_swap_xy = temp_val;
#if 1
	printk(KERN_INFO "[SHTP PARSE DT] reset gpio = %d\n",pdata->reset_gpio);
	printk(KERN_INFO "[SHTP PARSE DT] irq gpio = %d\n",pdata->irq_gpio);
#endif
	return 0;
}
#else
static int shtsc_parse_dt(struct device *dev, struct shtsc_i2c_pdata *pdata)
{
	return -ENODEV;
}
#endif

#if defined(CONFIG_FB)
static int shtsc_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct shtsc_i2c *data = i2c_get_clientdata(client);

	SHTSC_DEBUG("%s:enter ...",__func__);	

	if (data->dev_sleep) {
		SHTSC_INFO("Device already in sleep .");
		SHTSC_DEBUG("%s:exit",__func__);
		return 0;
	}
	if (data->enter_update){
		SHTSC_INFO("TW update flash,no need to suspend");
		SHTSC_DEBUG("%s:exit",__func__);
		return 0;
	}

	mutex_lock(&shtsc_mutex);
#ifdef ENABLE_SUSPEND_GESTURE
	/*sprintf(wakeup_slide, "none");
	if (support_gesture & TW_SUPPORT_GESTURE_IN_ALL) {
		enable_gesture(true);
		SHTSC_INFO("%s: support_gesture = %d \n", __func__, support_gesture);
	}
	else if(DOZE_FREEZE == doze_status) {
		shtsc_CMD_SetSystemState( g_ts, CMD_STATE_SLEEP);
		enable_gesture(false);
	}
        else if((DOZE_DISABLED == doze_status) && g_ts->gesture_disable_by_ps){
                g_ts->gesture_disable_by_ps = false;
		shtsc_CMD_SetSystemState( g_ts, CMD_STATE_SLEEP);
		enable_gesture(false);
        } //yangmingjin add 2016.1.15
	else {
		//shtsc_CMD_SetSystemState( g_ts, CMD_STATE_SLEEP);
		//enable_gesture(false);
		doze_status = DOZE_DISABLED;
		touch_disable(g_ts);
		shtsc_reset(g_ts,false);
                g_ts->not_working = true; //yangmingjin add 2016.1.15
		SHTSC_INFO("%s: support_gesture is invalid \n", __func__);
	}
#else
	doze_status = DOZE_DISABLED; 
	touch_disable(g_ts);
	shtsc_reset(g_ts, false);
        g_ts->not_working = true; 
	DBGLOG("Device in sleep\n");*///yangmingjin delete 2016.1.16
	
    sprintf(wakeup_slide, "none");
	if (g_ts->setting_gesture) {
		enable_gesture(true);
		SHTSC_INFO("%s: support_gesture = %d \n", __func__, support_gesture);
	}else {
		touch_disable(g_ts);
		shtsc_reset(g_ts,false);
		SHTSC_INFO("%s: not support gesture\n", __func__);//yangmingjin add 2016.1.16
	}
#else
	touch_disable(g_ts);
	shtsc_reset(g_ts, false);
	DBGLOG("Device in sleep\n");
#endif

	data->dev_sleep = true;
	
	SHTSC_DEBUG("%s:exit",__func__);
	mutex_unlock(&shtsc_mutex);

	return 0;
}

static int shtsc_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct shtsc_i2c *data = i2c_get_clientdata(client);

	SHTSC_DEBUG("%s enter...",__func__);
	if(data->enter_update) {
		SHTSC_INFO("TW update flash ,no need to resume");
		SHTSC_DEBUG("%s exit",__func__);
		return 0;
	}
	if (!data->dev_sleep) {
		SHTSC_INFO( "Device already in resume.");
		SHTSC_DEBUG("%s exit",__func__);
		return 0;
	}
	mutex_lock(&shtsc_mutex);

#if defined(DEBUG_SHTSC)
	printk(KERN_INFO "[RESUME] shtsc resume from DeepIdle\n");
#endif

#ifdef ENABLE_SUSPEND_GESTURE
	/*if (doze_status == DOZE_WAKEUP || doze_status == DOZE_ENABLED){
		enable_gesture(false);
	}*///yangmingjin delete 2016.1.16
	
	if (g_ts->setting_gesture)//yangmingjin add 2016.1.16
	    enable_gesture(false);
#endif

	touch_enable(g_ts);
	shtsc_reset_L_H(g_ts);

	if (custom_setting_mode == MODE_GLOVE) {
                if( MODE_WINDOW != current_working_mode){
			shtsc_CMD_SetSystemState( g_ts, CMD_STATE_GLOVE);
			current_working_mode = MODE_GLOVE;
			SHTSC_INFO("%s: sent command to enter glove mode \n", __func__);
                }
	}

	data->dev_sleep = false;
    //g_ts->not_working = false; //yangmingjin add 2016.1.15
#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	yl_chg_status_changed();
#endif
	DBGLOG("Device in active\n");
	SHTSC_DEBUG("%s exit",__func__);
	mutex_unlock(&shtsc_mutex);

	return 0;
}

static void fb_notify_resume_work(struct work_struct *work)
{
	struct shtsc_i2c *shtsc_dev_data =
		container_of(work, struct shtsc_i2c, fb_notify_work);
	shtsc_resume(&shtsc_dev_data->client->dev);
}

static int fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

	struct shtsc_i2c *shtsc_dev_data =
		container_of(self, struct shtsc_i2c, fb_notif);

	if (evdata && evdata->data && shtsc_dev_data && shtsc_dev_data->client) {
		if (event == FB_EVENT_BLANK) {
			blank = evdata->data;
			if (*blank == FB_BLANK_UNBLANK)
				schedule_work(&shtsc_dev_data->fb_notify_work);
			else if (*blank == FB_BLANK_POWERDOWN){
				flush_work(&shtsc_dev_data->fb_notify_work);
				shtsc_suspend(&shtsc_dev_data->client->dev);
			}
		}
	}
	return 0;
}
#endif

static int shtsc_i2c_test(void *_ts, u8 *chip_id)
{
  struct shtsc_i2c *ts = _ts;
  u8 retry=0;
  int r;

  r = SetBankAddr(ts, 0x18);
  if (r < 0)
    return r;

  while(retry++ < 5)
    {
      r = shtsc_read_reg_1byte(ts, 0x0D, chip_id); //      *chip_id = ReadOneByte(ts, 0x0D);
      if (*chip_id == 0x1F) 
        {		
	  break;
        }
      printk("shtsc: i2c test failed time %d.",retry);
      msleep(10);
    }
  printk("shtsc: chip_id is %x\n",*chip_id);
  r = SetBankAddr(ts, SHTSC_BANK_TOUCH_REPORT);

  return r;
}


/*#############################################################################
###########################YULONG Interface####################################
#############################################################################*/
DEFINE_MUTEX(shtsc_glove_mutex);

bool hall_enable = false;
int shtsc_mode_normal(void)
{
	int err = -1;
	if (NULL == g_ts){
		printk("SHTSC:YLLOG:device not present.\n");
		return err;
	}
	err = shtsc_CMD_SetSystemState( g_ts, CMD_STATE_IDLE);
	if (err < 0) {
		printk("SHTSC:YLLOG:enter normal mode failed.\n");
		return err;
	} else {
		printk("SHTSC:YLLOG:success enter normal mode.\n");
		custom_setting_mode = MODE_NORMAL;
		current_working_mode = MODE_NORMAL;
	}
	return 0;
}
int shtsc_mode_glove(void)
{
	int err = -1;
	if (NULL == g_ts){
		printk("SHTSC:YLLOG:device not present.\n");
		return err;
	}
	if ( MODE_GLOVE != current_working_mode ) {
		err = shtsc_CMD_SetSystemState( g_ts, CMD_STATE_GLOVE);
		if (err < 0) {
			printk("SHTSC:YLLOG:enter glove mode failed.\n");
			return err;
		} else {
			printk("SHTSC:YLLOG:success enter glove mode.\n");
			custom_setting_mode = MODE_GLOVE;
			current_working_mode = MODE_GLOVE;
		}
	}

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	yl_chg_status_changed();
#endif
	return 0;
}

int glove_windows_switch(int in_hall)
{
	//in_hall == 0:leave; in_hall == 1:near
	int error = -1;
	if (NULL == g_ts){
		SHTSC_ERROR(":YLLOG:device not present.\n");
		return error;
	}

	mutex_lock(&shtsc_glove_mutex);
	hall_enable = false;
	if (in_hall) {
		if ( current_working_mode == MODE_GLOVE && !g_ts->dev_sleep ) {
			error = shtsc_CMD_SetSystemState( g_ts, CMD_STATE_IDLE);
			if (error < 0) {
				SHTSC_ERROR(":YLLOG:enter cover mode failed.\n");
				goto err;
			}
		}
		SHTSC_INFO(":YLLOG:success enter cover mode.\n");
		current_working_mode = MODE_WINDOW;
	} else {
		if( MODE_GLOVE == custom_setting_mode ) {
			if (!g_ts->dev_sleep) {
				error = shtsc_CMD_SetSystemState( g_ts, CMD_STATE_GLOVE);
				if (error < 0) {
					SHTSC_ERROR(":YLLOG:enter glove mode failed.\n");
					goto err;
				}
			}
			SHTSC_INFO(":YLLOG:success enter glove mode hall leave.\n");
			current_working_mode = MODE_GLOVE;
		} else {
			SHTSC_INFO(":YLLOG:already in idle mode hall leave.\n");
			current_working_mode = MODE_NORMAL;
		}
	}
err:
	mutex_unlock(&shtsc_glove_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(glove_windows_switch);

#ifdef ENABLE_SUSPEND_GESTURE
static void enable_gesture(bool enable)
{
	printk(KERN_ERR "SHTSC:YLLOG:%s gesture, last status:%s\n",
			enable ? "enable" : "disable",
			g_ts->irq_wake_enable_status ? "enable" : "disable");

	mutex_lock(&gesture_lock);
	if (enable) {
		shtsc_CMD_SetSystemState(g_ts, CMD_STATE_DEEP_IDLE);
		if (!g_ts->irq_wake_enable_status) {
			enable_irq_wake(g_ts->client->irq);
			g_ts->irq_wake_enable_status = true;
		}
		//doze_status = DOZE_ENABLED;//yangmingjin delete 2016.1.16
	} else {
		if (g_ts->irq_wake_enable_status) {
			disable_irq_wake(g_ts->client->irq);
			g_ts->irq_wake_enable_status = false;
		}
		//doze_status = DOZE_DISABLED;//yangmingjin delete 2016.1.16
	}
	mutex_unlock(&gesture_lock);
}
#endif


/*****************************************************************************************/
//yulong interface function start
/*****************************************************************************************/
int get_wakeup_gesture(char* gesture)
{
	int i;
	char wakeup_gesture[64]={0};

        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }

	if(0 == support_gesture)
		return sprintf(gesture, "%s", "none");

	for(i = 0; i < ARRAY_SIZE(tp_ges_wakeup); i++) {
		if(support_gesture & tp_ges_wakeup[i].mask) {
			if(wakeup_gesture[0] != 0)
				sprintf(wakeup_gesture + strlen(wakeup_gesture),
						"%s", ",");
			sprintf(wakeup_gesture + strlen(wakeup_gesture),
					"%s", tp_ges_wakeup[i].gesture);
		}
	}

	return sprintf(gesture, "%s", wakeup_gesture);
}

int gesture_ctrl(const char* gesture_buf)
{
	char *gesture;
	int buf_len;
	char *gesture_p;
	char *temp_p;
	char tmp_buf[32]={0};
	int i;
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }

	SHTSC_INFO("%s gesture_buf:%s.", __func__, gesture_buf);
	mutex_lock(&shtsc_mutex);

	//doze_status = DOZE_FREEZE;//yangmingjin delete 2016.1.16
	buf_len = strlen(gesture_buf);
	gesture_p = kzalloc(buf_len + 1, GFP_KERNEL);
	if(gesture_p == NULL) {
		SHTSC_ERROR("%s: alloc mem error.", __func__);
		mutex_unlock(&shtsc_mutex);
		return -1;
	}
	temp_p = gesture_p;
	strlcpy(gesture_p, gesture_buf, buf_len + 1);

	while(gesture_p) {
		gesture = strsep(&gesture_p, ",");
		SHTSC_DEBUG("%s gesture:%s.", __func__, gesture);

		for(i = 0; i < ARRAY_SIZE(tp_ges_wakeup); i++) {
			memset(tmp_buf, 0, 32);
			strcpy(tmp_buf, tp_ges_wakeup[i].gesture);
			if(!strncmp(gesture, tmp_buf, strlen(tmp_buf))){
				if(!strncmp(gesture+strlen(tmp_buf), "=true", 5)) {
					support_gesture |= tp_ges_wakeup[i].mask;
					break;
				} else if(!strncmp(gesture+strlen(tmp_buf), "=false", 6)) {
					support_gesture &= ~tp_ges_wakeup[i].mask;
					break;
				}
			}
		}
	}

	/*if (g_ts->dev_sleep && !g_ts->not_working) //yangmingjin delete 2016.1.16
	{
		if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL) {
			SHTSC_INFO("SHTSC:YLLOG:enable gesture");
			enable_gesture(true);
		} else {
			SHTSC_INFO("SHTSC:YLLOG:disable gesture");	
			shtsc_CMD_SetSystemState( g_ts, CMD_STATE_SLEEP);
			enable_gesture(false);
                        g_ts->gesture_disable_by_ps = true; //yangmingjin add 2016.1.15
		}
	}*/

    if (g_ts->dev_sleep) //yangmingjin modified 2016.1.16
	{
		if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL) {
			SHTSC_INFO("SHTSC:YLLOG:enable gesture");
			enable_gesture(true);
		} else {
			    SHTSC_INFO("SHTSC:YLLOG:disable gesture");		
				shtsc_CMD_SetSystemState( g_ts, CMD_STATE_SLEEP);		
			    enable_gesture(false);
		}
	}else{
		if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
    	    g_ts->setting_gesture = true;
   	    else
   		    g_ts->setting_gesture = false;
		}//yangmingjin add 2016.1.16
		
	kfree(temp_p);
	mutex_unlock(&shtsc_mutex);

	return 0;
}

int shtsc_tp_active(void)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }
	SHTSC_INFO("%s.\n", __FUNCTION__);
	return 1;
}

int shtsc_tp_need_update(void)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }
	SHTSC_INFO("%s.\n", __FUNCTION__);
	return 0;
}

int shtsc_tp_do_update(void)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }
	SHTSC_INFO("%s.\n", __FUNCTION__);
	return 0;
}

int shtsc_tp_need_calibrate(void)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }
	SHTSC_INFO("%s.\n", __FUNCTION__);
	return 0;
}

int shtsc_tp_calibrate(void)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }
	SHTSC_INFO("%s.\n", __FUNCTION__);
	return 0;
}

touch_mode_type shtsc_tp_get_mode(void)
{//TODO
	int err = -1;
	if (NULL == g_ts){
		SHTSC_ERROR("device not present.");
		return err;
	}
	printk(KERN_ERR "SHTSC:YLLOG:curren tw mode flag is :%d\n",
			current_working_mode);
	return current_working_mode;
	//return 0;
}

int shtsc_tp_set_mode(touch_mode_type work_mode)
{//TODO
	int err = -1;
	if (NULL == g_ts){
		SHTSC_ERROR("device not present.");
		return err;
	}

	/*if(current_working_mode == work_mode)
		return 1;*///yangmingjin delete 2016.1.29 : cannot set normal mode when charging and the setting mode is glove

	switch(work_mode){
		case MODE_INVALID:
			break;
		case MODE_NORMAL:
			shtsc_mode_normal();
			break;
		case MODE_HANDWRITE:
			break;
		case MODE_GLOVE:
			shtsc_mode_glove();
			break;
		case MODE_WINDOW:
			break;
		case MODE_MAX:
			break;
		default:
			break;
	}
	return 0;
}

int shtsc_tp_get_version(char* version)
{
	unsigned char * vendor = NULL;
	unsigned char * fver = NULL;
	unsigned char * pver = NULL;

        if (NULL == g_ts) {
        	return sprintf(version, "%s", "device not present.");     
        }

        if ( cgid == CG_VENDOR_OFILM ) {
                vendor = "Ofilm";
		fver = FIRM_VERSION;
		pver = PARAM_VERSION;
        }
        else if ( cgid == CG_VENDOR_BIEL ) {
                vendor = "Biel";
		fver = FIRM_VERSION2;
		pver = PARAM_VERSION2;
        }
        else {
                vendor = "N/A";
		fver = FIRM_VERSION;
		pver = PARAM_VERSION;
        }

	if (!g_ts->dev_sleep) {

		issue_command(g_ts, CMD_GETPROPERTY, CMD_GETPROPERTY_LEN);
	        return sprintf(version, "%s:%s:F%02x%02x:P%02x%02x", vendor,"LR388K5",
			CommandResultBuf[0x0B-0x08],
			CommandResultBuf[0x0A-0x08],
			CommandResultBuf[0x13-0x08],
			CommandResultBuf[0x12-0x08]);
	}
	else{
		return sprintf(version, "%s:%s:F%02x%02x:P%02x%02x", vendor,"LR388K5",
			fver[1],
			fver[0],
			pver[1],
			pver[0]);
	}
}

int shtsc_tp_debug(int mode)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }
	SHTSC_INFO("enter %s !",__FUNCTION__);
	if(mode)
		SHTSC_DEBUG_ON=1;
	else
		SHTSC_DEBUG_ON=0;
	return 1;
}

int shtsc_vendor(char* vendor)
{
        if (NULL == g_ts) {
		return sprintf(vendor, "%s", "device not present.");
        }
       //return sprintf(vendor, "%s", "shtsc");
        return sprintf(vendor, "%s","shtsc:LR388K5");
        /*yangmingjin add Chip_ID show*/
}

int shtsc_get_wakeup_gesture(char* gesture)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }
	return get_wakeup_gesture(gesture);
}

int shtsc_gesture_ctrl(const char* gesture_buf)
{
	if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
		return -1;
	}

	return gesture_ctrl(gesture_buf);
}

int shtsc_get_enable_goddot(void)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }

	return 0;
}

int shtsc_set_enable_goddot(int enable)
{
        if (NULL == g_ts) {
                SHTSC_ERROR("device not present.");
                return -1;
        }

	return 0;
}

touchscreen_ops_tpye shtsc_tp_ops = {
	.touch_id               = 0,
	.touch_type             = 1,
	.active                 = shtsc_tp_active,
	.firmware_need_update   = shtsc_tp_need_update,
	.firmware_do_update     = shtsc_tp_do_update,
	.need_calibrate         = shtsc_tp_need_calibrate,
	.calibrate              = shtsc_tp_calibrate,
	.get_mode               = shtsc_tp_get_mode,
	.set_mode               = shtsc_tp_set_mode,
	.get_firmware_version   = shtsc_tp_get_version,
	.debug                  = shtsc_tp_debug,
	.get_vendor          = shtsc_vendor,
	.get_wakeup_gesture     = shtsc_get_wakeup_gesture,
	.gesture_ctrl           = shtsc_gesture_ctrl,
};

int register_yl_interface(void)
{
	int ret = -1;

	ret = touchscreen_set_ops(&shtsc_tp_ops);
	if (ret) {
		printk(KERN_ERR "SYNA:YLLOG:Set yulong UI error!\n");
	}
	return ret;
}
/*****************************************************************************************/
//yulong interface function end
/*****************************************************************************************/

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
void yl_chg_status_changed(void)
{
	unsigned int chg_status;
	s32 err = -1;

	if(custom_setting_mode != MODE_GLOVE)
		return;

	if(g_ts->dev_sleep){
		return;
	}

	chg_status = touch_get_charger_status();
	SHTSC_INFO("TW get charger status: %d", chg_status);

	if (chg_status == 1){

		if( MODE_GLOVE == current_working_mode){
        	        err = shtsc_CMD_SetSystemState( g_ts, CMD_STATE_IDLE);
	                if (err < 0) {
                        	SHTSC_ERROR("enter normal mode failed.\n");
				return;
                	}
			current_working_mode = MODE_NORMAL;
			SHTSC_INFO("charger status: %d  set to normal mode", chg_status);
		}
	}else{
		if(( MODE_GLOVE != current_working_mode)&&( MODE_WINDOW != current_working_mode)){
			err = shtsc_CMD_SetSystemState( g_ts, CMD_STATE_GLOVE);
			if (err < 0) {
				SHTSC_ERROR("enter glove mode failed.\n");
				return;
                	}
			current_working_mode = MODE_GLOVE;
			SHTSC_INFO("charger status: %d  set to glove  mode", chg_status);
		}
	}

}
#endif

/*#############################################################################
###########################YULONG Interface####################################
#############################################################################*/

#if defined(CONFIG_TOUCHSCREEN_SHTSC_I2C)
static int __devinit shtsc_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	//  const struct shtsc_pdata *pdata = client->dev.platform_data;
	struct shtsc_i2c_pdata *pdata;
	struct shtsc_i2c *ts;
	struct input_dev *input_dev;
	int err;

	SHTSC_DEBUG("[ENTER] shtsc_probe....");

	//2014.10.16 added
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
				sizeof(struct shtsc_i2c_pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
		err = shtsc_parse_dt(&client->dev, pdata);
		if (err){
			kfree(pdata);
			return err;
		}
	} else{
		pdata = client->dev.platform_data;
	}

	/* No pdata no way forward */
	if (pdata == NULL) {
		dev_err(&client->dev, "no pdata\n");
#if defined(DEBUG_SHTSC)
		printk(KERN_INFO "%s(%d):\n", __FILE__, __LINE__);
#endif
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

	ts = kzalloc(sizeof(struct shtsc_i2c), GFP_KERNEL);
	g_ts = ts;

	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	ts->client = client;
	ts->input = input_dev;
	ts->pdata = pdata;//2014.10.16 added
	ts->disabled = true;
	ts->irq_wake_enable_status = false;
	mutex_init(&gesture_lock);
	mutex_init(&shtsc_mutex);

	ts->min_x = pdata->panel_minx;
	ts->max_x = pdata->panel_maxx;
	ts->min_y = pdata->panel_miny;
	ts->max_y = pdata->panel_maxy;
	ts->pressure_max = pdata->ts_pressure_max;
	ts->touch_num_max = pdata->ts_touch_num_max;//2014.10.16 added
	ts->flip_x = pdata->ts_flip_x;
	ts->flip_y = pdata->ts_flip_y;
	ts->swap_xy = pdata->ts_swap_xy;

	ts->reset_pin = pdata->reset_gpio;
	ts->irq_pin = pdata->irq_gpio;
	ts->vcc_i2c_supply_en = pdata->vcc_i2c_supply_en;


	mutex_init(&(ts->mutex));

	snprintf(ts->phys, sizeof(ts->phys),
			"%s/input0", dev_name(&client->dev));

	input_dev->name = SHTSC_DRIVER_NAME;
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->open = shtsc_input_open;//2014.10.16 added
	input_dev->close = shtsc_input_close;//2014.10.16 added

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);//2014.10.16 added
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);//2014.10.16 added

	/* For multi-touch */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	err = input_mt_init_slots(input_dev, SHTSC_MAX_FINGERS, INPUT_MT_DIRECT);
#else /*  KERNEL_3_10 */
	err = input_mt_init_slots(input_dev, SHTSC_MAX_FINGERS);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */

	if (err)
		goto err_free_mem;
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, SHTSC_I2C_SIZE_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			pdata->disp_minx, pdata->disp_maxx, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			pdata->disp_miny, pdata->disp_maxy, 0, 0);
	//input_set_abs_params(input_dev, ABS_MT_PRESSURE  , 0, SHTSC_I2C_P_MAX, 0, 0);
	//input_set_abs_params(input_dev, ABS_MT_TOOL_TYPE , 0, MT_TOOL_MAX, 0, 0);

#ifdef SHTSC_ICON_KEY_NUM
	{
		int cnt;
		for(cnt=0;cnt<SHTSC_ICON_KEY_NUM;cnt++){
			input_set_capability( input_dev , EV_KEY , shtsc_keyarray[cnt] );
		}
	}
#endif

#ifdef ENABLE_SUSPEND_GESTURE
#if 0
	/* you need to add the KEYCODEs for your environment! */
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_DOUBLE_TAP );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_SLIDE_UP );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_SLIDE_DOWN );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_SLIDE_RIGHT );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_SLIDE_LEFT );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_CHAR_C );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_CHAR_E );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_CHAR_M );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_CHAR_O );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_CHAR_V );
	input_set_capability( input_dev , EV_KEY , KEY_GESTURE_CHAR_W );
#endif /* 0 */
#ifdef FORCE_DISPLAY_ON
	input_set_capability( input_dev , EV_KEY , KEY_POWER );
#endif /* FORCE_DISPLAY_ON */
#endif /* ENABLE_SUSPEND_GESTURE */

#ifdef DP_8074_DEMO
#elif defined DP_8084_EVA
	// 2015.04.09 added by Y.Nakamura for pma8084

        if (ts->vcc_i2c_supply_en > 0) {
                err = gpio_request(ts->vcc_i2c_supply_en, NULL);
                if (err) {
                        dev_err(&client->dev,
                                        "Unable to request GPIO pin %d.\n",
                                        ts->vcc_i2c_supply_en);
                        goto err_free_mem;
                }
        }

	err = shtsc_regulator_configure(ts, true);
	if(err < 0) {
		dev_err(&client->dev, "failed to configure regulators\n");
		goto err_reg_configure;
	}
	err = shtsc_power_on(ts, true);
	if(err < 0) {
		dev_err(&client->dev, "failed to power on\n");
		goto err_power_device;
	}
	// ---
#endif /* DP_8074_DEMO */

	if (ts->reset_pin) {
		err = gpio_request(ts->reset_pin, NULL);
		if (err) {
			dev_err(&client->dev,
					"Unable to request GPIO pin %d.\n",
					ts->reset_pin);
			goto err_free_mem;
		}
	}

	shtsc_reset(ts, false);
	//  shtsc_reset_delay();//2014.11.12 added

	if (gpio_is_valid(ts->irq_pin)) {//2014.11.12 added
		/* configure touchscreen irq gpio */
		err = gpio_request(ts->irq_pin, "shtsc_irq_gpio");
		if (err) {
			dev_err(&client->dev, "unable to request gpio [%d]\n",
					ts->irq_pin);
			goto err_free_irq_gpio;
		}
	}
	err = gpio_direction_input(ts->irq_pin);
	if (err < 0) {
		dev_err(&client->dev,
				"Failed to configure input direction for GPIO %d, error %d\n",
				ts->irq_pin, err);
		goto err_free_irq_gpio;
	}

	client->irq = gpio_to_irq(ts->irq_pin);
	if (client->irq < 0) {
		err = client->irq;
		dev_err(&client->dev,
				"Unable to get irq number for GPIO %d, error %d\n",
				ts->irq_pin, err);
		goto err_free_irq_gpio;
	}
#ifdef FORCE_FIRM_UPDATE
	ts->workqueue = create_singlethread_workqueue("shtsc_work");
	INIT_WORK(&ts->workstr, update_flash_func);
#endif /* FORCE_FIRM_UPDATE */

	//debug workstr2
	INIT_WORK(&ts->workstr2, shtsc_gesture_report);

	input_set_drvdata(input_dev, ts);//2014.10.16 added
	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	touch_register_charger_notify(&yl_chg_status_changed);
#endif

	err = register_yl_interface();
	if(err)
		goto err_free_irq;

	i2c_set_clientdata(client, ts);
	device_init_wakeup(&client->dev, 1);

	ts->wait_state = WAIT_RESET;
	ts->wait_result = false;
	shtsc_reset(ts, true);
	init_completion(&ts->sync_completion); //2015.12.21

#if 1  //check I2c function and IRQ
	msleep(100);

	if( WriteOneByte(ts,SHTSC_ADDR_BANK,0x18) < 0 )
		goto err_free_irq;
	if( ReadOneByte(ts,0x0D) != 0x1F ) 
		goto err_free_irq;
#endif

#ifndef DEBUG_IRQ_DISABLE
	err = request_threaded_irq(client->irq, NULL, shtsc_irq_thread,
			(IRQF_TRIGGER_HIGH|IRQF_ONESHOT), client->dev.driver->name, ts);
	if (err < 0) {
		dev_err(&client->dev,
				"irq %d busy? error %d\n", client->irq, err);
		goto err_free_irq_gpio;
	}
#endif
	// wait
	err = WaitAsync(ts);
	if (err)
		goto err_free_irq;

#ifdef FORCE_FIRM_UPDATE
	queue_work(ts->workqueue, &ts->workstr);
	//      flush_workqueue(ts->workqueue);
#endif /* FORCE_FIRM_UPDATE */

	VersionModelCode = detectDevice();
	shtsc_system_init(ts);
	ts->setting_gesture = false;//yangmingjin add 2016.1.15

#if defined(CONFIG_FB)
	INIT_WORK(&ts->fb_notify_work, fb_notify_resume_work);
	ts->fb_notif.notifier_call = fb_notifier_callback;
	ts->dev_sleep = false;
	err = fb_register_client(&ts->fb_notif);

	if (err)
		dev_err(&ts->client->dev, "Unable to register fb_notifier: %d\n",
				err);
#endif

	SHTSC_INFO("[EXIT] shtsc_probe I2C driver!");

	return 0;

err_free_irq:
	free_irq(client->irq, ts);
err_free_irq_gpio:
	gpio_free(ts->irq_pin);
	shtsc_reset(ts, true);
	if (ts->reset_pin)
		gpio_free(ts->reset_pin);
	if (ts->vcc_i2c_supply_en > 0)
		gpio_free(ts->vcc_i2c_supply_en);
err_free_mem:
#ifdef DP_8074_DEMO
#elif defined DP_8084_EVA
	// 2015.04.09 added by Y.Nakamura
	shtsc_power_on(ts, false);
err_power_device:
	shtsc_regulator_configure(ts, false);
err_reg_configure:
	// ---
#endif /* DP_8074_DEMO */
	input_free_device(input_dev);
	kfree(ts);
	g_ts = NULL ;

	return err;
}

static int __devexit shtsc_i2c_remove(struct i2c_client *client)
{
	struct shtsc_i2c *ts = i2c_get_clientdata(client);

	shtsc_reset(ts, true);

	mutex_init(&(ts->mutex));

	//	sysfs_remove_group(&client->dev.kobj, &mxt_attr_group);
	free_irq(ts->client->irq, ts);
	input_unregister_device(ts->input);
#if defined(CONFIG_FB)
	if (fb_unregister_client(&ts->fb_notif))
		dev_err(&client->dev, "Error occurred while unregistering fb_notifier.\n");
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	//	unregister_early_suspend(&ts->early_suspend);
#endif
	if (gpio_is_valid(ts->pdata->reset_gpio))
		gpio_free(ts->pdata->reset_gpio);

	if (gpio_is_valid(ts->pdata->irq_gpio))
		gpio_free(ts->pdata->irq_gpio);

	if (client->dev.of_node) {
		kfree(ts->pdata);
	}

#ifdef DP_8074_DEMO
#elif defined DP_8084_EVA
	// 2015.04.09 added by Y.Nakamura for pma8084
	shtsc_power_on(ts, false);
	shtsc_regulator_configure(ts, false);
#endif /* DP_8074_DEMO */

	//  kfree(ts->object_table);
	kfree(ts);

	//	debugfs_remove_recursive(debug_base);

	return 0;
}
#endif // (CONFIG_TOUCHSCREEN_SHTSC_UNKNOWNIF??)

unsigned long m_test_mode(unsigned long debug_level)
{
	u8 shtsc_chip_id = 0, boot_ctl;
	int r;
	unsigned long ret = 0;

	touch_disable(g_ts);

	r = shtsc_i2c_test(g_ts, &shtsc_chip_id);
	if (r < 0) { 
		printk(KERN_ERR "shtsc: m_test_mode i2c_error(0x%x)\n", r);
		ret |= D_ERR_I2C_ERROR; //-1
		goto ErrTestExit;
	}
	if(shtsc_chip_id != 0x1F) {
		printk(KERN_ERR "shtsc: m_test_mode chip_id_error(0x%x)\n", shtsc_chip_id);
		ret |= D_ERR_CHIP_ID; //-2
		goto ErrTestExit;
	}
	ret = shtsc_chip_id & 0xFF;

	r = SetBankAddr(g_ts, 0x18);
	if (r < 0) {
		printk(KERN_ERR "shtsc: m_test_mode i2c_error(0x%x)\n", r);
		ret |= D_ERR_I2C_ERROR;
		goto ErrTestExit;
	}
  
	r = shtsc_read_reg_1byte(g_ts, 0x14, &boot_ctl);
	if (r < 0) {
		printk(KERN_ERR "shtsc: m_test_mode i2c_error(0x%x)\n", r);
		ret |= D_ERR_I2C_ERROR;
		goto ErrTestExit;
	}
	ret |= (boot_ctl & 0xFF) << 8;

	r = SetBankAddr(g_ts, 0x0);
	if (r < 0) {
		printk(KERN_ERR "shtsc: m_test_mode i2c_error(0x%x)\n", r);
		ret |= D_ERR_I2C_ERROR;
		goto ErrTestExit;
	}
	ret |= (s_tpc_state[D_STATE_RESET] & 0xFF) << 16;

	touch_enable(g_ts);

ErrTestExit:

	return ret;
}

int current_page = 0;

char iobuf[4*1024];
static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = true;

	//  unsigned int index;
	u8 addr, val;
	//  unsigned long r;
	int r;

#if defined(DEBUG_SHTSC)
	printk(KERN_INFO "[ENTER] shtsc_ioctl\n");    
#endif

#if defined(DEBUG_SHTSC)
	printk(KERN_INFO "shtsc dev_ioctl switch - cmd: %x, arg: %lx\n", cmd, arg);
#endif

	switch(cmd) {

		case SHTSC_IOCTL_SET_PAGE:
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "shtsc dev_ioctl case - SET_PAGE; cmd %x, arg %lx\n", cmd, arg);
#endif
			current_page = arg;
			break;

		case SHTSC_IOCTL_FLASH_ACCESS_START:
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "shtsc dev_ioctl case - FLASH_ACCESS_START; cmd %x\n", cmd);
#endif
			flash_access_start_shtsc(g_ts);
			break;

		case SHTSC_IOCTL_FLASH_ACCESS_END:
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "shtsc dev_ioctl case - FLASH_ACCESS_END; cmd %x\n", cmd);
#endif
			flash_access_end_shtsc(g_ts);
			break;

		case SHTSC_IOCTL_ERASE:
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "shtsc dev_ioctl case - ERASE; cmd %x, current_page %d\n", cmd, current_page);
#endif
#ifdef FLASH_CHECK
			if (flash_erase_page_shtsc(g_ts, current_page))
				return -1;
#else /* FLASH_CHECK */
			flash_erase_page_shtsc(g_ts, current_page);
#endif /* FLASH_CHECK */
			break;

		case SHTSC_IOCTL_WRITE:
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "shtsc dev_ioctl case - WRITE; cmd %x, current_page %d\n", cmd, current_page);
#endif
			if (copy_from_user(iobuf, (char *)arg, (4*1024))) {
				printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_from_user\n");
				return -1;
			}
#ifdef FLASH_CHECK
			if (flash_write_page_shtsc(g_ts, current_page, iobuf))
				return -1;
#else /* FLASH_CHECK */
			flash_write_page_shtsc(g_ts, current_page, iobuf);
#endif /* FLASH_CHECK */
			break;

		case SHTSC_IOCTL_VERIFY:
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "shtsc dev_ioctl case - VERIFY; cmd %x, current_page %d\n", cmd, current_page);
#endif
			if (copy_from_user(iobuf, (char *)arg, (4*1024))) {
				printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_from_user\n");
				return -1;
			}
#ifdef FLASH_CHECK
			if (flash_verify_page_shtsc(g_ts, current_page, iobuf))
				return -1;
#else /* FLASH_CHECK */
			flash_verify_page_shtsc(g_ts, current_page, iobuf);
#endif /* FLASH_CHECK */
			break;

		case SHTSC_IOCTL_ERASE_ALL:
			{
#define LAST_PAGE 16 
				int page;
#if defined(DEBUG_SHTSC)
				printk(KERN_INFO "%s(%d): flash_access start\n", __FILE__, __LINE__);
#endif
				flash_access_start_shtsc(g_ts);
				for (page = 0; page < LAST_PAGE; page++) {
					flash_erase_page_shtsc(g_ts, page);
#if defined(DEBUG_SHTSC)
					printk(KERN_INFO "flash_erase_page_shtsc done: page %d\n",  page);
#endif
				}
				flash_access_end_shtsc(g_ts);
#if defined(DEBUG_SHTSC)
				printk(KERN_INFO "%s(%d): flash erased.\n", __FILE__, __LINE__);
#endif
			}
			break;
		case SHTSC_IOCTL_REG_1WRITE:
			addr = (arg >> 8) & 0xFF;
			val = arg & 0xFF;
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "SHTSC_IOCTL_REG_1WRITE: addr: %02X (Hex), data: %02X (Hex)\n", addr, val);
#endif
			ret = WriteOneByte(g_ts, addr, val);
			break;

		case SHTSC_IOCTL_REG_1READ:
			//addr = 0xFF & arg;
			ret = shtsc_read_reg_1byte(g_ts, ((struct reg *)arg)->addr, &val); //2015.12.11 add by misaki
			((struct reg *)arg)->data = val;

#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "SHTSC_IOCTL_REG_1READ: cmd %x, arg %lx a:0x%x d:0x%x(%d)\n", cmd, arg, ((struct reg *)arg)->addr, val, val);
#endif
			break;

		case SHTSC_IOCTL_REG_N_RW_SET_ADDR:
			s_shtsc_addr = 0xFF & (arg >> 16);
			s_shtsc_len = 0xFFFF & arg;
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "SHTSC_IOCTL_REG_N_RW_SET_ADDR: cmd %x, arg %lx a:0x%x len:%d\n", cmd, arg, s_shtsc_addr, s_shtsc_len);
#endif
			break;

		case SHTSC_IOCTL_REG_N_WRITE_1ADDR_GO:
			/* s_shtsc_len is initialized __IF__ SHTSC_IOCTL_REG_N_RW_SET_ADDR is issued before */
			r = copy_from_user(s_shtsc_buf, (char *)arg, s_shtsc_len);
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "Driver Multibyte write. addr %02X, len: %x, data[0-3]: %02X %02X %02X %02X ....\n", 
					s_shtsc_addr, s_shtsc_len, s_shtsc_buf[0], s_shtsc_buf[1], s_shtsc_buf[2], s_shtsc_buf[3]);
#endif
			if (r != 0) {
				printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_from_user(%d)\n", r);
				return -1;
			}
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "SHTSC_IOCTL_REG_N_WRITE_1ADDR: cmd %x, arg %lx a:0x%x d:0x%x(%d)\n", cmd, arg, s_shtsc_addr, s_shtsc_len, s_shtsc_len);
#endif
			ret = WriteMultiBytes(g_ts, s_shtsc_addr, s_shtsc_buf, s_shtsc_len);

			break;

		case SHTSC_IOCTL_REG_N_READ_1ADDR_GO:
			/* s_shtsc_len is initialized __IF__ SHTSC_IOCTL_REG_N_RW_SET_ADDR is issued before */
			ReadMultiBytes(g_ts, s_shtsc_addr, s_shtsc_len, s_shtsc_buf);
			//msleep(10); // not checked yet
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "Driver Multibyte read done. addr %02X, len: %x, data[0-3]: %02X %02X %02X %02X ....\n", 
					s_shtsc_addr, s_shtsc_len, s_shtsc_buf[0], s_shtsc_buf[1], s_shtsc_buf[2], s_shtsc_buf[3]);
#endif
			r = copy_to_user((char *)arg, s_shtsc_buf, s_shtsc_len);
			if (r != 0) {
				printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_to_user(%d)\n", r);
				return -1;
			}

			break;

		case SHTSC_IOCTL_SETIRQMASK:
#if defined(CONFIG_TOUCHSCREEN_SHTSC_I2C)
			if (arg) {
				enable_irq(g_ts->client->irq);
			} else {
				disable_irq(g_ts->client->irq);
			}
#endif // (CONFIG_TOUCHSCREEN_SHTSC_UNKNOWNIF??)
#if defined(DEBUG_SHTSC)
				printk(KERN_INFO "shtsc dev_ioctl case - SETIRQMASK; cmd %x, arg %lx\n", cmd, arg);
#endif
			break;

		case SHTSC_IOCTL_RESET:
		#ifdef D_DBG_LEVEL
			if ((s_debug_level & D_DBG_FUNC) == D_DBG_FUNC)
				printk(KERN_INFO "shtsc: ioctl: shtsc_reset: %ld\n", arg);
		#endif
			if (arg) {
				g_ts->wait_state = WAIT_RESET;
				g_ts->wait_result = false;
				INIT_COMPLETION(g_ts->sync_completion); //yangmingjin add 2016.1.27
				shtsc_reset(g_ts, true);				
				WaitAsync(g_ts);	//yangmingjin add 2016.1.27
				shtsc_system_init(g_ts);
			} else {
				shtsc_reset(g_ts, false);
			}
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "shtsc dev_ioctl case - RESET; cmd %x, arg %lx\n", cmd, arg);
#endif
			break;

		case SHTSC_IOCTL_DEBUG:
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "shtsc dev_ioctl case - DEBUG; cmd %x, arg %lx\n", cmd, arg);
#endif
			break;

		case SHTSC_IOCTL_CMD_ISSUE_RESULT:
			{
				unsigned int magicnumber;	/* 'int' width is 32bit for LP64 and LLP64 mode */
				if ( copy_from_user(&magicnumber, (char *) arg, 4) ){
					magicnumber = 0;
				}
				if( magicnumber != 0xA5A5FF00 ){
					msleep(100);
				}
				r = copy_to_user((char *) arg, CommandResultBuf,MAX_COMMAND_RESULT_LEN);
				if( magicnumber == 0xA5A5FF00 ){
					CommandResultBuf[0] = 0;
				}
			}
			if (r != 0) {
				printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_to_user(%d)\n", r);
				return -1;
			}
			break;

		case SHTSC_IOCTL_TPC_STATE: //2015.12.21
		{
			unsigned long magicnumber = 0;
			int r;
			r = copy_from_user(&magicnumber, (char *) arg, 4);
			if (r != 0) {
				printk(KERN_INFO "shtsc:[ERROR]:dev_ioctl SHTSC_IOCTL_TPC_STAT copy_from_user(%d)\n", r);
				return -1;
			}

			if (magicnumber == 0xA5FFA500) { //reset val
				s_tpc_state[D_STATE_RESET] = 0; //2015.12.23
			} else {
				s_tpc_state[D_STATE_WAIT] = g_ts->wait_state & 0xFF; //2015.12.23
				s_tpc_state[D_STATE_RESULT] = g_ts->wait_result & 0xFF; //2015.12.23
				s_tpc_state[D_STATE_UPFIRM] = FlashUpdateByNoFirm & 0xFF; //2015.12.23
				s_tpc_state[D_STATE_SUSPEND] = g_ts->dev_sleep && 0xFF; //2015.12.23;
				printk(KERN_INFO "shtsc:tpc_state:reset:%d, wait:%d, result %d, upfirm:%d, suspend:%d\n", 
				s_tpc_state[D_STATE_RESET], s_tpc_state[D_STATE_WAIT], s_tpc_state[D_STATE_RESULT],
				s_tpc_state[D_STATE_UPFIRM], s_tpc_state[D_STATE_SUSPEND]);
				r = copy_to_user((char *) arg, s_tpc_state, D_TPC_STATE_LEN);
				if (r != 0) {
				  printk(KERN_INFO "shtsc:[ERROR]:dev_ioctl SHTSC_IOCTL_TPC_STAT by copy_to_user(%d)\n", r);
				  return -1;
				}
     			}
		}
		break;

		case SHTSC_IOCTL_DRIVER_VERSION_READ:
			{
				char versionBuf[16];
				versionBuf[0] = VersionYear;
				versionBuf[1] = VersionMonth;
				versionBuf[2] = VersionDay;
				versionBuf[3] = VersionSerialNumber;
				versionBuf[4] = VersionModelCode;

				r = copy_to_user((char *)arg, versionBuf, DRIVER_VERSION_LEN); 
				if (r != 0) {
					printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_to_user(%d)\n", r);
					return -1;
				}
			}
			break;

		case SHTSC_IOCTL_DCMAP:
			{
				int x = dcmap[0];
				int y = dcmap[1];
				int len = (x * y * 2) + 4;

#if defined(DEBUG_SHTSC)
				printk(KERN_INFO "shtsc dev_ioctl case - DCMAP; cmd %x, arg %lx, %d*%d+4=%d\n", cmd, arg, x, y, len);
#endif

				if (buf_pos) {
					/* DC map ready to send out */
					if ( copy_to_user( (char *)arg, dcmap, len ) ) {
						printk( KERN_INFO "shtsc : copy_to_user failed\n" );
						return -EFAULT;
					}
					buf_pos = 0;
				}
				break;
			}

#ifdef GET_REPORT
		case SHTSC_IOCTL_GET_REPORT:
			{
				volatile int count;
				int len;


				while (Mutex_GetReport)
					;
				Mutex_GetReport = true;

				count = reportBuf[0];
				len = 4+ count*REP_SIZE;

#if defined(DEBUG_SHTSC)
				printk(KERN_INFO "shtsc dev_ioctl case - GET_REPORT; cmd %x, arg %lx, count=%d, len=%d\n", cmd, arg, count, len);
#endif

				r = copy_to_user((char *)arg, reportBuf, len);
				if (r != 0) {
					printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_to_user(%d)\n", r);
					return -1;
				}

				reportBuf[0] = (unsigned char)0;

				Mutex_GetReport = false;
			}
			break;
#endif /* GET_REPORT */

		case SHTSC_IOCTL_FLASH_READ:
			{
				/* arg: pointer to the content buffer */
				/* arg[3:0]: address to read (little endian) */
				/* arg[5:4]: length to read (little endian) */

				unsigned address;
				unsigned length;

				if (copy_from_user(iobuf, (char *)arg, (4+2))) {
					printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_from_user\n");
					return -1;
				}
				address = (iobuf[3] << 24) | (iobuf[2] << 16) | (iobuf[1] << 8) | (iobuf[0] << 0);
				length = (iobuf[5] << 8) | (iobuf[4] << 0);

#if defined(DEBUG_SHTSC)
				printk(KERN_INFO "shtsc dev_ioctl case - FLASH_READ; addr %x, arg %x\n", address, length);
#endif

				flash_read(g_ts, address, length, iobuf);
				if ( copy_to_user( (char *)arg, iobuf, length ) ) {
					printk( KERN_INFO "shtsc : copy_to_user failed\n" );
					return -EFAULT;
				}
				break;
			}

		case SHTSC_IOCTL_NOTIFY_PID:
			pid = arg; // save pid for later kill();
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "SHTSC_IOCTL_NOTIFY_PID: pid: %d\n", pid);
#endif
			break;

		case SHTSC_IOCTL_GET_INTERRUPT_STATUS:
#if defined(DEBUG_SHTSC)
			printk(KERN_INFO "Received SHTSC_IOCTL_GET_INTERRUPT_STATUS, %d\n", resumeStatus);
#endif

			r = copy_to_user((char *)arg, &resumeStatus, 1); // copy one-byte status
			if (r != 0) {
				printk(KERN_INFO "shtsc dev_ioctl ERROR by copy_to_user(%d)\n", r);
				return -1;
			}
			break;

		case SHTSC_IOCTL_SET_D_LEVEL: //2015.12.22
			r = copy_from_user(&s_debug_level, (char *) arg, 4);
			if (r == 0) {
				//printk(KERN_INFO "shtsc:s_debug_level(%d):0x%lx\n", r, s_debug_level);
			} else {
				printk(KERN_INFO "shtsc:[ERROR]s_debug_level(%d):0x%lx\n", r, s_debug_level);
			}

			if ((s_debug_level & D_DBG_TOUCH_IRQ_DISABLE) == D_DBG_TOUCH_IRQ_DISABLE) {
				s_test_mode = D_DBG_TOUCH_IRQ_DISABLE;
			} else if ((s_debug_level & D_DBG_TOUCH_IRQ_CLEAR) == D_DBG_TOUCH_IRQ_CLEAR) {
				u8 intmask0;
				s_test_mode = D_DBG_TOUCH_IRQ_CLEAR;
				touch_disable(g_ts);
				ret = shtsc_read_reg_1byte(g_ts, SHTSC_ADDR_INTMASK0, &intmask0);
				if (ret < 0) {
					return ret;
				}
				ret = WriteOneByte(g_ts, (u8)SHTSC_ADDR_INTMASK0, (u8)(intmask0 & (~SHTSC_STATUS_TOUCH_READY)));
				if (ret < 0) {
					return ret;
				}
				touch_enable(g_ts);
			} else if ((s_debug_level & D_DBG_TOUCH_IRQ_ENABLE) == D_DBG_TOUCH_IRQ_ENABLE) {
				s_test_mode = 0;
			}
			if ((s_debug_level & D_DBG_TEST_MODE) == D_DBG_TEST_MODE) {
				unsigned long test_state;
				test_state = m_test_mode(s_debug_level);
				r = copy_to_user((char *)arg, &test_state, 4);
				if (r != 0) {
					printk(KERN_INFO "shtsc:[ERROR]: ioctl D_DBG_TEST_MODE(%d)\n", r);
					return -1;
				}
			}
			printk(KERN_INFO "shtsc:s_debug_level(0x%lx) test_mode(%lx)\n", s_debug_level, s_test_mode);
			break;

		default:
			ret = false;
			break;
	}

#if defined(DEBUG_SHTSC)
	printk(KERN_INFO "[EXIT] shtsc_ioctl\n");
#endif

	return ret;
}


#if defined(CONFIG_TOUCHSCREEN_SHTSC_I2C)
static int dev_open(struct inode *inode, struct file *filp)
{
#if defined(DEBUG_SHTSC)
	printk(KERN_INFO "shtsc dev_open\n");
#endif
	return 0;
}

static int dev_release(struct inode *inode, struct file *filp)
{
#if defined(DEBUG_SHTSC)
	printk(KERN_INFO "shtsc dev_release\n");
#endif
	return 0;
}
#endif // (CONFIG_TOUCHSCREEN_SHTSC_UNKNOWNIF??)

static const struct file_operations dev_fops = {
	.owner = THIS_MODULE,
	.open = dev_open,
	.release = dev_release,
	.read = dev_read,
	//	.write = dev_write,
#ifdef CONFIG_COMPAT  //yulong add to resolve 32bit ioctl and 64bit ioctl compat problem
	.compat_ioctl = dev_ioctl,
#endif
	.unlocked_ioctl = dev_ioctl,
};

static struct miscdevice shtsc_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = SHTSC_DRIVER_NAME, // should be "/dev/shtsc"
	.fops = &dev_fops,
};

#if defined(CONFIG_TOUCHSCREEN_SHTSC_I2C)
static struct i2c_device_id shtsc_i2c_idtable[] = {
	{ SHTSC_DRIVER_NAME, 0 },
	{ }
};
#ifdef CONFIG_OF
static struct of_device_id shtsc_match_table[] = {
	{ .compatible = "sharp,shtsc_i2c",},
	{ },
};
#else
#define shtsc_match_table NULL
#endif

MODULE_DEVICE_TABLE(i2c, shtsc_i2c_idtable);

static struct i2c_driver shtsc_i2c_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= SHTSC_DRIVER_NAME,
		.of_match_table = shtsc_match_table,//2014.10.16 added
	},
	.id_table	= shtsc_i2c_idtable,
	.probe	= shtsc_i2c_probe,
	.remove	= __devexit_p(shtsc_i2c_remove),
};

static int __init shtsc_init(void)
{
	int ret;
	ret = misc_register(&shtsc_miscdev);
	if (ret) {
		printk(KERN_INFO "%s(%d): misc_register returns %d. Failed.\n", __FILE__, __LINE__, ret);
	}
#if defined(DEBUG_SHTSC)
	printk(KERN_INFO "%s(%d): loaded successfully\n", __FILE__, __LINE__);
#endif

	return i2c_add_driver(&shtsc_i2c_driver);
}
static void __exit shtsc_exit(void)
{
	misc_deregister(&shtsc_miscdev);
	i2c_del_driver(&shtsc_i2c_driver);
}
#endif // (CONFIG_TOUCHSCREEN_SHTSC_UNKNOWNIF??)

module_init(shtsc_init);
module_exit(shtsc_exit);

MODULE_DESCRIPTION("shtsc SHARP Touchscreen controller Driver");
MODULE_LICENSE("GPL v2");
