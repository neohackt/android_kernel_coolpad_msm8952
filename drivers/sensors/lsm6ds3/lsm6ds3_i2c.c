/*
 * STMicroelectronics lsm6ds3 i2c driver
 *
 * Copyright 2014 STMicroelectronics Inc.
 *
 * Giuseppe Barba <giuseppe.barba@st.com>
 * v 1.1.0
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hrtimer.h>
#include <linux/input.h>
#include <linux/types.h>

#include	<linux/sensors/lsm6ds3.h>
#include	"lsm6ds3_core.h"

#include <linux/sensors.h>

#ifdef CONFIG_SENSORS
static struct sensors_classdev sensors_gyro_cdev = {
         .name = "gyro",
         .vendor = "LSM6DS3",
         .version = 1,
         .handle = SENSORS_GYROSCOPE_HANDLE,
         .type = SENSOR_TYPE_GYROSCOPE,
         .max_range = "100000",
         .resolution = "1.0",
         .sensor_power = "0.1",
         .min_delay = 20000, /* in microseconds */
         .fifo_reserved_event_count = 0,
         .fifo_max_event_count = 0,
         .sensors_enable = NULL,
         .sensors_poll_delay = NULL,
};

static struct sensors_classdev sensors_accelerometer_cdev = {
         .name = "accelerometer",
         .vendor = "LSM6DS3",
         .version = 1,
         .handle = SENSORS_ACCELERATION_HANDLE,
         .type = SENSOR_TYPE_ACCELEROMETER,
         .max_range = "100000",
         .resolution = "1.0",
         .sensor_power = "0.1",
         .min_delay = 20000, /* in microseconds */
         .fifo_reserved_event_count = 0,
         .fifo_max_event_count = 0,
         .sensors_enable = NULL,
         .sensors_poll_delay = NULL,
};
#endif

static int lsm6ds3_i2c_read(struct lsm6ds3_data *cdata, u8 reg_addr, int len,
							u8 *data, bool b_lock)
{
	int err = 0;
	struct i2c_msg msg[2];
	struct i2c_client *client = to_i2c_client(cdata->dev);

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &reg_addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	if (b_lock) {
		mutex_lock(&cdata->bank_registers_lock);
		err = i2c_transfer(client->adapter, msg, 2);
		mutex_unlock(&cdata->bank_registers_lock);
	} else
		err = i2c_transfer(client->adapter, msg, 2);

	return err;
}

static int lsm6ds3_i2c_write(struct lsm6ds3_data *cdata, u8 reg_addr, int len,
							u8 *data, bool b_lock)
{
	int err = 0;
	u8 send[len + 1];
	struct i2c_msg msg;
	struct i2c_client *client = to_i2c_client(cdata->dev);

	send[0] = reg_addr;
	memcpy(&send[1], data, len * sizeof(u8));
	len++;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = send;

	if (b_lock) {
		mutex_lock(&cdata->bank_registers_lock);
		err = i2c_transfer(client->adapter, &msg, 1);
		mutex_unlock(&cdata->bank_registers_lock);
	} else
		err = i2c_transfer(client->adapter, &msg, 1);

	return err;
}


static const struct lsm6ds3_transfer_function lsm6ds3_tf_i2c = {
	.write = lsm6ds3_i2c_write,
	.read = lsm6ds3_i2c_read,
};

static int lsm6ds3_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	int err;
	struct lsm6ds3_data *cdata;

	cdata = kmalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	cdata->dev = &client->dev;
	cdata->name = client->name;
	cdata->tf = &lsm6ds3_tf_i2c;
	i2c_set_clientdata(client, cdata);

	err = lsm6ds3_common_probe(cdata, client->irq, BUS_I2C);
	if (err < 0)
		goto free_data;

	#ifdef CONFIG_SENSORS
        /* Register to sensors class */
        err = sensors_classdev_register(&client->dev, &sensors_gyro_cdev);
        if (err) {
                pr_err("%s: Unable to register to sensors class: %d\n",
                                __func__, err);
        }

        err = sensors_classdev_register(&client->dev, &sensors_accelerometer_cdev);
        if (err) {
                pr_err("%s: Unable to register to sensors class: %d\n",
                                __func__, err);
        }
    #endif	
		
		
	return 0;

free_data:
	kfree(cdata);
	return err;
}

static int lsm6ds3_i2c_remove(struct i2c_client *client)
{
	/* TODO: check the function */
	struct lsm6ds3_data *cdata = i2c_get_clientdata(client);

	lsm6ds3_common_remove(cdata, client->irq);
	dev_info(cdata->dev, "%s: removed\n", LSM6DS3_ACC_GYR_DEV_NAME);
	kfree(cdata);
	return 0;
}

#ifdef CONFIG_PM
static int lsm6ds3_suspend(struct device *dev)
{
	struct lsm6ds3_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return lsm6ds3_common_suspend(cdata);
}

static int lsm6ds3_resume(struct device *dev)
{
	struct lsm6ds3_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return lsm6ds3_common_resume(cdata);
}

static const struct dev_pm_ops lsm6ds3_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lsm6ds3_suspend, lsm6ds3_resume)
};

#define LSM6DS3_PM_OPS		(&lsm6ds3_pm_ops)
#else /* CONFIG_PM */
#define LSM6DS3_PM_OPS		NULL
#endif /* CONFIG_PM */


static const struct i2c_device_id lsm6ds3_ids[] = {
	{"lsm6ds3", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, lsm6ds3_ids);

#ifdef CONFIG_OF
static const struct of_device_id lsm6ds3_id_table[] = {
	{.compatible = "st,lsm6ds3", },
	{ },
};
MODULE_DEVICE_TABLE(of, lsm6ds3_id_table);
#endif

static struct i2c_driver lsm6ds3_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = LSM6DS3_ACC_GYR_DEV_NAME,
		.pm = LSM6DS3_PM_OPS,
#ifdef CONFIG_OF
		.of_match_table = lsm6ds3_id_table,
#endif
	},
	.probe    = lsm6ds3_i2c_probe,
	.remove   = lsm6ds3_i2c_remove,
	.id_table = lsm6ds3_ids,
};

module_i2c_driver(lsm6ds3_i2c_driver);

MODULE_DESCRIPTION("STMicroelectronics lsm6ds3 i2c driver");
MODULE_AUTHOR("Giuseppe Barba");
MODULE_LICENSE("GPL v2");