/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/gpio.h>
#include "msm_led_flash.h"
#include "msm_camera_io_util.h"

struct msm_led_flash_ctrl_t *fctrl;
static void msm_led_torch_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	int32_t rc = 0;
	if (!fctrl)
		return;

	if (fctrl->func_tbl->flash_led_init && fctrl->led_state != MSM_CAMERA_LED_INIT)
		rc = fctrl->func_tbl->flash_led_init(fctrl);
	
	pr_err("%s: E\n", __func__);
	if (value == 0) {
		if (fctrl->func_tbl->flash_led_off)
			rc = fctrl->func_tbl->flash_led_off(fctrl);
		if (fctrl->func_tbl->flash_led_release && fctrl->led_state == MSM_CAMERA_LED_INIT)
			rc = fctrl->func_tbl->flash_led_release(fctrl);
	} else if (value > 0 && value <= 127) {
		if (fctrl->func_tbl->flash_led_low)
			rc = fctrl->func_tbl->flash_led_low(fctrl);
	} else if (value > 127 && value <= 255) {
		if (fctrl->func_tbl->flash_led_high)
			rc = fctrl->func_tbl->flash_led_high(fctrl);
	} else {
		pr_err("%s: invalid value\n", __func__);
	}
	
	if (rc < 0) {
		pr_err("%s: free gpio failed\n", __func__);
		return;
	}
	
	return;
};

static struct led_classdev msm_torch_gpio_led = {
	.name			= "torch-light",
	.brightness_set	= msm_led_torch_brightness_set,
	.brightness		= LED_OFF,
};

int32_t msm_led_gpio_torch_create_classdev(struct platform_device *pdev,
				void *data)
{
	int rc;
	fctrl = (struct msm_led_flash_ctrl_t *)data;

	if (!fctrl) {
		pr_err("Invalid fctrl\n");
		return -EINVAL;
	}

	//msm_led_torch_brightness_set(&msm_torch_gpio_led, LED_OFF);

	rc = led_classdev_register(&pdev->dev, &msm_torch_gpio_led);
	if (rc) {
		pr_err("Failed to register led dev. rc = %d\n", rc);
		return rc;
	}

	return 0;
};
