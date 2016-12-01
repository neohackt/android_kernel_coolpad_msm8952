/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
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

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/qpnp/power-on.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/system_misc.h>

#include <soc/qcom/scm.h>
#include <soc/qcom/restart.h>
#include <soc/qcom/watchdog.h>

#define EMERGENCY_DLOAD_MAGIC1    0x322A4F99
#define EMERGENCY_DLOAD_MAGIC2    0xC67E4350
#define EMERGENCY_DLOAD_MAGIC3    0x77777777

#define SCM_IO_DISABLE_PMIC_ARBITER	1
#define SCM_IO_DEASSERT_PS_HOLD		2
#define SCM_WDOG_DEBUG_BOOT_PART	0x9
#define SCM_DLOAD_MODE			0X10
#define SCM_EDLOAD_MODE			0X01
#define SCM_DLOAD_CMD			0x10

#ifdef CONFIG_YL_POWER_ON_REASON
/* yl add */
enum yl_reboot_mode
{
	FASTBOOT_MODE = 0x77665500,
	NORMAL_REBOOT_MODE,
	RECOVERY_MODE,
	KEREXCEP_REBOOT_MODE,
	SYSEXCEP_REBOOT_MODE,
	SILENCE_REBOOT_MODE,
	OTA_BY_FORCE_MODE,
	LOWMEM_MODE,
	RUIMREFRESH_MODE,
	LOSTCARD_MODE,
	RTC_REBOOT_MODE,
	FASTMMI_MODE,
	NV_RECOVERY_MODE,
	AT_CFUN_RESET_MODE,
	CHECK_SCHEDULE_MODE,
	SAFE_MODE,
	FASTBOOT_REBOOT_MODE,
	ACTION_REBOOT,
	NULL_CMD_MODE,
	INVALID_REBOOT_MODE
};
/* yl add end */
#endif

static int restart_mode;
void *restart_reason;
static bool scm_pmic_arbiter_disable_supported;
static bool scm_deassert_ps_hold_supported;
/* Download mode master kill-switch */
static void __iomem *msm_ps_hold;
static phys_addr_t tcsr_boot_misc_detect;

#ifdef CONFIG_MSM_DLOAD_MODE
#define EDL_MODE_PROP "qcom,msm-imem-emergency_download_mode"
#define DL_MODE_PROP "qcom,msm-imem-download_mode"

static int in_panic;
static void *dload_mode_addr;
static bool dload_mode_enabled;
static void *emergency_dload_mode_addr;
static bool scm_dload_supported;

static int dload_set(const char *val, struct kernel_param *kp);
#ifdef CONFIG_YL_POWER_ON_REASON
static int download_mode = 0;
/* by LIHAIBO
   force_dload, means prevent EngMode.apk from setting dload_mode,
   in other word, can't change dload_mode manully before reboot.
   To enable force_dload, we must pass it by cmdline:
   fastboot oem param set force_dload
   */
static int force_dload = 0;
static int __init force_dload_setup(char *__unused)
{
	force_dload = 1;
	return 1;
}

__setup("force_dload", force_dload_setup);
#else
static int download_mode = 1;
#endif
module_param_call(download_mode, dload_set, param_get_int,
			&download_mode, 0644);
static int panic_prep_restart(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	in_panic = 1;
	return NOTIFY_DONE;
}

static struct notifier_block panic_blk = {
	.notifier_call	= panic_prep_restart,
};

int scm_set_dload_mode(int arg1, int arg2)
{
	struct scm_desc desc = {
		.args[0] = arg1,
		.args[1] = arg2,
		.arginfo = SCM_ARGS(2),
	};

	if (!scm_dload_supported) {
		if (tcsr_boot_misc_detect)
			return scm_io_write(tcsr_boot_misc_detect, arg1);

		return 0;
	}

	if (!is_scm_armv8())
		return scm_call_atomic2(SCM_SVC_BOOT, SCM_DLOAD_CMD, arg1,
					arg2);

	return scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_BOOT, SCM_DLOAD_CMD),
				&desc);
}

static void set_dload_mode(int on)
{
	int ret;

	if (dload_mode_addr) {
		__raw_writel(on ? 0xE47B337D : 0, dload_mode_addr);
		__raw_writel(on ? 0xCE14091A : 0,
		       dload_mode_addr + sizeof(unsigned int));
		mb();
	}

	ret = scm_set_dload_mode(on ? SCM_DLOAD_MODE : 0, 0);
	if (ret)
		pr_err("Failed to set secure DLOAD mode: %d\n", ret);

	dload_mode_enabled = on;
}

static bool get_dload_mode(void)
{
	return dload_mode_enabled;
}

#ifndef CONFIG_YL_POWER_ON_REASON
static void enable_emergency_dload_mode(void)
{
	int ret;

	if (emergency_dload_mode_addr) {
		__raw_writel(EMERGENCY_DLOAD_MAGIC1,
				emergency_dload_mode_addr);
		__raw_writel(EMERGENCY_DLOAD_MAGIC2,
				emergency_dload_mode_addr +
				sizeof(unsigned int));
		__raw_writel(EMERGENCY_DLOAD_MAGIC3,
				emergency_dload_mode_addr +
				(2 * sizeof(unsigned int)));

		/* Need disable the pmic wdt, then the emergency dload mode
		 * will not auto reset. */
		qpnp_pon_wd_config(0);
		mb();
	}

	ret = scm_set_dload_mode(SCM_EDLOAD_MODE, 0);
	if (ret)
		pr_err("Failed to set secure EDLOAD mode: %d\n", ret);
}
#endif
static int dload_set(const char *val, struct kernel_param *kp)
{
	int ret;
	int old_val = download_mode;
#ifdef CONFIG_BOARD_CP3600I
    static unsigned int dltimes = 2;

    if((dltimes > 0) && (dltimes--))
        return -EINVAL;
#endif

#ifdef CONFIG_YL_POWER_ON_REASON
	if (force_dload)
		return 0;
#endif
	ret = param_set_int(val, kp);

	if (ret)
		return ret;

	/* If download_mode is not zero or one, ignore. */
	if (download_mode >> 1) {
		download_mode = old_val;
		return -EINVAL;
	}

	set_dload_mode(download_mode);

	return 0;
}
#else
#define set_dload_mode(x) do {} while (0)

static void enable_emergency_dload_mode(void)
{
	pr_err("dload mode is not enabled on target\n");
}

static bool get_dload_mode(void)
{
	return false;
}
#endif

void msm_set_restart_mode(int mode)
{
	restart_mode = mode;
}
EXPORT_SYMBOL(msm_set_restart_mode);

/*
 * Force the SPMI PMIC arbiter to shutdown so that no more SPMI transactions
 * are sent from the MSM to the PMIC.  This is required in order to avoid an
 * SPMI lockup on certain PMIC chips if PS_HOLD is lowered in the middle of
 * an SPMI transaction.
 */
static void halt_spmi_pmic_arbiter(void)
{
	struct scm_desc desc = {
		.args[0] = 0,
		.arginfo = SCM_ARGS(1),
	};

	if (scm_pmic_arbiter_disable_supported) {
		pr_crit("Calling SCM to disable SPMI PMIC arbiter\n");
		if (!is_scm_armv8())
			scm_call_atomic1(SCM_SVC_PWR,
					 SCM_IO_DISABLE_PMIC_ARBITER, 0);
		else
			scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_PWR,
				  SCM_IO_DISABLE_PMIC_ARBITER), &desc);
	}
}

#ifdef CONFIG_YL_POWER_ON_REASON
extern int system_reboot_flag;
#endif
static void msm_restart_prepare(const char *cmd)
{
#ifdef CONFIG_YL_POWER_ON_REASON
	bool need_warm_reset = true;
#else
	bool need_warm_reset = false;
#endif
#ifdef CONFIG_MSM_DLOAD_MODE

	/* Write download mode flags if we're panic'ing
	 * Write download mode flags if restart_mode says so
	 * Kill download mode if master-kill switch is set
	 */

	set_dload_mode(download_mode &&
			(in_panic || restart_mode == RESTART_DLOAD));
#endif

#ifndef CONFIG_YL_POWER_ON_REASON
	need_warm_reset = (get_dload_mode() ||
				(cmd != NULL && cmd[0] != '\0'));

	if (qpnp_pon_check_hard_reset_stored()) {
		/* Set warm reset as true when device is in dload mode
		 *  or device doesn't boot up into recovery, bootloader or rtc.
		 */
		if (get_dload_mode() ||
			((cmd != NULL && cmd[0] != '\0') &&
			strcmp(cmd, "recovery") &&
			strcmp(cmd, "bootloader") &&
			strcmp(cmd, "rtc")))
			need_warm_reset = true;
	}
#else
	if (get_dload_mode() /*|| cmd == NULL*/)
		pr_notice("...reboot cmd = %s,need_warm_reset=%d...\n",cmd,need_warm_reset);
	else if(qpnp_pon_check_hard_reset_stored())
		need_warm_reset=false;
		pr_notice("...reboot cmd = %s,need_warm_reset=%d...\n",cmd,need_warm_reset);
#endif

	/* Hard reset the PMIC unless memory contents must be maintained. */
	if (need_warm_reset) {
#ifdef CONFIG_YL_POWER_ON_REASON
                pr_notice("...Now begin WARM RESET...\n");
#endif
		qpnp_pon_system_pwr_off(PON_POWER_OFF_WARM_RESET);
	} else {
#ifdef CONFIG_YL_POWER_ON_REASON
                pr_notice("...Now begin HARD RESET...\n");
#endif
		qpnp_pon_system_pwr_off(PON_POWER_OFF_HARD_RESET);
	}

#ifdef CONFIG_YL_POWER_ON_REASON
	if (cmd != NULL) {
		if (strlen(cmd) == 0) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_NULL_CMD);
			__raw_writel(NULL_CMD_MODE, restart_reason);
		} else if (!strcmp(cmd, "bootloader")) {
			qpnp_pon_set_restart_reason(
				PON_RESTART_REASON_FASTBOOT);
			__raw_writel(0x77665500, restart_reason);
		} else if (!strcmp(cmd, "recovery")) {
			qpnp_pon_set_restart_reason(
				PON_RESTART_REASON_RECOVERY);
			__raw_writel(0x77665502, restart_reason);
		} else if (!strcmp(cmd, "rtc")) {
			qpnp_pon_set_restart_reason(
				PON_RESTART_REASON_RTC);
			__raw_writel(0x77665503, restart_reason);
		} else if (!strncmp(cmd, "oem-", 4)) {
			unsigned long code;
			int ret;
			ret = kstrtoul(cmd + 4, 16, &code);
			if (!ret)
				__raw_writel(0x6f656d00 | (code & 0xff),
					     restart_reason);
			}
#if 0 //Loong mask the dload mode
		 else if (!strncmp(cmd, "edl", strlen(cmd))) 
		{
			enable_emergency_dload_mode();
		}
#endif
              else if (!strcmp(cmd, "reboot")){
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_NORMAL_REBOOT);
			__raw_writel(NORMAL_REBOOT_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "silence")){
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_SILENCE_REBOOT);
			__raw_writel(SILENCE_REBOOT_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "otabyforce")){
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_OTA_BY_FORCE);
			__raw_writel(OTA_BY_FORCE_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "lowmem")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_LOWMEM);
			__raw_writel(LOWMEM_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "RuimRefresh")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_RUIMREFRESH);
			__raw_writel(RUIMREFRESH_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "lostcard")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_LOSTCARD);
			__raw_writel(LOSTCARD_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "rtc")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_RTC);
			__raw_writel(RTC_REBOOT_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "fastmmi")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_FASTMMI);
			__raw_writel(FASTMMI_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "NvRecoveryOver")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_NV_RECOVERY);
			__raw_writel(NV_RECOVERY_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "AT_CFUN_Reset")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_AT_CFUN_RESET);
			__raw_writel(AT_CFUN_RESET_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "Checkin scheduled forced")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_CHECK_SCHEDULE);
			__raw_writel(CHECK_SCHEDULE_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "Checked scheduled range")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_CHECK_SCHEDULE);
			__raw_writel(CHECK_SCHEDULE_MODE, restart_reason);
		}
		else if (!strcmp(cmd, "safe_mode")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_SAFE);
				__raw_writel(SAFE_MODE, restart_reason);
		}else if (!strcmp(cmd, "Received ACTION_REBOOT broadcast")) {
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_ACTION);
			__raw_writel(ACTION_REBOOT, restart_reason);   
		} else {
			if (system_reboot_flag){
				qpnp_pon_set_restart_reason(PON_RESTART_REASON_SYSEXCEP_REBOOT);
				__raw_writel(SYSEXCEP_REBOOT_MODE, restart_reason);
				}
			else{
				qpnp_pon_set_restart_reason(PON_RESTART_REASON_KEREXCEP_REBOOT);
				__raw_writel(KEREXCEP_REBOOT_MODE, restart_reason);
				}
		}
	  } else {

		   qpnp_pon_system_pwr_off(PON_POWER_OFF_WARM_RESET);
                   pr_notice("...cmd = NULL Now change to WARM RESET...\n");

		   if (system_reboot_flag){
			pr_info("...Write SYSEXCEP REBOOT MODE...\n");
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_SYSEXCEP_REBOOT);
			__raw_writel(SYSEXCEP_REBOOT_MODE, restart_reason);
	           } 
		   else{
			pr_info("...Write KEREXCEP REBOOT MODE...\n");
			qpnp_pon_set_restart_reason(PON_RESTART_REASON_KEREXCEP_REBOOT);
                	}	__raw_writel(KEREXCEP_REBOOT_MODE, restart_reason);
		}
#else
if (cmd != NULL) {
		if (!strncmp(cmd, "bootloader", 10)) {
			qpnp_pon_set_restart_reason(
				PON_RESTART_REASON_BOOTLOADER);
			__raw_writel(0x77665500, restart_reason);
		} else if (!strncmp(cmd, "recovery", 8)) {
			qpnp_pon_set_restart_reason(
				PON_RESTART_REASON_RECOVERY);
			__raw_writel(0x77665502, restart_reason);
		} else if (!strcmp(cmd, "rtc")) {
			qpnp_pon_set_restart_reason(
				PON_RESTART_REASON_RTC);
			__raw_writel(0x77665503, restart_reason);
		} else if (!strncmp(cmd, "oem-", 4)) {
			unsigned long code;
			int ret;
			ret = kstrtoul(cmd + 4, 16, &code);
			if (!ret)
				__raw_writel(0x6f656d00 | (code & 0xff),
					     restart_reason);
		} else if (!strncmp(cmd, "edl", 3)) {
			enable_emergency_dload_mode();
		} else {
			__raw_writel(0x77665501, restart_reason);
		}
	}

#endif

	flush_cache_all();

	/*outer_flush_all is not supported by 64bit kernel*/
#ifndef CONFIG_ARM64
	outer_flush_all();
#endif

}

/*
 * Deassert PS_HOLD to signal the PMIC that we are ready to power down or reset.
 * Do this by calling into the secure environment, if available, or by directly
 * writing to a hardware register.
 *
 * This function should never return.
 */
static void deassert_ps_hold(void)
{
	struct scm_desc desc = {
		.args[0] = 0,
		.arginfo = SCM_ARGS(1),
	};

	if (scm_deassert_ps_hold_supported) {
		/* This call will be available on ARMv8 only */
		scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_PWR,
				 SCM_IO_DEASSERT_PS_HOLD), &desc);
	}

	/* Fall-through to the direct write in case the scm_call "returns" */
	__raw_writel(0, msm_ps_hold);
}

static void do_msm_restart(enum reboot_mode reboot_mode, const char *cmd)
{
	int ret;
	struct scm_desc desc = {
		.args[0] = 1,
		.args[1] = 0,
		.arginfo = SCM_ARGS(2),
	};

	pr_notice("Going down for restart now\n");

	msm_restart_prepare(cmd);

#ifdef CONFIG_MSM_DLOAD_MODE
	/*
	 * Trigger a watchdog bite here and if this fails,
	 * device will take the usual restart path.
	 */

	if (WDOG_BITE_ON_PANIC && in_panic)
		msm_trigger_wdog_bite();
#endif

	/* Needed to bypass debug image on some chips */
	if (!is_scm_armv8())
		ret = scm_call_atomic2(SCM_SVC_BOOT,
			       SCM_WDOG_DEBUG_BOOT_PART, 1, 0);
	else
		ret = scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_BOOT,
			  SCM_WDOG_DEBUG_BOOT_PART), &desc);
	if (ret)
		pr_err("Failed to disable secure wdog debug: %d\n", ret);

	halt_spmi_pmic_arbiter();
	deassert_ps_hold();

	mdelay(10000);
}

static void do_msm_poweroff(void)
{
	int ret;
	struct scm_desc desc = {
		.args[0] = 1,
		.args[1] = 0,
		.arginfo = SCM_ARGS(2),
	};
#ifdef CONFIG_YL_POWER_ON_REASON
	int force_dload_save = force_dload;
#endif

	pr_notice("Powering off the SoC\n");
#ifdef CONFIG_MSM_DLOAD_MODE
#ifdef CONFIG_YL_POWER_ON_REASON
	force_dload = 0;
#endif
	set_dload_mode(0);
#endif
	qpnp_pon_system_pwr_off(PON_POWER_OFF_SHUTDOWN);
	/* Needed to bypass debug image on some chips */
	if (!is_scm_armv8())
		ret = scm_call_atomic2(SCM_SVC_BOOT,
			       SCM_WDOG_DEBUG_BOOT_PART, 1, 0);
	else
		ret = scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_BOOT,
			  SCM_WDOG_DEBUG_BOOT_PART), &desc);
	if (ret)
		pr_err("Failed to disable wdog debug: %d\n", ret);

	halt_spmi_pmic_arbiter();
	deassert_ps_hold();

	mdelay(10000);
	pr_err("Powering off has failed\n");
#ifdef CONFIG_YL_POWER_ON_REASON
	force_dload = force_dload_save;
#endif
	return;
}

static int msm_restart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *mem;
	struct device_node *np;
	int ret = 0;

#ifdef CONFIG_MSM_DLOAD_MODE
	if (scm_is_call_available(SCM_SVC_BOOT, SCM_DLOAD_CMD) > 0)
		scm_dload_supported = true;

	atomic_notifier_chain_register(&panic_notifier_list, &panic_blk);
	np = of_find_compatible_node(NULL, NULL, DL_MODE_PROP);
	if (!np) {
		pr_err("unable to find DT imem DLOAD mode node\n");
	} else {
		dload_mode_addr = of_iomap(np, 0);
		if (!dload_mode_addr)
			pr_err("unable to map imem DLOAD offset\n");
	}

	np = of_find_compatible_node(NULL, NULL, EDL_MODE_PROP);
	if (!np) {
		pr_err("unable to find DT imem EDLOAD mode node\n");
	} else {
		emergency_dload_mode_addr = of_iomap(np, 0);
		if (!emergency_dload_mode_addr)
			pr_err("unable to map imem EDLOAD mode offset\n");
	}

#endif
	np = of_find_compatible_node(NULL, NULL,
				"qcom,msm-imem-restart_reason");
	if (!np) {
		pr_err("unable to find DT imem restart reason node\n");
	} else {
		restart_reason = of_iomap(np, 0);
		if (!restart_reason) {
			pr_err("unable to map imem restart reason offset\n");
			ret = -ENOMEM;
			goto err_restart_reason;
		}
	}
#ifdef CONFIG_YL_POWER_ON_REASON
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
#else
	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pshold-base");
#endif
	msm_ps_hold = devm_ioremap_resource(dev, mem);
	if (IS_ERR(msm_ps_hold))
		return PTR_ERR(msm_ps_hold);
#ifdef CONFIG_YL_POWER_ON_REASON
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
#else
	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "tcsr-boot-misc-detect");
#endif
	if (mem)
		tcsr_boot_misc_detect = mem->start;

	pm_power_off = do_msm_poweroff;
	arm_pm_restart = do_msm_restart;

	if (scm_is_call_available(SCM_SVC_PWR, SCM_IO_DISABLE_PMIC_ARBITER) > 0)
		scm_pmic_arbiter_disable_supported = true;

	if (scm_is_call_available(SCM_SVC_PWR, SCM_IO_DEASSERT_PS_HOLD) > 0)
		scm_deassert_ps_hold_supported = true;

	set_dload_mode(download_mode);

	return 0;

err_restart_reason:
#ifdef CONFIG_MSM_DLOAD_MODE
	iounmap(emergency_dload_mode_addr);
	iounmap(dload_mode_addr);
#endif
	return ret;
}

static const struct of_device_id of_msm_restart_match[] = {
	{ .compatible = "qcom,pshold", },
	{},
};
MODULE_DEVICE_TABLE(of, of_msm_restart_match);

static struct platform_driver msm_restart_driver = {
	.probe = msm_restart_probe,
	.driver = {
		.name = "msm-restart",
		.of_match_table = of_match_ptr(of_msm_restart_match),
	},
};

static int __init msm_restart_init(void)
{
	return platform_driver_register(&msm_restart_driver);
}
device_initcall(msm_restart_init);
