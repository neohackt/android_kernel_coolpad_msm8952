/********************************************************************************/
/*                                                                              */
/* Copyright (c) 2014-2014  YULONG Company             　　　　　　　           */
/*         宇龙计算机通信科技（深圳）有限公司  版权所有 2014-2014               */
/*                                                                              */
/* PROPRIETARY RIGHTS of YULONG Company are involved in the                     */
/* subject matter of this material.  All manufacturing, reproduction, use,      */
/* and sales rights pertaining to this subject matter are governed by the       */
/* license agreement.  The recipient of this software implicitly accepts        */
/* the terms of the license.                                                    */
/* 本软件文档资料是宇龙公司的资产,任何人士阅读和使用本资料必须获得              */
/* 相应的书面授权,承担保密责任和接受相应的法律约束.                             */
/*                                                                              */
/********************************************************************************/

/**************************************************************************
**  Copyright (C), 2014-2014, Yulong Tech. Co., Ltd.
**  FileName:    tf9890_stub.c
**  Author:      王延忠
**  Version :    1.00
**  Date:        2014-07-17
**  Description: 实现TFA9890的桩驱动，给应用提供获取I2S clock打开状态的接口
**
**  History:
**  <author>      <time>      <version >      <desc>
**  王延忠       2014-07-17     1.00           创建
**
**************************************************************************/

#include <linux/of.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/compat.h>
#include <sound/soc.h>


#define TFA9890_RATES	SNDRV_PCM_RATE_8000_48000
#define TFA9890_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE)

#define TFA_IOCTL_GET_CLK_ENABLE_STATUS _IOR('T', 0x01, int)


static DECLARE_WAIT_QUEUE_HEAD(clk_enable_wq);

static atomic_t clk_enable_flag;

static int tfa9890_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	pr_info("%s: enter\n", __func__);

	return 0;
}

static void tfa9890_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	pr_info("%s: enter\n", __func__);

	atomic_set(&clk_enable_flag, 0);
	wake_up(&clk_enable_wq);
}

/* Trigger callback is atomic function, It gets called when pcm is started */

static int tfa9890_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *dai)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* To initialize dsp all the I2S signals should be bought up,
		 * so that the DSP's internal PLL can sync up and memory becomes
		 * accessible. Trigger callback is called when pcm write starts,
		 * so this should be the place where DSP is initialized
		 */
		if (atomic_cmpxchg(&clk_enable_flag, 0, 1) == 0) {
	                pr_info("%s: wake up clk_enable_wq\n", __func__);
			wake_up(&clk_enable_wq);
		}
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct snd_soc_dai_ops tfa9890_ops = {
	.startup	= tfa9890_startup,
	.shutdown	= tfa9890_shutdown,
	.trigger        = tfa9890_trigger,
};

static struct snd_soc_dai_driver tfa9890_stub_dais[] = {
	{
		.name = "tfa9890-stub-rx",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TFA9890_RATES,
			.formats = TFA9890_FORMATS,
		},
                .ops = &tfa9890_ops,
	},
	{
		.name = "tfa9890-stub-tx",
		.capture = {
			.stream_name = "Record",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TFA9890_RATES,
			.formats = TFA9890_FORMATS,
		},
	},
};

static struct snd_soc_codec_driver soc_tfa9890_stub = {};

static int tfa9890_misc_open(struct inode *inode, struct file *file)
{
	int err = 0;

	pr_info("%s: enter", __func__);
	err = nonseekable_open(inode, file);
	if (err)
		return err;

	return 0;
}

static int tfa9890_misc_release(struct inode *inode, struct file *file)
{
	pr_info("%s: enter", __func__);

	return 0;
}

static long tfa9890_misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *) arg;
	int status;

	pr_debug("%s: cmd %u, TFA_IOCTL_GET_CLK_ENABLE_STATUS %lu\n",
		__func__, cmd, TFA_IOCTL_GET_CLK_ENABLE_STATUS);
	switch (cmd) {
	case TFA_IOCTL_GET_CLK_ENABLE_STATUS:
		wait_event_interruptible(clk_enable_wq,
					 (atomic_read(&clk_enable_flag) != 0));
		status = atomic_read(&clk_enable_flag);
		if (!argp) {
			if (copy_to_user(argp, &status, sizeof(status)))
				return -EFAULT;
		}
		break;

	default:
		pr_err("%s: Unknown cmd\n", __func__);
		return -ENXIO;
	}

	return 0;
}

#ifdef CONFIG_COMPAT
static long tfa9890_misc_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	pr_debug("%s: cmd %u, TFA_IOCTL_GET_CLK_ENABLE_STATUS %lu\n",
		__func__, cmd, TFA_IOCTL_GET_CLK_ENABLE_STATUS);
	return tfa9890_misc_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations tfa9890_misc_fops = {
	.owner = THIS_MODULE,
	.open = tfa9890_misc_open,
	.release = tfa9890_misc_release,
	.unlocked_ioctl = tfa9890_misc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tfa9890_misc_compat_ioctl,
#endif
};

static struct miscdevice tfa9890_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tfa9890-misc-dev",
	.fops = &tfa9890_misc_fops,
};

static int tfa9890_stub_dev_probe(struct platform_device *pdev)
{
	int ret;

        if (pdev->dev.of_node)
		dev_set_name(&pdev->dev, "%s.%d", "tfa9890-stub-codec", 1);

	dev_info(&pdev->dev, "dev name %s\n", dev_name(&pdev->dev));

	/* register codec */
	ret = snd_soc_register_codec(&pdev->dev, &soc_tfa9890_stub,
			tfa9890_stub_dais, ARRAY_SIZE(tfa9890_stub_dais));
	if (ret < 0) {
		pr_err("%s: Error registering tfa9890 codec\n", __func__);
		goto codec_fail;
	}

	/* register tfa9890 misc device */
        ret = misc_register(&tfa9890_misc_device);
	if (ret) {
		pr_err("%s: tfa9890_misc_device register failed\n", __func__);
		goto misc_dev_fail;
	}

	pr_info("%s: tfa9890 probed successfully!\n", __func__);
	return 0;

misc_dev_fail:
codec_fail:
	pr_err("%s: tfa9890 probe fail!\n", __func__);
	return ret;
}

static int tfa9890_stub_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct of_device_id tfa9890_stub_dt_match[] = {
	{ .compatible = "nxp,tfa9890-stub-codec" },
	{ },
};

static struct platform_driver tfa9890_stub_driver = {
	.driver = {
		.name = "tfa9890-stub-codec",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tfa9890_stub_dt_match),
	},
	.probe =    tfa9890_stub_dev_probe,
	.remove =   tfa9890_stub_dev_remove,
};

static int __init tfa9890_modinit(void)
{
	int ret;

	ret = platform_driver_register(&tfa9890_stub_driver);
	if (ret != 0) {
		pr_err("Failed to register tfa9890 stub driver: %d\n", ret);
	}
	return ret;
}
module_init(tfa9890_modinit);

static void __exit tfa9890_exit(void)
{
	platform_driver_unregister(&tfa9890_stub_driver);
}
module_exit(tfa9890_exit);

MODULE_DESCRIPTION("ASoC tfa9890 stub codec driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yulong Company");
