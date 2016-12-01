#ifndef __LINUX_FT5X0X_TS_H__
#define __LINUX_FT5X0X_TS_H__

#include <linux/interrupt.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/i2c.h>
#include <linux/input.h>
//#include <linux/i2c/touchscreen_yl.h>
#include <linux/input/touchscreen_yl.h>


/* -- dirver configure -- */
#define CFG_SUPPORT_AUTO_UPG 0
#define CFG_SUPPORT_UPDATE_PROJECT_SETTING  0
#define CFG_SUPPORT_TOUCH_KEY  1    //touch key, HOME, SEARCH, RETURN etc


#define CFG_MAX_TOUCH_POINTS  5
#define CFG_NUMOFKEYS 3
#define CFG_FTS_CTP_DRIVER_VERSION "3.0"

#define PRESS_MAX       255

#define CFG_POINT_READ_BUF  (3 + 6 * (CFG_MAX_TOUCH_POINTS))

#define FT5X0X_NAME	"ft5x0x_ts"

#define KEY_PRESS       1
#define KEY_RELEASE     0

struct ts_event {
    u16 au16_x[CFG_MAX_TOUCH_POINTS];              //x coordinate
    u16 au16_y[CFG_MAX_TOUCH_POINTS];              //y coordinate
    u8  au8_touch_event[CFG_MAX_TOUCH_POINTS];     //touch event:  0 -- down; 1-- up; 2 -- contact
    u8  au8_finger_id[CFG_MAX_TOUCH_POINTS];       //touch ID
    u16 pressure[CFG_MAX_TOUCH_POINTS];            //pressure
    u16 touch_size[CFG_MAX_TOUCH_POINTS];         //touch_size
    u8  touch_point;
};

struct ft5x0x_ts_data {
	struct i2c_client *client;   //add by taokai
	struct input_dev	         *input_dev;
	struct ts_event		         event;
	struct work_struct 	         pen_event_work;
	struct work_struct		fb_notify_work;
	struct workqueue_struct          *ts_workqueue;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend	         early_suspend;
#elif defined(CONFIG_FB)            //add by taokai
       struct notifier_block fb_notif;  //add by taokai
#endif

  struct mutex device_mode_mutex;   /* Ensures that only one function can specify the Device Mode at a time. */
  struct tw_platform_data   *pdata;
  struct pinctrl *ts_pinctrl;
  struct pinctrl_state *gpio_state_active;
 /* yulong add */
 /* reason : key debounce */
 /* author : tanliyu */
 /* time : 2012-10-04 */
  struct timer_list timer;
  unsigned char tp_is_suspend;
 /* yulong end */
	unsigned int irq;
};

enum ft5x0x_ts_regs {
	FT5X0X_REG_THGROUP					= 0x80,     /* touch threshold, related to sensitivity */
	FT5X0X_REG_THPEAK						= 0x81,
	FT5X0X_REG_THCAL						= 0x82,
	FT5X0X_REG_THWATER					= 0x83,
	FT5X0X_REG_THTEMP					= 0x84,
	FT5X0X_REG_THDIFF						= 0x85,
	FT5X0X_REG_CTRL						= 0x86,
	FT5X0X_REG_TIMEENTERMONITOR			= 0x87,
	FT5X0X_REG_PERIODACTIVE				= 0x88,      /* report rate */
	FT5X0X_REG_PERIODMONITOR			= 0x89,
	FT5X0X_REG_HEIGHT_B					= 0x8a,
	FT5X0X_REG_MAX_FRAME					= 0x8b,
	FT5X0X_REG_DIST_MOVE					= 0x8c,
	FT5X0X_REG_DIST_POINT				= 0x8d,
	FT5X0X_REG_FEG_FRAME					= 0x8e,
	FT5X0X_REG_SINGLE_CLICK_OFFSET		= 0x8f,
	FT5X0X_REG_DOUBLE_CLICK_TIME_MIN	= 0x90,
	FT5X0X_REG_SINGLE_CLICK_TIME			= 0x91,
	FT5X0X_REG_LEFT_RIGHT_OFFSET		= 0x92,
	FT5X0X_REG_UP_DOWN_OFFSET			= 0x93,
	FT5X0X_REG_DISTANCE_LEFT_RIGHT		= 0x94,
	FT5X0X_REG_DISTANCE_UP_DOWN		= 0x95,
	FT5X0X_REG_ZOOM_DIS_SQR				= 0x96,
	FT5X0X_REG_RADIAN_VALUE				=0x97,
	FT5X0X_REG_MAX_X_HIGH                       	= 0x98,
	FT5X0X_REG_MAX_X_LOW             			= 0x99,
	FT5X0X_REG_MAX_Y_HIGH            			= 0x9a,
	FT5X0X_REG_MAX_Y_LOW             			= 0x9b,
	FT5X0X_REG_K_X_HIGH            			= 0x9c,
	FT5X0X_REG_K_X_LOW             			= 0x9d,
	FT5X0X_REG_K_Y_HIGH            			= 0x9e,
	FT5X0X_REG_K_Y_LOW             			= 0x9f,
	FT5X0X_REG_AUTO_CLB_MODE			= 0xa0,
	FT5X0X_REG_LIB_VERSION_H 				= 0xa1,
	FT5X0X_REG_LIB_VERSION_L 				= 0xa2,
	FT5X0X_REG_CIPHER						= 0xa3,
	FT5X0X_REG_MODE						= 0xa4,
	FT5X0X_REG_PMODE						= 0xa5,	  /*Power Consume Mode*/
	FT5X0X_REG_FIRMID						= 0xa6,   /* Firmware version */
	FT5X0X_REG_STATE						= 0xa7,
	FT5X0X_REG_FT5201ID					= 0xa8,
	FT5X0X_REG_ERR						= 0xa9,
	FT5X0X_REG_CLB						= 0xaa,
};

	//FT5X0X_REG_PMODE
	#define PMODE_ACTIVE        0x00
	#define PMODE_MONITOR       0x01
	#define PMODE_STANDBY       0x02
	#define PMODE_HIBERNATE     0x03


	#ifndef ABS_MT_TOUCH_MAJOR
	#define ABS_MT_TOUCH_MAJOR	0x30	/* touching ellipse */
	#define ABS_MT_TOUCH_MINOR	0x31	/* (omit if circular) */
	#define ABS_MT_WIDTH_MAJOR	0x32	/* approaching ellipse */
	#define ABS_MT_WIDTH_MINOR	0x33	/* (omit if circular) */
	#define ABS_MT_ORIENTATION	0x34	/* Ellipse orientation */
	#define ABS_MT_POSITION_X	0x35	/* Center X ellipse position */
	#define ABS_MT_POSITION_Y	0x36	/* Center Y ellipse position */
	#define ABS_MT_TOOL_TYPE	0x37	/* Type of touching device */
	#define ABS_MT_BLOB_ID		0x38	/* Group set of pkts as blob */
	#endif /* ABS_MT_TOUCH_MAJOR */

  #ifndef ABS_MT_TRACKING_ID
  #define ABS_MT_TRACKING_ID 0x39 /* Unique ID of initiated contact */
  #endif



/*FT5206, FT5306, FT5406*/
#ifdef IC_FT5X06
#define CTPM_ID2 0x03
#define DELAY_AA 50
#define DELAY_55 30
#define DELAY_ID 1
#define DELAY_EARSE 2000
#endif

/*FT5X16*/
#ifdef IC_FT5X16
#define CTPM_ID2 0x07
#define DELAY_AA 50
#define DELAY_55 30
#define DELAY_ID 1
#define DELAY_EARSE 1500
#endif

/*FT5506, FT5606*/
#ifdef IC_FT5606
#define CTPM_ID2 0x06
#define DELAY_AA 50
#define DELAY_55 10
#define DELAY_ID 100
#define DELAY_EARSE 2000
#endif

/*FT5X36*/
#ifdef IC_FT5X36
#define CTPM_ID2 0x11
#define DELAY_AA 30
#define DELAY_55 30
#define DELAY_ID 10
#define DELAY_EARSE 1500
#endif

/*FT5X46*/
#ifdef	IC_FT5X46
#define FT5X46_UPGRADE_AA_DELAY 		10
#define FT5X46_UPGRADE_55_DELAY 		10
#define FT5X46_UPGRADE_ID_1			    0x54
#define FT5X46_UPGRADE_ID_2			    0x2c
#define FT5X46_UPGRADE_READID_DELAY 	10
#endif

/*FT8606*/
#ifdef IC_FT8606
#define FT8606_UPGRADE_AA_DELAY 		2
#define FT8606_UPGRADE_55_DELAY 		2
#define FT8606_UPGRADE_ID_1			    0x86
#define FT8606_UPGRADE_ID_2			    0xA6
#define FT8606_UPGRADE_READID_DELAY 	20
#endif

#endif








