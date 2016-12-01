#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "../staging/android/timed_output.h"

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#define YULONG_VIB_NAME       "yulong-vibrator"
#define VIB_VOL_MAX	2800000
#define VIB_VOL_MIN	2800000


struct vib_platform_data{
	int max_timeout_ms;
	const char *pwr_reg_name;
};

struct yl_vib {
	struct hrtimer vib_timer;
	struct timed_output_dev timed_dev;
	spinlock_t lock;
	struct work_struct work;
	struct device *dev;
	struct regulator *vib_regulator;
	const struct vib_platform_data *pdata;
	int state;
	int level;
	u8  reg_vib_drv;

	unsigned int set_vol;
	bool pwr_status;
};

static int yl_vib_set(struct yl_vib *vib, int on)
{
	int ret;

	if (on) {
		if (!vib->pwr_status) {
			ret = regulator_enable(vib->vib_regulator);
			vib->pwr_status = true;
		}
        } else {
		if (vib->pwr_status) {
			ret = regulator_disable(vib->vib_regulator);
			vib->pwr_status = false;
		}
	}

	return ret;
}

static void yl_vib_enable(struct timed_output_dev *dev, int value)
{
	struct yl_vib *vib = container_of(dev, struct yl_vib, timed_dev);
	unsigned long flags;

retry:
	spin_lock_irqsave(&vib->lock, flags);
	if (hrtimer_try_to_cancel(&vib->vib_timer) < 0) {
		spin_unlock_irqrestore(&vib->lock, flags);
		cpu_relax();
		goto retry;
	}

	if (value == 0)
		vib->state = 0;
	else {
		value = (value > vib->pdata->max_timeout_ms ?
				 vib->pdata->max_timeout_ms : value);
		vib->state = 1;
		hrtimer_start(&vib->vib_timer,
			      ktime_set(value / 1000, (value % 1000) * 1000000),
			      HRTIMER_MODE_REL);
	}
	spin_unlock_irqrestore(&vib->lock, flags);
	schedule_work(&vib->work);
}


static int yl_vib_get_blank(struct timed_output_dev *sdev)
{
	return 0;
}

static void yl_vib_set_blank(struct timed_output_dev *dev, int blank)
{
}

static int yl_vib_get_voltage(struct timed_output_dev *dev)
{
	return 0;
}

static void yl_vib_set_level(struct timed_output_dev *dev, int level)
{
	struct yl_vib *vib = container_of(dev, struct yl_vib, timed_dev);
	int set_level;
	int ret;

	if (0 >= level && level > 100) {
		set_level = 60;
	} else {
		set_level = level;
	}

	vib->set_vol = (VIB_VOL_MAX - VIB_VOL_MIN)*set_level/100 + VIB_VOL_MIN;
	ret = regulator_set_voltage(vib->vib_regulator, vib->set_vol, vib->set_vol);
	printk(KERN_ERR "VIB:YLLOG:set VOL %d\n", vib->set_vol);
	if (ret) {
		printk(KERN_ERR "VIB:YLLOG:Failed to set VOL\n");
	}

	return;
}

static int yl_vib_get_level(struct timed_output_dev *dev)
{
	return 0;
}

static void yl_vib_update(struct work_struct *work)
{
	struct yl_vib *vib = container_of(work, struct yl_vib, work);

	yl_vib_set(vib, vib->state);
}

static int yl_vib_get_time(struct timed_output_dev *dev)
{
	struct yl_vib *vib = container_of(dev, struct yl_vib, timed_dev);

	if (hrtimer_active(&vib->vib_timer)) {
		ktime_t r = hrtimer_get_remaining(&vib->vib_timer);
		return (int)ktime_to_us(r);
	} else
		return 0;
}

static enum hrtimer_restart yl_vib_timer_func(struct hrtimer *timer)
{
	struct yl_vib *vib = container_of(timer, struct yl_vib, vib_timer);

	vib->state = 0;
	schedule_work(&vib->work);

	return HRTIMER_NORESTART;
}

#ifdef CONFIG_PM
static int yl_vib_suspend(struct device *dev)
{
	struct yl_vib *vib = dev_get_drvdata(dev);

	hrtimer_cancel(&vib->vib_timer);
	cancel_work_sync(&vib->work);
	/* turn-off vibrator */
	yl_vib_set(vib, 0);

	return 0;
}

static const struct dev_pm_ops yl_vib_pm_ops = {
	.resume = NULL,
	.suspend = yl_vib_suspend,
};
#endif

#ifdef CONFIG_OF
int vib_get_devtree_pdata(struct device *dev, struct vib_platform_data *pdata)
{
	struct device_node *dt_node = NULL;
	int ret;

	dt_node = dev->of_node;
	if (!dt_node)
		return -ENODEV;
	memset(pdata, 0, sizeof *pdata);

	ret = of_property_read_string(dt_node, "vib,pwr-reg-name", &(pdata->pwr_reg_name));
	if (ret)
		printk(KERN_ERR "VIB:YLLOG:get regulator error. ret %d\n", ret);

	return ret;
}
#endif

extern int yl_get_bootreason(char *, int );

static int yl_vib_probe(struct platform_device *pdev)

{
	struct yl_vib *vib_dev;
	struct vib_platform_data *pdata;
	int ret;
#ifdef CONFIG_BOOTREASON
        char reason[28] = {0};
#endif

	printk(KERN_ERR "VIB: probe begin.\n");
	pdata = kzalloc(sizeof(struct vib_platform_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	vib_dev = kzalloc(sizeof(struct yl_vib), GFP_KERNEL);
	if (!vib_dev)
		return -ENOMEM;

#ifdef CONFIG_OF
	ret = vib_get_devtree_pdata(&pdev->dev, pdata);
        if (ret)
		goto err_vib;
#else
	pdata = pdev->dev.platform_data;
#endif
	pdata->max_timeout_ms = 10000;//get from dts

	vib_dev->pdata	= pdata;
	vib_dev->dev	= &pdev->dev;
	vib_dev->pwr_status	= false;

        if (pdata->pwr_reg_name != NULL) {
		vib_dev->vib_regulator = regulator_get(pdev->dev.parent, pdata->pwr_reg_name);
                if (IS_ERR(vib_dev->vib_regulator)) {
			printk(KERN_ERR "VIB:YLLOG:Failed to get power regulator\n");
                        ret = PTR_ERR(vib_dev->vib_regulator);
			goto err_vib;
                }
        }

	spin_lock_init(&vib_dev->lock);
	INIT_WORK(&vib_dev->work, yl_vib_update);

	hrtimer_init(&vib_dev->vib_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vib_dev->vib_timer.function = yl_vib_timer_func;

	vib_dev->timed_dev.name = "vibrator";
	vib_dev->timed_dev.get_time = yl_vib_get_time;
	vib_dev->timed_dev.enable = yl_vib_enable;
        vib_dev->timed_dev.set_level = yl_vib_set_level;
	vib_dev->timed_dev.get_level = yl_vib_get_level;
	vib_dev->timed_dev.get_blank = yl_vib_get_blank;
	vib_dev->timed_dev.set_blank = yl_vib_set_blank;
	vib_dev->timed_dev.get_voltage = yl_vib_get_voltage;

	ret = timed_output_dev_register(&vib_dev->timed_dev);
	if (ret < 0)
		goto err_vib;

	platform_set_drvdata(pdev, vib_dev);

#ifdef CONFIG_BOOTREASON
        if (yl_get_bootreason(reason, 28) || \
                        strcmp(reason, "excep_press_poweron")) {
                printk(KERN_ERR "VIB:YLLOG:boot reason=%s\n", reason);
		yl_vib_set(vib_dev, 1);
                mdelay(100);
		yl_vib_set(vib_dev, 0);
        }
#endif

	printk(KERN_ERR "VIB: init done.\n");
	return 0;

err_vib:
	printk(KERN_ERR "VIB: init failed.\n");
	kfree(vib_dev);
	kfree(pdata);
	return ret;
}

static int yl_vib_remove(struct platform_device *pdev)
{
	struct yl_vib *vib = platform_get_drvdata(pdev);

	cancel_work_sync(&vib->work);
	hrtimer_cancel(&vib->vib_timer);
	timed_output_dev_unregister(&vib->timed_dev);
	platform_set_drvdata(pdev, NULL);
	kfree(vib);

	return 0;
}

static struct of_device_id yl_vib_match_table[] = {
        {       .compatible = "yulong-vibrator",
        },
        {}
};

static struct platform_driver yl_vib_driver = {
	.probe		= yl_vib_probe,
	.remove		= yl_vib_remove,
	.driver		= {
		.name	= YULONG_VIB_NAME,
		.of_match_table = yl_vib_match_table,
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &yl_vib_pm_ops,
#endif
	},
};

static int __init yl_vib_init(void)
{   
        printk(KERN_ERR"yulong vibrator init!");
	return platform_driver_register(&yl_vib_driver);
}
module_init(yl_vib_init);
static void __exit yl_vib_exit(void)
{
	platform_driver_unregister(&yl_vib_driver);
}
module_exit(yl_vib_exit);
MODULE_ALIAS("platform:yulong-vibrator");
MODULE_DESCRIPTION("yulong vibrator driver");
MODULE_LICENSE("GPL v2");
