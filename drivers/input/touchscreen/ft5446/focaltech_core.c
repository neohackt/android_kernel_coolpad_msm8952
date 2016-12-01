/*
 *
 * FocalTech fts TouchScreen driver.
 * 
 * Copyright (c) 2010-2015, Focaltech Ltd. All rights reserved.
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
 * VERSION      	DATE			AUTHOR
 *    1.0		       2014-09			mshl
 *
 */

 /*******************************************************************************
*
* File Name: focaltech.c
*
* Author: mshl
*
* Created: 2014-09
*
* Modify by mshl on 2015-04-30
*
* Abstract:
*
* Reference:
*
*******************************************************************************/
/*******************************************************************************
* Included header files
*******************************************************************************/
//user defined include header files
#include "focaltech_core.h"



#if FTS_UPGRADE_EN
#include "focaltech_firmware.h"
#endif

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>

#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
/* Early-suspend level */
#define FTS_SUSPEND_LEVEL 1
#endif


/*******************************************************************************
* Private constant and macro definitions using #define
*******************************************************************************/
#define FTS_META_REGS		3
#define FTS_ONE_TCH_LEN		6
#define FTS_TCH_LEN(x)		(FTS_META_REGS + FTS_ONE_TCH_LEN * x)

#define FTS_PRESS		0x7F
#define FTS_MAX_ID		0x0F
#define FTS_TOUCH_X_H_POS	3
#define FTS_TOUCH_X_L_POS	4
#define FTS_TOUCH_Y_H_POS	5
#define FTS_TOUCH_Y_L_POS	6
#define FTS_TOUCH_POINT_NUM		2
#define FTS_TOUCH_EVENT_POS	3
#define FTS_TOUCH_ID_POS		5

#define FTS_TOUCH_DOWN		0
#define FTS_TOUCH_UP		1
#define FTS_TOUCH_CONTACT	2

#define POINT_READ_BUF	(3 + FTS_ONE_TCH_LEN * FTS_MAX_POINTS)

/*register address*/
#define FTS_REG_DEV_MODE		0x00
#define FTS_DEV_MODE_REG_CAL	0x02

#define FTS_REG_PMODE		0xA5

#define FTS_REG_POINT_RATE	0x88
#define FTS_REG_THGROUP		0x80

/* power register bits*/
#define FTS_PMODE_ACTIVE		0x00
#define FTS_PMODE_MONITOR	0x01
#define FTS_PMODE_STANDBY	0x02
#define FTS_PMODE_HIBERNATE	0x03

#define FTS_STATUS_NUM_TP_MASK	0x0F

#define FTS_VTG_MIN_UV		2600000
#define FTS_VTG_MAX_UV		3300000
#define FTS_I2C_VTG_MIN_UV		1800000
#define FTS_I2C_VTG_MAX_UV		1800000

#define FTS_COORDS_ARR_SIZE	4
#define MAX_BUTTONS		4

#define FTS_8BIT_SHIFT		8
#define FTS_4BIT_SHIFT		4

#define PINCTRL_STATE_ACTIVE	"pmx_ts_active"
#define PINCTRL_STATE_SUSPEND	"pmx_ts_suspend"
#define PINCTRL_STATE_RELEASE	"pmx_ts_release"

/*******************************************************************************
* Private enumerations, structures and unions using typedef
*******************************************************************************/

/*******************************************************************************
* Static variables
*******************************************************************************/
static u16 fts_touch_key[] = {KEY_APPSELECT, KEY_HOMEPAGE, KEY_BACK};
#define FTS_TOUCH_KEY_NUM (sizeof(fts_touch_key)/sizeof(fts_touch_key[0]))

/*******************************************************************************
* Global variable or extern global variabls/functions
*******************************************************************************/
struct i2c_client *fts_i2c_client;
struct fts_ts_data *fts_wq_data;
struct input_dev *fts_input_dev;

u8 buf_touch_data[30*POINT_READ_BUF] = { 0 };

//yulong add gesture
int doze_status = DOZE_DISABLED;
u32 support_gesture = TW_SUPPORT_NONE_SLIDE_WAKEUP;
/*******************************************************************************
* Static function prototypes
*******************************************************************************/
static int fts_ts_start(struct device *dev);
static int fts_ts_stop(struct device *dev);

//yulong add
void delay_qt_ms(unsigned long  w_ms)
{
    unsigned long i;
    unsigned long j;

    for (i = 0; i < w_ms; i++)
    {
        for (j = 0; j < 1000; j++)
        {
            udelay(1);
        }
    }
} 


/* yulong touchscreen ops start */
int fts_active(void)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_firmware_need_update(void)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 0;
}

int fts_firmware_do_update(void)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_need_calibrate(void)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_calibrate(void)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_get_firmware_version(char *version)
{
    int ret = 0;

    u8 fts_dev_id = 0x00;
    u8 fts_fw_ver = 0xff;
    u8 fts_fw_vendor_id = 0;

    char TP_IC_NAME[20] = {0};
    char TP_VENDOR[20] = {0};

    /* 1.read device id */
    ret = fts_read_reg(fts_i2c_client, FTS_REG_ID, &fts_dev_id);
    if (ret < 0)
        dev_err(&fts_i2c_client->dev, "read device id fail, err = %d.", ret);

    /* 2.read fw version */
    ret = fts_read_reg(fts_i2c_client, FTS_REG_FW_VER, &fts_fw_ver);
    if (ret < 0)
        dev_err(&fts_i2c_client->dev, "read fw version fail, err = %d.", ret);

    /* 3.read vendor id */
    ret = fts_read_reg(fts_i2c_client, FTS_REG_FW_VENDOR_ID, &fts_fw_vendor_id);
    if (ret < 0)
        dev_err(&fts_i2c_client->dev, "read fw vendor id fail, err = %d.", ret);

    switch (fts_dev_id)
    {
        case 0x54: sprintf(TP_IC_NAME, "%s", "ft5446"); break;
        default  : sprintf(TP_IC_NAME, "%s", "unknow"); break;
    }

    switch (fts_fw_vendor_id)
    {
        case 0x51: sprintf(TP_VENDOR, "%s", "ofilm" ); break;
        case 0x80: sprintf(TP_VENDOR, "%s", "yeji"  ); break;
        case 0x3B: sprintf(TP_VENDOR, "%s", "boen"  ); break;
        default  : sprintf(TP_VENDOR, "%s", "unknow"); break;
    }

    return sprintf(version, "%s:%s:0x%2x", TP_IC_NAME, TP_VENDOR, fts_fw_ver);
}

int fts_reset_touchscreen(void)
{
    printk(KERN_INFO "enter %s\n", __func__);

    /*write 0xaa to register 0xfc*/
    fts_write_reg(fts_i2c_client,0xfc,0xaa);
    delay_qt_ms(50);
    /*write 0x55 to register 0xfc*/
    fts_write_reg(fts_i2c_client,0xfc,0x55);
    printk(KERN_ERR "[FTS]: Reset CTPM ok!\n");
    return 1;
}

touch_mode_type fts_get_mode(void)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_set_mode(touch_mode_type work_mode)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

touch_oreitation_type fts_get_oreitation(void)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_set_oreitation(touch_oreitation_type oreitate)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_read_regs(char *buf)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_write_regs(const char *buf)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_debug(int val)
{
    printk(KERN_INFO "%s nothing to do.\n", __func__);

    return 1;
}

int fts_vendor(char *vendor)
{
    printk(KERN_INFO "%s: focaltech.\n", __func__);

    return sprintf(vendor, "%s", "focaltech");
}


touchscreen_ops_tpye fts_ops=
{
    .touch_id                   = 0,
    .touch_type                 = 1,
    .active                     = fts_active,
    .firmware_need_update       = fts_firmware_need_update,
    .firmware_do_update         = fts_firmware_do_update,
    .need_calibrate             = fts_need_calibrate,
    .calibrate                  = fts_calibrate,
    .get_firmware_version       = fts_get_firmware_version,
    .reset_touchscreen          = fts_reset_touchscreen,
    .get_mode                   = fts_get_mode,
    .set_mode                   = fts_set_mode,
    .get_oreitation             = fts_get_oreitation,
    .set_oreitation             = fts_set_oreitation,
    .read_regs                  = fts_read_regs,
    .write_regs                 = fts_write_regs,
    .debug                      = fts_debug,
    .get_vendor                 = fts_vendor,
    .get_wakeup_gesture         = fts_get_wakeup_gesture,
    .gesture_ctrl               = fts_gesture_ctrl,
};

/* yulong touchscreen ops end */

/*******************************************************************************
*  Name: fts_i2c_read
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
int fts_i2c_read(struct i2c_client *client, char *writebuf, int writelen, char *readbuf, int readlen)
{
	int ret;

	mutex_lock(&i2c_rw_access);
	
	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
				 .addr = client->addr,
				 .flags = 0,
				 .len = writelen,
				 .buf = writebuf,
			 },
			{
				 .addr = client->addr,
				 .flags = I2C_M_RD,
				 .len = readlen,
				 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret < 0)
			dev_err(&client->dev, "%s: i2c read error.\n", __func__);
	} else {
		struct i2c_msg msgs[] = {
			{
				 .addr = client->addr,
				 .flags = I2C_M_RD,
				 .len = readlen,
				 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			dev_err(&client->dev, "%s:i2c read error.\n", __func__);
	}

	mutex_unlock(&i2c_rw_access);
	
	return ret;
}

/*******************************************************************************
*  Name: fts_i2c_write
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
int fts_i2c_write(struct i2c_client *client, char *writebuf, int writelen)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			 .addr = client->addr,
			 .flags = 0,
			 .len = writelen,
			 .buf = writebuf,
		 },
	};
	mutex_lock(&i2c_rw_access);
	ret = i2c_transfer(client->adapter, msgs, 1);
	if (ret < 0)
		dev_err(&client->dev, "%s: i2c write error.\n", __func__);

	mutex_unlock(&i2c_rw_access);
	
	return ret;
}

/*******************************************************************************
*  Name: fts_write_reg
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
int fts_write_reg(struct i2c_client *client, u8 addr, const u8 val)
{
	u8 buf[2] = {0};

	buf[0] = addr;
	buf[1] = val;

	return fts_i2c_write(client, buf, sizeof(buf));
}

/*******************************************************************************
*  Name: fts_read_reg
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
int fts_read_reg(struct i2c_client *client, u8 addr, u8 *val)
{
	return fts_i2c_read(client, &addr, 1, val, 1);
}

/*******************************************************************************
*  Name: fts_ts_interrupt
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static irqreturn_t fts_ts_interrupt(int irq, void *dev_id)
{
	struct fts_ts_data *fts_ts = dev_id;

	disable_irq_nosync(fts_ts->client->irq);

	if (!fts_ts) {
		pr_err("%s: Invalid fts_ts\n", __func__);
		return IRQ_HANDLED;
	}
	queue_work(fts_ts->ts_workqueue, &fts_ts->touch_event_work);

	return IRQ_HANDLED;
}

/*******************************************************************************
*  Name: fts_touch_irq_work
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static void fts_touch_irq_work(struct work_struct *work)
{
    int i = 0;
    int ret = -1;
    
    u8 key_index = 0;
    u8 buf[POINT_READ_BUF] = {0};

    struct i2c_client *client = fts_wq_data->client;
    struct input_dev *input_dev = fts_wq_data->input_dev;
    struct ts_event *event = &fts_wq_data->event;

#if FTS_GESTRUE_EN
     if (DOZE_ENABLED == doze_status)
       {
          FTS_DBG("enter doze_status !%s , %d\n",__func__, __LINE__);
	  ret =fts_read_Gestruedata();
          enable_irq(client->irq);
          return;
      }

#endif
    /* read data from ft5446 */
    ret = fts_i2c_read(client, buf, 1, buf, POINT_READ_BUF);
    if (ret < 0)
    {
        dev_err(&client->dev, "%s read touchdata failed.\n", __func__);

        return;
    }

    /* parse data */
    memset(event, 0, sizeof(struct ts_event));

    event->point_num = buf[FTS_TOUCH_POINT_NUM] & 0x0f;
    event->pressure = FTS_PRESS;
    event->touch_point = 0;

    for (i = 0; i < FTS_MAX_POINTS; i++)
    {
        event->au8_finger_id[i] = (buf[FTS_TOUCH_ID_POS + FTS_ONE_TCH_LEN * i]) >> 4;

        if (event->au8_finger_id[i] >= FTS_MAX_ID)
            break;
        else
        {
            event->touch_point++;

            event->au16_x[i] = (s16)(buf[FTS_TOUCH_X_H_POS + FTS_ONE_TCH_LEN * i] & 0x0f) << 8 |
                               (s16)(buf[FTS_TOUCH_X_L_POS + FTS_ONE_TCH_LEN * i]);

            event->au16_y[i] = (s16)(buf[FTS_TOUCH_Y_H_POS + FTS_ONE_TCH_LEN * i] & 0x0f) << 8 |
                               (s16)(buf[FTS_TOUCH_Y_L_POS + FTS_ONE_TCH_LEN * i]);
        
            event->au8_touch_event[i] = (buf[FTS_TOUCH_EVENT_POS + FTS_ONE_TCH_LEN * i]) >> 6;
    
            /* handle error state */
            if ((event->point_num == 0) && (event->au8_touch_event[i] == FTS_TOUCH_DOWN || event->au8_touch_event[i] == FTS_TOUCH_CONTACT))
            {
                dev_err(&client->dev, "%s: touch down or contact illegal, event->point_num = 0.\n", __func__);

                goto exit_error_state;
            }

            // print debug info
            if (0)
            {
                dev_err(&client->dev ,"finger_id = %d, x = %d, y = %d, event = %d\n",
                    event->au8_finger_id[i], event->au16_x[i], event->au16_y[i], event->au8_touch_event[i]);
            }
        }
    }

    /* report TOUCH_KEY */
    if (event->au16_y[0] == 2000)
    {
        if (event->touch_point == 1)
        {
            if (event->au16_x[0] >= 235 && event->au16_x[0] <= 305)      // 270 +-35
                key_index = 0;
            else if (event->au16_x[0] >= 505 && event->au16_x[0] <= 575) // 540 +-35
                key_index = 1;
            else if (event->au16_x[0] >= 775 && event->au16_x[0] <= 845) // 810 +-35
                key_index = 2;
            else
            {
                dev_err(&client->dev, "%s: TOUCH_KEY x = %d, illegal.\n", __func__, event->au16_x[0]);
        
                for (i = 0; i < FTS_TOUCH_KEY_NUM; i++)
                    input_report_key(input_dev, fts_touch_key[i], 0);
            }

            if (event->au8_touch_event[0] == FTS_TOUCH_DOWN || event->au8_touch_event[0] == FTS_TOUCH_CONTACT)
                input_report_key(input_dev, fts_touch_key[key_index], 1);
            else
            {
                for (i = 0; i < FTS_TOUCH_KEY_NUM; i++)
                    input_report_key(input_dev, fts_touch_key[i], 0);
            }

            goto exit_touch_key;
        }
        else
            goto exit_error_state;
    }

    /* report POINTS */
    if (event->point_num > 0)
    {
        for (i = 0; i < event->touch_point; i++)
        {
            if (event->au8_touch_event[i] == FTS_TOUCH_DOWN || event->au8_touch_event[i] == FTS_TOUCH_CONTACT)
            {
                input_report_key(input_dev, BTN_TOUCH, 1);
                input_report_abs(input_dev, ABS_MT_TRACKING_ID, event->au8_finger_id[i]);
                input_report_abs(input_dev, ABS_MT_POSITION_X, event->au16_x[i]);
                input_report_abs(input_dev, ABS_MT_POSITION_Y, event->au16_y[i]);
                input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
                input_report_abs(input_dev, ABS_MT_WIDTH_MAJOR, event->pressure);
                input_mt_sync(input_dev);
            }
        }
    }
    else
    {
        input_report_key(input_dev, BTN_TOUCH, 0);
        input_mt_sync(input_dev);
    }

exit_touch_key:
    input_sync(input_dev);
exit_error_state:
	enable_irq(client->irq);
}

static int fts_power_on_sequence_start(struct fts_ts_data *data)
{
    int err = 0;

    if (gpio_is_valid(data->pdata->irq_gpio)) {
        err = gpio_request(data->pdata->irq_gpio, "fts_irq_gpio");
        if (err) {
            dev_err(&data->client->dev, "irq gpio request failed");
            goto err_irq_gpio_req;
        }

        err = gpio_direction_input(data->pdata->irq_gpio);
        if (err) {
            dev_err(&data->client->dev, "set_direction for irq gpio failed\n");
            goto err_irq_gpio_dir;
        }
    }

    if (gpio_is_valid(data->pdata->reset_gpio)) {
        err = gpio_request(data->pdata->reset_gpio, "fts_reset_gpio");
        if (err) {
            dev_err(&data->client->dev, "reset gpio request failed");
            goto err_irq_gpio_dir;
        }

        err = gpio_direction_output(data->pdata->reset_gpio, 0);
        if (err) {
            dev_err(&data->client->dev, "set_direction for reset gpio failed\n");
            goto err_reset_gpio_dir;
        }

        msleep(1);
    }

    return 0;

err_reset_gpio_dir:
    if (gpio_is_valid(data->pdata->reset_gpio))
        gpio_free(data->pdata->reset_gpio);

err_irq_gpio_dir:
    if (gpio_is_valid(data->pdata->irq_gpio))
        gpio_free(data->pdata->irq_gpio);

err_irq_gpio_req:

    return err;
}

static int fts_power_on_sequence_end(struct fts_ts_data *data)
{
    int err = 0;

    if (gpio_is_valid(data->pdata->reset_gpio)) {
        err = gpio_direction_output(data->pdata->reset_gpio, 1);
        if (err) {
            dev_err(&data->client->dev, "set_direction for reset gpio failed\n");
            goto err_reset_gpio_dir;
        }

        msleep(data->pdata->hard_rst_dly);
    }

    return 0;

err_reset_gpio_dir:
    if (gpio_is_valid(data->pdata->reset_gpio))
        gpio_free(data->pdata->reset_gpio);
    if (gpio_is_valid(data->pdata->irq_gpio))
        gpio_free(data->pdata->irq_gpio);

    return err;
}

/*******************************************************************************
*  Name: fts_gpio_configure
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_gpio_configure(struct fts_ts_data *data, bool on)
{
	int err = 0;

	if (on) {
		if (gpio_is_valid(data->pdata->irq_gpio)) {
			err = gpio_request(data->pdata->irq_gpio,
						"fts_irq_gpio");
			if (err) {
				dev_err(&data->client->dev,
					"irq gpio request failed");
				goto err_irq_gpio_req;
			}

			err = gpio_direction_input(data->pdata->irq_gpio);
			if (err) {
				dev_err(&data->client->dev,
					"set_direction for irq gpio failed\n");
				goto err_irq_gpio_dir;
			}
		}

		if (gpio_is_valid(data->pdata->reset_gpio)) {
			err = gpio_request(data->pdata->reset_gpio,
						"fts_reset_gpio");
			if (err) {
				dev_err(&data->client->dev,
					"reset gpio request failed");
				goto err_irq_gpio_dir;
			}

			err = gpio_direction_output(data->pdata->reset_gpio, 0);
			if (err) {
				dev_err(&data->client->dev,
				"set_direction for reset gpio failed\n");
				goto err_reset_gpio_dir;
			}
			msleep(data->pdata->hard_rst_dly);
			gpio_set_value_cansleep(data->pdata->reset_gpio, 1);
		}

		return 0;
	} else {
		if (gpio_is_valid(data->pdata->irq_gpio))
			gpio_free(data->pdata->irq_gpio);
		if (gpio_is_valid(data->pdata->reset_gpio)) {
			/*
			 * This is intended to save leakage current
			 * only. Even if the call(gpio_direction_input)
			 * fails, only leakage current will be more but
			 * functionality will not be affected.
			 */
			err = gpio_direction_input(data->pdata->reset_gpio);
			if (err) {
				dev_err(&data->client->dev,
					"unable to set direction for gpio "
					"[%d]\n", data->pdata->irq_gpio);
			}
			gpio_free(data->pdata->reset_gpio);
		}

		return 0;
	}

err_reset_gpio_dir:
	if (gpio_is_valid(data->pdata->reset_gpio))
		gpio_free(data->pdata->reset_gpio);
err_irq_gpio_dir:
	if (gpio_is_valid(data->pdata->irq_gpio))
		gpio_free(data->pdata->irq_gpio);
err_irq_gpio_req:
	return err;
}

/*******************************************************************************
*  Name: fts_power_on
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_power_on(struct fts_ts_data *data, bool on)
{
	int rc;

	if (!on)
	{
		FTS_DBG("Enter fts_power_on false ! \n");
		goto power_off;
	}

	rc = regulator_enable(data->vdd);
	if (rc) {
		dev_err(&data->client->dev, "Regulator vdd enable failed rc=%d\n", rc);
		return rc;
	}

    msleep(2); // delay > Tvdr(1ms);

	rc = regulator_enable(data->vcc_i2c);
	if (rc) {
		dev_err(&data->client->dev, "Regulator vcc_i2c enable failed rc=%d\n", rc);
		regulator_disable(data->vdd);
	}

	return rc;


power_off:
	rc = regulator_disable(data->vdd);
	if (rc) {
		dev_err(&data->client->dev, "Regulator vdd disable failed rc=%d\n", rc);
		return rc;
	}

	rc = regulator_disable(data->vcc_i2c);
	if (rc) {
		dev_err(&data->client->dev, "Regulator vcc_i2c disable failed rc=%d\n", rc);
		rc = regulator_enable(data->vdd);
        if (rc) {
		    dev_err(&data->client->dev, "Regulator vdd enable failed rc=%d\n", rc);
        }
	}

	return rc;
}

/*******************************************************************************
*  Name: fts_power_init
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_power_init(struct fts_ts_data *data, bool on)
{
	int rc;

	if (!on)
	{
		dev_err(&data->client->dev, "fts_power_init false \n");
		goto pwr_deinit;
	}

	data->vdd = regulator_get(&data->client->dev, "vdd");
	if (IS_ERR(data->vdd)) {
		rc = PTR_ERR(data->vdd);
		dev_err(&data->client->dev, "Regulator get failed vdd rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(data->vdd) > 0) {
		rc = regulator_set_voltage(data->vdd, FTS_VTG_MIN_UV, FTS_VTG_MAX_UV);
		if (rc) {
			dev_err(&data->client->dev, "Regulator set_vtg failed vdd rc=%d\n", rc);
			goto reg_vdd_put;
		}
	}

	data->vcc_i2c = regulator_get(&data->client->dev, "vcc_i2c");
	if (IS_ERR(data->vcc_i2c)) {
		rc = PTR_ERR(data->vcc_i2c);
		dev_err(&data->client->dev, "Regulator get failed vcc_i2c rc=%d\n", rc);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(data->vcc_i2c) > 0)  {
		rc = regulator_set_voltage(data->vcc_i2c, FTS_I2C_VTG_MIN_UV, FTS_I2C_VTG_MAX_UV);
		if (rc) {
			dev_err(&data->client->dev, "Regulator set_vtg failed vcc_i2c rc=%d\n", rc);
			goto reg_vcc_i2c_put;
		}
	}

	return 0;

reg_vcc_i2c_put:
	regulator_put(data->vcc_i2c);
reg_vdd_set_vtg:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, FTS_VTG_MAX_UV);
reg_vdd_put:
	regulator_put(data->vdd);
	return rc;

pwr_deinit:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, FTS_VTG_MAX_UV);

	regulator_put(data->vdd);

	if (regulator_count_voltages(data->vcc_i2c) > 0)
		regulator_set_voltage(data->vcc_i2c, 0, FTS_I2C_VTG_MAX_UV);

	regulator_put(data->vcc_i2c);
	return 0;
}

/*******************************************************************************
*  Name: fts_ts_pinctrl_init
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_ts_pinctrl_init(struct fts_ts_data *fts_data)
{
	int retval;

	/* Get pinctrl if target uses pinctrl */
	fts_data->ts_pinctrl = devm_pinctrl_get(&(fts_data->client->dev));
	if (IS_ERR_OR_NULL(fts_data->ts_pinctrl)) {
		retval = PTR_ERR(fts_data->ts_pinctrl);
		dev_dbg(&fts_data->client->dev,
			"Target does not use pinctrl %d\n", retval);
		goto err_pinctrl_get;
	}

	fts_data->pinctrl_state_active
		= pinctrl_lookup_state(fts_data->ts_pinctrl,
				PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(fts_data->pinctrl_state_active)) {
		retval = PTR_ERR(fts_data->pinctrl_state_active);
		dev_err(&fts_data->client->dev,
			"Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_ACTIVE, retval);
		goto err_pinctrl_lookup;
	}

	fts_data->pinctrl_state_suspend
		= pinctrl_lookup_state(fts_data->ts_pinctrl,
			PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(fts_data->pinctrl_state_suspend)) {
		retval = PTR_ERR(fts_data->pinctrl_state_suspend);
		dev_err(&fts_data->client->dev,
			"Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_SUSPEND, retval);
		goto err_pinctrl_lookup;
	}

	fts_data->pinctrl_state_release
		= pinctrl_lookup_state(fts_data->ts_pinctrl,
			PINCTRL_STATE_RELEASE);
	if (IS_ERR_OR_NULL(fts_data->pinctrl_state_release)) {
		retval = PTR_ERR(fts_data->pinctrl_state_release);
		dev_dbg(&fts_data->client->dev,
			"Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_RELEASE, retval);
	}

	return 0;

err_pinctrl_lookup:
	devm_pinctrl_put(fts_data->ts_pinctrl);
err_pinctrl_get:
	fts_data->ts_pinctrl = NULL;
	return retval;
}

#ifdef CONFIG_PM
/*******************************************************************************
*  Name: fts_ts_start
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_ts_start(struct device *dev)
{
	struct fts_ts_data *data = dev_get_drvdata(dev);
	int err;

    /* set pin status */
	if (data->ts_pinctrl) {
		err = pinctrl_select_state(data->ts_pinctrl,
				data->pinctrl_state_active);
		if (err < 0)
			dev_err(dev, "Cannot get active pinctrl state\n");
	}

	err = fts_gpio_configure(data, true);
	if (err < 0) {
		dev_err(&data->client->dev,
			"failed to put gpios in resue state\n");
		goto err_gpio_configuration;
	}

	msleep(data->pdata->soft_rst_dly);

	enable_irq(data->client->irq);

	data->suspended = false;

	return 0;

err_gpio_configuration:
	if (data->ts_pinctrl) {
		err = pinctrl_select_state(data->ts_pinctrl,
					data->pinctrl_state_suspend);
		if (err < 0)
			dev_err(dev, "Cannot get suspend pinctrl state\n");
	}

	return err;
}

/*******************************************************************************
*  Name: fts_ts_stop
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_ts_stop(struct device *dev)
{
	struct fts_ts_data *data = dev_get_drvdata(dev);
	char txbuf[2];
	int i, err;

	disable_irq(data->client->irq);

	/* release all touches */
	for (i = 0; i < data->pdata->num_max_touches; i++) {
		input_mt_slot(data->input_dev, i);
		input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, 0);
	}
	input_mt_report_pointer_emulation(data->input_dev, false);
	input_sync(data->input_dev);

    /* set PMODE reg: sleep */
	if (gpio_is_valid(data->pdata->reset_gpio)) {
		txbuf[0] = FTS_REG_PMODE;
		txbuf[1] = FTS_PMODE_HIBERNATE;
		fts_i2c_write(data->client, txbuf, sizeof(txbuf));
	}

    /* set pin status */
	if (data->ts_pinctrl) {
		err = pinctrl_select_state(data->ts_pinctrl,
					data->pinctrl_state_suspend);
		if (err < 0)
			dev_err(dev, "Cannot get suspend pinctrl state\n");
	}

	err = fts_gpio_configure(data, false);
	if (err < 0) {
		dev_err(&data->client->dev,
			"failed to put gpios in suspend state\n");
		goto gpio_configure_fail;
	}

	data->suspended = true;

	return 0;

gpio_configure_fail:
	if (data->ts_pinctrl) {
		err = pinctrl_select_state(data->ts_pinctrl,
					data->pinctrl_state_active);
		if (err < 0)
			dev_err(dev, "Cannot get active pinctrl state\n");
	}

	enable_irq(data->client->irq);
	
    return err;
}

/*******************************************************************************
*  Name: fts_ts_suspend
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
int fts_ts_suspend(struct device *dev)
{
	struct fts_ts_data *data = dev_get_drvdata(dev);

    printk(KERN_ERR "test:%s\n", __func__);

#if FTS_GESTRUE_EN
        if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
    {
         fts_write_reg(fts_i2c_client, 0xd0, 0x01);
	  if (fts_updateinfo_curr.CHIP_ID==0x54 || fts_updateinfo_curr.CHIP_ID==0x58)
	  {
	  	fts_write_reg(fts_i2c_client, 0xd1, 0xff);
		fts_write_reg(fts_i2c_client, 0xd2, 0xff);
		fts_write_reg(fts_i2c_client, 0xd5, 0xff);
		fts_write_reg(fts_i2c_client, 0xd6, 0xff);
		fts_write_reg(fts_i2c_client, 0xd7, 0xff);
		fts_write_reg(fts_i2c_client, 0xd8, 0xff);
	  }
         doze_status = DOZE_ENABLED;
	// data->suspended = true;
	enable_irq_wake(data->client->irq);
       return 0;
  }
#endif
	if (data->loading_fw) {
		dev_info(dev, "Firmware loading in process...\n");
		return 0;
	}

	if (data->suspended) {
		dev_info(dev, "Already in suspend state\n");
		return 0;
	}

	return fts_ts_stop(dev);
}

/*******************************************************************************
*  Name: fts_ts_resume
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
int fts_ts_resume(struct device *dev)
{
	struct fts_ts_data *data = dev_get_drvdata(dev);
	int err;

        printk(KERN_ERR "test:%s\n", __func__);
#if    FTS_GESTRUE_EN
       //u8 auc_i2c_write_buf[2] = {0};
       printk(KERN_ERR "resume doze_status = %d\n", doze_status);
       if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
       {
           if(doze_status == DOZE_ENABLED)
           {
              printk("resume doze_status == DOZE_ENABLED %d \n",__LINE__);

              fts_write_reg(fts_i2c_client,0xD0,0x00);
              doze_status = DOZE_DISABLED;
              printk("enter normal\n");
           }
        data->suspended = false;
       }
#endif


	if (!data->suspended) {
		dev_dbg(dev, "Already in awake state\n");
		return 0;
	}
        printk(KERN_ERR "guojing resume no return\n");
        delay_qt_ms(10);
	err = fts_ts_start(dev);
	if (err < 0)
		return err;

	return 0;
}

static const struct dev_pm_ops fts_ts_pm_ops = {
#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND))
	.suspend = fts_ts_suspend,
	.resume = fts_ts_resume,
#endif
};

#else
/*******************************************************************************
*  Name: fts_ts_suspend
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_ts_suspend(struct device *dev)
{
	return 0;
}

/*******************************************************************************
*  Name: fts_ts_resume
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_ts_resume(struct device *dev)
{
	return 0;
}

#endif

#if defined(CONFIG_FB)
/*******************************************************************************
*  Name: fb_notifier_callback
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct fts_ts_data *fts_data =
		container_of(self, struct fts_ts_data, fb_notif);

	if (evdata && evdata->data && event == FB_EVENT_BLANK &&
			fts_data && fts_data->client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
			fts_ts_resume(&fts_data->client->dev);
		else if (*blank == FB_BLANK_POWERDOWN)
			fts_ts_suspend(&fts_data->client->dev);
	}

	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
/*******************************************************************************
*  Name: fts_ts_early_suspend
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static void fts_ts_early_suspend(struct early_suspend *handler)
{
	struct fts_ts_data *data = container_of(handler,
						   struct fts_ts_data,
						   early_suspend);

	fts_ts_suspend(&data->client->dev);
}

/*******************************************************************************
*  Name: fts_ts_late_resume
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static void fts_ts_late_resume(struct early_suspend *handler)
{
	struct fts_ts_data *data = container_of(handler,
						   struct fts_ts_data,
						   early_suspend);

	fts_ts_resume(&data->client->dev);
}
#endif


#ifdef CONFIG_OF
/*******************************************************************************
*  Name: fts_get_dt_coords
*  Brief:
*  Input:
*  Output:
*  Return: 
*******************************************************************************/
static int fts_get_dt_coords(struct device *dev, char *name,
				struct fts_ts_platform_data *pdata)
{
	u32 coords[FTS_COORDS_ARR_SIZE];
	struct property *prop;
	struct device_node *np = dev->of_node;
	int coords_size, rc;

	prop = of_find_property(np, name, NULL);	
	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;
	

	coords_size = prop->length / sizeof(u32);
	if (coords_size != FTS_COORDS_ARR_SIZE) {
		dev_err(dev, "invalid %s\n", name);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(np, name, coords, coords_size);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read %s\n", name);
		return rc;
	}

	if (!strcmp(name, "focaltech,panel-coords")) {
		pdata->panel_minx = coords[0];
		pdata->panel_miny = coords[1];
		pdata->panel_maxx = coords[2];
		pdata->panel_maxy = coords[3];
	} else if (!strcmp(name, "focaltech,display-coords")) {
		pdata->x_min = coords[0];
		pdata->y_min = coords[1];
		pdata->x_max = coords[2];
		pdata->y_max = coords[3];
	} else {
		dev_err(dev, "unsupported property %s\n", name);
		return -EINVAL;
	}

	return 0;
}

/*******************************************************************************
*  Name: fts_parse_dt
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_parse_dt(struct device *dev, struct fts_ts_platform_data *pdata)
{
	int rc;
	struct device_node *np = dev->of_node;
	struct property *prop;
	u32 temp_val, num_buttons;
	u32 button_map[MAX_BUTTONS];

	pdata->name = "focaltech";
	rc = of_property_read_string(np, "focaltech,name", &pdata->name);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read name\n");
		return rc;
	}

	rc = fts_get_dt_coords(dev, "focaltech,panel-coords", pdata);
	if (rc && (rc != -EINVAL))
		return rc;

	rc = fts_get_dt_coords(dev, "focaltech,display-coords", pdata);
	if (rc)
		return rc;

	pdata->i2c_pull_up = of_property_read_bool(np, "focaltech,i2c-pull-up");

	pdata->no_force_update = of_property_read_bool(np, "focaltech,no-force-update");
	/* reset, irq gpio info */
	pdata->reset_gpio = of_get_named_gpio_flags(np, "focaltech,reset-gpio", 0, &pdata->reset_gpio_flags);
	if (pdata->reset_gpio < 0)
		return pdata->reset_gpio;

	pdata->irq_gpio = of_get_named_gpio_flags(np, "focaltech,irq-gpio", 0, &pdata->irq_gpio_flags);
	if (pdata->irq_gpio < 0)
		return pdata->irq_gpio;

	pdata->fw_name = "ft_fw.bin";
	rc = of_property_read_string(np, "focaltech,fw-name", &pdata->fw_name);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read fw name\n");
		return rc;
	}

	rc = of_property_read_u32(np, "focaltech,group-id", &temp_val);
	if (!rc)
		pdata->group_id = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "focaltech,hard-reset-delay-ms", &temp_val);
	if (!rc)
		pdata->hard_rst_dly = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "focaltech,soft-reset-delay-ms", &temp_val);
	if (!rc)
		pdata->soft_rst_dly = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "focaltech,num-max-touches", &temp_val);
	if (!rc)
		pdata->num_max_touches = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "focaltech,fw-delay-aa-ms", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read fw delay aa\n");
		return rc;
	} else if (rc != -EINVAL)
		pdata->info.delay_aa =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-delay-55-ms", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read fw delay 55\n");
		return rc;
	} else if (rc != -EINVAL)
		pdata->info.delay_55 =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-upgrade-id1", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read fw upgrade id1\n");
		return rc;
	} else if (rc != -EINVAL)
		pdata->info.upgrade_id_1 =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-upgrade-id2", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read fw upgrade id2\n");
		return rc;
	} else if (rc != -EINVAL)
		pdata->info.upgrade_id_2 =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-delay-readid-ms", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read fw delay read id\n");
		return rc;
	} else if (rc != -EINVAL)
		pdata->info.delay_readid =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-delay-era-flsh-ms", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read fw delay erase flash\n");
		return rc;
	} else if (rc != -EINVAL)
		pdata->info.delay_erase_flash =  temp_val;

	pdata->info.AUTO_CLB = of_property_read_bool(np, "focaltech,fw-auto-cal");

	pdata->fw_vkey_support = of_property_read_bool(np, "focaltech,fw-vkey-support");

	pdata->ignore_id_check = of_property_read_bool(np, "focaltech,ignore-id-check");

	rc = of_property_read_u32(np, "focaltech,family-id", &temp_val);
	if (!rc)
		pdata->family_id = temp_val;
	else
		return rc;

	prop = of_find_property(np, "focaltech,button-map", NULL);
	if (prop) {
		num_buttons = prop->length / sizeof(temp_val);
		if (num_buttons > MAX_BUTTONS)
			return -EINVAL;

		rc = of_property_read_u32_array(np, "focaltech,button-map", button_map, num_buttons);
		if (rc) {
			dev_err(dev, "Unable to read key codes\n");
			return rc;
		}
	}

	return 0;
}
#else
/*******************************************************************************
*  Name: fts_parse_dt
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_parse_dt(struct device *dev, struct fts_ts_platform_data *pdata)
{
	return -ENODEV;
}
#endif

/*******************************************************************************
*  Name: fts_debug_addr_is_valid
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static bool fts_debug_addr_is_valid(int addr)
{
	if (addr < 0 || addr > 0xFF) {
		pr_err("FT reg address is invalid: 0x%x\n", addr);
		return false;
	}

	return true;
}

/*******************************************************************************
*  Name: fts_debug_data_set
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_debug_data_set(void *_data, u64 val)
{
	struct fts_ts_data *data = _data;

	mutex_lock(&data->input_dev->mutex);

	if (fts_debug_addr_is_valid(data->addr))
		dev_info(&data->client->dev,
			"Writing into FT registers not supported\n");

	mutex_unlock(&data->input_dev->mutex);

	return 0;
}

/*******************************************************************************
*  Name: fts_debug_data_get
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_debug_data_get(void *_data, u64 *val)
{
	struct fts_ts_data *data = _data;
	int rc;
	u8 reg;

	mutex_lock(&data->input_dev->mutex);

	if (fts_debug_addr_is_valid(data->addr)) {
		rc = fts_read_reg(data->client, data->addr, &reg);
		if (rc < 0)
			dev_err(&data->client->dev,
				"FT read register 0x%x failed (%d)\n",
				data->addr, rc);
		else
			*val = reg;
	}

	mutex_unlock(&data->input_dev->mutex);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_data_fops, fts_debug_data_get, fts_debug_data_set, "0x%02llX\n");

/*******************************************************************************
*  Name: fts_debug_addr_set
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_debug_addr_set(void *_data, u64 val)
{
	struct fts_ts_data *data = _data;

	if (fts_debug_addr_is_valid(val)) {
		mutex_lock(&data->input_dev->mutex);
		data->addr = val;
		mutex_unlock(&data->input_dev->mutex);
	}

	return 0;
}

/*******************************************************************************
*  Name: fts_debug_addr_get
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_debug_addr_get(void *_data, u64 *val)
{
	struct fts_ts_data *data = _data;

	mutex_lock(&data->input_dev->mutex);

	if (fts_debug_addr_is_valid(data->addr))
		*val = data->addr;

	mutex_unlock(&data->input_dev->mutex);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_addr_fops, fts_debug_addr_get, fts_debug_addr_set, "0x%02llX\n");

/*******************************************************************************
*  Name: fts_debug_suspend_set
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_debug_suspend_set(void *_data, u64 val)
{
	struct fts_ts_data *data = _data;

	mutex_lock(&data->input_dev->mutex);

	if (val)
		fts_ts_suspend(&data->client->dev);
	else
		fts_ts_resume(&data->client->dev);

	mutex_unlock(&data->input_dev->mutex);

	return 0;
}

/*******************************************************************************
*  Name: fts_debug_suspend_get
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_debug_suspend_get(void *_data, u64 *val)
{
	struct fts_ts_data *data = _data;

	mutex_lock(&data->input_dev->mutex);
	*val = data->suspended;
	mutex_unlock(&data->input_dev->mutex);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_suspend_fops, fts_debug_suspend_get, fts_debug_suspend_set, "%lld\n");

/*******************************************************************************
*  Name: fts_debug_dump_info
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
int fts_debug_dump_info(struct seq_file *m, void *v)
{
	struct fts_ts_data *data = m->private;

	seq_printf(m, "%s\n", data->ts_info);

	return 0;
}

/*******************************************************************************
*  Name: debugfs_dump_info_open
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int debugfs_dump_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, fts_debug_dump_info, inode->i_private);
}

static const struct file_operations debug_dump_info_fops = {
	.owner		= THIS_MODULE,
	.open		= debugfs_dump_info_open,
	.read		= seq_read,
	.release	= single_release,
};

#if FTS_UPGRADE_EN
extern int fts_5x46_ctpm_fw_upgrade(struct i2c_client * client, u8* pbt_buf, u32 dw_lenth);
int fts_upgrade(struct i2c_client *client)
{
    int i = 0;
    int ret = 0;
    int tp_index = -1;

    u8 fts_dev_id = 0x00;
    u8 fts_fw_ver = 0xff;
    u8 fts_fw_vendor_id = 0;
    u8 fts_i_file_ver = 0;

    unsigned char *pbuf = NULL;
    unsigned int   sbuf = 0;

    /* 1.read device id */
    ret = fts_read_reg(client, FTS_REG_ID, &fts_dev_id);
    if (ret < 0)
        dev_err(&client->dev, "read device id fail, err = %d.", ret);

    dev_info(&client->dev, "device id = 0x%x.", fts_dev_id);

    /* 2.read fw version */
    ret = fts_read_reg(client, FTS_REG_FW_VER, &fts_fw_ver);
    if (ret < 0)
        dev_err(&client->dev, "read fw version fail, err = %d.", ret);

    dev_info(&client->dev, "fw version = 0x%x.", fts_fw_ver);

    /* 3.read vendor id */
    ret = fts_read_reg(client, FTS_REG_FW_VENDOR_ID, &fts_fw_vendor_id);
    if (ret < 0)
        dev_err(&client->dev, "read fw vendor id fail, err = %d.", ret);

    dev_info(&client->dev, "fw vendor id = 0x%x.", fts_fw_vendor_id);

    /* 4.select yl fw */
    for (i = 0; i < ARRAY_SIZE(yl_fw); i++)
    {
        if (fts_fw_vendor_id == yl_fw[i].tp_id)
        {
            tp_index = i;
            dev_info(&client->dev, "tp_index = %d.", tp_index);

            break;
        }
    }

    if (-1 == tp_index)
    {
        dev_err(&client->dev, "not found the valid yl_fw.");

        return -1;
    }

    /* 5.get i file version */
    pbuf = yl_fw[tp_index].tp_fw;
    sbuf = yl_fw[tp_index].tp_fw_size;

    if (yl_fw[tp_index].tp_fw_size > 2)
    {
        switch (fts_dev_id)
        {
        case 0x36: fts_i_file_ver = pbuf[0x10a];
            break;
        case 0x58: fts_i_file_ver = pbuf[0x1D0A];
            break;
        case 0x54: fts_i_file_ver = pbuf[sbuf - 2];
            break;
        default:   fts_i_file_ver = 0x00;
            break;
        }
    }
    else
        fts_i_file_ver = 0x00;

    dev_info(&client->dev, "i file version = 0x%x.", fts_i_file_ver);

    /* 6.upgrade */
    if (fts_fw_ver < fts_i_file_ver || 0x00 == fts_i_file_ver)
    {
        msleep(100);
        switch (fts_dev_id)
        {
        case 0x54: ret = fts_5x46_ctpm_fw_upgrade(client, pbuf, sbuf);
            break;
        default:
            break;
        }
        msleep(300);

        if (ret)
        {
            dev_err(&client->dev, "fw upgrade fail, ret = %d.", ret);
            return -EIO;
        }

        dev_info(&client->dev, "firmware upgrade successful.");
    }
    else
        dev_info(&client->dev, "can not upgrade firmware, i file version to low.");

	return 0;
}
#endif

/*******************************************************************************
*  Name: fts_ts_probe
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct fts_ts_platform_data *pdata;
	struct fts_ts_data *data;
	struct input_dev *input_dev;
	struct dentry *temp;
	u8 reg_value;
	u8 reg_addr;
	int err, len;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "I2C not supported\n");
        err =  -ENODEV;
        goto exit_probe;
    }

#ifdef CONFIG_OF
    if (client->dev.of_node) {
        pdata = devm_kzalloc(&client->dev,
            sizeof(struct fts_ts_platform_data), GFP_KERNEL);
        if (!pdata) {
            dev_err(&client->dev, "Failed to allocate memory\n");
            err =  -ENOMEM;
            goto exit_probe;
        }

        err = fts_parse_dt(&client->dev, pdata);
        if (err) {
            dev_err(&client->dev, "DT parsing failed\n");
            goto free_pdata;
        }
    } else {
        dev_err(&client->dev, "dev.of_node is null!\n");
        err =  -ENOMEM;
        goto exit_probe;
    }
#else
    pdata = client->dev.platform_data;
    if (!pdata) {
        dev_err(&client->dev, "Invalid pdata\n");
        err =  -EINVAL;
        goto exit_probe;
    }
#endif

    data = devm_kzalloc(&client->dev, sizeof(struct fts_ts_data), GFP_KERNEL);
    if (!data) {
        dev_err(&client->dev, "Not enough memory\n");
        err =  -ENOMEM;
        goto free_pdata;
    } else
        fts_wq_data = data;

    if (pdata->fw_name) {
        len = strlen(pdata->fw_name);
        if (len > FTS_FW_NAME_MAX_LEN - 1) {
            dev_err(&client->dev, "Invalid firmware name\n");
            err = -EINVAL;
            goto free_data;
        }

        strlcpy(data->fw_name, pdata->fw_name, len + 1);
    }

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "failed to allocate input device\n");
		err = -ENOMEM;
        goto free_data;
	}

	data->input_dev = input_dev;
	data->client = client;
	data->pdata = pdata;

	input_dev->name = "fts_ts";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	input_set_drvdata(input_dev, data);
	i2c_set_clientdata(client, data);

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

    input_set_abs_params(input_dev, ABS_PRESSURE      , 0           , 255           , 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_X , pdata->x_min, pdata->x_max  , 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_Y , pdata->y_min, pdata->y_max  , 0, 0);
    input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0           , 255           , 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0           , 255           , 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0           , FTS_MAX_POINTS, 0, 0);

    input_set_capability(input_dev, EV_KEY, fts_touch_key[0]);
    input_set_capability(input_dev, EV_KEY, fts_touch_key[1]);
    input_set_capability(input_dev, EV_KEY, fts_touch_key[2]);

    err = input_register_device(input_dev);
    if (err) {
        dev_err(&client->dev, "Input device registration failed\n");
        input_free_device(input_dev);
        goto free_data;
    }

    /*
     * RST have an illegal voltage(1.1v), normal voltage is 1.8V, so that
     * wo need four steps to guarantee ft5446 Power on Sequence in probe:
     * 1.pinctrl_select_state from active to suspend;
     * 2.config INT to input, and config RST to output low, keep 100us(Trtp);
     * 3.power on VDD and keep 1ms(Tvdr);
     * 4.cofnig RST from low to high;
     */

    /* first step */
    err = fts_ts_pinctrl_init(data);
    if (!err && data->ts_pinctrl) {
        /*
         * Pinctrl handle is optional. If pinctrl handle is found
         * let pins to be configured in active state. If not
         * found continue further without error.
         */
        err = pinctrl_select_state(data->ts_pinctrl, data->pinctrl_state_suspend);
        if (err < 0) {
            dev_err(&client->dev, "failed to select pin to active state");
            goto unreg_inputdev;
        }
    }

    /* second step */
    err = fts_power_on_sequence_start(data);
    if (err < 0) {
        dev_err(&client->dev, "fts_power_on_sequence_start() failed");
        goto unreg_inputdev;
    }

    /* third step */
	if (pdata->power_init) {
		err = pdata->power_init(true);
		if (err) {
			dev_err(&client->dev, "pdata->power_init power init failed");
			goto free_gpio;
		}
	} else {
		err = fts_power_init(data, true);
		if (err) {
			dev_err(&client->dev, "fts_power_init power init failed");
			goto free_gpio;
		}
	}

	if (pdata->power_on) {
		err = pdata->power_on(true);
		if (err) {
			dev_err(&client->dev, "power on failed");
			goto pwr_deinit;
		}
	} else {
		err = fts_power_on(data, true);
		if (err) {
			dev_err(&client->dev, "power on failed");
			goto pwr_deinit;
		}
	}

    /* fourth step */
    err = fts_power_on_sequence_end(data);
    if (err < 0) {
        dev_err(&client->dev, "fts_power_on_sequence_end() failed");
        goto pwr_off;
    }

	/* make sure CTP already finish startup process */
	msleep(data->pdata->soft_rst_dly);

	INIT_WORK(&data->touch_event_work, fts_touch_irq_work);
	data->ts_workqueue = create_workqueue(FTS_WORKQUEUE_NAME);
	if (!data->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

	/* check the controller id */
	reg_addr = FTS_REG_ID;
	err = fts_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	if (err < 0) {
		dev_err(&client->dev, "version read failed");
		goto exit_create_singlethread;
	}

	dev_info(&client->dev, "Device ID = 0x%x\n", reg_value);

	if ((pdata->family_id != reg_value) && (!pdata->ignore_id_check)) {
		dev_err(&client->dev, "%s:Unsupported controller\n", __func__);
		goto exit_create_singlethread;
	}

	data->family_id = pdata->family_id;

    fts_i2c_client = client;
    fts_input_dev = input_dev;

	err = request_threaded_irq(client->irq, NULL, fts_ts_interrupt,
				pdata->irqflags | IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
				client->dev.driver->name, data);
	if (err) {
		dev_err(&client->dev, "request irq failed\n");
		goto exit_create_singlethread;
	}

	disable_irq(client->irq);

	data->dir = debugfs_create_dir(FTS_DEBUG_DIR_NAME, NULL);
	if (data->dir == NULL || IS_ERR(data->dir)) {
		pr_err("debugfs_create_dir failed(%ld)\n", PTR_ERR(data->dir));
		err = PTR_ERR(data->dir);
        goto irq_free;
	}

	temp = debugfs_create_file("addr", S_IRUSR | S_IWUSR, data->dir, data,
				   &debug_addr_fops);
	if (temp == NULL || IS_ERR(temp)) {
		pr_err("debugfs_create_file failed: rc=%ld\n", PTR_ERR(temp));
		err = PTR_ERR(temp);
		goto free_debug_dir;
	}

	temp = debugfs_create_file("data", S_IRUSR | S_IWUSR, data->dir, data,
				   &debug_data_fops);
	if (temp == NULL || IS_ERR(temp)) {
		pr_err("debugfs_create_file failed: rc=%ld\n", PTR_ERR(temp));
		err = PTR_ERR(temp);
		goto free_debug_dir;
	}

	temp = debugfs_create_file("suspend", S_IRUSR | S_IWUSR, data->dir,
					data, &debug_suspend_fops);
	if (temp == NULL || IS_ERR(temp)) {
		pr_err("debugfs_create_file failed: rc=%ld\n", PTR_ERR(temp));
		err = PTR_ERR(temp);
		goto free_debug_dir;
	}

	temp = debugfs_create_file("dump_info", S_IRUSR | S_IWUSR, data->dir,
					data, &debug_dump_info_fops);
	if (temp == NULL || IS_ERR(temp)) {
		pr_err("debugfs_create_file failed: rc=%ld\n", PTR_ERR(temp));
		err = PTR_ERR(temp);
		goto free_debug_dir;
	}
	
	data->ts_info = devm_kzalloc(&client->dev, FTS_INFO_MAX_LEN, GFP_KERNEL);
	if (!data->ts_info) {
		dev_err(&client->dev, "Not enough memory\n");
		goto free_debug_dir;
	}

	/*get some register information */
	reg_addr = FTS_REG_POINT_RATE;
	fts_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	if (err < 0)
		dev_err(&client->dev, "report rate read failed");

	dev_info(&client->dev, "report rate = %dHz\n", reg_value * 10);

	reg_addr = FTS_REG_THGROUP;
	err = fts_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	if (err < 0)
		dev_err(&client->dev, "threshold read failed");

	dev_dbg(&client->dev, "touch threshold = %d\n", reg_value * 4);

    fts_update_fw_ver(data);
    fts_update_fw_vendor_id(data);

	FTS_STORE_TS_INFO(data->ts_info, data->family_id, data->pdata->name,
			data->pdata->num_max_touches, data->pdata->group_id,
			data->pdata->fw_vkey_support ? "yes" : "no",
			data->pdata->fw_name, data->fw_ver[0],
			data->fw_ver[1], data->fw_ver[2]);

#if FTS_UPGRADE_EN
    fts_get_upgrade_array();
    fts_upgrade(client);
#endif

	#ifdef FTS_APK_DEBUG
		fts_create_apk_debug_channel(client);
	#endif

	#ifdef FTS_SYSFS_DEBUG
		fts_create_sysfs(client);
	#endif

	#if FTS_GESTRUE_EN
		fts_Gesture_init(input_dev);
	#endif
	
#if defined(CONFIG_FB)
	data->fb_notif.notifier_call = fb_notifier_callback;

	err = fb_register_client(&data->fb_notif);

	if (err)
		dev_err(&client->dev, "Unable to register fb_notifier: %d\n", err);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + FTS_SUSPEND_LEVEL;
	data->early_suspend.suspend = fts_ts_early_suspend;
	data->early_suspend.resume = fts_ts_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

    touchscreen_set_ops(&fts_ops);

	enable_irq(client->irq);

	return 0;

free_debug_dir:
	debugfs_remove_recursive(data->dir);

irq_free:
	free_irq(client->irq, data);

exit_create_singlethread:
	printk(KERN_ERR "singlethread error!\n");
	i2c_set_clientdata(client, NULL);

pwr_off:
	if (pdata->power_on)
		pdata->power_on(false);
	else
		fts_power_on(data, false);

pwr_deinit:
	if (pdata->power_init)
		pdata->power_init(false);
	else
		fts_power_init(data, false);

free_gpio:
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);

unreg_inputdev:
	input_unregister_device(input_dev);
	input_dev = NULL;

free_data:
	devm_kfree(&client->dev, data);

free_pdata:
#ifdef CONFIG_OF
    devm_kfree(&client->dev, pdata);
#endif

exit_probe:
	return err;
}

/*******************************************************************************
*  Name: fts_ts_remove
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int fts_ts_remove(struct i2c_client *client)
{
	struct fts_ts_data *data = i2c_get_clientdata(client);

	cancel_work_sync(&data->touch_event_work);
	destroy_workqueue(data->ts_workqueue);

	debugfs_remove_recursive(data->dir);
	
#ifdef FTS_APK_DEBUG
		fts_release_apk_debug_channel();
#endif

#ifdef FTS_SYSFS_DEBUG
		fts_remove_sysfs(fts_i2c_client);
#endif

#if defined(CONFIG_FB)
	if (fb_unregister_client(&data->fb_notif))
		dev_err(&client->dev, "Error occurred while unregistering fb_notifier.\n");
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&data->early_suspend);
#endif
	free_irq(client->irq, data);

	if (gpio_is_valid(data->pdata->reset_gpio))
		gpio_free(data->pdata->reset_gpio);

	if (gpio_is_valid(data->pdata->irq_gpio))
		gpio_free(data->pdata->irq_gpio);

	if (data->pdata->power_on)
		data->pdata->power_on(false);
	else
		fts_power_on(data, false);

	if (data->pdata->power_init)
		data->pdata->power_init(false);
	else
		fts_power_init(data, false);

	input_unregister_device(data->input_dev);

	return 0;
}

static const struct i2c_device_id fts_ts_id[] = {
	{"fts_ts", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, fts_ts_id);

#ifdef CONFIG_OF
static struct of_device_id fts_match_table[] = {
	{ .compatible = "focaltech,fts",},
	{ },
};
#else
#define fts_match_table NULL
#endif

static struct i2c_driver fts_ts_driver = {
	.probe = fts_ts_probe,
	.remove = fts_ts_remove,
	.driver = {
		   .name = "fts_ts",
		   .owner = THIS_MODULE,
		   .of_match_table = fts_match_table,
#ifdef CONFIG_PM
		   .pm = &fts_ts_pm_ops,
#endif
		   },
	.id_table = fts_ts_id,
};

/*******************************************************************************
*  Name: fts_ts_init
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static int __init fts_ts_init(void)
{
    #ifdef CONFIG_LPM_MODE
    extern unsigned int poweroff_charging;
    extern unsigned int recovery_mode;
    if (1 == poweroff_charging || 1 == recovery_mode) {
        printk(KERN_ERR"%s: probe exit, lpm=%d recovery=%d\n", __func__, poweroff_charging, recovery_mode);
        return -ENODEV;
    }
    #endif

	return i2c_add_driver(&fts_ts_driver);
}

/*******************************************************************************
*  Name: fts_ts_exit
*  Brief:
*  Input:
*  Output: 
*  Return: 
*******************************************************************************/
static void __exit fts_ts_exit(void)
{
	i2c_del_driver(&fts_ts_driver);
}

module_init(fts_ts_init);
module_exit(fts_ts_exit);

MODULE_DESCRIPTION("FocalTech fts TouchScreen driver");
MODULE_LICENSE("GPL v2");
