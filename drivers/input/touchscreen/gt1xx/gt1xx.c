/* drmvers/input/touchscreen/gt9xx.c
 *
 * 2010 - 2013 Goodix Technology.
 *
 * This program is free software; iyou can redistribute it and/or modify
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
 * Version: 2.2
 * Authors: andrew@goodix.com, meta@goodix.com
 * Release Date: 2014/01/14
 * Revision record:
 *      V1.0:
 *          first Release. By Andrew, 2012/08/31
 *      V1.2:
 *          modify gtp_reset_guitar,slot report,tracking_id & 0x0F. By Andrew, 2012/10/15
 *      V1.4:
 *          modify gt9xx_update.c. By Andrew, 2012/12/12
 *      V1.6:
 *          1. new heartbeat/esd_protect mechanism(add external watchdog)
 *          2. doze mode, sliding wakeup
 *          3. 3 more cfg_group(GT9 Sensor_ID: 0~5)
 *          3. config length verification
 *          4. names & comments
 *                  By Meta, 2013/03/11
 *      V1.8:
 *          1. pen/stylus identification
 *          2. read double check & fixed config support
 *          3. new esd & slide wakeup optimization
 *                  By Meta, 2013/06/08
 *      V2.0:
 *          1. compatible with GT9XXF
 *          2. send config after resume
 *                  By Meta, 2013/08/06
 *      V2.2:
 *          1. gt9xx_config for debug
 *          2. gesture wakeup
 *          3. pen separate input device, active-pen button support
 *          4. coordinates & keys optimization
 *                  By Meta, 2014/01/14
 */

#include <linux/irq.h>
#include <linux/input/touchscreen_yl.h>
#include "gt1xx.h"
#include <linux/pinctrl/consumer.h>

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#elif defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#if GTP_ICS_SLOT_REPORT
#include <linux/input/mt.h>
#endif

#ifdef CONFIG_OF
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

const char * TW_IC_PREFIX_NAME=NULL;
unsigned char GTP_DEBUG_ON=0;
//#define YL_TW_DEBUG
static unsigned int tp_index;
struct gt1x_version_info gt1x_fw_version;
u8 gtp_cfg_version = 0;
u8 tp_supported = 0;
#ifdef CONFIG_TOUCHSCREEN_COB
u8 cfg_version_bak = 0;
#endif
u8 gt1x_rawdiff_mode = 0;

#ifdef CONFIG_TP_I2C_TIMEOUT_WAIT
static int tw_resume = 1;
static DECLARE_WAIT_QUEUE_HEAD(wait_tw_resume);
#endif

/*********** dtsi config ********************/
struct goodix_config_info {
	unsigned char tp_id;
	const char *vendor_name;
#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	unsigned int charger_status_force_check;
#endif
	unsigned char *config;
	unsigned char *config_glove;
	unsigned char *config_window;
	unsigned int config_length;
	unsigned int config_glove_length;
	unsigned int config_window_length;
};
struct goodix_config_info *yl_cfg=NULL;
static unsigned int config_array_size = 0;
/*********** dtsi config ********************/

static const char *goodix_ts_name = "goodix";
static struct workqueue_struct *goodix_wq;
struct i2c_client * i2c_connect_client = NULL;
static u8 config[GTP_CONFIG_MAX_LENGTH] = {0};

#if GTP_HAVE_TOUCH_KEY
static const u16 touch_key_array[] = GTP_KEY_TAB;
#define GTP_MAX_KEY_NUM	 (sizeof(touch_key_array)/sizeof(touch_key_array[0]))
#endif

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
extern void touch_register_charger_notify(void (*fn)(void));
extern unsigned int touch_get_charger_status(void);
#endif

static s8 gtp_i2c_test(struct i2c_client *client);
void gtp_reset_guitar(struct i2c_client *client, s32 ms);
extern int gt1x_init_tool_node(void);

#if defined (CONFIG_HAS_EARLYSUSPEND)
static void goodix_ts_early_suspend(struct early_suspend *handler);
static void goodix_ts_late_resume(struct early_suspend *handler);
#elif defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data);
static void fb_notify_resume_work(struct work_struct *work);
#endif
static int goodix_ts_disable(struct input_dev *in_dev);
static int goodix_ts_enable(struct input_dev *in_dev);
#ifdef CONFIG_TP_I2C_TIMEOUT_WAIT
static int goodix_suspend(struct device *dev);
static int goodix_resume(struct device *dev);
#endif
static int goodix_ts_suspend(struct device *dev);
static int goodix_ts_resume(struct device *dev);
s32 gt1x_send_cmd(u8 cmd, u8 data);
s32 gt1x_send_cmd_esd(u8 cmd, u8 data);

#if GTP_ESD_PROTECT
static struct delayed_work gtp_esd_check_work;
static struct workqueue_struct * gtp_esd_check_workqueue = NULL;
static void gtp_esd_check_func(struct work_struct *);
static s32 gtp_init_ext_watchdog(struct i2c_client *client);
void gtp_esd_switch(struct i2c_client *, s32);
#endif

#ifdef YL_TW_DEBUG
char *key_name[3] ={
	"Menu",
	"Home",
	"Back",
};
#endif


//extern int get_device_info(char* buf); // yinchao add tp_info to factory mode @20140901
static int current_mode_flag  = MODE_NORMAL;

#ifdef CONFIG_TOUCHSCREEN_WINDOW
bool gtp_window_mode_flag = false;
struct cover_window_info {
	unsigned int win_x_min;
	unsigned int win_x_max;
	unsigned int win_y_min;
	unsigned int win_y_max;
};
struct cover_window_info gt1xx_cover_window;
#endif

#if GTP_SLIDE_WAKEUP
typedef enum
{
	DOZE_DISABLED = 0,
	DOZE_ENABLED = 1,
	DOZE_WAKEUP = 2,
}DOZE_T;
static DOZE_T doze_status = DOZE_DISABLED;
static s8 gtp_enter_doze(struct goodix_ts_data *ts);

enum support_gesture_e {
	TW_SUPPORT_NONE_SLIDE_WAKEUP   = 0x0,
	TW_SUPPORT_UP_SLIDE_WAKEUP     = 0x1,
	TW_SUPPORT_DOWN_SLIDE_WAKEUP   = 0x2,
	TW_SUPPORT_LEFT_SLIDE_WAKEUP   = 0x4,
	TW_SUPPORT_RIGHT_SLIDE_WAKEUP  = 0x8,
	TW_SUPPORT_E_SLIDE_WAKEUP      = 0x10,
	TW_SUPPORT_O_SLIDE_WAKEUP      = 0x20,
	TW_SUPPORT_W_SLIDE_WAKEUP      = 0x40,
	TW_SUPPORT_C_SLIDE_WAKEUP      = 0x80,
	TW_SUPPORT_M_SLIDE_WAKEUP      = 0x100,
	TW_SUPPORT_DOUBLE_CLICK_WAKEUP = 0x200,

	TW_SUPPORT_GESTURE_IN_ALL = (
			TW_SUPPORT_UP_SLIDE_WAKEUP    |
			TW_SUPPORT_DOWN_SLIDE_WAKEUP  |
			TW_SUPPORT_LEFT_SLIDE_WAKEUP  |
			TW_SUPPORT_RIGHT_SLIDE_WAKEUP |
			TW_SUPPORT_E_SLIDE_WAKEUP |
			TW_SUPPORT_O_SLIDE_WAKEUP |
			TW_SUPPORT_W_SLIDE_WAKEUP |
			TW_SUPPORT_C_SLIDE_WAKEUP |
			TW_SUPPORT_M_SLIDE_WAKEUP |
			TW_SUPPORT_DOUBLE_CLICK_WAKEUP)
};

u32 support_gesture = TW_SUPPORT_NONE_SLIDE_WAKEUP;
char wakeup_slide[32];
extern struct device *touchscreen_get_dev(void);
struct slide_wakeup
{
	unsigned char code;
	unsigned int mask;
	char *evp[2];
	char *gesture;
};
struct slide_wakeup tp_slide_wakeup[]={
	{0xAA,TW_SUPPORT_RIGHT_SLIDE_WAKEUP,{"GESTURE=RIGHT",NULL}, "right"},
	{0xBB,TW_SUPPORT_LEFT_SLIDE_WAKEUP,{"GESTURE=LEFT",NULL}, "left"},
	{0xBA,TW_SUPPORT_UP_SLIDE_WAKEUP,{"GESTURE=UP",NULL}, "up"},
	{0xAB,TW_SUPPORT_DOWN_SLIDE_WAKEUP,{"GESTURE=DOWN",NULL}, "down"},
	{0xCC,TW_SUPPORT_DOUBLE_CLICK_WAKEUP,{"GESTURE=DOUBLE_CLICK",NULL}, "double_click"},
	{0x63,TW_SUPPORT_C_SLIDE_WAKEUP,{"GESTURE=C",NULL}, "c"},
	{0x65,TW_SUPPORT_E_SLIDE_WAKEUP,{"GESTURE=E",NULL}, "e"},
	{0x6D,TW_SUPPORT_M_SLIDE_WAKEUP,{"GESTURE=M",NULL}, "m"},
	{0x6F,TW_SUPPORT_O_SLIDE_WAKEUP,{"GESTURE=O",NULL}, "o"},
	{0x77,TW_SUPPORT_W_SLIDE_WAKEUP,{"GESTURE=W",NULL}, "w"},
};
#endif

DEFINE_MUTEX(gtp_mutex);
/*******************************************************	
Function:
Read data from the i2c slave device.

Input:
client:	i2c device.
buf[0]:operate address.
buf[1]~buf[len]:read data buffer.
len:operate length.

return:0:success,otherwise:failed
 *********************************************************/
s32 gt1x_i2c_read(u16 addr, u8 *buf, s32 len)
{
	struct i2c_msg msgs[2];
	s32 ret = -1;
	s32 pos = 0;
	u16 address = addr;
	s32 data_length = len;
	s32 transfer_length = 0;
	u8 addr_buf[GTP_ADDR_LENGTH] = { (addr >> 8) & 0xFF, addr & 0xFF };
	
	GTP_DEBUG_FUNC();

	msgs[0].buf = addr_buf;

	while (pos < data_length) {
		if ((data_length - pos) > (GTP_MAX_I2C_XFER_LEN - GTP_ADDR_LENGTH)) {
			transfer_length = GTP_MAX_I2C_XFER_LEN - GTP_ADDR_LENGTH;
		} else {
			transfer_length = data_length - pos;
		}

		msgs[0].flags = !I2C_M_RD;
		msgs[0].addr  = i2c_connect_client->addr;
		msgs[0].len   = GTP_ADDR_LENGTH;
		msgs[0].buf[0] = (address >> 8) & 0xFF;
		msgs[0].buf[1] = address & 0xFF;

		msgs[1].flags = I2C_M_RD;
		msgs[1].addr  = i2c_connect_client->addr;
		msgs[1].len = transfer_length;
		msgs[1].buf = &buf[pos];

		ret = i2c_transfer(i2c_connect_client->adapter, msgs, 2);
		if (ret != 2) {
			GTP_INFO("I2c Transfer error! (%d)", ret);
			return ERROR_IIC;
		}
		pos += transfer_length;
		address += transfer_length;
	}

	return 0;
}

/*******************************************************	
Function:
write data to the i2c slave device.

Input:
client:	i2c device.
buf[0]:operate address.
buf[1]~buf[len]:write data buffer.
len:operate length.

return:0:success,otherwise:failed
 *********************************************************/
s32 gt1x_i2c_write(u16 addr,u8 *buf,s32 len)
{	
	s32 ret = -1;
	s32 pos = 0;
	s32 data_length = len;
	s32 transfer_length = 0;
	u8 data[GTP_MAX_I2C_XFER_LEN] = {0};
	u16 address = addr;
	struct i2c_msg msg;

	GTP_DEBUG_FUNC();

	msg.buf = data;

	while (pos < data_length) {
		if ((data_length - pos) > (GTP_MAX_I2C_XFER_LEN - GTP_ADDR_LENGTH)) {
			transfer_length = GTP_MAX_I2C_XFER_LEN - GTP_ADDR_LENGTH;
		} else {
			transfer_length = data_length - pos;
		}

		msg.flags = !I2C_M_RD;
		msg.addr  = i2c_connect_client->addr;
		msg.buf[0] = (address >> 8) & 0xFF;
		msg.buf[1] = address & 0xFF;
		msg.len = transfer_length + GTP_ADDR_LENGTH;
		memcpy(&msg.buf[GTP_ADDR_LENGTH], &buf[pos], transfer_length);

		ret = i2c_transfer(i2c_connect_client->adapter, &msg, 1);
		if (ret != 1) {
			GTP_INFO("I2c Transfer error! (%d)", ret);
			return ERROR_IIC;
		}
		pos += transfer_length;
		address += transfer_length;
	}

	return 0;
}

#if GTP_SLIDE_WAKEUP
void gtp_enable_irq_wake(struct goodix_ts_data *ts, u8 enable)
{
	static u8 irq_wake_status = 0;

	if (irq_wake_status != enable) {
		if (enable) {
			enable_irq_wake(ts->client->irq);
		} else {
			disable_irq_wake(ts->client->irq);
		}
		irq_wake_status = enable;
	}
}
#endif
/*******************************************************
Function:
i2c read twice, compare the results
Input:
client:  i2c device
addr:    operate address
rxbuf:   read data to store, if compare successful
len:     bytes to read
Output:
FAIL:    read failed
SUCCESS: read successful
 *********************************************************/
s32 gt1x_i2c_read_dbl_check(u16 addr, u8 * buffer, s32 len)
{
	u8 buf[16] = { 0 };
	u8 confirm_buf[16] = { 0 };

	if (len > 16) {
		GTP_ERROR("i2c_read_dbl_check length %d is too long, do not beyond %d", len, 16);
		return ERROR;
	}

	memset(buf, 0xAA, sizeof(buf));
	gt1x_i2c_read(addr, buf, len);

	msleep(5);

	memset(confirm_buf, 0, sizeof(confirm_buf));
	gt1x_i2c_read(addr, confirm_buf, len);

	if (!memcmp(buf, confirm_buf, len)) {
		memcpy(buffer, confirm_buf, len);
		return 0;
	}
	GTP_ERROR("i2c read 0x%04X, %d bytes, double check failed!", addr, len);
	return ERROR;
}

/*******************************************************
Function:
Send config Function.

Input:
client:	i2c client.

Output:
Executive outcomes.0--success,non-0--fail.
 *******************************************************/
s32 gtp_send_cfg(struct i2c_client *client)
{
	s32 ret = 0;

#if GTP_DRIVER_SEND_CFG
	s32 retry = 0;

	for (retry = 0; retry < 5; retry++)
	{
		ret = gt1x_i2c_write(GTP_REG_CONFIG_DATA, config , GTP_CONFIG_MAX_LENGTH);
		if (ret==0)
		{
			break;
		}
	}
	GTP_DEBUG_FUNC();
#endif
	
	return ret;
}
s32 gt1x_send_cfg(u8 * config, int cfg_len)
{
#if GTP_DRIVER_SEND_CFG
	int i;
	s32 ret = 0;
	s32 retry = 0;
	u16 checksum = 0;
	GTP_DEBUG("Driver Send Config, length: %d", cfg_len);
	for (i = 0; i < cfg_len - 3; i += 2) {
		checksum += (config[i] << 8) + config[i + 1];
	}
	if (!checksum) {
		GTP_ERROR("Invalid config, all of the bytes is zero!");
		return -1;
	}
	checksum = 0 - checksum;
	GTP_DEBUG("Config checksum: 0x%04X", checksum);
	config[cfg_len - 3] = (checksum >> 8) & 0xFF;
	config[cfg_len - 2] = checksum & 0xFF;
	config[cfg_len - 1] = 0x01;
	while (retry++ < 5) {
		ret = gt1x_i2c_write(GTP_REG_CONFIG_DATA, config, cfg_len);
		if (!ret) {
			msleep(200);	/* must 200ms, wait for storing config into flash. */
			GTP_DEBUG("Send config successfully!");
			return 0;
		}
	}
	GTP_ERROR("Send config failed!");
	return ret;
#endif
	return 0;
}

/*******************************************************
Function:
Enable IRQ Function.

Input:
ts:	i2c client private struct.

Output:
None.
 *******************************************************/
void gtp_irq_disable(struct goodix_ts_data *ts)
{
	unsigned long irqflags;

	GTP_DEBUG_FUNC();

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (!ts->irq_is_disable)
	{
		ts->irq_is_disable = 1;
		disable_irq_nosync(ts->client->irq);
	}
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}
void gt1x_irq_disable(void)
{
	struct goodix_ts_data *ts=NULL;
	ts = i2c_get_clientdata(i2c_connect_client);
	gtp_irq_disable(ts);
}
/*******************************************************
Function:
Disable IRQ Function.

Input:
ts:	i2c client private struct.

Output:
None.
 *******************************************************/
void gtp_irq_enable(struct goodix_ts_data *ts)
{
	unsigned long irqflags = 0;

	GTP_DEBUG_FUNC();

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (ts->irq_is_disable)
	{
		enable_irq(ts->client->irq);
		ts->irq_is_disable = 0;	
	}
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}
void gt1x_irq_enable(void)
{
	struct goodix_ts_data *ts=NULL;
	ts = i2c_get_clientdata(i2c_connect_client);
	gtp_irq_enable(ts);
}

/*******************************************************
Function:
Touch down report function.

Input:
ts:private data.
id:tracking id.
x:input x.
y:input y.
w:input weight.

Output:
None.
 *******************************************************/
static void gtp_touch_down(struct goodix_ts_data* ts,s32 id,s32 x,s32 y,s32 w)
{
#if GTP_CHANGE_X2Y
	GTP_SWAP(x, y);
#endif

#ifdef CONFIG_TOUCHSCREEN_WINDOW
	if(gtp_window_mode_flag &&
		!( (x > gt1xx_cover_window.win_x_min)
		&& (x < gt1xx_cover_window.win_x_max)
		&& (y > gt1xx_cover_window.win_y_min)
		&& (y < gt1xx_cover_window.win_y_max)))
		return;
#endif

#if GTP_ICS_SLOT_REPORT
	input_mt_slot(ts->input_dev, id);
	input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, w);
	input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
#else
	input_report_key(ts->input_dev, BTN_TOUCH, 1);//for fastmmi
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, w);
	input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
	//input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 200);
	input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, id);
	input_mt_sync(ts->input_dev);
#endif
	GTP_DEBUG("ID:%d, X:%d, Y:%d, W:%d", id, x, y, w);

}

/*******************************************************
Function:
Touch up report function.

Input:
ts:private data.

Output:
None.
 *******************************************************/
static void gtp_touch_up(struct goodix_ts_data* ts, s32 id)
{
#if GTP_ICS_SLOT_REPORT
	input_mt_slot(ts->input_dev, id);
	input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
	GTP_DEBUG("Touch id[%2d] release!", id);
#else	
	input_report_key(ts->input_dev, BTN_TOUCH, 0);//for fastmmi
	input_mt_sync(ts->input_dev);
#endif
}

#if GTP_SLIDE_WAKEUP
int gesture_event_handler(struct goodix_ts_data *ts)
{
	char **envp;
	u8 doze_buf[4] = {0};
	struct device *touchscreen_dev;
	int ret;
	int i;
	
	if (DOZE_ENABLED != doze_status)
		return -1;
	
	ret = gt1x_i2c_read(GTP_REG_WAKEUP_GESTURE, doze_buf, 4);
	GTP_DEBUG("0x%x = 0x%02X,0x%02X,0x%02X,0x%02X", GTP_REG_WAKEUP_GESTURE,doze_buf[0], doze_buf[1], doze_buf[2], doze_buf[3]);
	if (ret == 0) 
	{
		for(i = 0; i < ARRAY_SIZE(tp_slide_wakeup); i++)
		{
			if( (doze_buf[0] == tp_slide_wakeup[i].code) && (support_gesture&tp_slide_wakeup[i].mask))
			{
				GTP_INFO("Slide(0x%x) To Light up the screen!", doze_buf[0]);
				doze_status = DOZE_WAKEUP;
				gtp_enable_irq_wake(ts, 0);
				sprintf(wakeup_slide,tp_slide_wakeup[i].gesture);
				envp = tp_slide_wakeup[i].evp;
				touchscreen_dev = touchscreen_get_dev();
				kobject_uevent_env(&touchscreen_dev->kobj, KOBJ_CHANGE, envp);
				GTP_INFO("send < %s slide wakeup > kobject uevent!", wakeup_slide);
				break;	
			}
		}	
	}

	// clear 0x814C
	doze_buf[0] = 0x00;
	gt1x_i2c_write(GTP_REG_WAKEUP_GESTURE, doze_buf, 1);
	gtp_enter_doze(ts);

	return 0;
}
#endif


/**
 * gt1x_touch_event_handler - handle touch event 
 * (pen event, key event, finger touch envent)
 * @data: 
 * Return    <0: failed, 0: succeed
 */
s32 gt1x_touch_event_handler(u8 * data, struct goodix_ts_data *ts)
{
	u8 touch_data[1 + 8 * GTP_MAX_TOUCH + 2] = { 0 };
	u8 touch_num = 0;
	u16 cur_event = 0;
	static u16 pre_event = 0;
	static u16 pre_index = 0;

	u8 key_value = 0;
	u8 *coor_data = NULL;
	s32 input_x = 0;
	s32 input_y = 0;
	s32 input_w = 0;
	s32 id = 0;
	s32 i = 0;
	s32 ret = -1;

	GTP_DEBUG_FUNC();
	
	touch_num = data[0] & 0x0f;
	if (touch_num > GTP_MAX_TOUCH) {
		GTP_ERROR("Illegal finger number!");
		return ERROR_VALUE;
	}

	memcpy(touch_data, data, 11);

	/* read the remaining coor data */
	if (touch_num > 1) {
		ret = gt1x_i2c_read((GTP_READ_COOR_ADDR + 11), &touch_data[11], 1 + 8 * touch_num + 2 - 11);
		if (ret!=0) {
			GTP_ERROR("Read coordinate i2c error.");
			return ret;
		}
	}

	/* checksum */

/* 
 * cur_event , pre_event bit defination
 * bit4	bit3		    bit2	 bit1	   bit0
 * hover  stylus_key  stylus  key     touch
 *
 */
	key_value = touch_data[1 + 8 * touch_num];
	/* check current event */
	if ((touch_data[0] & 0x10) && key_value) {
#if (GTP_HAVE_TOUCH_KEY)
		if (key_value & 0x0F) {
			SET_BIT(cur_event, BIT_TOUCH_KEY);
		}
#endif
	}
	else if (touch_num) {
		SET_BIT(cur_event, BIT_TOUCH);
	}

#if GTP_HAVE_TOUCH_KEY
	if (CHK_BIT(cur_event, BIT_TOUCH_KEY) || CHK_BIT(pre_event, BIT_TOUCH_KEY)) {
		for (i = 0; i < GTP_MAX_KEY_NUM; i++) {
			input_report_key(ts->input_dev, touch_key_array[i], key_value & (0x01 << i));
		}
		if (CHK_BIT(cur_event, BIT_TOUCH_KEY)) {
			GTP_DEBUG("Key Down.");
		} else {
			GTP_DEBUG("Key Up.");
		}
	}
#endif

/* finger touch event*/
	if (CHK_BIT(cur_event, BIT_TOUCH)) {
		u8 report_num = 0;
		coor_data = &touch_data[1];
		id = coor_data[0] & 0x0F;
		for (i = 0; i < GTP_MAX_TOUCH; i++) {
			if (i == id) {
				input_x = coor_data[1] | (coor_data[2] << 8);
				input_y = coor_data[3] | (coor_data[4] << 8);
				input_w = coor_data[5] | (coor_data[6] << 8);

				GTP_DEBUG("(%d)(%d, %d)[%d]", id, input_x, input_y, input_w);
				gtp_touch_down(ts, i, input_x, input_y, input_w);
				if (report_num++ < touch_num) {
					coor_data += 8;
					id = coor_data[0] & 0x0F;
				}
				pre_index |= 0x01 << i;
			} else if (pre_index & (0x01 << i)) {
#if GTP_ICS_SLOT_REPORT
				gtp_touch_up(ts, i);
#endif
				pre_index &= ~(0x01 << i);
			}
		}
	} else if (CHK_BIT(pre_event, BIT_TOUCH)) {
#if GTP_ICS_SLOT_REPORT
		int cycles = pre_index < 3 ? 3 : GTP_MAX_TOUCH;
		for (i = 0; i < cycles; i++) {
			if (pre_index >> i & 0x01) {
				gtp_touch_up(ts, i);
			}
		}
#else
		gtp_touch_up(ts, 0);
#endif
		GTP_DEBUG("Released Touch.");
		pre_index = 0;
	}

	if (CHK_BIT(cur_event, BIT_TOUCH_KEY | BIT_TOUCH)
	    || CHK_BIT(pre_event, BIT_TOUCH_KEY | BIT_TOUCH)) {
		input_sync(ts->input_dev);
	}

	if (!pre_event && !cur_event) {
		GTP_DEBUG("Additional Int Pulse.");
	} else {
		pre_event = cur_event;
	}

	return 0;
}


s32 gt1x_request_event_handler(void)
{
	s32 ret = -1;
	u8 rqst_data = 0;
	ret = gt1x_i2c_read(GTP_REG_RQST, &rqst_data, 1);
	if (ret!=0) {
		GTP_ERROR("I2C transfer error. errno:%d", ret);
		return -1;
	}
	GTP_DEBUG("Request state:0x%02x.", rqst_data);
	switch (rqst_data & 0x0F) {
	case GTP_RQST_CONFIG:
		GTP_INFO("Request Config.");
		ret = gtp_send_cfg(i2c_connect_client);
		if (ret) {
			GTP_ERROR("Send gt1x_config error.");
		} else {
			GTP_INFO("Send gt1x_config success.");
			rqst_data = GTP_RQST_RESPONDED;
			gt1x_i2c_write(GTP_REG_RQST, &rqst_data, 1);
		}
		break;
	case GTP_RQST_RESET:
		GTP_INFO("Request Reset.");
		gtp_reset_guitar(i2c_connect_client, 50);
		rqst_data = GTP_RQST_RESPONDED;
		gt1x_i2c_write(GTP_REG_RQST, &rqst_data, 1);
		break;
	case GTP_RQST_BAK_REF:
		GTP_INFO("Request Ref.");
		break;
	case GTP_RQST_MAIN_CLOCK:
		GTP_INFO("Request main clock.");
		break;
	default:
		break;
	}
	return 0;
}



/*******************************************************
Function:
Goodix touchscreen work function.

Input:
work:	work_struct of goodix_wq.

Output:
None.
 *******************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{
#ifdef CONFIG_TP_I2C_TIMEOUT_WAIT
        int wait_event;
#endif
	u8 end_cmd = 0;
	u8 finger = 0;
	s32 ret = 0;
	u8 point_data[11] = { 0 };
	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);
	
	GTP_DEBUG_FUNC();
	if (ts->enter_update||ts->gtp_is_sleep_suspend==1)
		return;

#ifdef CONFIG_TP_I2C_TIMEOUT_WAIT 
	wait_event = wait_event_interruptible(wait_tw_resume,tw_resume == 1);
       if (wait_event < 0){
               pr_err("%s: wait for i2c bus resume error\n", __func__);
	}
#endif

	mutex_lock(&gtp_mutex);
#if GTP_SLIDE_WAKEUP
	ret = gesture_event_handler(ts);
	if (ret >= 0) {
		goto exit_work_func1;
	}
#endif

	ret = gt1x_i2c_read(GTP_READ_COOR_ADDR, point_data, sizeof(point_data));
	if (ret!=0) {
		GTP_ERROR("I2C transfer error!");
		goto exit_work_func;
	}

	finger = point_data[0];
	if (finger == 0x00) {
		gt1x_request_event_handler();
	}

	if ((finger & 0x80) == 0) {
		GTP_DEBUG("buffer not ready:0x%02x", finger);
		goto exit_work_func1;
	}

	ret = gt1x_touch_event_handler(point_data, ts);
	if (ret) {
		return;
	}
		
exit_work_func:
	if (!gt1x_rawdiff_mode) {
		ret = gt1x_i2c_write(GTP_READ_COOR_ADDR, &end_cmd, 1);
		if (ret!=0) {
			GTP_INFO("I2C write end_cmd  error!");
		}
	}
	
exit_work_func1:
	if (ts->use_irq)
	{
		gtp_irq_enable(ts);
	}
	
	mutex_unlock(&gtp_mutex);
}



/*******************************************************
Function:
Timer interrupt service routine.

Input:
timer:	timer struct pointer.

Output:
Timer work mode. HRTIMER_NORESTART---not restart mode
 *******************************************************/
static enum hrtimer_restart goodix_ts_timer_handler(struct hrtimer *timer)
{
	struct goodix_ts_data *ts = container_of(timer, struct goodix_ts_data, timer);

	GTP_DEBUG_FUNC();

	queue_work(goodix_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, (GTP_POLL_TIME+6)*1000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

/*******************************************************
Function:
External interrupt service routine.

Input:
irq:	interrupt number.
dev_id: private data pointer.

Output:
irq execute status.
 *******************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;

	GTP_DEBUG_FUNC();

	gtp_irq_disable(ts);
	queue_work(goodix_wq, &ts->work);

	return IRQ_HANDLED;
}
/*******************************************************
Function:
Reset chip Function.

Input:
ms:reset time.

Output:
None.
 *******************************************************/
#if 1
void gtp_reset_guitar(struct i2c_client *client, s32 ms)
{
	struct goodix_ts_data * ts = i2c_get_clientdata(client);

	GTP_DEBUG_FUNC();

	if(ts->pdata->reset)
		ts->pdata->reset(ms);

}
#endif
s32 gt1x_reset_guitar(void)
{
	gtp_reset_guitar(i2c_connect_client,20);
	return 0;
}

#if GTP_SLIDE_WAKEUP
/*******************************************************
Function:
Enter doze mode for sliding wakeup.
Input:
ts: goodix tp private data
Output:
1: succeed, otherwise failed
 *******************************************************/
static s8 gtp_enter_doze(struct goodix_ts_data *ts)
{
	s8 ret = -1;
	s8 retry = 0;

	GTP_DEBUG_FUNC();

	gtp_irq_disable(ts);
	sprintf(wakeup_slide,"none");
	GTP_DEBUG("entering doze mode...");
	while(retry++ < 5)
	{
		ret = gt1x_send_cmd(0x08, 0);
		if (ret==0)
		{
			doze_status = DOZE_ENABLED;
			ts->gtp_is_sleep_suspend=0;
			GTP_INFO("GTP has been working in doze mode!");
			gtp_enable_irq_wake(ts, 1);
			gtp_irq_enable(ts);
			return ret;
		}
		msleep(10);
	}
	GTP_ERROR("GTP send doze cmd failed.");
	gtp_irq_enable(ts);
	return ret;
}
#endif
/*******************************************************
Function:
Eter sleep function.

Input:
ts:private data.

Output:
Executive outcomes.0--success,non-0--fail.
 *******************************************************/
static s8 gtp_enter_sleep(struct goodix_ts_data * ts)
{
	s8 ret = 0;

	s8 retry = 0;

#if GTP_POWER_CTRL_SLEEP
	ret = gt1x_send_cmd(GTP_CMD_SLEEP, 0);
	if(ts->pdata->power)
		ts->pdata->power(0);
#else
	//irq output 0
	gpio_direction_output(ts->pdata->gpio_irq, 0);
	msleep(5);

	while(retry++ < 5)
	{
		ret = gt1x_send_cmd(GTP_CMD_SLEEP, 0);
		if (ret==0)
		{
			ts->gtp_is_sleep_suspend = 1;
			GTP_DEBUG("GTP enter sleep!");
			return ret;	
		}
		msleep(10);
	}
	GTP_ERROR("GTP send sleep cmd failed.");
#endif

	return ret;
}
/*******************************************************
Function:
Wakeup from sleep mode Function.

Input:
ts:	private data.

Output:
Executive outcomes.0--success,non-0--fail.
 *******************************************************/
static s8 gtp_wakeup_sleep(struct goodix_ts_data * ts)
{
	u8 retry = 0;
	s8 ret = -1;

	GTP_DEBUG_FUNC();

#if GTP_POWER_CTRL_SLEEP
	if(ts->pdata->power)
		ts->pdata->power(1);
#endif

	while(retry++ < 5)
	{
#if GTP_SLIDE_WAKEUP
	  doze_status = DOZE_DISABLED;
	  if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
	     gtp_irq_disable(ts);
#endif

	ts->gtp_is_sleep_suspend=0;	
	gtp_reset_guitar(ts->client, 20);

#if GTP_SLIDE_WAKEUP
    if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
	     gtp_irq_enable(ts);
#endif


		ret = gtp_send_cfg(ts->client);
		if (ret < 0)
		{
			GTP_INFO("Wakeup sleep send config failed!");
			continue;
		}
		GTP_INFO("GTP wakeup sleep");
		return 1;
	}

	GTP_ERROR("GTP wakeup sleep failed.");
	return ret;
}
/*******************************************************
Function:
GTP initialize function.

Input:
ts:	i2c client private struct.

Output:
Executive outcomes.0---succeed.
 *******************************************************/
s32 gtp_init_panel(struct goodix_ts_data *ts)
{
	s32 ret = -1;
	s32 i;
	u16 check_sum = 0;
	u8 opr_buf[16];
	u8 sensor_id = 0;
	u8 retry = 0;

	ret = gt1x_i2c_read_dbl_check(0x41E4, opr_buf, 1);
	if (0 == ret)
	{
		if (opr_buf[0] != 0xBE)
		{
			ts->fw_error = 1;
			printk("Firmware error, no config sent!");
			return -1;
		}
	}	

	// module id	
	ret = gt1x_i2c_read_dbl_check(GTP_REG_SENSOR_ID, &sensor_id, 1);
	if (0 == ret)
	{
		while((sensor_id == 0xff)&&(retry++ < 3))
		{
			msleep(100);
			ret = gt1x_i2c_read_dbl_check(GTP_REG_SENSOR_ID, &sensor_id, 1);
			GTP_ERROR("GTP sensor_ID read failed time %d.",retry);
		}
		if (sensor_id >= 0x06)
		{
			GTP_ERROR("Invalid sensor_id(0x%02X), No Config Sent!", sensor_id);
			return -1;
		}
	}	
	else
	{
		GTP_ERROR("Failed to get sensor_id, No config sent!");
		return -1;
	}

	tp_supported = 0;
	for(i=0; i<config_array_size; i++)
	{
		if(sensor_id == yl_cfg[i].tp_id)
		{
			tp_index = i;
			tp_supported = 1;
			ts->gtp_cfg_len = yl_cfg[tp_index].config_length;		
			break;
		}			
	}	
	if(tp_supported != 1)
	{
		GTP_ERROR("Invalid sensor_id(0x%02X), No Config Sent!", sensor_id);
		return -1;
	}

	if (ts->gtp_cfg_len < GTP_CONFIG_MIN_LENGTH)
	{
		GTP_ERROR("Sensor_ID(%d) matches with NULL or INVALID CONFIG GROUP! \
				NO Config Sent! You need to check you header file CFG_GROUP section!", sensor_id);
		return -1;
	}

	//config
	ts->gtp_cfg_len = yl_cfg[tp_index].config_length;		
	memset(&config[0], 0, GTP_CONFIG_MAX_LENGTH);
	memcpy(&config[0], yl_cfg[tp_index].config, ts->gtp_cfg_len);

#ifdef CONFIG_TOUCHSCREEN_COB
	ret = gt1x_i2c_read_dbl_check(GTP_REG_CONFIG_DATA, &opr_buf[0], 1);
	if (ret == 0)
	{
		if ( (opr_buf[0] < 90) && (opr_buf[0] > config[0]) )
		{
			cfg_version_bak = config[0];
			config[0] = 0x00;
		}
	}
#endif

#if GTP_CUSTOM_CFG
	//max size
	config[RESOLUTION_LOC]     = ts->pdata->screen_x&0xff;		
	config[RESOLUTION_LOC + 1] = (ts->pdata->screen_x>>8)&0xff;
	config[RESOLUTION_LOC + 2] = ts->pdata->screen_y&0xff;
	config[RESOLUTION_LOC + 3] = (ts->pdata->screen_y>>8)&0xff;

	//trigger type
	if (ts->pdata->irqflag == IRQF_TRIGGER_RISING)  //RISING
	{
		config[TRIGGER_LOC] &= 0xfc;
	}
	else if (ts->pdata->irqflag == IRQF_TRIGGER_FALLING)  //FALLING
	{
		config[TRIGGER_LOC] &= 0xfc;
		config[TRIGGER_LOC] |= 0x01;
	}
#endif  //endif GTP_CUSTOM_CFG

	//chechsum
	check_sum = 0;
	for (i = 0; i < ts->gtp_cfg_len - 3; i += 2) {
		check_sum += (config[i] << 8) + config[i + 1];
	}
	if (!check_sum) {
		GTP_ERROR("Invalid config, all of the bytes is zero!");
		return -1;
	}
	check_sum = 0 - check_sum;
	GTP_DEBUG("Config check_sum: 0x%04X", check_sum);
	config[ts->gtp_cfg_len-3] = (check_sum >> 8) & 0xFF;
	config[ts->gtp_cfg_len-2] = check_sum & 0xFF;
	config[ts->gtp_cfg_len-1] = 1;

	ts->abs_x_max = (config[RESOLUTION_LOC + 1] << 8) + config[RESOLUTION_LOC];
	ts->abs_y_max = (config[RESOLUTION_LOC + 3] << 8) + config[RESOLUTION_LOC + 2];
	ts->int_trigger_type = (config[TRIGGER_LOC]) & 0x03;
	if ((!ts->abs_x_max)||(!ts->abs_y_max))
	{
		GTP_ERROR("GTP resolution & max_touch_num invalid, use default value!");
		ts->abs_x_max = ts->pdata->screen_x;
		ts->abs_y_max = ts->pdata->screen_y;
	}

	ret = gtp_send_cfg(ts->client);
	if (ret < 0)
	{
		GTP_ERROR("Send config error.");
	}

	msleep(10);
	
#ifdef CONFIG_TOUCHSCREEN_COB
	if(cfg_version_bak)
	{
		config[0] =cfg_version_bak;

		//chechsum
		check_sum = 0;
		for (i = 0; i < ts->gtp_cfg_len - 3; i += 2) {
			check_sum += (config[i] << 8) + config[i + 1];
		}
		if (!check_sum) {
			GTP_ERROR("Invalid config, all of the bytes is zero!");
			return -1;
		}
		check_sum = 0 - check_sum;
		GTP_DEBUG("Config check_sum: 0x%04X", check_sum);
		config[ts->gtp_cfg_len-3] = (check_sum >> 8) & 0xFF;
		config[ts->gtp_cfg_len-2] = check_sum & 0xFF;
		config[ts->gtp_cfg_len-1] = 1;

		msleep(100);
		ret = gtp_send_cfg(ts->client);
		msleep(10);
	}
#endif

	return 0;
}
s32 gt1x_init_panel(void)
{
        struct goodix_ts_data *ts = NULL;
        ts = i2c_get_clientdata(i2c_connect_client);
        return  gtp_init_panel(ts);
}
/*******************************************************
Function:
Read goodix touchscreen version function.

Input:
client:	i2c client struct.
version:address to store version info

Output:
Executive outcomes.0---succeed.
 *******************************************************/
s32 gt1x_read_version(struct gt1x_version_info * ver_info)
{
	s32 ret = -1;
	u8 buf[12] = { 0 };
	u32 mask_id = 0;
	u32 patch_id = 0;
	u8 product_id[5] = { 0 };
	u8 sensor_id = 0;
	u8 match_opt = 0;
	int i, retry = 3;
	u8 checksum = 0;

	GTP_DEBUG_FUNC();

	while (retry--) {
		ret = gt1x_i2c_read_dbl_check(GTP_REG_VERSION, buf, sizeof(buf));
		if (!ret) {
			checksum = 0;

			for (i = 0; i < sizeof(buf); i++) {
				checksum += buf[i];
			}

			if (checksum == 0 &&	/* first 3 bytes must be number or char */
				IS_NUM_OR_CHAR(buf[0]) && 
				IS_NUM_OR_CHAR(buf[1]) && 
				IS_NUM_OR_CHAR(buf[2]) && buf[10] != 0xFF) 
			{	/*sensor id == 0xFF, retry */
				break;
			} else {
				GTP_ERROR("GTP read version failed!(checksum error)");
			}
		} else {
			GTP_ERROR("GTP read version failed!");
		}
		GTP_DEBUG("GTP reread version : %d", retry);
		msleep(100);
	}

	if (retry <= 0) {
		return -1;
	}

	mask_id = (u32) ((buf[7] << 16) | (buf[8] << 8) | buf[9]);
	patch_id = (u32) ((buf[4] << 16) | (buf[5] << 8) | buf[6]);
	memcpy(product_id, buf, 4);
	sensor_id = buf[10] & 0x0F;
	match_opt = (buf[10] >> 4) & 0x0F;

	GTP_INFO("IC VERSION: GT%s_%06X(Patch)_%04X(Mask)_%02X(SensorID)", product_id, patch_id >> 8, mask_id >> 8, sensor_id);

	if (ver_info != NULL) {
		ver_info->mask_id = mask_id;
		ver_info->patch_id = patch_id;
		memcpy(ver_info->product_id, product_id, 5);
		ver_info->sensor_id = sensor_id;
		ver_info->match_opt = match_opt;
	}
	return 0;
}

s32 gtp_read_fw_cfg_version(void)
{
	s32 ret = -1;
	u8 cfg[1] = {0};

	ret = gt1x_read_version(&gt1x_fw_version);
	if (ret < 0)
	{
		GTP_ERROR("GTP read version failed");
		return ret;
	}

	ret = gt1x_i2c_read(GTP_REG_CONFIG_DATA, cfg, 1);
	if( ret!=0 )
	{
		GTP_ERROR("Read config version failed.");
		return ret;
	}
	gtp_cfg_version = cfg[0]&0x7F;

	return 0;
}

/*******************************************************
Function:
I2c test Function.

Input:
client:i2c client.

Output:
Executive outcomes.0--success,non-0--fail.
 *******************************************************/
static s8 gtp_i2c_test(struct i2c_client *client)
{
	u8 test[1]={0};
	u8 retry = 0;
	s8 ret = -1;

	GTP_DEBUG_FUNC();

	while(retry++ < 5)
	{
		ret = gt1x_i2c_read(GTP_REG_CONFIG_DATA, test, 1);
		if (ret==0)
		{
			return ret;
		}
		GTP_ERROR("GTP i2c test failed time %d.",retry);
		msleep(10);
	}

	return ret;
}
/*******************************************************
Function:
Request irq Function.

Input:
ts:private data.

Output:
Executive outcomes.0--success,non-0--fail.
 *******************************************************/
static s8 gtp_request_irq(struct goodix_ts_data *ts)
{
	s32 ret = -1;
	const u8 irq_table[] = GTP_IRQ_TAB;

	GTP_DEBUG("INT trigger type:%x", ts->int_trigger_type);

	ret  = request_irq(ts->client->irq,
			goodix_ts_irq_handler,
			irq_table[ts->int_trigger_type],
			ts->client->name,
			ts);
	if (ret)
	{
		GTP_ERROR("Request IRQ failed!ERRNO:%d.", ret);
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = goodix_ts_timer_handler;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
		return -1;
	}
	else
	{
		ts->use_irq = 1;
		gtp_irq_disable(ts);
		return 0;
	}
}
/*******************************************************
Function:
Request input device Function.

Input:
ts:private data.

Output:
Executive outcomes.0--success,non-0--fail.
 *******************************************************/
static s8 gtp_request_input_dev(struct goodix_ts_data *ts)
{
	s8 ret = -1;
	s8 phys[32];
#if GTP_HAVE_TOUCH_KEY
	u8 index = 0;
#endif

	GTP_DEBUG_FUNC();

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL)
	{
		GTP_ERROR("Failed to allocate input device.");
		return -ENOMEM;
	}

#if GTP_ICS_SLOT_REPORT
	input_mt_init_slots(ts->input_dev, 10);     // in case of "out of memory"
#else
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif

#if GTP_HAVE_TOUCH_KEY
	for (index = 0; index < GTP_MAX_KEY_NUM; index++)
	{
		input_set_capability(ts->input_dev,EV_KEY,touch_key_array[index]);	
	}
#endif

#if GTP_SLIDE_WAKEUP
	input_set_capability(ts->input_dev, EV_KEY, KEY_POWER);
#endif
#if GTP_CHANGE_X2Y
	GTP_SWAP(ts->abs_x_max, ts->abs_y_max);
#endif

	//input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);	
	//input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, GTP_MAX_TOUCH, 0, 0);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);

	sprintf(phys, "input/ts");
	ts->input_dev->name = goodix_ts_name;
	ts->input_dev->phys = phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#elif defined(CONFIG_FB)
	INIT_WORK(&ts->fb_notify_work, fb_notify_resume_work);
	ts->fb_notif.notifier_call = fb_notifier_callback;
	ret = fb_register_client(&ts->fb_notif);
	if (ret)
		GTP_ERROR("Unable to register fb_notifier: %d\n",ret);
#endif		
	input_set_drvdata(ts->input_dev, ts);

	ret = input_register_device(ts->input_dev);
	if (ret)
	{
		GTP_ERROR("Register %s input device failed", ts->input_dev->name);
		return -ENODEV;
	}

	return 0;
}
/***********************************************************************************************
Name	:

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/

/*******************************************************
 ********  1--active  0--not active***********
 *******************************************************/
int goodix_active(void)
{
	GTP_DEBUG("enter %s",__FUNCTION__);
	return 1;
}
/*******************************************************
 ***********check firmware if need update***********
 *******************************************************/
int goodix_firmware_need_update(void)
{
	int ret = -1; 
	GTP_INFO(" check if need firmware update..."); 
	GTP_DEBUG("enter %s",__FUNCTION__); 
	ret=gt1x_update_prepare(NULL);
	if(ret!=0)
	{ 
		GTP_ERROR("gt1x update prepare fail!");
		return 0; 
	}
	ret=gt1x_check_firmware();
	if(ret!=0)
	{ 
		GTP_ERROR("gt1x check firmware fail!");
		return 0;
	}
	ret=gt1x_update_judge();
	if(ret != 0)
		{ ret = 0; }
	else
		{ ret = 1; }
	return ret;
}
/*******************************************************
 *********************do firmware update ***************
 *******************************************************/
int goodix_firmware_do_update(void)
{
	int ret = -1;
	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);

	GTP_DEBUG("enter %s",__FUNCTION__);

	mutex_lock(&gtp_mutex);

	if (ts->gtp_is_suspend)
	{
		mutex_unlock(&gtp_mutex);
		return ret;
	}

	ret = goodix_firmware_need_update();
	if(ret)
	{
		if(0 != gt1x_update_firmware(NULL))
			ret = -1;
		else
		{
			gtp_read_fw_cfg_version();
			ret = 0;
		}		
	}

	mutex_unlock(&gtp_mutex);
	return ret;
}
/*******************************************************
 *******************check if need calibrate***********
 *******************************************************/
int goodix_need_calibrate(void)
{
	GTP_DEBUG("enter %s",__FUNCTION__);
	return 0;
}
/*******************************************************
 ******************system write "calibrate"************
 *******************************************************/
int goodix_calibrate(void)
{
	GTP_DEBUG("enter %s",__FUNCTION__);	
	return 0;
}
/*******************************************************
 ******************get firmware version **************
 *******************************************************/
int goodix_get_firmware_version(char * version )
{
	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);

	if (!ts->gtp_is_suspend)
		gtp_read_fw_cfg_version();
	if(tp_supported)
		return sprintf(version, "%s:%s:0x%4x:0x%2x", yl_cfg[tp_index].vendor_name,
				TW_IC_PREFIX_NAME, gt1x_fw_version.patch_id, gtp_cfg_version);
	else
		return sprintf(version, "%s:%s:0x%4x:0x%2x", "----", TW_IC_PREFIX_NAME,
				gt1x_fw_version.patch_id, gtp_cfg_version);	
}
/*******************************************************
 ******************system write "reset"***************
 *******************************************************/
int goodix_reset_touchscreen(void)	
{
	GTP_DEBUG("enter %s",__FUNCTION__);
	return 1;
}
/*******************************************************
 ******************charger status check***************
 *******************************************************/
#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
void yl_chg_status_changed(void)
{
	unsigned int chg_status;
	int count = 0;
	u8 write_val=0;
	s32 ret = -1;
	struct goodix_ts_data *ts;

	if(!yl_cfg->charger_status_force_check && (current_mode_flag != MODE_GLOVE))
		return;

	ts = i2c_get_clientdata(i2c_connect_client);

	chg_status = touch_get_charger_status();
	GTP_DEBUG("tw get charger status is: %d", chg_status);

	mutex_lock(&gtp_mutex);
	if(ts->gtp_is_suspend){
		mutex_unlock(&gtp_mutex);
		return;
	}

	if (current_mode_flag == MODE_GLOVE)
	{
		if (chg_status == 0)
		{
			write_val = 0x0c;
		} else
		{
			write_val = 0x0b;
		}
	}
	else
	{
		mutex_unlock(&gtp_mutex);
		return;
	}

	ret = gt1x_i2c_write(GTP_REG_CMD, &write_val, 1);
	if(ret==0)
	{
		GTP_DEBUG("[GTS]:%s write 0x8040 register success ,value = %d.\n", __func__, write_val);
	} else
	{
		printk(KERN_ERR "%s: glove mode  write 0x8040 error, count = %d, write_val = %d.\n",
				__func__, count, write_val);
	}
	
	mutex_unlock(&gtp_mutex);
}
#endif

/*******************************************************
 ************ get work mode &set work mode*************
 *******************************************************/
void gtp_switch_cfg(void)
{
	u8 retry = 0;
	int ret = -1;
	struct goodix_ts_data *ts;

	if (i2c_connect_client == NULL)
		return;
	ts = i2c_get_clientdata(i2c_connect_client);

#ifdef CONFIG_TOUCHSCREEN_WINDOW
	if(gtp_window_mode_flag){
		if(yl_cfg[tp_index].config_window_length){
			ts->gtp_cfg_len = yl_cfg[tp_index].config_window_length;
			memset(&config[0], 0, GTP_CONFIG_MAX_LENGTH);
			memcpy(&config[0], yl_cfg[tp_index].config_window, ts->gtp_cfg_len);
			GTP_INFO("send window mode config.");
		}else{
			ts->gtp_cfg_len = yl_cfg[tp_index].config_glove_length;
			memset(&config[0], 0, GTP_CONFIG_MAX_LENGTH);
			memcpy(&config[0], yl_cfg[tp_index].config_glove, ts->gtp_cfg_len);
			GTP_INFO("send glove mode config.");
		}
		goto _send_cfg;
	}
#endif

#ifdef CONFIG_TOUCHSCREEN_GLOVE
	if(MODE_GLOVE == current_mode_flag){
		ts->gtp_cfg_len = yl_cfg[tp_index].config_glove_length;
		memset(&config[0], 0, GTP_CONFIG_MAX_LENGTH);
		memcpy(&config[0], yl_cfg[tp_index].config_glove, ts->gtp_cfg_len);
		GTP_INFO("send glove mode config.");
		goto _send_cfg;
	}
#endif

	ts->gtp_cfg_len = yl_cfg[tp_index].config_length;
	memset(&config[0], 0, GTP_CONFIG_MAX_LENGTH);
	memcpy(&config[0], yl_cfg[tp_index].config, ts->gtp_cfg_len);
	GTP_INFO("send normal mode config.");

#if defined(CONFIG_TOUCHSCREEN_GLOVE) || defined(CONFIG_TOUCHSCREEN_WINDOW)
_send_cfg:
#endif
	if(ts->gtp_is_suspend)
		return;

	if (ts->use_irq)
	{
		gtp_irq_disable(ts);
	}
	else
	{
		hrtimer_cancel(&ts->timer);
	}

	while(retry++ < 5)
	{
		gtp_reset_guitar(ts->client, 20);
		ret = gtp_send_cfg(ts->client);
		if (ret >= 0)
		{
			GTP_INFO("send config successed!");
			break;
		}
	}

	if (ts->use_irq)
	{
		gtp_irq_enable(ts);
	}
	else
	{
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

	return;
}


void glove_windows_switch(int in_hall)
{
#ifdef CONFIG_TOUCHSCREEN_WINDOW
	int static pre_hall_state=0;

	if(!tp_supported)
		return;
	if(pre_hall_state == in_hall)
		return;

	mutex_lock(&gtp_mutex);
	pre_hall_state = in_hall;

	if(in_hall)
	{
		gtp_window_mode_flag = true;
	} else {
		gtp_window_mode_flag = false;
	}
	gtp_switch_cfg();

	mutex_unlock(&gtp_mutex);
#endif

	return;
}
EXPORT_SYMBOL_GPL(glove_windows_switch);


touch_mode_type goodix_get_mode(void)
{
	GTP_DEBUG("enter %s",__FUNCTION__);

	GTP_INFO("current tw mode flag is %d", current_mode_flag);
	return current_mode_flag;

}

int goodix_set_mode(touch_mode_type work_mode)
{
#ifdef CONFIG_TOUCHSCREEN_GLOVE

	GTP_DEBUG("enter %s",__FUNCTION__);

	if (i2c_connect_client == NULL)
		return -1;

	if(current_mode_flag == work_mode)
		return 1;

	mutex_lock(&gtp_mutex);
	current_mode_flag = work_mode;
	gtp_switch_cfg();
	mutex_unlock(&gtp_mutex);

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	yl_chg_status_changed();
#endif

	printk("[GTP]: current_mode_flag = %d\n", current_mode_flag);
#endif

	return 1;
}
/*******************************************************
 ****************get "oreitation:X" ************
 *******************************************************/
touch_oreitation_type goodix_get_oreitation(void)
{
	GTP_DEBUG("enter %s\n",__FUNCTION__);
	return 1;
}
int goodix_set_oreitation(touch_oreitation_type oreitate)
{
	GTP_DEBUG("enter %s\n",__FUNCTION__);
	return 1;
}
/*******************************************************
 **********read buf[256]: ef:ab [???????]??oo????  ********
 *******************************************************/
int goodix_read_regs(char * buf)
{
	int ret = -1;
	u8 cfg[GTP_CONFIG_MAX_LENGTH] = { 0 };

	GTP_DEBUG_FUNC();

	ret = gt1x_i2c_read(GTP_REG_CONFIG_DATA, cfg, GTP_CONFIG_MAX_LENGTH);
	if( ret!= 0 )
	{
		GTP_ERROR("Read config version failed.\n");
		return ret;
	}

	for(ret=0; ret<GTP_CONFIG_MAX_LENGTH; ret++)
	{
		printk("reg%d = 0x%x\n", ret, cfg[ret]);
	}	

	return 1;
}
/*******************************************************
 **********write buf[256]: ef:ab [???????]??oo???? ********
 *******************************************************/
int goodix_write_regs(const char * buf)
{		
	GTP_DEBUG("enter %s\n",__FUNCTION__);
	return 1;
}
/*******************************************************
 ***************tw debug on or off *************
 *******************************************************/
int goodix_debug(int val)
{
	GTP_INFO("enter %s\n",__FUNCTION__);
	if(val)
		GTP_DEBUG_ON=1;
	else
		GTP_DEBUG_ON=0;
	return 1;
}
/*******************************************************
 ********************tw vendor**********************
 *******************************************************/
int goodix_vendor(char*  vendor)
{
	return sprintf(vendor, "%s", "goodix");
}
/*******************************************************
 *******get slide wakeup current setting gesture*******
 *******************************************************/
int goodix_get_wakeup_gesture(char*  gesture)
{
#if GTP_SLIDE_WAKEUP
	int i;
	char wakeup_gesture[64]={0};

	if(support_gesture == 0)
		return sprintf(gesture, "%s", "none");

	for(i=0; i<ARRAY_SIZE(tp_slide_wakeup); i++)
	{
		if(support_gesture & tp_slide_wakeup[i].mask)
		{
			if(wakeup_gesture[0] != 0)
				sprintf(wakeup_gesture + strlen(wakeup_gesture), "%s", ",");
			sprintf(wakeup_gesture + strlen(wakeup_gesture) , "%s", tp_slide_wakeup[i].gesture);
		}
	}

	return sprintf(gesture, "%s", wakeup_gesture);	
#endif

	return 1;
}
/*******************************************************
 *************** system set wakeup gesture*************
 *******************************************************/
int goodix_gesture_ctrl(const char*  gesture_buf)
{
#if GTP_SLIDE_WAKEUP
	char *gesture;
	int buf_len;
	char *gesture_p;
	char *temp_p;
	char tmp_buf[32]={0};
	int i;
	s8 ret = -1;
	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);

	buf_len = strlen(gesture_buf);

	GTP_DEBUG("%s buf_len = %d.", __func__, buf_len);
	printk("%s gesture_buf:%s.\n", __func__, gesture_buf);

	gesture_p = kzalloc(buf_len + 1, GFP_KERNEL);
	if(gesture_p == NULL) {
		GTP_ERROR("%s: alloc mem error.", __func__);
		return -1;
	}
	temp_p = gesture_p;
	strlcpy(gesture_p, gesture_buf, buf_len + 1);

	mutex_lock(&gtp_mutex);
	while(gesture_p) {	
		gesture = strsep(&gesture_p, ",");
		GTP_DEBUG("%s gesture:%s.", __func__, gesture);

		for(i=0; i<ARRAY_SIZE(tp_slide_wakeup); i++)
		{
			memset(tmp_buf, 0, 32);
			strcpy(tmp_buf, tp_slide_wakeup[i].gesture);
			if(!strncmp(gesture, tmp_buf, strlen(tmp_buf))){
				if(!strncmp(gesture+strlen(tmp_buf), "=true", 5)) {
					GTP_DEBUG("%s: enable up slide wakeup func.", tmp_buf);
					support_gesture |= tp_slide_wakeup[i].mask;
					break;
				} else if(!strncmp(gesture+strlen(tmp_buf), "=false", 6)) {
					GTP_DEBUG("%s: disable up slide wakeup func.", tmp_buf);
					support_gesture &= ~tp_slide_wakeup[i].mask;				
					break;
				}
			}	
		}
	}

	GTP_DEBUG("%s: gtp_is_suspend=%d\n", __func__, ts->gtp_is_suspend);
	if (ts->gtp_is_suspend && ts->gtp_is_sleep_suspend){
		if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
		{
			if (ts->use_irq)
			{
				gtp_irq_disable(ts);
			}
			gtp_reset_guitar(ts->client, 20);
			if (ts->use_irq)
			{
				gtp_irq_enable(ts);
			}
			ret = gtp_enter_doze(ts);
			if (ret < 0) {
				printk("%s: gtp enter doze failed, ret=%d\n", __func__, ret);
			} else {
				printk("%s: gtp enter doze success\n", __func__);
			}
		} else {
			ts->gtp_is_sleep_suspend=1;
			if (ts->use_irq)
			{
				gtp_irq_disable(ts);
			}
			gtp_reset_guitar(ts->client, 20);
			if (ts->use_irq)
			{
				gtp_irq_enable(ts);
			}
			if (ts->use_irq)
			{
				gtp_irq_disable(ts);
			} else {
				hrtimer_cancel(&ts->timer);
			}
			ret = gtp_enter_sleep(ts);
			if (ret < 0) {
				printk("%s: gtp enter sleep failed, ret=%d\n", __func__, ret);
			} else {
				printk("%s: gtp enter sleep success\n", __func__);
			}
			gtp_enable_irq_wake(ts, 0);

			if(ts->pdata->suspend)
				ts->pdata->suspend();
		}
	}
	mutex_unlock(&gtp_mutex);
	kfree(temp_p);
#endif

	return 1;
}

touchscreen_ops_tpye goodix_ops=
{
	.touch_id					= 0,		
	.touch_type			        = 1,
	.active						= goodix_active,
	.firmware_need_update		= goodix_firmware_need_update,
	.firmware_do_update			= goodix_firmware_do_update,
	.need_calibrate				= goodix_need_calibrate,
	.calibrate					= goodix_calibrate,
	.get_firmware_version		= goodix_get_firmware_version,
	.reset_touchscreen			= goodix_reset_touchscreen,
	.get_mode					= goodix_get_mode,
	.set_mode					= goodix_set_mode,
	.get_oreitation				= goodix_get_oreitation,
	.set_oreitation				= goodix_set_oreitation,
	.read_regs					= goodix_read_regs,
	.write_regs					= goodix_write_regs,
	.debug						= goodix_debug,
	.get_vendor					= goodix_vendor,
	.get_wakeup_gesture			= goodix_get_wakeup_gesture,
	.gesture_ctrl				= goodix_gesture_ctrl,

};

#ifdef CONFIG_OF
static struct regulator *ts_vdd;
static struct regulator *ts_vdd_io;

static unsigned int gtp_gpio_reset;
static unsigned int gtp_gpio_irq;
static unsigned int gtp_gpio_vcc_i2c_en;
static unsigned int gtp_pwr_en=0;

static int goodix_pinctrl_init(struct goodix_ts_data *ts)
{
	int retval;

	/* Get pinctrl if target uses pinctrl */
	ts->ts_pinctrl = devm_pinctrl_get(&(ts->client->dev));
	if (IS_ERR_OR_NULL(ts->ts_pinctrl)) {
		dev_dbg(&ts->client->dev,
				"Target does not use pinctrl\n");
		retval = PTR_ERR(ts->ts_pinctrl);
		ts->ts_pinctrl = NULL;
		return retval;
	}

	ts->gpio_state_active
		= pinctrl_lookup_state(ts->ts_pinctrl, "pmx_ts_active");
	if (IS_ERR_OR_NULL(ts->gpio_state_active)) {
		dev_dbg(&ts->client->dev,
				"Can not get ts default pinstate\n");
		retval = PTR_ERR(ts->gpio_state_active);
		ts->ts_pinctrl = NULL;
		return retval;
	}

	ts->gpio_state_suspend
		= pinctrl_lookup_state(ts->ts_pinctrl, "pmx_ts_suspend");
	if (IS_ERR_OR_NULL(ts->gpio_state_suspend)) {
		dev_dbg(&ts->client->dev,
				"Can not get ts sleep pinstate\n");
		retval = PTR_ERR(ts->gpio_state_suspend);
		ts->ts_pinctrl = NULL;
		return retval;
	}

	return 0;
}

static int goodix_pinctrl_select(struct goodix_ts_data *ts,
		bool on)
{
	struct pinctrl_state *pins_state;
	int ret;

	pins_state = on ? ts->gpio_state_active
		: ts->gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pins_state)) {
		ret = pinctrl_select_state(ts->ts_pinctrl, pins_state);
		if (ret) {
			dev_err(&ts->client->dev,
					"can not set %s pins\n",
					on ? "pmx_ts_active" : "pmx_ts_suspend");
			return ret;
		}
	} else
		dev_err(&ts->client->dev,
				"not a valid '%s' pinstate\n",
				on ? "pmx_ts_active" : "pmx_ts_suspend");

	return 0;
}

static int goodix_power_init(bool on)
{
	int rc;

	if(!gtp_pwr_en)
		return 0;


	if (!on)
		goto gtp_pwr_deinit;

	ts_vdd = regulator_get(&i2c_connect_client->dev, "vdd_ana");
	if (IS_ERR(ts_vdd)) {
		rc = PTR_ERR(ts_vdd);
		pr_err("Touchscreen vdd Regulator ts_vdd get failed rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(ts_vdd) > 0) {
		rc = regulator_set_voltage(ts_vdd, 2800000,2800000);
		if (rc) {
			pr_err("Touchscreen Regulator vdd set failed, rc=%d\n", rc);
			goto gtp_reg_vdd_put;
		}
	}

	ts_vdd_io = regulator_get(&i2c_connect_client->dev,"vcc_i2c");
	if (IS_ERR(ts_vdd_io)) {
		rc = PTR_ERR(ts_vdd_io);
		pr_err("Touchscreen vdd_io Regulator ts_vdd_io get failed, rc=%d\n",rc);
		goto gtp_reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(ts_vdd_io) > 0) {
		rc = regulator_set_voltage(ts_vdd_io, 1800000,1800000);
		if (rc) {
			pr_err("Touchscreen Regulator vdd_io set voltage failed, rc=%d\n", rc);
			goto gtp_reg_vcc_i2c_put;
		}
	}

	return 0;

gtp_reg_vcc_i2c_put:
	regulator_put(ts_vdd_io);
gtp_reg_vdd_set_vtg:
	if (regulator_count_voltages(ts_vdd) > 0)
		regulator_set_voltage(ts_vdd, 0, 3300000);
gtp_reg_vdd_put:
	regulator_put(ts_vdd);
	return rc;

gtp_pwr_deinit:
	if (regulator_count_voltages(ts_vdd) > 0)
		regulator_set_voltage(ts_vdd, 0, 3300000);

	regulator_put(ts_vdd);

	if (regulator_count_voltages(ts_vdd_io) > 0)
		regulator_set_voltage(ts_vdd_io, 0, 1800000);

	regulator_put(ts_vdd_io);

	return 0;
}

static int goodix_ts_power(int on)
{
	int rc;

	if(!gtp_pwr_en)
		return 0;

	if (!on)
		goto goodix_power_off;

	rc = regulator_enable(ts_vdd);
	if (rc) {
		pr_err("Touchscreen Regulator vdd enable failed rc=%d\n", rc);
		return rc;
	}

	rc = regulator_enable(ts_vdd_io);
	if (rc) {
		pr_err("Touchscreen Regulator vdd_i2c enable failed rc=%d\n", rc);
		regulator_disable(ts_vdd);
	}

	gpio_set_value(gtp_gpio_vcc_i2c_en,1);

	return rc;

goodix_power_off:
	rc = regulator_disable(ts_vdd);
	if (rc) {
		pr_err("Touchscreen Regulator vdd disable failed rc=%d\n", rc);
		return rc;
	}

	rc = regulator_disable(ts_vdd_io);
	if (rc) {
		pr_err("Touchscreen Regulator vdd_i2c disable failed rc=%d\n", rc);
		rc = regulator_enable(ts_vdd);
	}

	return rc;
}

static int goodix_ts_gpio_init(bool on)
{
	int ret = 0;

	if(!on)
		goto gtp_gpio_deinit;

	/* touchscreen reset gpio request and init */
	if (gpio_is_valid(gtp_gpio_reset)) {
		ret = gpio_request(gtp_gpio_reset, "goodix-ts-reset");
		if (ret){
			pr_err("TOUCH:%s:Failed to request GPIO %d\n",__func__, gtp_gpio_reset);
			return ret;
		}
		gpio_direction_output(gtp_gpio_reset, 1);
	} else {
		pr_err("TOUCH:%s:irq gpio not provided\n",__func__);
		return -1;
	}

	if (gpio_is_valid(gtp_gpio_vcc_i2c_en)) {
		ret = gpio_request(gtp_gpio_vcc_i2c_en, "goodix-ts-vcc-i2c-en");
		if (ret){
			pr_err("TOUCH:%s:Failed to request GPIO %d\n",__func__, gtp_gpio_vcc_i2c_en);
			return ret;
		}
		gpio_direction_output(gtp_gpio_vcc_i2c_en, 1);
	} else {
		pr_err("TOUCH:%s:irq gpio not provided\n",__func__);
		return -1;
	}

	if (gpio_is_valid(gtp_gpio_irq)) {
		ret = gpio_request(gtp_gpio_irq, "goodix-ts-irq");
		if (ret){
			pr_err("TOUCH:%s: Failed to request GPIO %d\n",__func__, gtp_gpio_irq);
			gpio_free(gtp_gpio_reset);		
			return ret;	
		}
		gpio_direction_input(gtp_gpio_irq);
	} else {
		pr_err("TOUCH:%s: reset gpio not provided\n",__func__);
		gpio_free(gtp_gpio_reset);
		return -1;
	}

	return 0;


gtp_gpio_deinit:
	gpio_free(gtp_gpio_reset);
	gpio_free(gtp_gpio_irq);

	return 0;
}

static int goodix_ts_hw_init(void)
{
	int ret = 0;

	ret = goodix_ts_gpio_init(1);
	if(ret)
	{
		pr_err("Touchscreen GPIO init failed!\n");
		return ret;
	}

	ret = goodix_power_init(1);	
	if(ret)
	{
		pr_err("Touchscreen Power init failed!\n");
		goodix_ts_gpio_init(0);
		return ret;
	}

	pr_debug("TOUCH: requst and init resource complete\n");

	return 0;	
}

static void goodix_ts_release(void)
{
	int ret = 0;

	ret = goodix_ts_gpio_init(0);
	if(ret)
	{
		pr_err("Touchscreen GPIO release failed!\n");
	}

	ret = goodix_power_init(0);	
	if(ret)
	{
		pr_err("Touchscreen Power release failed!\n");
	}

}

static int goodix_ts_reset(int ms)
{
	gpio_direction_output(gtp_gpio_irq, 0);
	gpio_direction_output(gtp_gpio_reset, 0);
	msleep(ms);

	//step2:select I2C slave addr,INT:0--0x5d;1--0x14.
	gpio_direction_output(gtp_gpio_irq, i2c_connect_client->addr == 0x14);
	mdelay(2);

	//end select I2C slave addr	
	gpio_direction_output(gtp_gpio_reset, 1);
	mdelay(10);                          //must > 6ms

	gpio_direction_input(gtp_gpio_reset);
	gpio_direction_output(gtp_gpio_irq, 0);
	msleep(50);
	gpio_direction_input(gtp_gpio_irq);

#if GTP_ESD_PROTECT
    gtp_init_ext_watchdog(i2c_connect_client);
#endif

	return 0;
}


static int goodix_ts_sleep(void)
{
	return 0;
}


static int goodix_ts_wakeup(void)
{
	return 0;
}

static int goodix_parse_config(struct device *dev, struct device_node *np,
		struct goodix_config_info *info)
{
	struct property *prop;
	unsigned char *temp_cfg;
#ifdef CONFIG_TOUCHSCREEN_GLOVE
	unsigned char *temp_glove_cfg;
#endif
#ifdef CONFIG_TOUCHSCREEN_WINDOW
	unsigned char *temp_window_cfg;
#endif

	prop = of_find_property(np, "goodix,config_normal", &info->config_length);
	if(!prop){
		GTP_ERROR("Looking up goodix,config property failed.\n");
		return -ENODEV;
	}else if(!info->config_length){
		GTP_ERROR("Invalid length of configuration data\n");
		return -EINVAL;
	}
	temp_cfg = devm_kzalloc(dev,
			info->config_length*sizeof(unsigned int),GFP_KERNEL);
	if(!temp_cfg){
		GTP_ERROR("Unable to allocate memory to store cfg\n");
		return -ENOMEM;
	}
	memcpy(temp_cfg, prop->value, info->config_length);
	info->config = temp_cfg;

#ifdef CONFIG_TOUCHSCREEN_GLOVE
	prop = of_find_property(np, "goodix,config_glove", &info->config_glove_length);
	if(!prop){
		GTP_ERROR("Looking up goodix,config glove property failed.\n");
		return -EINVAL;
	}else if(!info->config_glove_length){
		GTP_ERROR("Invalid length of glove config data\n");
		return -EINVAL;
	}

	temp_glove_cfg = devm_kzalloc(dev,
			info->config_glove_length*sizeof(unsigned int),GFP_KERNEL);
	if(!temp_glove_cfg){
		GTP_ERROR("Unable to allocate memory to store cfg\n");
		return -ENOMEM;
	}
	memcpy(temp_glove_cfg, prop->value, info->config_glove_length);
	info->config_glove = temp_glove_cfg;
#endif

#ifdef CONFIG_TOUCHSCREEN_WINDOW
	prop = of_find_property(np, "goodix,config_window", &info->config_window_length);
	if(!prop){
		GTP_ERROR("Looking up goodix,config window property failed.\n");
		return 0;
	}else if(!info->config_window_length){
		GTP_ERROR("Invalid length of window config data\n");
		return 0;
	}
	temp_window_cfg = devm_kzalloc(dev,
			info->config_window_length*sizeof(unsigned int),GFP_KERNEL);
	if(!temp_window_cfg){
		GTP_ERROR("Unable to allocate memory to store cfg\n");
		return -ENOMEM;
	}
	memcpy(temp_window_cfg, prop->value, info->config_window_length);
	info->config_window = temp_window_cfg;
#endif


	return 0;
}

static int goodix_ts_parse_dt(struct device *dev, struct tw_platform_data *pdata)
{	
	struct goodix_config_info *info;
	struct device_node *temp,*np = dev->of_node;
	int ret;
	enum of_gpio_flags flags;
	unsigned int res[4];

	/* reset, irq gpio info */
	ret = of_get_named_gpio_flags(np, "goodix,vcc_i2c_supply_en", 0, &flags);
	if (ret < 0){
		dev_err(dev, "Looking up %s property in node %s failed",
				"goodix,vcc_i2c_supply_en", np->full_name);
		return -ENODEV;
	}
	pdata->sleep_pwr_en = ret;
	GTP_DEBUG("vcc_i2c_supply_en = %d", pdata->sleep_pwr_en);

	ret = of_get_named_gpio_flags(np, "goodix,reset-gpio", 0, &flags);
	if (ret < 0){
		dev_err(dev, "Looking up %s property in node %s failed",
				"goodix,reset-gpio", np->full_name);
		return -ENODEV;
	}
	pdata->gpio_reset = ret;
	GTP_DEBUG("reset-gpio = %d", pdata->gpio_reset);

	ret = of_get_named_gpio_flags(np, "goodix,irq-gpio", 0, &flags);
	if (ret < 0) {
		dev_err(dev, "Looking up %s property in node %s failed",
				"goodix,irq-gpio", np->full_name);
		return -ENODEV;
	}
	pdata->gpio_irq = ret;
	GTP_DEBUG("irq-gpio = %d", pdata->gpio_irq);

	ret = of_property_read_u32(np, "goodix,irq_flags", (unsigned int *)&pdata->irqflag);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
				"goodix,irq_flags", np->full_name);
		return -ENODEV;
	}
	GTP_DEBUG("irq-flag = %d", (int)pdata->irqflag);

	ret = of_property_read_u32_array(np, "goodix,pixel", res, 2);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
				"goodix,pixel", np->full_name);
		return -ENODEV;
	}
	pdata->screen_x = res[0];
	pdata->screen_y = res[1];
	GTP_DEBUG("screen_x=%d, screen_y=%d", pdata->screen_x, pdata->screen_y);

	ret = of_property_read_u32_array(np, "goodix,pwr_ctrl", res, 1);
	if (ret) {
		GTP_ERROR("Looking up %s property in node %s failed",
				"goodix,pwr_ctrl", np->full_name);
		gtp_pwr_en = 0;
	}
	else
	{
		gtp_pwr_en = res[0];
	}
	GTP_DEBUG("gtp_pwr_en=%d", gtp_pwr_en);
#if 0
	ret = of_property_read_string(np, "vdd_ana-supply", &gtp_vdd);
	if (ret)
		GTP_ERROR("Looking up %s property in node %s failed",
				"goodix,vdd", np->full_name);

	ret = of_property_read_string(np, "vcc_i2c-supply", &gtp_vdd_io);
	if (ret)
		GTP_ERROR("Looking up %s property in node %s failed",
				"goodix,vddio", np->full_name);
#endif	
	ret = of_property_read_string(np, "goodix,ic_type", &TW_IC_PREFIX_NAME);
	if (ret)
	{
		TW_IC_PREFIX_NAME = "GT9XX";
		GTP_ERROR("Looking up %s property in node %s failed",
				"goodix,ic_type", np->full_name);
	}


/************* dtsi config parse start *********************/
	config_array_size = 0;
	temp = NULL;
	while((temp = of_get_next_child(np, temp)))
	{
		config_array_size++;
	}
	if(!config_array_size)
		return 0;

	info = devm_kzalloc(dev,config_array_size*
			sizeof(struct goodix_config_info),GFP_KERNEL);
	if(!info){
		GTP_ERROR("Unable to allocate memory\n");
		return -ENOMEM;
	}
	yl_cfg = info;

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	ret = of_property_read_u32(np, "goodix,charger_status_force_check", &info->charger_status_force_check);
	if (ret ) {
		GTP_DEBUG("Looking up goodix,charger_status_force_check failed");
	}
	GTP_DEBUG("charger_status_force_check=%d", info->charger_status_force_check);
#endif

#ifdef CONFIG_TOUCHSCREEN_WINDOW
	ret = of_property_read_u32_array(np, "goodix,cover", res, 4);
	if (ret ) {
		GTP_DEBUG("Looking up goodix,cover failed");
		return -ENODEV;
	}
	gt1xx_cover_window.win_x_min = res[0];
	gt1xx_cover_window.win_y_min = res[1];
	gt1xx_cover_window.win_x_max = res[2];
	gt1xx_cover_window.win_y_max = res[3];
	GTP_DEBUG("cover_window (%d, %d) (%d, %d)",res[0],res[1],res[2],res[3]);
#endif

	for_each_child_of_node(np,temp){
		ret = of_property_read_u32(temp, "goodix,vendor-id", (unsigned int *)&info->tp_id);
		if (ret) {
			dev_err(dev, "Looking up %s property in node %s failed",
					"goodix, vendor-id", np->full_name);
			return -ENODEV;
		}
		ret = of_property_read_string(temp, "goodix,vendor-name", &info->vendor_name);
		if (ret)
			GTP_ERROR("Looking up vendor_name property failed");

		ret = goodix_parse_config(dev,temp,info);
		if(ret){
			GTP_ERROR("Unable to parse config data\n");
			return ret;
		}
		info++;
	}
/************* dtsi config parse end *********************/

	pdata->init = goodix_ts_hw_init;
	pdata->release = goodix_ts_release;
	pdata->reset = goodix_ts_reset;
	pdata->power = goodix_ts_power;
	pdata->suspend = goodix_ts_sleep;
	pdata->resume = goodix_ts_wakeup;

	gtp_gpio_reset = pdata->gpio_reset;
	gtp_gpio_irq = pdata->gpio_irq;
	gtp_gpio_vcc_i2c_en = pdata->sleep_pwr_en;
	return 0;	
}
#endif
/****************************back_off_cfg_ver************/
static ssize_t goodix_back_off_cfg_ver_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{
	unsigned char cfg_version = 0;
	u16 check_sum = 0;
	unsigned char save_cfg_version = 0;
	u16 save_check_sum = 0;

	u8 retry = 0;
	s32 ret = -1;
	s32 i;
	struct goodix_ts_data *goodix_ts;

	goodix_ts = i2c_get_clientdata(i2c_connect_client);
	if(buf == NULL) {
		GTP_ERROR("buf is NULL!");
		return -ENOMEM;	
	}

	cfg_version = (unsigned char)simple_strtoul(buf, NULL, 10);
	GTP_DEBUG("cfg version = %d.",cfg_version);

	if(cfg_version < 'A' || cfg_version > 'Z' ) {
		cfg_version = 0x00;
	}

	save_cfg_version = config[0];	
	save_check_sum = (config[goodix_ts->gtp_cfg_len-3]<<8)|config[goodix_ts->gtp_cfg_len-2];

	config[0] = cfg_version;
	for (i = 0; i < goodix_ts->gtp_cfg_len-3; i+=2)
	{
		check_sum += (config[i] << 8) + config[i + 1];
	}
	check_sum = 0 - check_sum;
	GTP_DEBUG("Config check_sum: 0x%04X", check_sum);
	config[goodix_ts->gtp_cfg_len-3] = (check_sum >> 8) & 0xFF;
	config[goodix_ts->gtp_cfg_len-2] = check_sum & 0xFF;
	config[goodix_ts->gtp_cfg_len-1] = 1;

	if (goodix_ts->use_irq) {
		gtp_irq_disable(goodix_ts);
	}	
	while(retry++ < 3)
	{
		gtp_reset_guitar(goodix_ts->client, 20);
		ret = gtp_send_cfg(goodix_ts->client);
		if (ret > 0)
		{
			GTP_INFO("send new config success.");
			break;
		}
	}

	if (goodix_ts->use_irq) {
		gtp_irq_enable(goodix_ts);
	}

	config[0] = save_cfg_version;
	config[goodix_ts->gtp_cfg_len-3] = (save_check_sum>>8)&0xFF;	
	config[goodix_ts->gtp_cfg_len-2] = save_check_sum&0xFF;	
	return count;
}

static DEVICE_ATTR(back_off_cfg_ver,S_IRUGO|S_IWUSR, NULL, goodix_back_off_cfg_ver_store);

static struct attribute *goodix_attributes[] = {
	&dev_attr_back_off_cfg_ver.attr,
	NULL
};

static struct attribute_group goodix_attribute_group = {
	.attrs = goodix_attributes
};
/*******************************************************
Function:
Goodix touchscreen probe function.

Input:
client:	i2c device struct.
id:device id.

Output:
Executive outcomes. 0---succeed.
 *******************************************************/
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	s32 ret = -1;
	struct goodix_ts_data *ts=NULL;
	struct tw_platform_data *pdata=NULL;
	char tp_info[30] = {0};
	// struct pinctrl_state* set_state;

	GTP_DEBUG_FUNC();

	//do NOT remove these output log
	GTP_INFO("GTP Driver Version:%s",GTP_DRIVER_VERSION);
	//GTP_INFO("GTP Driver build@%s,%s", __TIME__,__DATE__);
	GTP_INFO("GTP I2C Address:0x%02x", client->addr);

	i2c_connect_client = client;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		GTP_ERROR("I2C check functionality failed.");
		return -ENODEV;
	}

#ifdef CONFIG_OF
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
				sizeof(struct tw_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		ret = goodix_ts_parse_dt(&client->dev, pdata);
		if (ret)
		{
			dev_err(&client->dev, "Failed to parse dt\n");
			devm_kfree(&client->dev, pdata);
			return ret;
		}
	} else
#endif
		pdata = client->dev.platform_data;

	if(!pdata)
	{
		GTP_ERROR("dev platform data is null.");
		return -ENODEV;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL)
	{
		GTP_ERROR("Alloc GFP_KERNEL memory failed.");
		return -ENOMEM;
	}

	memset(ts, 0, sizeof(*ts));
	//ts->gtp_rawdiff_mode = 0;
	ts->pdata = pdata;
	ts->abs_x_max = ts->pdata->screen_x;
	ts->abs_y_max = ts->pdata->screen_y;
	ts->client = client;
	ts->client->irq = gpio_to_irq(ts->pdata->gpio_irq);
	ts->fw_error = 0;
	if(!ts->pdata->init || ts->pdata->init() < 0)
	{
		GTP_ERROR("GTP request IO port failed.");
		goto exit_init_failed;
	}	

	if(ts->pdata->power)
	{
		ret = ts->pdata->power(1);
		if(ret < 0)
		{
			GTP_ERROR("GTP power on failed.");
			goto exit_power_failed;
		}					
	}

	if(ts->pdata->reset)
	{
		ret = ts->pdata->reset(20);
		if(ret < 0)
		{
			GTP_ERROR("GTP reset failed.");
			goto exit_reset_failed;
		}					
	}

	/* Get pinctrl if target uses pinctrl */
#if 0
	ts->ts_pinctrl = devm_pinctrl_get(&(client->dev));
	if (IS_ERR(ts->ts_pinctrl)) {
		if(PTR_ERR(ts->ts_pinctrl) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		pr_debug("Target does not use pinctrl\n");
		ts->ts_pinctrl = NULL;
	}
	pr_info("after pinctrl_get \n");
	if(ts->ts_pinctrl){
		set_state =
			pinctrl_lookup_state(ts->ts_pinctrl, "pmx_ts_active");
		if(IS_ERR(set_state)){
			pr_info("cannot get ts pinctrl active state\n");
			ret = pinctrl_select_state(ts->ts_pinctrl, set_state);
			if(ret){
				pr_info("cannot set ts pinctrl active state\n");
			}
		}
	}
#endif
	ret = goodix_pinctrl_init(ts);
	if (!ret && ts->ts_pinctrl) {
		ret = goodix_pinctrl_select(ts, true);
		if (ret < 0)
			goto exit_init_panel_failed;
	}

	spin_lock_init(&ts->irq_lock);
	INIT_WORK(&ts->work, goodix_ts_work_func);

#if GTP_ESD_PROTECT
	ts->clk_tick_cnt = GTP_ESD_CHECK_CIRCLE * HZ;      // HZ: clock ticks in 1 second generated by system
	GTP_DEBUG("Clock ticks for an esd cycle: %d", ts->clk_tick_cnt);
	spin_lock_init(&ts->esd_lock);
#endif
	i2c_set_clientdata(client, ts);

	ret = gtp_i2c_test(client);
	if (ret!=0)
	{
		GTP_ERROR("I2C communication ERROR!!");
	goto exit_i2c_test_failed;
	}

#if GTP_AUTO_UPDATE
	mutex_lock(&gtp_mutex);
	gt1x_update_firmware(NULL);
	mutex_unlock(&gtp_mutex);
#endif

	ret = gtp_init_panel(ts);
	if (ret < 0)
	{
		GTP_ERROR("GTP init panel failed .");
		ts->abs_x_max = ts->pdata->screen_x;
		ts->abs_y_max = ts->pdata->screen_y;
		ts->int_trigger_type = ts->pdata->irqflag;				
	}

	// beg yinchao add tp_info to factory mode @20140901
	sprintf(tp_info, "TW: %s %s Version 0x%x\n", TW_IC_PREFIX_NAME,
			yl_cfg[tp_index].vendor_name, yl_cfg[tp_index].config[0]);
	//get_device_info(tp_info);
	// end

	ret = gtp_request_input_dev(ts);
	if (ret < 0)
	{
		GTP_ERROR("GTP request input dev failed");
		goto exit_init_panel_failed;								
	}

	GTP_DEBUG("GTP ts->client->irq = %d",ts->client->irq);
	ret = gtp_request_irq(ts);
	if (ret < 0)
	{
		GTP_INFO("GTP works in polling mode.");
	}
	else
	{
		GTP_INFO("GTP works in interrupt mode.");
	}
	ret = gtp_read_fw_cfg_version();
	if (ret < 0)
	{
		GTP_ERROR("Read version failed.");
	}

	gtp_irq_enable(ts);

#if GTP_CREATE_WR_NODE
	gt1x_init_tool_node();
#endif

#if GTP_SLIDE_WAKEUP
	sprintf(wakeup_slide,"none");
#endif

	touchscreen_set_ops(&goodix_ops);

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	touch_register_charger_notify(&yl_chg_status_changed);
#endif

#if GTP_ESD_PROTECT
	gtp_esd_switch(client, SWITCH_ON);
#endif

	ret= sysfs_create_group(&client->dev.kobj, &goodix_attribute_group);
	if (0 != ret)
	{
		dev_err(&client->dev, "%s() - ERROR: sysfs_create_group() failed: %d\n", __FUNCTION__, ret);
		sysfs_remove_group(&client->dev.kobj, &goodix_attribute_group);
	}
	else
	{
		GTP_INFO("%s sysfs_create_group() success",__func__);
	}
	GTP_INFO("probe end...");
	return 0;

exit_init_panel_failed:
exit_i2c_test_failed:
exit_reset_failed:
exit_power_failed:
	if(ts->pdata->release)
		ts->pdata->release();
exit_init_failed:	
	if (ts->ts_pinctrl) {
		ret = goodix_pinctrl_select(ts, false);
		if (ret < 0)
			pr_err("Cannot get idle pinctrl state\n");
	}
#ifdef CONFIG_OF
	if(pdata)
		devm_kfree(&client->dev,pdata);
	if(yl_cfg){
		if(yl_cfg->config)
			devm_kfree(&client->dev,yl_cfg->config);
		if(yl_cfg->config_glove)
			devm_kfree(&client->dev,yl_cfg->config_glove);
		if(yl_cfg->config_window)
			devm_kfree(&client->dev,yl_cfg->config_window);
		devm_kfree(&client->dev,yl_cfg);
	}
#endif
	kfree(ts);
	return ret;
}
/*******************************************************
Function:
Goodix touchscreen driver release function.

Input:
client:	i2c device struct.

Output:
Executive outcomes. 0---succeed.
 *******************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	GTP_DEBUG_FUNC();

#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
#elif defined(CONFIG_FB)
	fb_unregister_client(&ts->fb_notif);
#endif

#if GTP_CREATE_WR_NODE
//	uninit_wr_node();
#endif

#if GTP_ESD_PROTECT
	destroy_workqueue(gtp_esd_check_workqueue);
#endif

	if (ts)
	{
		if (ts->use_irq)
		{
			free_irq(client->irq, ts);
		}
		else
		{
			hrtimer_cancel(&ts->timer);
		}
	}	

	GTP_INFO("GTP driver is removing...");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	input_free_device(ts->input_dev);
	if(ts->pdata->release)
		ts->pdata->release();
	kfree(ts);

	return 0;
}

/*******************************************************
Function:
Early suspend function.

Input:
h:early_suspend struct.

Output:
None.
 *******************************************************/
static int goodix_ts_disable(struct input_dev *in_dev)
{
	struct goodix_ts_data *ts;
	s8 ret = -1;	

	ts = i2c_get_clientdata(i2c_connect_client);
	GTP_DEBUG_FUNC();

	mutex_lock(&gtp_mutex);

	if(ts->gtp_is_suspend || ts->enter_update){
		mutex_unlock(&gtp_mutex);
		return 0;
	}

#ifdef YL_TW_DEBUG
	GTP_INFO("==goodix_ts_early_suspend==");
#endif
	ts->gtp_is_suspend = 1;
#if GTP_ESD_PROTECT
	gtp_esd_switch(ts->client, SWITCH_OFF);
#endif

#if GTP_SLIDE_WAKEUP
	sprintf(wakeup_slide,"none");

	if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
		ret = gtp_enter_doze(ts);
	else
#endif
	{
#if GTP_SLIDE_WAKEUP
		sprintf(wakeup_slide,"none");
#endif
		if (ts->use_irq)
		{
			gtp_irq_disable(ts);
		}
		else
		{
			hrtimer_cancel(&ts->timer);
		}
		ret = gtp_enter_sleep(ts);
	}
	mutex_unlock(&gtp_mutex);

	if (ret < 0)
	{
		GTP_ERROR("GTP early suspend failed.");
		return ret;
	}
	return ret;
}
/*******************************************************
Function:
Late resume function.

Input:
h:early_suspend struct.

Output:
None.
 *******************************************************/
static int goodix_ts_enable(struct input_dev *in_dev)
{
	struct goodix_ts_data *ts;
	s8 ret = -1;

	ts = i2c_get_clientdata(i2c_connect_client);

	GTP_DEBUG_FUNC();

	mutex_lock(&gtp_mutex);

	if(!ts->gtp_is_suspend || ts->enter_update){
		mutex_unlock(&gtp_mutex);
		return 0;
	}

#if GTP_SLIDE_WAKEUP
	doze_status = DOZE_DISABLED;
	gtp_enable_irq_wake(ts, 0);
#endif
	ret = gtp_wakeup_sleep(ts);
	if (ret < 0)
	{
		GTP_ERROR("GTP later resume failed.");
	}


	if (ts->use_irq)
	{
		gtp_irq_enable(ts);
	}
	else
	{
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

	ts->gtp_is_suspend = 0;
	mutex_unlock(&gtp_mutex);

#if GTP_ESD_PROTECT
	gtp_esd_switch(ts->client, SWITCH_ON);
#endif

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	yl_chg_status_changed();
#endif

	return ret;
}

#ifdef CONFIG_TP_I2C_TIMEOUT_WAIT
static int goodix_suspend(struct device *dev)
{
       tw_resume = 0;
       pr_err("%s: tw_resume = %d\n", __func__, tw_resume);
       return 0;
}

static int goodix_resume(struct device *dev)
{
       tw_resume = 1;
       wake_up_interruptible(&wait_tw_resume);
       pr_err("%s: tw_resume = %d\n", __func__, tw_resume);
       return 0;
}
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND)
static void goodix_ts_early_suspend(struct early_suspend *handler)
{
	struct goodix_ts_data *ts = container_of(handler, struct goodix_ts_data, early_suspend);
	goodix_ts_disable(ts->input_dev);
}
static void goodix_ts_late_resume(struct early_suspend *handler)
{
	struct goodix_ts_data *ts = container_of(handler, struct goodix_ts_data, early_suspend);
	goodix_ts_enable(ts->input_dev);
}
#endif

static int goodix_ts_suspend(struct device *dev)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);
	GTP_DEBUG_FUNC();
	return goodix_ts_disable(ts->input_dev);
}
static int goodix_ts_resume(struct device *dev)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);
	GTP_DEBUG_FUNC();
	return goodix_ts_enable(ts->input_dev);
}

#if defined(CONFIG_FB)
static void fb_notify_resume_work(struct work_struct *work)
{
        struct goodix_ts_data *goodix_data =
        container_of(work, struct goodix_ts_data, fb_notify_work);
        goodix_ts_resume(&goodix_data->client->dev);
}

static int fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct goodix_ts_data *ts_data =
		container_of(self, struct goodix_ts_data, fb_notif);
	GTP_DEBUG_FUNC();

	if (evdata && evdata->data && event == FB_EVENT_BLANK && ts_data &&
			ts_data->client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
			schedule_work(&ts_data->fb_notify_work); 
		else if (*blank == FB_BLANK_POWERDOWN) { 
			flush_work(&ts_data->fb_notify_work); 
			goodix_ts_suspend(&ts_data->client->dev);
		}
	}

	return 0;
}
#endif

static const struct dev_pm_ops goodix_pm_ops = {
#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND))
	.suspend	= goodix_ts_suspend,
	.resume	    = goodix_ts_resume,
#elif (defined(CONFIG_TP_I2C_TIMEOUT_WAIT))
        .suspend        = goodix_suspend,
        .resume     = goodix_resume,
#endif
};

 s32 gt1x_send_cmd(u8 cmd, u8 data)
{
	s32 ret;
	u8 buffer[3] = { cmd, data, 0 };

	buffer[2] = (u8) ((0 - cmd - data) & 0xFF);
	ret = gt1x_i2c_write(GTP_REG_CMD + 1, &buffer[1], 2);
	ret |= gt1x_i2c_write(GTP_REG_CMD, &buffer[0], 1);
	msleep(50);

	return ret;
}

 s32 gt1x_send_cmd_esd(u8 cmd, u8 data)
{
        s32 ret;
        u8 buffer[3] = { cmd, data, 0 };

        buffer[2] = (u8) ((0 - cmd - data) & 0xFF);
        ret = gt1x_i2c_write(GTP_REG_CMD + 1, &buffer[1], 2);
        ret |= gt1x_i2c_write(GTP_REG_CMD, &buffer[0], 1);
        //msleep(50);

        return ret;
}

#if GTP_ESD_PROTECT
/*******************************************************
Function:
switch on & off esd delayed work
Input:
client:  i2c device
on:      SWITCH_ON / SWITCH_OFF
Output:
void
 *********************************************************/
void gtp_esd_switch(struct i2c_client *client, s32 on)
{
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(client);
	spin_lock(&ts->esd_lock);

	if (SWITCH_ON == on)     // switch on esd
	{
		if (!ts->esd_running)
		{
			ts->esd_running = 1;
			spin_unlock(&ts->esd_lock);
			GTP_INFO("Esd started");
			queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work, ts->clk_tick_cnt);
		}
		else
		{
			spin_unlock(&ts->esd_lock);
		}
	}
	else    // switch off esd
	{
		if (ts->esd_running)
		{
			ts->esd_running = 0;
			spin_unlock(&ts->esd_lock);
			GTP_INFO("Esd cancelled");
			cancel_delayed_work_sync(&gtp_esd_check_work);
		}
		else
		{
			spin_unlock(&ts->esd_lock);
		}
	}
}

void gt1x_esd_switch(s32 state)
{
	gtp_esd_switch(i2c_connect_client, state);
}

/*******************************************************
Function:
Initialize external watchdog for esd protect
Input:
client:  i2c device.
Output:
result of i2c write operation.
1: succeed, otherwise: failed
 *********************************************************/
static s32 gtp_init_ext_watchdog(struct i2c_client *client)
{
	s32 ret;
	u8 value = 0xAA;
	GTP_DEBUG("Init external watchdog.");
	ret = gt1x_send_cmd(GTP_CMD_ESD, 0);
	ret |= gt1x_i2c_write(GTP_REG_ESD_CHECK, &value, 1);
	return ret;
}

/*******************************************************
Function:
Esd protect function.
External watchdog added by meta, 2013/03/07
Input:
work: delayed work
Output:
None.
 *******************************************************/
static void gtp_esd_check_func(struct work_struct *work)
{
	s32 i;
	s32 ret = -1;
	struct goodix_ts_data *ts = NULL;
	u8 esd_buf[4] = {0};

	GTP_DEBUG_FUNC();

	ts = i2c_get_clientdata(i2c_connect_client);

	mutex_lock(&gtp_mutex);
	if (ts->gtp_is_suspend || ts->enter_update)
	{
		GTP_INFO("Esd suspended or IC update firmware!");
		mutex_unlock(&gtp_mutex);
		return;
	}


	for (i = 0; i < 3; i++) {
		ret = gt1x_i2c_read(GTP_REG_CMD, esd_buf, 4);
		GTP_DEBUG("[Esd]0x8040 = 0x%02X, 0x8043 = 0x%02X", esd_buf[0], esd_buf[3]);
		if (!ret && esd_buf[0] != 0xAA && esd_buf[3] == 0xAA) {
			break;
		}
		msleep(50);
	}


	if (i < 3) {
		/* IC works normally, Write 0x8040 0xAA, feed the watchdog */
		//gt1x_send_cmd(GTP_CMD_ESD, 0);
		gt1x_send_cmd_esd(GTP_CMD_ESD, 0);
	} else {
		if(!ts->gtp_is_suspend && ts->esd_running){
			GTP_INFO("IC works abnormally! Process reset guitar.");
			memset(esd_buf, 0x01, sizeof(esd_buf));
			gt1x_i2c_write(0x4226, esd_buf, sizeof(esd_buf));
			msleep(50);
			if (ts->use_irq)
			{
				gtp_irq_disable(ts);
			}
			gtp_reset_guitar(ts->client, 50);
			if (ts->use_irq)
			{
				gtp_irq_enable(ts);
			}
			msleep(50);
			gtp_send_cfg(ts->client);
		} else {
			GTP_INFO("Esd protector suspended, no need reset!");
		}
	}

	if(!ts->gtp_is_suspend && ts->esd_running)
	{
		queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work, ts->clk_tick_cnt);
	}
	else
	{
		GTP_INFO("Esd suspended!");
	}

	mutex_unlock(&gtp_mutex);
	return;
}
#endif

static const struct i2c_device_id goodix_ts_id[] = {
	{ GTP_I2C_NAME, 0 },
	{ }
};

static struct of_device_id goodix_match_table[] = {
	{ .compatible = "Goodix,Goodix-TS",},
	{ },
};

static struct i2c_driver goodix_ts_driver = {
	.probe      = goodix_ts_probe,
	.remove     = goodix_ts_remove,
	.id_table   = goodix_ts_id,
	.driver = {
		.name     = GTP_I2C_NAME,
		.owner    = THIS_MODULE,
		.of_match_table = goodix_match_table,
#if defined(CONFIG_PM)
		.pm   = &goodix_pm_ops,
#endif
	},
};

/*******************************************************	
Function:
Driver Install function.
Input:
None.
Output:
Executive Outcomes. 0---succeed.
 ********************************************************/
static int __init goodix_ts_init(void)
{
	s32 ret;

	GTP_DEBUG_FUNC();	
	goodix_wq = create_singlethread_workqueue("goodix_wq");
	if (!goodix_wq)
	{
		GTP_ERROR("Creat workqueue failed.");
		return -ENOMEM;
	}
#if GTP_ESD_PROTECT
	INIT_DELAYED_WORK(&gtp_esd_check_work, gtp_esd_check_func);
	gtp_esd_check_workqueue = create_workqueue("gtp_esd_check");
#endif
	ret = i2c_add_driver(&goodix_ts_driver);
	return ret;
}
/*******************************************************	
Function:
Driver uninstall function.
Input:
None.
Output:
Executive Outcomes. 0---succeed.
 ********************************************************/
static void __exit goodix_ts_exit(void)
{
	GTP_DEBUG_FUNC();
	GTP_INFO("GTP driver exited.");
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
	{
		destroy_workqueue(goodix_wq);
	}
}

late_initcall(goodix_ts_init);
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("GTP Series Driver");
MODULE_LICENSE("GPL");
