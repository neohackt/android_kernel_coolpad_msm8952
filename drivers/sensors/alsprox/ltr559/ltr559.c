/***************************************************************************/
/* Copyright (c) 2000-2010  YULONG Company                                 */
/* PROPRIETARY RIGHTS of YULONG Company are involved in the                */
/* subject matter of this material.  All manufacturing, reproduction, use, */
/* and sales rights pertaining to this subject matter are governed by the  */
/* license agreement.  The recipient of this software implicitly accepts   */ 
/* the terms of the license.                                               */
/***************************************************************************/

/***************************************************************************
**
**  Copyright (C), 2013-2015, Yulong Tech. Co., Ltd.
**  FileName:    ltr559.c
**  Description: Linux device driver for ltr559 light and proximity sensors 
**  <author>      <time>      <version >      <desc>
**  longjiang     2013.09.25    1.0             Create
**  longjiang     2013.12.19    2.0             Modify
**  longjiang     2014.10.28    3.0             Add HeartBeat
**  longjiang     2015.08.25    4.0             Modify
**
****************************************************************************/

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/printk.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#endif
#include <linux/sensors/ltr559.h>
#include <linux/sensors/sensparams.h>

#include <linux/sensors.h>

#ifdef CONFIG_YL_SENSORS_DEBUG
#undef pr_debug
#define pr_debug(fmt, args...) pr_info(fmt, ##args)
#endif

#define I2C_RETRY_TIMES 3
#define CONFIG_YL_SENSORS_POWER
#define SENSORS_WAKE_LOCK_TIMEOUT
#define SENSORS_I2C_RETRY_ERROR
#define SENSORS_IRQ_WAIT_QUEUE

#ifdef SENSORS_I2C_RETRY_ERROR
static int i2c_retry_err = 0;
#endif

#ifdef SENSORS_IRQ_WAIT_QUEUE
static int sensors_resume = 1;
static DECLARE_WAIT_QUEUE_HEAD(wait_sensors_resume);
#endif

enum {
	LIGHT_ENABLED = BIT(0),
	PROXIMITY_ENABLED = BIT(1),
};

/* driver data */
struct ltr559_data {
	bool on;
	u8 power_state;
	struct ltr559_platform_data *pdata;
	struct i2c_client *i2c_client;
	struct mutex lock;
	struct workqueue_struct *wq;
	struct work_struct work_light;
	struct hrtimer light_timer;
	ktime_t light_poll_delay;
#ifdef SENSORS_WAKE_LOCK_TIMEOUT
	struct wake_lock prx_wake_lock;
#endif
	struct input_dev *input_dev_light;
	struct input_dev *input_dev_proximity;
	unsigned int als_ps_int;
	u8	reg_addr;
#ifdef CONFIG_YL_SENSORS_POWER
	struct regulator *vcc_i2c;
	struct regulator *vdd_ana;
#endif
#ifdef CONFIG_SENSORS
	struct sensors_classdev als_cdev;
	struct sensors_classdev ps_cdev;
#endif
};

#ifdef CONFIG_SENSORS
static struct sensors_classdev sensors_light_cdev = {
	.name = "light",
	.vendor = "ltr559",
	.version = 1,
	.handle = SENSORS_LIGHT_HANDLE,
	.type = SENSOR_TYPE_LIGHT,
	.max_range = "100000",
	.resolution = "1.0",
	.sensor_power = "0.1",
	.min_delay = 20000, /* in microseconds */
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.delay_msec = 100,
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
};

static struct sensors_classdev sensors_proximity_cdev = {
	.name = "proximity",
	.vendor = "ltr559",
	.version = 1,
	.handle = SENSORS_PROXIMITY_HANDLE,
	.type = SENSOR_TYPE_PROXIMITY,
	.max_range = "5",
	.resolution = "1.0",
	.sensor_power = "0.1",
	.min_delay = 0, /* in microseconds */
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.delay_msec = 100,
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
};
#endif

static struct ltr559_data *ltr559_dev = NULL; 
static int als_zero_flag = 0;

static int ltr559_i2c_read(struct ltr559_data *ltr559, u8 regnum, u8 *data)
{
	int readdata;
	int tries = 0;
	struct i2c_client *client = ltr559->i2c_client;

	do {
		readdata = i2c_smbus_read_byte_data(client, regnum);
		if (0 > readdata) {
			pr_debug("%s: i2c_read failed err = %d, tries=%d\n",
				__func__, readdata, tries);
			msleep(5);
		}
	} while ((readdata < 0) && (++tries < I2C_RETRY_TIMES));

	if (readdata < 0) {
#ifdef SENSORS_I2C_RETRY_ERROR
		i2c_retry_err = 1;
		pr_err("%s: i2c retry %d failed i2c_retry_err = %d\n",
			__func__, tries, i2c_retry_err);
#endif
		return readdata;
	} else {
		*data=readdata;
		return 0;
	}
}

static int ltr559_i2c_write(struct ltr559_data *ltr559, u8 regnum, u8 value)
{
	int writeerror;
	int tries = 0;
	struct i2c_client *client = ltr559->i2c_client;

	do {
		writeerror = i2c_smbus_write_byte_data(client, regnum, value);
		if (writeerror < 0) {
			pr_debug("%s: i2c_write failed err = %d, tries=%d\n",
				__func__, writeerror, tries);
			msleep(5);
		}
	} while ((writeerror < 0) && (++tries < I2C_RETRY_TIMES));

	if (writeerror < 0) {
#ifdef SENSORS_I2C_RETRY_ERROR
		i2c_retry_err = 1;
		pr_err("%s: i2c retry %d failed i2c_retry_err = %d\n",
			__func__, tries, i2c_retry_err);
#endif
		return writeerror;
	} else {
		return 0;
	}
}

static int ltr559_light_enable(struct ltr559_data *ltr559)
{
	int ret;

	ret = ltr559_i2c_write(ltr559, LTR559_ALS_CONTR, LTR559_MODE_ALS_ON_Range4);
	if (ret) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_ALS_CONTR);
		return ret;
	}
	als_zero_flag = 0;
	mdelay(LTR559_WAKEUP_DELAY);//10ms
			
	return ret;
}

static int ltr559_light_disable(struct ltr559_data *ltr559)
{
	int ret;

	ret = ltr559_i2c_write(ltr559, LTR559_ALS_CONTR, LTR559_MODE_ALS_StdBy);
	if (ret) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_ALS_CONTR);
		return ret;
	}
	
	return ret;
}

static int ltr559_proximity_enable(struct ltr559_data *ltr559)
{
	int ret;

	ret = ltr559_i2c_write(ltr559, LTR559_PS_CONTR, LTR559_MODE_PS_ON_Gain16);	
	if (ret) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_CONTR);
		return ret;
	}

	mdelay(LTR559_WAKEUP_DELAY);//10ms

	return ret;
}

static int ltr559_proximity_disable(struct ltr559_data *ltr559)
{
	int ret;
        /*  disable ps interrupt  */
	ret = ltr559_i2c_write(ltr559, LTR559_PS_CONTR, LTR559_MODE_PS_StdBy);	
	if (ret) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_CONTR);
		return ret;
	}
	
	return ret;
}

static int ltr559_ps_read(struct ltr559_data *ltr559, int *psdata)
{
	u8 psval_lo, psval_hi;
	int err;

	err = ltr559_i2c_read(ltr559, LTR559_PS_DATA_0, &psval_lo);
	if (err) {
		pr_err("%s: i2c_read 0x%x failed\n", __func__, LTR559_PS_DATA_0);
		return err;
	}
		
	if (psval_lo < 0) {
		pr_err("%s: unexpected psval_lo=%d\n", __func__, psval_lo);
		return psval_lo;
	}

	err = ltr559_i2c_read(ltr559, LTR559_PS_DATA_1, &psval_hi);	
	if (err) {
		pr_err("%s: i2c_read 0x%x failed\n", __func__, LTR559_PS_DATA_1);
		return err;
	}
		
	if (psval_hi < 0) {
		pr_err("%s: unexpected psval_hi=%d\n", __func__, psval_hi);
		return psval_hi;
	}
	
	*psdata = ((psval_hi & 7)* 256) + psval_lo;

	/*pr_debug("%s: psval_lo:%d psval_hi:%d psdata:%d",
		__func__, psval_lo, psval_hi & 7, *psdata);*/
	return err;
}

static int ltr559_als_read(struct ltr559_data *ltr559, unsigned int *lux_value)
{
	u8 alsval_ch0_lo, alsval_ch0_hi;
	u8 alsval_ch1_lo, alsval_ch1_hi;
	int ratio;
	int alsval_ch0, alsval_ch1;
	int ch0_coeff, ch1_coeff;
	int err;

	err = ltr559_i2c_read(ltr559, LTR559_ALS_DATA_CH1_0, &alsval_ch1_lo);
	if (err) {
		pr_err("%s: i2c_read 0x%x failed\n", __func__, LTR559_ALS_DATA_CH1_0);
		return err;
	}
	
	err = ltr559_i2c_read(ltr559, LTR559_ALS_DATA_CH1_1, &alsval_ch1_hi);
	if (err) {
		pr_err("%s: i2c_read 0x%x failed\n", __func__, LTR559_ALS_DATA_CH1_1);
		return err;
	}
	
	alsval_ch1 = (alsval_ch1_hi << 8) + alsval_ch1_lo;

	err = ltr559_i2c_read(ltr559, LTR559_ALS_DATA_CH0_0, &alsval_ch0_lo);
	if (err) {
		pr_err("%s: unexpected alsval_ch1_lo=%d\n", __func__, alsval_ch0_lo);
		return err;
	}
	
	err = ltr559_i2c_read(ltr559, LTR559_ALS_DATA_CH0_1, &alsval_ch0_hi);
	if (err) {
		pr_err("%s: unexpected alsval_ch1_lo=%d\n", __func__, alsval_ch0_hi);
		return err;
	}

	alsval_ch0 = (alsval_ch0_hi << 8) + alsval_ch0_lo;

	// lux formula
	if ((alsval_ch0 + alsval_ch1) == 0) {
		//pr_debug("Both CH0 and CH1 are zero\n");
		ratio = 0;
	} else {
		ratio = (100 * alsval_ch1)/(alsval_ch1 + alsval_ch0);
	}

	if(ratio == 0){
		ch0_coeff = 0;
		ch1_coeff = 0;
	}else if ((ratio > 0) && (ratio < 45)){
		ch0_coeff = 17743;
		ch1_coeff = -11059;
	} else if ((ratio >= 45) && (ratio < 64)) {
		ch0_coeff = 42785;
		ch1_coeff = 19548;
	} else if ((ratio >= 64) && (ratio < 85)) {
		ch0_coeff = 5926;
		ch1_coeff = -1185;
	} else if (ratio >= 85) {
		ch0_coeff = 0;
		ch1_coeff = 0;
	}

	/**lux_value = ((alsval_ch0 * ch0_coeff) - (alsval_ch1 * ch1_coeff))/10000; EV23 *lux_value *= 4;*/
	*lux_value = ((alsval_ch0 * ch0_coeff) - (alsval_ch1 * ch1_coeff))/10000;
	*lux_value *= 2;

	//pr_debug("%s: alsval_ch0[%d], alsval_ch1[%d], lux_value:%d\n ", __func__, alsval_ch0, alsval_ch1, *lux_value);
	return err;
}

static void ltr559_work_func_light(struct work_struct *work)
{
	struct ltr559_data *ltr559 = container_of(work, struct ltr559_data,
					      work_light);
	unsigned int lux_val=0;
	int err;

	err = ltr559_als_read(ltr559, &lux_val);
	if(als_zero_flag < 2){
		als_zero_flag++;
		return;
	}
	if (!err) {
		if(LTR559_LIGHT_LUX_MAX < lux_val)
			lux_val = LTR559_LIGHT_LUX_MAX;
			
		pr_debug("%s: lux_val:%d\n", __func__, lux_val);
		input_event(ltr559->input_dev_light, EV_MSC, MSC_SCAN, lux_val);
		input_sync(ltr559->input_dev_light);
	} else {
		pr_err("ltr559 iic read err \n");
	}
}

/* This function is for light sensor.  It operates every a few seconds.
 * It asks for work to be done on a thread because i2c needs a thread
 * context (slow and blocking) and then reschedules the timer to run again.
 */
static enum hrtimer_restart ltr559_light_timer_func(struct hrtimer *timer)
{
	struct ltr559_data *ltr559 = container_of(timer, struct ltr559_data, light_timer);
	queue_work(ltr559->wq, &ltr559->work_light);
	hrtimer_forward_now(&ltr559->light_timer, ltr559->light_poll_delay);
	return HRTIMER_RESTART;
}

static int ltr559_als_init_regs(struct ltr559_data *ltr559)
{
	int ret;

	ret = ltr559_i2c_write(ltr559, LTR559_ALS_MEAS_RATE, ltr559->pdata->als_meas_rate);
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_ALS_MEAS_RATE);
		return ret;
	}

#if 0
	ltr559_i2c_write(ltr559, LTR559_ALS_THRES_UP_0, 0x01);//0x97
	ltr559_i2c_write(ltr559, LTR559_ALS_THRES_UP_1, 0x00);
	ltr559_i2c_write(ltr559, LTR559_ALS_THRES_LOW_0, 0x00);		
	ltr559_i2c_write(ltr559, LTR559_ALS_THRES_LOW_1, 0x00);
#endif

	return ret;
}

static int ltr559_ps_cal_init(struct ltr559_data *ltr559)
{
	int ret;

	ret = ltr559_i2c_write(ltr559, LTR559_INTERRUPT_PERSIST, ltr559->pdata->interrupt_persist);
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_INTERRUPT_PERSIST);
		return ret;
	}

	ret = ltr559_i2c_write(ltr559, LTR559_PS_LED, ltr559->pdata->ps_led);//0x7B
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_LED);
		return ret;
	}

	ret = ltr559_i2c_write(ltr559, LTR559_PS_N_PULSES, ltr559->pdata->ps_n_pulses);//0x04
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_N_PULSES);
		return ret;
	}

	ret = ltr559_i2c_write(ltr559, LTR559_PS_MEAS_RATE, ltr559->pdata->ps_meas_rate);
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_MEAS_RATE);
		return ret;
	}

	mdelay(LTR559_WAKEUP_DELAY);//10ms

	return ret;
}

static int ltr559_ps_init_regs(struct ltr559_data *ltr559)
{
	int ret;

	ret = ltr559_i2c_write(ltr559, LTR559_PS_THRES_UP_0, ltr559->pdata->prox_threshold_hi & 0x0FF);//0x90
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_THRES_UP_0);
		return ret;
	}

	ret = ltr559_i2c_write(ltr559, LTR559_PS_THRES_UP_1, (ltr559->pdata->prox_threshold_hi >> 8) & 0x07);
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_THRES_UP_1);
		return ret;
	}

	ret = ltr559_i2c_write(ltr559, LTR559_PS_THRES_LOW_0, ltr559->pdata->prox_threshold_lo & 0x0FF);
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_THRES_LOW_0);
		return ret;
	}

	ret = ltr559_i2c_write(ltr559, LTR559_PS_THRES_LOW_1, (ltr559->pdata->prox_threshold_lo >> 8) & 0x07);
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_PS_THRES_LOW_1);
		return ret;
	}

	/*Only PS measurement can trigger interrupt*/
	/*
	ret = ltr559_i2c_write(ltr559, LTR559_INTERRUPT, 0x01);
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_INTERRUPT);
		return ret;
	}
        */

	return ret;

}
static int ltr559_parse_configs(struct device *dev, char *name, u32 *array) {
	int i, rc;
	struct property *prop;
	struct device_node *np = dev->of_node;

	prop = of_find_property(np, name, NULL);
	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;

	rc = of_property_read_u32_array(np, name, array, prop->length/sizeof(u32));
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "%s: Unable to read %s\n", __func__, name);
		return rc;
	}

	for (i = 0; i < prop->length/sizeof(u32); i++) {
		pr_debug("%s: arrary[%d]=%d,\n", __func__, i, array[i]);
	}

	return rc;
}

static int ltr559_parse_dt(struct device *dev, struct ltr559_platform_data *pdata)
{
	int rc = 0;
	int index = 0;
	u32 array[17];
	struct device_node *np = dev->of_node;

	pdata->gpio_int = of_get_named_gpio_flags(np, "ltr559,gpio_int",
		0, &pdata->irq_gpio_flags);

	rc = of_property_read_u32(np, "ltr559,prox_version", &pdata->prox_version);
	pr_err("%s: prox_version=0x%x\n", __func__, pdata->prox_version);

	/* general_reg */
	rc = ltr559_parse_configs(dev, "ltr559,cfgs", array);
	if (rc) {
		dev_err(dev, "Looking up %s property in node %s failed", "ltr559,cfgs", np->full_name);
		return -ENODEV;
	}

	pdata->prox_threshold_hi                 = array[index++];
	pdata->prox_threshold_lo                 = array[index++];
	pdata->prox_factory_threshold_hi      = array[index++];
	pdata->prox_factory_threshold_lo      = array[index++];
	pdata->interrupt_persist                   = array[index++];
	pdata->ps_led                                 = array[index++];
 	pdata->ps_n_pulses                         = array[index++];
 	pdata->ps_meas_rate                       = array[index++];
 	pdata->als_meas_rate                      = array[index++];
	pdata->ltr559_proximity_low_offset0  = array[index++];
	pdata->ltr559_proximity_high_offset0 = array[index++];
	pdata->ltr559_proximity_low_offset1  = array[index++];
	pdata->ltr559_proximity_high_offset1 = array[index++];
	pdata->ltr559_proximity_low_offset2  = array[index++];
	pdata->ltr559_proximity_high_offset2 = array[index++];
	pdata->ltr559_struct_noise_threshold = array[index++];

	pr_debug("%s: prox_threshold_hi=%d, prox_threshold_lo=%d\n", __func__, pdata->prox_threshold_hi, pdata->prox_threshold_lo);
	pr_debug("%s: prox_factory_threshold_hi=%d, prox_factory_threshold_lo=%d\n", __func__, pdata->prox_factory_threshold_hi, pdata->prox_factory_threshold_lo);
	pr_debug("%s: gpio_int=%d, irq_gpio_flags=%d\n", __func__, pdata->gpio_int, pdata->irq_gpio_flags);
	pr_debug("%s: ltr559_interrupt_persist=%d\n", __func__, pdata->interrupt_persist);
	pr_debug("%s: ltr559_ps_led=%d\n", __func__, pdata->ps_led);
	pr_debug("%s: ltr559_ps_n_pulses=%d\n", __func__, pdata->ps_n_pulses);
	pr_debug("%s: ltr559_ps_meas_rate=%d\n", __func__, pdata->ps_meas_rate);
	pr_debug("%s: ltr559_als_meas_rate=%d\n", __func__, pdata->als_meas_rate);
	
	return rc;
}

#ifdef CONFIG_YL_SENSORS_POWER
static int sensors_power_on(struct ltr559_data *ltr559, bool on)
{
	int rc,ret;

	if (!on)
        	goto power_off;

	rc = regulator_enable(ltr559->vdd_ana);
	if (rc) {
		dev_err(&ltr559->i2c_client->dev,
			"Regulator vdd_ana enable failed rc=%d\n", rc);
		return rc;
	}
	rc = regulator_enable(ltr559->vcc_i2c);
	if (rc) {
        	dev_err(&ltr559->i2c_client->dev,
			"Regulator vcc_i2c enable failed rc=%d\n", rc);
		regulator_disable(ltr559->vdd_ana);
	}
	return rc;

power_off:
	rc = regulator_disable(ltr559->vdd_ana);
	if (rc) {
		dev_err(&ltr559->i2c_client->dev,
			"Regulator vdd_ana disable failed rc=%d\n", rc);
		return rc;
	}
	rc = regulator_disable(ltr559->vcc_i2c);
	if (rc) {
		dev_err(&ltr559->i2c_client->dev,
			"Regulator vcc_i2c disable failed rc=%d\n", rc);
		ret = regulator_enable(ltr559->vdd_ana);
		if (ret) {
		dev_err(&ltr559->i2c_client->dev,
                    "Regulator vdd_ana enable failed ret=%d\n", ret);
		return ret;
		}
	}
	return rc;
}

static int sensors_power_init(struct ltr559_data *ltr559, bool on)
{
	int rc;

	if (!on)
		goto pwr_deinit;

	ltr559->vdd_ana = regulator_get(&ltr559->i2c_client->dev, "vdd_ana");
	if (IS_ERR(ltr559->vdd_ana)) {
		rc = PTR_ERR(ltr559->vdd_ana);
		dev_err(&ltr559->i2c_client->dev,
			"Regulator get failed vdd_ana rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(ltr559->vdd_ana) > 0) {
		rc = regulator_set_voltage(ltr559->vdd_ana, 2600000,3300000);
		if (rc) {
			dev_err(&ltr559->i2c_client->dev,
				"Regulator set_vtg failed vdd_ana rc=%d\n", rc);
			goto reg_vdd_ana_put;
		}
    	}

	ltr559->vcc_i2c = regulator_get(&ltr559->i2c_client->dev, "vcc_i2c");
	if (IS_ERR(ltr559->vcc_i2c)) {
		rc = PTR_ERR(ltr559->vcc_i2c);
		dev_err(&ltr559->i2c_client->dev,
			"Regulator get failed vcc_i2c rc=%d\n", rc);
		goto reg_vdd_ana_set_vtg;
	}

	if (regulator_count_voltages(ltr559->vcc_i2c) > 0) {
		rc = regulator_set_voltage(ltr559->vcc_i2c, 1800000,1800000);
		if (rc) {
			dev_err(&ltr559->i2c_client->dev,
				"Regulator set_vtg failed vcc_i2c rc=%d\n", rc);
			goto reg_vcc_i2c_put;
		}
	}
	return 0;

reg_vcc_i2c_put:
	regulator_put(ltr559->vcc_i2c);
reg_vdd_ana_set_vtg:
	if (regulator_count_voltages(ltr559->vdd_ana) > 0)
		regulator_set_voltage(ltr559->vdd_ana, 0, 3300000);
reg_vdd_ana_put:
	regulator_put(ltr559->vdd_ana);
	return rc;

pwr_deinit:
	if (regulator_count_voltages(ltr559->vdd_ana) > 0)
		regulator_set_voltage(ltr559->vdd_ana, 0, 3300000);
	regulator_put(ltr559->vdd_ana);
	if (regulator_count_voltages(ltr559->vcc_i2c) > 0)
		regulator_set_voltage(ltr559->vcc_i2c, 0, 1800000);
	regulator_put(ltr559->vcc_i2c);
	return 0;
}
#endif

/* Report PS input event */
static void report_ps_input_event(struct ltr559_data *ltr559)
{
	u8 isproximity = 0;
	int err, psdata;
	char buf[4]={0};

	err = ltr559_ps_read(ltr559, &psdata);
	if (err) {
		pr_err("%s: ltr559_ps_read fail\n", __func__);
		return;
	}

	if (ltr559->pdata->prox_threshold_lo > psdata) {
		isproximity = PROX_FAR;
	
		buf[0] = 0x0;
		buf[1] = 0x0;
		buf[2] = ltr559->pdata->prox_threshold_hi & 0x0FF;
		buf[3] = (ltr559->pdata->prox_threshold_hi >> 8) & 0x07;
	} else if (ltr559->pdata->prox_threshold_hi < psdata) {
		isproximity = PROX_NEAR;

		buf[0] = ltr559->pdata->prox_threshold_lo & 0x0FF;
		buf[1] = (ltr559->pdata->prox_threshold_lo >> 8) & 0x07;
		buf[2] = 0xFF;
		buf[3] = 0xFF;
	} 

	/* 3 is close, 5 is far */
	if(PROX_NEAR==isproximity || PROX_FAR==isproximity)
	{
		pr_info("ltr559: %s: psdata: %d isproximity: %d, hi: %d, lo:%d\n",
			__func__, psdata, isproximity, ltr559->pdata->prox_threshold_hi, ltr559->pdata->prox_threshold_lo);
		input_event(ltr559->input_dev_proximity, EV_MSC, MSC_SCAN, isproximity);
		input_sync(ltr559->input_dev_proximity);

		ltr559_i2c_write(ltr559, LTR559_PS_THRES_LOW_0, buf[0]);	
		ltr559_i2c_write(ltr559, LTR559_PS_THRES_LOW_1, buf[1]);
		ltr559_i2c_write(ltr559, LTR559_PS_THRES_UP_0, buf[2]);
		ltr559_i2c_write(ltr559, LTR559_PS_THRES_UP_1, buf[3]);  
	}
}

/* Report ALS input event */
static void report_als_input_event(struct ltr559_data *ltr559)
{
	int8_t ret;
	int i, adc_value;
	int thresh_hi, thresh_lo, thresh_delta;
	char buf[4]={0};

	ret = ltr559_als_read(ltr559, &adc_value);
	if (ret) {
		pr_err("%s: als_read failed\n", __func__);
		return;
	}
	//input_report_abs(ltr559->input_dev_light, ABS_MISC, adc_value);
	input_event(ltr559->input_dev_light, EV_MSC, MSC_SCAN, adc_value);
	input_sync(ltr559->input_dev_light);

	/* Adjust measurement range using a crude filter to prevent interrupt jitter */
	thresh_delta = (adc_value >> 12)+2;
	thresh_lo = adc_value - thresh_delta;
	thresh_hi = adc_value + thresh_delta;
	if (thresh_lo < LTR559_LIGHT_LUX_MIN) {
		thresh_lo = LTR559_LIGHT_LUX_MIN;
	}
	if (thresh_hi > LTR559_LIGHT_LUX_MAX) {
		thresh_hi = LTR559_LIGHT_LUX_MAX;
	}

	buf[0] = thresh_hi&0xFF;
	buf[1] = (thresh_hi>>8)&0xFF;
	buf[2] = thresh_lo&0xFF;
	buf[3] = (thresh_lo>>8)&0xFF;

	for(i = 0; i < 4; i++){
		ret = ltr559_i2c_write(ltr559, LTR559_ALS_THRES_UP_0 + i, buf[i]);
		if (ret) {
			pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_ALS_THRES_UP_0 + i);
			return;
		}
	}
}

static int ltr559_get_data(void)
{
	int8_t ret;
	uint8_t status;
	uint8_t	interrupt_stat, newdata;
	struct ltr559_data *ltr559 = ltr559_dev;

	ret = ltr559_i2c_read(ltr559, LTR559_ALS_PS_STATUS, &status);
	if (ret < 0) {
		pr_err("%s: i2c_read 0x%x failed\n", __func__, LTR559_ALS_PS_STATUS);
		return ret;
	}

	interrupt_stat = status & 0x0A;
	newdata = status & 0x05;
	pr_debug("%s: interrupt_stat = %d, newdata = %d\n", __func__, interrupt_stat, newdata);

	if (!interrupt_stat) {
                uint8_t value;
                
		pr_err("%s Unexpected interrupt status:0x%02x\n", __func__, status);
                /*added by liwenpeng start*/
                printk("%s(): interrupt gpio level=%d\n",__func__,gpio_get_value(ltr559->pdata->gpio_int));
                ret = ltr559_i2c_read(ltr559, LTR559_INTERRUPT, &value);
                if (ret < 0) {
                   printk("%s(): read LTR559_INTERRUPT error\n",__func__);
                }else{
                   printk("%s(): LTR559_INTERRUPT=0x%x\n",__func__,value);
                }
                /*added by liwenpeng end*/
                
             
	} else {
		// PS interrupt and PS with new data
		if ((interrupt_stat & 0x02) && (newdata & 0x01)) {
			report_ps_input_event(ltr559);
		}
		
		// ALS interrupt and ALS with new data
		if ((interrupt_stat & 0x08) && (newdata & 0x04)) {
			report_als_input_event(ltr559);
		}
	}
	
	return ret;
}

/* Work when interrupt */
static void ltr559_schedwork(struct work_struct *work)
{
#ifdef SENSORS_IRQ_WAIT_QUEUE
	int wait_event;
#endif
	struct ltr559_data *ltr559 = ltr559_dev;

#ifdef SENSORS_IRQ_WAIT_QUEUE
	wait_event = wait_event_interruptible(wait_sensors_resume,
		sensors_resume == 1);
	if (wait_event < 0) {
		pr_err("%s: wait_event_interruptible fail\n", __func__);
	}
#endif

	ltr559_get_data();
	enable_irq(ltr559->als_ps_int);
}

static DECLARE_WORK(irq_workqueue, ltr559_schedwork);

/* IRQ Handler */
static irqreturn_t ltr559_irq_handler(int irq, void *data)
{
	struct ltr559_data *ltr559 = data;

	//pr_debug("%s: Entry\n", __func__);

#ifdef SENSORS_WAKE_LOCK_TIMEOUT
	wake_lock_timeout(&ltr559->prx_wake_lock, 3*HZ);
#endif

	/* disable an irq without waiting */
	disable_irq_nosync(ltr559->als_ps_int);

	schedule_work(&irq_workqueue);

	return IRQ_HANDLED;
}

static int ltr559_gpio_irq(struct ltr559_data *ltr559)
{
	int rc = 0;

	rc = gpio_request(ltr559->pdata->gpio_int, "ltr559_irq");
	if (rc) {
		pr_err("%s: gpio_request %d failed\n", __func__, ltr559->pdata->gpio_int);
		return rc;
	}

	rc = gpio_direction_input(ltr559->pdata->gpio_int);
	if (rc) {
		pr_err("%s: unable to set directon for gpio = %d\n", __func__, ltr559->pdata->gpio_int);
		goto err_free_gpio;
	}

	ltr559->als_ps_int = gpio_to_irq(ltr559->pdata->gpio_int);
	pr_debug("%s: gpio_int=%d, als_ps_int=%d\n", __func__, ltr559->pdata->gpio_int, ltr559->als_ps_int);

	/* Configure an active low trigger interrupt for the device */
	rc = request_irq(ltr559->als_ps_int, ltr559_irq_handler, IRQF_TRIGGER_LOW, "ltr559_irq", ltr559);
	if (rc) {
		pr_err("%s: request_irq failed rc = %d\n", __func__, rc);
		goto err_free_gpio;
	}
	return rc;

err_free_gpio:
	gpio_free(ltr559->pdata->gpio_int);
	return rc;
}

static int ltr559_proximity_crosstalk(int count)
{
	int err = 0;
	int psdata=0;
	unsigned int sum=0;
	unsigned int struct_noise=0;
	unsigned char invalid_num=0;

	while (0 < count) {
		err = ltr559_ps_read(ltr559_dev, &psdata);
		pr_debug("%s: psdata = %d\n", __func__, psdata);
		if (!err) {
			sum += psdata;
			invalid_num++;
		}
		count--;
		msleep(50);
	}

	if(5 > invalid_num)
		return -1;

	struct_noise = sum/invalid_num;
	pr_err("%s: sum=%d, invalid_num=%d, struct_noise=%d\n",
		__func__, sum, invalid_num, struct_noise);

	return struct_noise;
}

static int ltr559_set_prox_threshold(int struct_noise)
{
	struct ltr559_data *ltr559 = ltr559_dev;

	if (90 > struct_noise) {
		ltr559->pdata->prox_threshold_lo = struct_noise+ltr559->pdata->ltr559_proximity_low_offset0;
		ltr559->pdata->prox_threshold_hi = struct_noise+ ltr559->pdata->ltr559_proximity_high_offset0;		
	} else if (255 > struct_noise) {
		ltr559->pdata->prox_threshold_lo = struct_noise+ ltr559->pdata->ltr559_proximity_low_offset1;
		ltr559->pdata->prox_threshold_hi = struct_noise+ltr559->pdata->ltr559_proximity_high_offset1;
	} else if (ltr559->pdata->ltr559_struct_noise_threshold > struct_noise) {
		ltr559->pdata->prox_threshold_lo = struct_noise+ltr559->pdata->ltr559_proximity_low_offset2;
		ltr559->pdata->prox_threshold_hi = struct_noise+ltr559->pdata->ltr559_proximity_high_offset2;
	} else {
		pr_err("%s: Calibration fail, prox_avg = [%d]\n", __func__, struct_noise);
		return -1;
	}

	pr_err("%s: thres_hi = [%d], thres_lo = [%d], prox_avg = [%d]\n", __func__,
		ltr559->pdata->prox_threshold_hi, ltr559->pdata->prox_threshold_lo, struct_noise);
	return 0;
}

static long ltr559_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	u8 prox_param[8] = {0};
	unsigned int struct_noise=0;
	struct ltr559_data *ltr559 = ltr559_dev;
	struct prox_offset ltr559_cal_data;
	struct prox_offset *ltr559_cal_ptr = &ltr559_cal_data;
	unsigned long prox_update;

	pr_debug("%s: cmd = %d\n", __func__, _IOC_NR(cmd));

	mutex_lock(&ltr559->lock);
	switch(cmd){
	case LTR559_IOCTL_ALS_ON32:
		pr_debug("%s: ALS_ON Entry\n", __func__);

		if (!(ltr559->power_state & LIGHT_ENABLED)) {
			ret = ltr559_als_init_regs(ltr559);
			if (ret < 0) {
				pr_err("%s: ltr559_als_init_regs failed\n", __func__);
				break;
			}

			ret = ltr559_light_enable(ltr559);
			if (ret) {
				pr_err("%s: ltr559_light_enable failed\n", __func__);
				break;
			}
			ltr559->power_state |= LIGHT_ENABLED;
			hrtimer_start(&ltr559->light_timer, ltr559->light_poll_delay, HRTIMER_MODE_REL);
		}

		pr_debug("%s: ALS_ON exit\n", __func__);
		break;

	case LTR559_IOCTL_ALS_OFF32:
		pr_debug("%s: ALS_OFF Entry\n", __func__);

		if (ltr559->power_state & LIGHT_ENABLED) {
			ret = ltr559_light_disable(ltr559);
			if (ret) {
				pr_err("%s: ltr559_light_disable failed\n", __func__);
				break;
			}
			ltr559->power_state &= ~LIGHT_ENABLED;
			hrtimer_cancel(&ltr559->light_timer);
			cancel_work_sync(&ltr559->work_light);
		}

		pr_debug("%s: ALS_OFF exit\n", __func__);
		break;

	case LTR559_IOCTL_PROX_ON32:
		pr_debug("%s: PROX_ON Entry, hi=%d, lo=%d, factory_hi=%d, factory_lo=%d\n", __func__, ltr559->pdata->prox_threshold_hi,
			ltr559->pdata->prox_threshold_lo, ltr559->pdata->prox_factory_threshold_hi, ltr559->pdata->prox_factory_threshold_lo);

		if (!(ltr559->power_state & PROXIMITY_ENABLED)) {
			ret = ltr559_ps_cal_init(ltr559);
			if (ret < 0) {
				pr_err("%s: ltr559_ps_cal_init failed\n", __func__);
				break;
			}
                        disable_irq(ltr559->als_ps_int);
	  		ret = ltr559_proximity_enable(ltr559);
	  		if (ret) {
				pr_err("%s: ltr559_proximity_enable failed\n", __func__);
                                enable_irq(ltr559->als_ps_int);
				break;
	  		}
			ltr559->power_state |= PROXIMITY_ENABLED;
			ret = ltr559_proximity_crosstalk(6);
			if (ret < 0) {
				pr_err("%s: fast crosstalk failed\n", __func__);
                                enable_irq(ltr559->als_ps_int);
				break;
			}
			ltr559_set_prox_threshold(ret);

			if ((ltr559->pdata->prox_factory_threshold_hi != 0)&&
				(ltr559->pdata->prox_threshold_hi > ltr559->pdata->prox_factory_threshold_hi + 250)) {
				ltr559->pdata->prox_threshold_hi = ltr559->pdata->prox_factory_threshold_hi;
				ltr559->pdata->prox_threshold_lo = ltr559->pdata->prox_factory_threshold_lo; 
				pr_err("%s: use factory calibration hi = [%d], lo = [%d]\n", __func__,
					ltr559->pdata->prox_threshold_hi, ltr559->pdata->prox_threshold_lo);
			}

			ret = ltr559_ps_init_regs(ltr559);
                        enable_irq(ltr559->als_ps_int);
			if (ret < 0) {
				pr_err("%s: ltr559_ps_init_regs failed\n", __func__);
				break;
			}

			ret = irq_set_irq_wake(ltr559->als_ps_int, 1);
			if (ret) {
				pr_err("%s: irq_set_irq_wake failed\n", __func__);
				break;
			}
		}

		pr_debug("%s: PROX_ON exit\n", __func__);
	  	break;

	  case LTR559_IOCTL_PROX_OFF32:
		pr_debug("%s: PROX_OFF Entry\n", __func__);

		if (ltr559->power_state & PROXIMITY_ENABLED) {
			ret = irq_set_irq_wake(ltr559->als_ps_int, 0);
			if (ret) {
				pr_err("%s: irq_set_irq_wake failed\n", __func__);
				break;
			}
		
	  		ret = ltr559_proximity_disable(ltr559);
	  		if (ret) {
				pr_err("%s: ltr559_proximity_disable failed\n", __func__);
				break;
	  		}
			ltr559->power_state &= ~PROXIMITY_ENABLED;
		}

		pr_debug("%s: PROX_OFF exit\n", __func__);
	  	break;

	case LTR559_IOCTL_PROX_CALIBRATE32:
		pr_debug("%s: PROX_CALIBRATE Entry\n", __func__);

		struct_noise = ltr559_proximity_crosstalk(15);
		if (struct_noise < 0) {
			pr_err("%s: ltr559_proximity_crosstalk failed\n", __func__);
		}

		ltr559_set_prox_threshold(struct_noise);

		prox_param[0] = struct_noise < ltr559->pdata->ltr559_struct_noise_threshold ? 1 : 2;
		prox_param[5] = ltr559->pdata->prox_version;
		if (prox_param[0] == 1) {
			prox_param[1] = ltr559->pdata->prox_threshold_hi & 0x00ff;
			prox_param[2] = (ltr559->pdata->prox_threshold_hi & 0xff00) >> 8;
			prox_param[3] = ltr559->pdata->prox_threshold_lo & 0x00ff;
			prox_param[4] = (ltr559->pdata->prox_threshold_lo & 0xff00) >> 8;
		}
		sensparams_write_to_flash(SENSPARAMS_TYPE_PROX, prox_param, 6);

		pr_debug("%s: prox_hi = %d\n", __func__, ltr559->pdata->prox_threshold_hi);
		pr_debug("%s: prox_lo = %d\n", __func__, ltr559->pdata->prox_threshold_lo);
		pr_debug("%s: prox_avg = %d\n", __func__, struct_noise);

		((struct prox_offset *)ltr559_cal_ptr)->x = (unsigned short)(ltr559->pdata->prox_threshold_hi);
		((struct prox_offset *)ltr559_cal_ptr)->y = (unsigned short)(ltr559->pdata->prox_threshold_lo);
		((struct prox_offset *)ltr559_cal_ptr)->z = (unsigned short)(struct_noise);
		((struct prox_offset *)ltr559_cal_ptr)->offset = 0;
		((struct prox_offset *)ltr559_cal_ptr)->key = struct_noise < ltr559->pdata->ltr559_struct_noise_threshold ? 1 : 2;

		if(copy_to_user((struct prox_offset *)arg, ltr559_cal_ptr, sizeof(ltr559_cal_data)))
		{	
			printk(KERN_ERR"%s: data trans error,use default offset ! \n", __func__);
			ret = -EFAULT;
		}

		pr_debug("%s: PROX_CALIBRATE exit\n", __func__);
	  	break;

	  case LTR559_IOCTL_PROX_OFFSET32:
		pr_debug("%s: PROX_OFFSET Entry\n", __func__);

		sensparams_read_from_flash(SENSPARAMS_TYPE_PROX, prox_param, 6);
		if (prox_param[0] != 0) {
			ltr559->pdata->prox_threshold_hi = (prox_param[2] << 8) | prox_param[1];
			ltr559->pdata->prox_threshold_lo = (prox_param[4] << 8) | prox_param[3];
			ltr559->pdata->prox_factory_threshold_hi = ltr559->pdata->prox_threshold_hi;
			ltr559->pdata->prox_factory_threshold_lo = ltr559->pdata->prox_threshold_lo;
		}
		if ((ltr559->pdata->prox_version) != prox_param[5]) {
			prox_update = 1;
			pr_err("%s: need for recalibration\n", __func__);
		} else {
			prox_update = 0;
		}

		ret = copy_to_user((unsigned long *)arg, &prox_update, sizeof(unsigned long));
		if (ret)
		{
			pr_err("%s: copy_to_user() failed, ret=%d\n", __func__, ret);
			ret = -EFAULT;
		}

		pr_debug("%s: PROX_OFFSET prox_version = %d\n", __func__, ltr559->pdata->prox_version);
		pr_debug("%s: PROX_OFFSET prox_high = %d\n", __func__, ltr559->pdata->prox_threshold_hi);
		pr_debug("%s: PROX_OFFSET prox_low = %d\n", __func__, ltr559->pdata->prox_threshold_lo);
		pr_debug("%s: PROX_OFFSET prox_factory_high = %d\n", __func__, ltr559->pdata->prox_factory_threshold_hi);
		pr_debug("%s: PROX_OFFSET prox_factory_low = %d\n", __func__, ltr559->pdata->prox_factory_threshold_lo);

	  	break;

	  default:
		pr_err("%s: DEFAULT!\n", __func__);
		ret = -EINVAL;

		break;
	}
	mutex_unlock(&ltr559->lock);

	return ret;
}



static long ltr559_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	u8 prox_param[8] = {0};
	unsigned int struct_noise=0;
	struct ltr559_data *ltr559 = ltr559_dev;
	struct prox_offset ltr559_cal_data;
	struct prox_offset *ltr559_cal_ptr = &ltr559_cal_data;
	unsigned long prox_update;

	pr_debug("%s: cmd = %d\n", __func__, _IOC_NR(cmd));

	mutex_lock(&ltr559->lock);
	switch(cmd){
	case LTR559_IOCTL_ALS_ON:
		pr_debug("%s: ALS_ON Entry\n", __func__);

		if (!(ltr559->power_state & LIGHT_ENABLED)) {
			ret = ltr559_als_init_regs(ltr559);
			if (ret < 0) {
				pr_err("%s: ltr559_als_init_regs failed\n", __func__);
				break;
			}

			ret = ltr559_light_enable(ltr559);
			if (ret) {
				pr_err("%s: ltr559_light_enable failed\n", __func__);
				break;
			}
			ltr559->power_state |= LIGHT_ENABLED;
			hrtimer_start(&ltr559->light_timer, ltr559->light_poll_delay, HRTIMER_MODE_REL);
		}

		pr_debug("%s: ALS_ON exit\n", __func__);
		break;

	case LTR559_IOCTL_ALS_OFF:
		pr_debug("%s: ALS_OFF Entry\n", __func__);

		if (ltr559->power_state & LIGHT_ENABLED) {
			ret = ltr559_light_disable(ltr559);
			if (ret) {
				pr_err("%s: ltr559_light_disable failed\n", __func__);
				break;
			}
			ltr559->power_state &= ~LIGHT_ENABLED;
			hrtimer_cancel(&ltr559->light_timer);
			cancel_work_sync(&ltr559->work_light);
		}

		pr_debug("%s: ALS_OFF exit\n", __func__);
		break;

	case LTR559_IOCTL_PROX_ON:
		pr_debug("%s: PROX_ON Entry, hi=%d, lo=%d, factory_hi=%d, factory_lo=%d\n", __func__, ltr559->pdata->prox_threshold_hi,
			ltr559->pdata->prox_threshold_lo, ltr559->pdata->prox_factory_threshold_hi, ltr559->pdata->prox_factory_threshold_lo);

		if (!(ltr559->power_state & PROXIMITY_ENABLED)) {
			ret = ltr559_ps_cal_init(ltr559);
			if (ret < 0) {
				pr_err("%s: ltr559_ps_cal_init failed\n", __func__);
				break;
			}
                        disable_irq(ltr559->als_ps_int);
	  		ret = ltr559_proximity_enable(ltr559);
	  		if (ret) {
				pr_err("%s: ltr559_proximity_enable failed\n", __func__);
                                enable_irq(ltr559->als_ps_int);
				break;
	  		}
			ltr559->power_state |= PROXIMITY_ENABLED;
			ret = ltr559_proximity_crosstalk(6);
			if (ret < 0) {
				pr_err("%s: fast crosstalk failed\n", __func__);
                                enable_irq(ltr559->als_ps_int);
				break;
			}
			ltr559_set_prox_threshold(ret);

			if ((ltr559->pdata->prox_factory_threshold_hi != 0)&&
				(ltr559->pdata->prox_threshold_hi > ltr559->pdata->prox_factory_threshold_hi + 250)) {
				ltr559->pdata->prox_threshold_hi = ltr559->pdata->prox_factory_threshold_hi;
				ltr559->pdata->prox_threshold_lo = ltr559->pdata->prox_factory_threshold_lo; 
				pr_err("%s: use factory calibration hi = [%d], lo = [%d]\n", __func__,
					ltr559->pdata->prox_threshold_hi, ltr559->pdata->prox_threshold_lo);
			}

			ret = ltr559_ps_init_regs(ltr559);
                        enable_irq(ltr559->als_ps_int);
			if (ret < 0) {
				pr_err("%s: ltr559_ps_init_regs failed\n", __func__);
				break;
			}

			ret = irq_set_irq_wake(ltr559->als_ps_int, 1);
			if (ret) {
				pr_err("%s: irq_set_irq_wake failed\n", __func__);
				break;
			}
		}

		pr_debug("%s: PROX_ON exit\n", __func__);
	  	break;

	  case LTR559_IOCTL_PROX_OFF:
		pr_debug("%s: PROX_OFF Entry\n", __func__);

		if (ltr559->power_state & PROXIMITY_ENABLED) {
			ret = irq_set_irq_wake(ltr559->als_ps_int, 0);
			if (ret) {
				pr_err("%s: irq_set_irq_wake failed\n", __func__);
				break;
			}
		
	  		ret = ltr559_proximity_disable(ltr559);
	  		if (ret) {
				pr_err("%s: ltr559_proximity_disable failed\n", __func__);
				break;
	  		}
			ltr559->power_state &= ~PROXIMITY_ENABLED;
		}

		pr_debug("%s: PROX_OFF exit\n", __func__);
	  	break;

	case LTR559_IOCTL_PROX_CALIBRATE:
		pr_debug("%s: PROX_CALIBRATE Entry\n", __func__);

		struct_noise = ltr559_proximity_crosstalk(15);
		if (struct_noise < 0) {
			pr_err("%s: ltr559_proximity_crosstalk failed\n", __func__);
		}

		ltr559_set_prox_threshold(struct_noise);

		prox_param[0] = struct_noise < ltr559->pdata->ltr559_struct_noise_threshold ? 1 : 2;
		prox_param[5] = ltr559->pdata->prox_version;
		if (prox_param[0] == 1) {
			prox_param[1] = ltr559->pdata->prox_threshold_hi & 0x00ff;
			prox_param[2] = (ltr559->pdata->prox_threshold_hi & 0xff00) >> 8;
			prox_param[3] = ltr559->pdata->prox_threshold_lo & 0x00ff;
			prox_param[4] = (ltr559->pdata->prox_threshold_lo & 0xff00) >> 8;
		}
		sensparams_write_to_flash(SENSPARAMS_TYPE_PROX, prox_param, 6);

		pr_debug("%s: prox_hi = %d\n", __func__, ltr559->pdata->prox_threshold_hi);
		pr_debug("%s: prox_lo = %d\n", __func__, ltr559->pdata->prox_threshold_lo);
		pr_debug("%s: prox_avg = %d\n", __func__, struct_noise);

		((struct prox_offset *)ltr559_cal_ptr)->x = (unsigned short)(ltr559->pdata->prox_threshold_hi);
		((struct prox_offset *)ltr559_cal_ptr)->y = (unsigned short)(ltr559->pdata->prox_threshold_lo);
		((struct prox_offset *)ltr559_cal_ptr)->z = (unsigned short)(struct_noise);
		((struct prox_offset *)ltr559_cal_ptr)->offset = 0;
		((struct prox_offset *)ltr559_cal_ptr)->key = struct_noise < ltr559->pdata->ltr559_struct_noise_threshold ? 1 : 2;

		if(copy_to_user((struct prox_offset *)arg, ltr559_cal_ptr, sizeof(ltr559_cal_data)))
		{	
			printk(KERN_ERR"%s: data trans error,use default offset ! \n", __func__);
			ret = -EFAULT;
		}

		pr_debug("%s: PROX_CALIBRATE exit\n", __func__);
	  	break;

	  case LTR559_IOCTL_PROX_OFFSET:
		pr_debug("%s: PROX_OFFSET Entry\n", __func__);

		sensparams_read_from_flash(SENSPARAMS_TYPE_PROX, prox_param, 6);
		if (prox_param[0] != 0) {
			ltr559->pdata->prox_threshold_hi = (prox_param[2] << 8) | prox_param[1];
			ltr559->pdata->prox_threshold_lo = (prox_param[4] << 8) | prox_param[3];
			ltr559->pdata->prox_factory_threshold_hi = ltr559->pdata->prox_threshold_hi;
			ltr559->pdata->prox_factory_threshold_lo = ltr559->pdata->prox_threshold_lo;
		}
		if ((ltr559->pdata->prox_version) != prox_param[5]) {
			prox_update = 1;
			pr_err("%s: need for recalibration\n", __func__);
		} else {
			prox_update = 0;
		}

		ret = copy_to_user((unsigned long *)arg, &prox_update, sizeof(unsigned long));
		if (ret)
		{
			pr_err("%s: copy_to_user() failed, ret=%d\n", __func__, ret);
			ret = -EFAULT;
		}

		pr_debug("%s: PROX_OFFSET prox_version = %d\n", __func__, ltr559->pdata->prox_version);
		pr_debug("%s: PROX_OFFSET prox_high = %d\n", __func__, ltr559->pdata->prox_threshold_hi);
		pr_debug("%s: PROX_OFFSET prox_low = %d\n", __func__, ltr559->pdata->prox_threshold_lo);
		pr_debug("%s: PROX_OFFSET prox_factory_high = %d\n", __func__, ltr559->pdata->prox_factory_threshold_hi);
		pr_debug("%s: PROX_OFFSET prox_factory_low = %d\n", __func__, ltr559->pdata->prox_factory_threshold_lo);

	  	break;

	  default:
		pr_err("%s: DEFAULT!\n", __func__);
		ret = -EINVAL;

		break;
	}
	mutex_unlock(&ltr559->lock);

	return ret;
}

static int ltr559_open(struct inode *inode, struct file *file)
{
	pr_debug("%s: start\n", __func__);
	return 0;
}

static int ltr559_release(struct inode *inode, struct file *file)
{
	pr_debug("%s: start\n", __func__);
	return 0;
}

static struct file_operations ltr559_fops = {
	.owner		= THIS_MODULE,
	.open 		= ltr559_open,
	.release	= ltr559_release,
	#ifdef CONFIG_COMPAT
	.compat_ioctl	= ltr559_ioctl_compat,
	#endif
	.unlocked_ioctl	= ltr559_ioctl,
};

static struct miscdevice ltr559_device = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= LTR559_DEVICE_NAME,
	.fops 	= &ltr559_fops,
};

#ifdef CONFIG_SENSORS
static ssize_t ltr559_reg_addr_store(struct device *dev,
		struct device_attribute *attr,const char *buf, size_t count)
{
	unsigned long val;
	struct ltr559_data *ltr559 = ltr559_dev;

	if (strict_strtoul(buf, 16, &val))
		return -EINVAL;

	ltr559->reg_addr = val;

	return count;
}

static ssize_t ltr559_reg_addr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	u8 data;
	struct ltr559_data *ltr559 = ltr559_dev;
	
	ret = ltr559_i2c_read(ltr559, ltr559->reg_addr, &data);
	/*TODO: error need to be managed */
	ret = sprintf(buf, "addr_0x%02x = 0x%02x\n", ltr559->reg_addr, data);
	return ret;
}

static DEVICE_ATTR(reg_addr, S_IRUGO|S_IWUSR, ltr559_reg_addr_show, ltr559_reg_addr_store);

static ssize_t ltr559_reg_value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t ret;
	u8 data[32];
	char *ptr = buf;
	struct ltr559_data *ltr559 = ltr559_dev;

	for (i = 0; i < 32; i++) {
		ret = ltr559_i2c_read(ltr559, LTR559_ALS_CONTR + i, &data[i]);
		if (ret) {
			pr_err("%s: ltr559_i2c_read 0x%x failed\n", __func__, LTR559_ALS_CONTR + i);
			return ret;
		}
	}

	for (i = 0; i < 32; i++) {
		ptr += sprintf(ptr, "0x%02X=0x%02X ", LTR559_ALS_CONTR + i, data[i]);
		if ((i % 8) == 7)
			ptr += sprintf(ptr, "\n");
	}

	return (ptr-buf);
}

static DEVICE_ATTR(reg_value, S_IRUGO, ltr559_reg_value_show, NULL);

static struct attribute *ltr559_attribute[] = {
	&dev_attr_reg_addr.attr,
	&dev_attr_reg_value.attr,
	NULL
};

static struct attribute_group ltr559_attribute_group = {
	.attrs = ltr559_attribute
};

static int ltr559_als_poll_delay(struct sensors_classdev *sensors_cdev,
		unsigned int delay_msec)
{
	int64_t new_delay;
	struct ltr559_data *ltr559 = ltr559_dev;

	new_delay = delay_msec;
	pr_err("%s new_delay %lld\n", __func__, new_delay);
	if (200000000 > new_delay)
           new_delay = 200000000;

	if (new_delay != ktime_to_ns(ltr559->light_poll_delay)) {
		ltr559->light_poll_delay = ns_to_ktime(new_delay);
		if (ltr559->power_state & LIGHT_ENABLED) {
			hrtimer_cancel(&ltr559->light_timer);
			cancel_work_sync(&ltr559->work_light);
			hrtimer_start(&ltr559->light_timer, ltr559->light_poll_delay, HRTIMER_MODE_REL);
		}
	}

	return 0;
}

static int ltr559_als_set_enable(struct sensors_classdev *sensors_cdev,
		unsigned int enable)
{
	int ret = -EINVAL;
	struct ltr559_data *ltr559 = ltr559_dev;

	if ((enable != 0) && (enable != 1)) {
		pr_err("%s: invalid value(%d)\n", __func__, enable);
		return -EINVAL;
	}

	pr_err("%s enable = %d, old state = %d\n",
		    __func__, enable, (ltr559->power_state & LIGHT_ENABLED) ? 1 : 0);
	if (enable && !(ltr559->power_state & LIGHT_ENABLED))
	{
		ret = ltr559_light_enable(ltr559);
		if (!ret)
		{
			ltr559->power_state |= LIGHT_ENABLED;
			hrtimer_start(&ltr559->light_timer, ltr559->light_poll_delay, HRTIMER_MODE_REL);
		}
	}
	else if (!enable && (ltr559->power_state & LIGHT_ENABLED))
	{
		ret = ltr559_light_disable(ltr559);
		if (!ret)
		{
			ltr559->power_state &= ~LIGHT_ENABLED;
			hrtimer_cancel(&ltr559->light_timer);
			cancel_work_sync(&ltr559->work_light);
		}
	}

	return ret;
}

static int ltr559_ps_set_enable(struct sensors_classdev *sensors_cdev,
		unsigned int enable)
{
	int ret = -EINVAL;
	struct ltr559_data *ltr559 = ltr559_dev;

	if ((enable != 0) && (enable != 1)) {
		pr_err("%s: invalid value(%d)\n", __func__, enable);
		return -EINVAL;
	}

	if (enable && !(ltr559->power_state & PROXIMITY_ENABLED)) {
		ret = ltr559_proximity_enable(ltr559);
		if (ret)
			pr_err("%s: ret=%d, enable=%d\n", __func__, ret, enable);
	}
	else if (!enable && (ltr559->power_state & PROXIMITY_ENABLED))
	{
		ret = ltr559_proximity_disable(ltr559);
		if (ret)
			pr_err("%s: ret=%d, enable=%d\n", __func__, ret, enable);
	}

	return ret;
}
#endif
	
static int ltr559_i2c_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	struct ltr559_data *ltr559;
	struct ltr559_platform_data *pdata;
	int chip_id = 0;

	pr_info("%s: start\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: i2c functionality check failed!\n", __func__);
		return ret;
	}

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev, sizeof(struct ltr559_platform_data), GFP_KERNEL);
		if (NULL == pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
		ret = ltr559_parse_dt(&client->dev, pdata);
		if (ret) {
	 		dev_err(&client->dev, "Get pdata failed from Device Tree\n");
			return ret;
		}
	} else {
		pdata = client->dev.platform_data;
		if (NULL == pdata) {
			dev_err(&client->dev, "pdata is NULL\n");
			return -ENOMEM;
		}
	}

	ltr559 = kzalloc(sizeof(struct ltr559_data), GFP_KERNEL);
	if (!ltr559) {
		pr_err("%s: failed to alloc memory for module data\n",
		       __func__);
		return -ENOMEM;
	}

	ltr559->power_state = 0;
	ltr559->pdata = pdata;
	ltr559->i2c_client = client;
	i2c_set_clientdata(client, ltr559);

	ltr559_dev = ltr559;
#ifdef CONFIG_YL_SENSORS_POWER
	sensors_power_init(ltr559_dev, true);
	sensors_power_on(ltr559_dev, true);
	msleep(100);
#endif

	chip_id = i2c_smbus_read_byte_data(client, LTR559_MANUFACTURER_ID);
	pr_info("%s: chip_id = %x\n", __func__, chip_id);
	if (chip_id != 0x5) {
		pr_err("%s: err chip_id = %x\n", __func__, chip_id);
		ret = -ENODEV;
		goto err_free_dev;
	}

#ifdef SENSORS_WAKE_LOCK_TIMEOUT
	/* wake lock init */
	wake_lock_init(&ltr559->prx_wake_lock, WAKE_LOCK_SUSPEND,
		       "prx_wake_lock");
#endif
	mutex_init(&ltr559->lock);

	/* allocate light input_device */
	ltr559->input_dev_light = input_allocate_device();
	if (!ltr559->input_dev_light) {
		pr_err("%s: could not allocate input device\n", __func__);
		goto err_input_l_allocate_device;
	}
	input_set_drvdata(ltr559->input_dev_light, ltr559);
	ltr559->input_dev_light->name = LTR559_INPUT_NAME_L;
	input_set_capability(ltr559->input_dev_light, EV_MSC, MSC_SCAN);

	ret = input_register_device(ltr559->input_dev_light);
	if (ret < 0) {
		pr_err("%s: could not register input device\n", __func__);
		goto err_input_l_register_device;
	}

	/* allocate proximity input_device */
	ltr559->input_dev_proximity = input_allocate_device();
	if (!ltr559->input_dev_proximity) {
		pr_err("%s: could not allocate input device\n", __func__);
		goto err_input_p_allocate_device;
	}
	input_set_drvdata(ltr559->input_dev_proximity, ltr559);
	ltr559->input_dev_proximity->name = LTR559_INPUT_NAME_P;
	input_set_capability(ltr559->input_dev_proximity, EV_MSC, MSC_SCAN);

	ret = input_register_device(ltr559->input_dev_proximity);
	if (ret < 0) {
		pr_err("%s: could not register input device\n", __func__);
		goto err_input_p_register_device;
	}


	/* light hrtimer settings.  we poll for light values using a timer. */
	hrtimer_init(&ltr559->light_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ltr559->light_poll_delay = ns_to_ktime(LTR559_LIGHT_MEA_INTERVAL * NSEC_PER_MSEC);
	ltr559->light_timer.function = ltr559_light_timer_func;

	/* the timer just fires off a work queue request.  we need a thread
	   to read the i2c (can be slow and blocking). */
	ltr559->wq = create_singlethread_workqueue("ltr559_wq");
	if (!ltr559->wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create workqueue\n", __func__);
		goto err_create_workqueue;
	}
	/* this is the thread function we run on the work queue */
	INIT_WORK(&ltr559->work_light, ltr559_work_func_light); 
	/*added by liwenpeng start */
	ret = ltr559_i2c_write(ltr559, LTR559_INTERRUPT, 0x01);
	if (ret < 0) {
		pr_err("%s: i2c_write 0x%x failed\n", __func__, LTR559_INTERRUPT);
	}
	/*added by liwenpeng end*/

	ret = ltr559_gpio_irq(ltr559);
	if (ret < 0) {
		pr_err("%s: ltr559_gpio_irq failed ret = %d\n", __func__, ret);
		goto err_gpio_irq;
	}

  	ret = misc_register(&ltr559_device);
  	if (ret) {
		pr_err("%s: ltr559_device register failed\n", __func__);
		goto err_misc_register;
	}

#ifdef CONFIG_SENSORS
	/* Register to sensors class */
	ltr559_dev->als_cdev = sensors_light_cdev;
	ltr559_dev->als_cdev.sensors_enable = ltr559_als_set_enable;
	ltr559_dev->als_cdev.sensors_poll_delay = ltr559_als_poll_delay;
	ltr559_dev->ps_cdev = sensors_proximity_cdev;
	ltr559_dev->ps_cdev.sensors_enable = ltr559_ps_set_enable;
	ltr559_dev->ps_cdev.sensors_poll_delay = NULL;

	ret = sensors_classdev_register(&client->dev, &ltr559_dev->als_cdev);
	if (ret) {
		pr_err("%s: Unable to register to sensors class: %d\n",
				__func__, ret);
		goto exit_unregister_als_class;
	}

	ret = sensors_classdev_register(&client->dev, &ltr559_dev->ps_cdev);
	if (ret) {
		pr_err("%s: Unable to register to sensors class: %d\n",
			       __func__, ret);
		goto exit_unregister_ps_class;
	}

	ret = sysfs_create_group(&ltr559->als_cdev.dev->kobj,
				 &ltr559_attribute_group);
	if (ret) {
		pr_err("%s: could not create sysfs group\n", __func__);
		goto err_sysfs_create_group;
	}
#endif

	pr_info("%s: success\n", __func__);
	goto done;

#ifdef CONFIG_SENSORS
err_sysfs_create_group:
	sensors_classdev_unregister(&ltr559_dev->ps_cdev);
exit_unregister_ps_class:
	sensors_classdev_unregister(&ltr559_dev->als_cdev);
exit_unregister_als_class:
	misc_deregister(&ltr559_device);
#endif
err_misc_register:
err_gpio_irq:
	destroy_workqueue(ltr559->wq);
err_create_workqueue:
	input_unregister_device(ltr559->input_dev_proximity);
err_input_p_register_device:
	input_free_device(ltr559->input_dev_proximity);
err_input_p_allocate_device:
	input_unregister_device(ltr559->input_dev_light);
err_input_l_register_device:
	input_free_device(ltr559->input_dev_light);
err_input_l_allocate_device:
	mutex_destroy(&ltr559->lock);
#ifdef SENSORS_WAKE_LOCK_TIMEOUT
	wake_lock_destroy(&ltr559->prx_wake_lock);
#endif
err_free_dev:
#ifdef CONFIG_YL_SENSORS_POWER
    sensors_power_on(ltr559_dev, false);
    sensors_power_init(ltr559_dev, false);	
#endif
	kfree(ltr559);
done:
	return ret;
}

static int ltr559_i2c_remove(struct i2c_client *client)
{
	struct ltr559_data *ltr559 = i2c_get_clientdata(client);

	misc_deregister(&ltr559_device);
	gpio_free(ltr559->pdata->gpio_int);
	destroy_workqueue(ltr559->wq);
  	input_unregister_device(ltr559->input_dev_light);
  	input_unregister_device(ltr559->input_dev_proximity);
	input_free_device(ltr559->input_dev_light);
	input_free_device(ltr559->input_dev_proximity);
	mutex_destroy(&ltr559->lock);
#ifdef SENSORS_WAKE_LOCK_TIMEOUT
	wake_lock_destroy(&ltr559->prx_wake_lock);
#endif
#ifdef CONFIG_YL_SENSORS_POWER
	sensors_power_on(ltr559_dev, false);
	sensors_power_init(ltr559_dev, false);
#endif
	kfree(ltr559);
	return 0;
}

static int ltr559_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret = 0;
	struct ltr559_data *ltr559 = ltr559_dev;

	pr_debug("%s: Entry power_state = %d\n", __func__, ltr559->power_state);

#ifdef SENSORS_I2C_RETRY_ERROR
	if (i2c_retry_err == 0) {
#endif
	if (ltr559->power_state & LIGHT_ENABLED) {
		ret = ltr559_light_disable(ltr559);
		if (ret) {
			pr_err("%s: ltr559_light_disable failed\n", __func__);
			return ret;
		}
		hrtimer_cancel(&ltr559->light_timer);
		cancel_work_sync(&ltr559->work_light);
	}
#ifdef SENSORS_I2C_RETRY_ERROR
	}
#endif

#ifdef SENSORS_IRQ_WAIT_QUEUE
	sensors_resume = 0;
	pr_err("%s: sensors_resume = %d\n", __func__, sensors_resume);
#endif

	return ret;
}

static int ltr559_i2c_resume(struct i2c_client *client)
{
	int ret = 0;
	struct ltr559_data *ltr559 = ltr559_dev;

	pr_debug("%s: Entry power_state = %d\n", __func__, ltr559->power_state);

#ifdef SENSORS_I2C_RETRY_ERROR
	if (i2c_retry_err == 0) {
#endif
	if (ltr559->power_state & LIGHT_ENABLED) {
		ret = ltr559_light_enable(ltr559);
		if (ret) {
			pr_err("%s: ltr559_light_enable failed\n", __func__);
			return ret;
		}
		hrtimer_start(&ltr559->light_timer, ltr559->light_poll_delay, HRTIMER_MODE_REL);
	}
#ifdef SENSORS_I2C_RETRY_ERROR
	}
#endif

#ifdef SENSORS_IRQ_WAIT_QUEUE
	sensors_resume = 1;
	wake_up_interruptible(&wait_sensors_resume);
	pr_err("%s: sensors_resume = %d\n", __func__, sensors_resume);
#endif

	return ret;
}

static const struct i2c_device_id ltr559_device_id[] = {
	{LTR559_DRIVER_NAME, 0},
	{}
};
/*MODULE_DEVICE_TABLE(i2c, ltr559_device_id);*/

#ifdef CONFIG_OF
static struct of_device_id ltr559_match_table[] = {
	{ .compatible = "ltr559",},
	{ },
};
#else
#define ltr559_match_table NULL
#endif

static struct i2c_driver ltr559_i2c_driver = {
	.driver = {
		.name = LTR559_DRIVER_NAME,
		.owner = THIS_MODULE,
	#ifdef CONFIG_OF
		.of_match_table = ltr559_match_table,
	#endif
	},
	.id_table = ltr559_device_id,
	.probe	= ltr559_i2c_probe,
	.remove	= ltr559_i2c_remove,
	.suspend = ltr559_i2c_suspend,
	.resume	= ltr559_i2c_resume,
};


#ifdef CONFIG_LPM_MODE
extern unsigned int poweroff_charging;
extern unsigned int recovery_mode;
#endif
static int __init ltr559_init(void)
{
#ifdef CONFIG_LPM_MODE
	if((1 == poweroff_charging) || (1 == recovery_mode))
	{
		printk("%s: poweroff_charging = %d, recovery_mode = %d.\n",
				__func__, poweroff_charging, recovery_mode);
		return 0;
	}
#endif
	return i2c_add_driver(&ltr559_i2c_driver);
}

static void __exit ltr559_exit(void)
{
	i2c_del_driver(&ltr559_i2c_driver);
}

module_init(ltr559_init);
module_exit(ltr559_exit);

MODULE_AUTHOR("AHONG IN. DRIVER GROUP");
MODULE_DESCRIPTION("Optical Sensor driver for ltr55955");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("1.0");

