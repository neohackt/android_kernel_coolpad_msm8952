/* drivers/misc/timed_output.c
 *
 * Copyright (C) 2009 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
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

#define pr_fmt(fmt) "timed_output: " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/err.h>

#include "timed_output.h"

static struct class *timed_output_class;
static atomic_t device_count;

#ifdef CONFIG_YL_SET_VOLTAGE_LEVEL
static ssize_t voltage_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct timed_output_dev *tdev = dev_get_drvdata(dev);
	int voltage = 0;
	if(tdev->get_voltage)
		voltage = tdev->get_voltage(tdev);
	
	return sprintf(buf, "%d\n", voltage);
}

static ssize_t voltage_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	return 0;
}


static DEVICE_ATTR(voltage, S_IRUGO | S_IWUSR, voltage_show, voltage_store);

//static DEVICE_ATTR(voltage, S_IRUGO | S_IWUSR, voltage_show, NULL);

static ssize_t blank_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct timed_output_dev *tdev = dev_get_drvdata(dev);
	int blank;

	if (sscanf(buf, "%d", &blank) != 1)
		return -EINVAL;

	if(tdev->set_blank == NULL)
		return -EINVAL;
	
	tdev->set_blank(tdev, blank);

	return size;
}

static ssize_t blank_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct timed_output_dev *tdev = dev_get_drvdata(dev);
	int blank = 0;

	if(tdev->get_blank)
		blank = tdev->get_blank(tdev);
	
	return sprintf(buf, "%d\n", blank);
}

static DEVICE_ATTR(blank, S_IRUGO | S_IWUSR, blank_show, blank_store);
static ssize_t level_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct timed_output_dev *tdev = dev_get_drvdata(dev);
	int level;

	if (sscanf(buf, "%d", &level) != 1)
		return -EINVAL;

	if(tdev->set_level == NULL)
		return -EINVAL;
	
	tdev->set_level(tdev, level);

	return size;
}

static ssize_t level_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct timed_output_dev *tdev = dev_get_drvdata(dev);
	int level = 0;

	if(tdev->get_level)
		level = tdev->get_level(tdev);
	
	return sprintf(buf, "%d\n", level);
}

//	static DEVICE_ATTR(level, S_IRUGO | S_IWUSR, level_show, level_store);
// group can write_wangjun8_20151127__for_factory___S_IWUGO
static DEVICE_ATTR(level, S_IRUGO | S_IWUGO, level_show, level_store);

#endif
static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct timed_output_dev *tdev = dev_get_drvdata(dev);
#ifdef CONFIG_YL_SET_VOLTAGE_LEVEL
	int remaining = 0;

	if(tdev->get_time)
		remaining = tdev->get_time(tdev);
#else
	int remaining = tdev->get_time(tdev);
#endif

	return sprintf(buf, "%d\n", remaining);
}

static ssize_t enable_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct timed_output_dev *tdev = dev_get_drvdata(dev);
	int value;

	if (sscanf(buf, "%d", &value) != 1)
		return -EINVAL;
#ifdef CONFIG_YL_SET_VOLTAGE_LEVEL
	if(tdev->enable == NULL)
		return -EINVAL;
#endif
	
	tdev->enable(tdev, value);

	return size;
}

// static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, enable_show, enable_store);
// group can write_wangjun8_20151127__for_factory___S_IWUGO
//static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, enable_show, enable_store);
static DEVICE_ATTR(enable, S_IRUGO | S_IWUGO, enable_show, enable_store);


static int create_timed_output_class(void)
{
	if (!timed_output_class) {
		timed_output_class = class_create(THIS_MODULE, "timed_output");
		if (IS_ERR(timed_output_class))
			return PTR_ERR(timed_output_class);
		atomic_set(&device_count, 0);
	}

	return 0;
}

int timed_output_dev_register(struct timed_output_dev *tdev)
{
	int ret;

	if (!tdev || !tdev->name || !tdev->enable || !tdev->get_time)
		return -EINVAL;

	ret = create_timed_output_class();
	if (ret < 0)
		return ret;

	tdev->index = atomic_inc_return(&device_count);
	tdev->dev = device_create(timed_output_class, NULL,
		MKDEV(0, tdev->index), NULL, tdev->name);
	if (IS_ERR(tdev->dev))
		return PTR_ERR(tdev->dev);

	ret = device_create_file(tdev->dev, &dev_attr_enable);
	if (ret < 0)
		goto err_create_file;
#ifdef CONFIG_YL_SET_VOLTAGE_LEVEL

	ret = device_create_file(tdev->dev, &dev_attr_level);
	if (ret < 0)
		goto err_create_file;

	ret = device_create_file(tdev->dev, &dev_attr_blank);
	if (ret < 0)
		goto err_create_file;

	ret = device_create_file(tdev->dev, &dev_attr_voltage);
	if (ret < 0)
		goto err_create_file;
#endif
	dev_set_drvdata(tdev->dev, tdev);
	tdev->state = 0;
	return 0;

err_create_file:
	device_destroy(timed_output_class, MKDEV(0, tdev->index));
	pr_err("failed to register driver %s\n",
			tdev->name);

	return ret;
}
EXPORT_SYMBOL_GPL(timed_output_dev_register);

void timed_output_dev_unregister(struct timed_output_dev *tdev)
{
	tdev->enable(tdev, 0);
	device_remove_file(tdev->dev, &dev_attr_enable);
#ifdef CONFIG_YL_SET_VOLTAGE_LEVEL
	device_remove_file(tdev->dev, &dev_attr_level);
        device_remove_file(tdev->dev, &dev_attr_blank);
        device_remove_file(tdev->dev, &dev_attr_voltage);
#else
	dev_set_drvdata(tdev->dev, NULL);
#endif
	device_destroy(timed_output_class, MKDEV(0, tdev->index));
#ifdef CONFIG_YL_SET_VOLTAGE_LEVEL
	dev_set_drvdata(tdev->dev, NULL);
#endif
}
EXPORT_SYMBOL_GPL(timed_output_dev_unregister);

static int __init timed_output_init(void)
{
	return create_timed_output_class();
}

static void __exit timed_output_exit(void)
{
	class_destroy(timed_output_class);
}

module_init(timed_output_init);
module_exit(timed_output_exit);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("timed output class driver");
MODULE_LICENSE("GPL");

