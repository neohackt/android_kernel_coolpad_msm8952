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
 */

#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "mdss_dsi.h"
#include "mdss_mdp.h"

#ifdef CONFIG_YL_LCD_DISPLAY_COMMON
int pwon_count=0;

extern void mdss_dsi_panel_color_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
			struct dsi_panel_cmds *pcmds);
extern u32 mdss_dsi_panel_cmd_read(struct mdss_dsi_ctrl_pdata *ctrl, char cmd0,
                char cmd1, void (*fxn)(int), char *rbuf, int len);
/**
 * mdss_dsi_reg_status_check() - Check dsi panel status through reg read
 * @ctrl_pdata: pointer to the dsi controller structure
 *
 * This function can be used to check the panel status through reading the
 * status register from the panel.
 *
 * Return: positive value if the panel is in good state, negative value or
 * zero otherwise.
 */
int mdss_dsi_reg_status_check(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	int ret = 0;
	int i=0,j=0;
	char *regdata;
	char *regcontent;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	if (ctrl_pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return 0;
	}

	pr_debug("%s: Checking Register status\n", __func__);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	if (ctrl_pdata->status_cmds.link_state == DSI_HS_MODE)
		mdss_dsi_set_tx_power_mode(0, &ctrl_pdata->panel_data);

	if((pinfo->esd_check_enabled)&&(pinfo->panel_power_state)){
		if(ctrl_pdata->hx8394a_cmds.cmd_cnt!=0)
		{
			pr_debug("william %s: esd--ctrl_pdata->hx8394a_cmds.cmd_cnt=0x%x\n",__func__,ctrl_pdata->hx8394a_cmds.cmd_cnt);
			mdss_dsi_panel_color_cmds_send(ctrl_pdata, &ctrl_pdata->hx8394a_cmds);
		}
		for(i=0;i<ctrl_pdata->panel_alive_reg.reg_num;i++)
		{
			regdata = ctrl_pdata->status_buf.data;
			for(j=0;j<ctrl_pdata->panel_alive_reg.check_regs[i].ret_num;j++){
				regdata[j]=0;		//clear data, to avoid using last value,because the data will not change if the function(mdss_dsi_panel_cmd_read) read error.
			}
			mdss_dsi_panel_cmd_read(ctrl_pdata,ctrl_pdata->panel_alive_reg.check_regs[i].reg,0x00,NULL,regdata,ctrl_pdata->panel_alive_reg.check_regs[i].ret_num);
			regcontent=ctrl_pdata->panel_alive_reg.check_regs[i].reg_content;
			for(j=0;j<ctrl_pdata->panel_alive_reg.check_regs[i].ret_num;j++){
				pr_debug("%s: reg_0x%x [%d], reg_content=0x%x, regdata=0x%x\n", __func__,ctrl_pdata->panel_alive_reg.check_regs[i].reg,j,*regcontent,*regdata);
				if(*(regdata++)!=*(regcontent++)){
					pr_err( "%s: william esdcheck: [%d]reg 0x%x = 0x%x should be 0x%x\n",__func__, i, ctrl_pdata->panel_alive_reg.check_regs[i].reg,*(regdata-1),*(regcontent-1));
					ret = -EINVAL;
					goto esd_check_end;
				}
			}
		}
	}
	ret = 1;

esd_check_end:
	if (ctrl_pdata->status_cmds.link_state == DSI_HS_MODE)
		mdss_dsi_set_tx_power_mode(1, &ctrl_pdata->panel_data);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);
	pr_debug("%s: Read register done with ret: %d\n", __func__, ret);
	return ret;
}
#endif
/*
 * mdss_check_te_status() - Check the status of panel for TE based ESD.
 * @ctrl_pdata   : dsi controller data
 * @pstatus_data : dsi status data
 * @interval     : duration in milliseconds to schedule work queue
 *
 * This function is called when the TE signal from the panel doesn't arrive
 * after 'interval' milliseconds. If the TE IRQ is not ready, the workqueue
 * gets re-scheduled. Otherwise, report the panel to be dead due to ESD attack.
 */
static bool mdss_check_te_status(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		struct dsi_status_data *pstatus_data, uint32_t interval)
{
	bool ret;

	/*
	 * During resume, the panel status will be ON but due to race condition
	 * between ESD thread and display UNBLANK (or rather can be put as
	 * asynchronuous nature between these two threads), the ESD thread might
	 * reach this point before the TE IRQ line is enabled or before the
	 * first TE interrupt arrives after the TE IRQ line is enabled. For such
	 * cases, re-schedule the ESD thread.
	 */
	ret = !atomic_read(&ctrl_pdata->te_irq_ready);
	if (ret) {
		schedule_delayed_work(&pstatus_data->check_status,
			msecs_to_jiffies(interval));
		pr_debug("%s: TE IRQ line not enabled yet\n", __func__);
	}

	return ret;
}

/*
 * mdss_check_dsi_ctrl_status() - Check MDP5 DSI controller status periodically.
 * @work     : dsi controller status data
 * @interval : duration in milliseconds to schedule work queue
 *
 * This function calls check_status API on DSI controller to send the BTA
 * command. If DSI controller fails to acknowledge the BTA command, it sends
 * the PANEL_ALIVE=0 status to HAL layer.
 */
void mdss_check_dsi_ctrl_status(struct work_struct *work, uint32_t interval)
{
	struct dsi_status_data *pstatus_data = NULL;
	struct mdss_panel_data *pdata = NULL;
	struct mipi_panel_info *mipi = NULL;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_overlay_private *mdp5_data = NULL;
	struct mdss_mdp_ctl *ctl = NULL;
	int ret = 0;

	pstatus_data = container_of(to_delayed_work(work),
		struct dsi_status_data, check_status);
	if (!pstatus_data || !(pstatus_data->mfd)) {
		pr_err("%s: mfd not available\n", __func__);
		return;
	}
#ifdef CONFIG_YL_LCD_DISPLAY_COMMON
	if((++pwon_count)<=10) {
		pr_err("william %s: pwon_count=%d\n",__func__,pwon_count);
		schedule_delayed_work(&pstatus_data->check_status,
			msecs_to_jiffies(interval));
			return;
	}
	pwon_count =11;
#endif
	pdata = dev_get_platdata(&pstatus_data->mfd->pdev->dev);
	if (!pdata) {
		pr_err("%s: Panel data not available\n", __func__);
		return;
	}
	mipi = &pdata->panel_info.mipi;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
							panel_data);
	if (!ctrl_pdata || (!ctrl_pdata->check_status &&
		(ctrl_pdata->status_mode != ESD_TE))) {
		pr_err("%s: DSI ctrl or status_check callback not available\n",
								__func__);
		return;
	}

	if (!pdata->panel_info.esd_rdy) {
		pr_warn("%s: unblank not complete, reschedule check status\n",
			__func__);
		schedule_delayed_work(&pstatus_data->check_status,
				msecs_to_jiffies(interval));
		return;
	}

	mdp5_data = mfd_to_mdp5_data(pstatus_data->mfd);
	ctl = mfd_to_ctl(pstatus_data->mfd);

	if (!ctl) {
		pr_err("%s: Display is off\n", __func__);
		return;
	}

	if (ctrl_pdata->status_mode == ESD_TE) {
		if (mdss_check_te_status(ctrl_pdata, pstatus_data, interval))
			return;
		else
			goto status_dead;
	}


	/*
	 * TODO: Because mdss_dsi_cmd_mdp_busy has made sure DMA to
	 * be idle in mdss_dsi_cmdlist_commit, it is not necessary
	 * to acquire ov_lock in case of video mode. Removing this
	 * lock to fix issues so that ESD thread would not block other
	 * overlay operations. Need refine this lock for command mode
	 */

	if (mipi->mode == DSI_CMD_MODE)
		mutex_lock(&mdp5_data->ov_lock);
	mutex_lock(&ctl->offlock);
	mutex_lock(&ctrl_pdata->mutex);

	if (mdss_panel_is_power_off(pstatus_data->mfd->panel_power_state) ||
			pstatus_data->mfd->shutdown_pending) {
		mutex_unlock(&ctrl_pdata->mutex);
		mutex_unlock(&ctl->offlock);
		if (mipi->mode == DSI_CMD_MODE)
			mutex_unlock(&mdp5_data->ov_lock);
		pr_err("%s: DSI turning off, avoiding panel status check\n",
							__func__);
		return;
	}

	/*
	 * For the command mode panels, we return pan display
	 * IOCTL on vsync interrupt. So, after vsync interrupt comes
	 * and when DMA_P is in progress, if the panel stops responding
	 * and if we trigger BTA before DMA_P finishes, then the DSI
	 * FIFO will not be cleared since the DSI data bus control
	 * doesn't come back to the host after BTA. This may cause the
	 * display reset not to be proper. Hence, wait for DMA_P done
	 * for command mode panels before triggering BTA.
	 */
	if (ctl->ops.wait_pingpong)
		ctl->ops.wait_pingpong(ctl, NULL);

	pr_debug("%s: DSI ctrl wait for ping pong done\n", __func__);

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);
	ret = ctrl_pdata->check_status(ctrl_pdata);
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);

	mutex_unlock(&ctrl_pdata->mutex);
	mutex_unlock(&ctl->offlock);
	if (mipi->mode == DSI_CMD_MODE)
		mutex_unlock(&mdp5_data->ov_lock);

	if ((pstatus_data->mfd->panel_power_state == MDSS_PANEL_POWER_ON)) {
		if (ret > 0)
			schedule_delayed_work(&pstatus_data->check_status,
				msecs_to_jiffies(interval));
		else
			goto status_dead;
	}

	if (pdata->panel_info.panel_force_dead) {
		pr_debug("force_dead=%d\n", pdata->panel_info.panel_force_dead);
		pdata->panel_info.panel_force_dead--;
		if (!pdata->panel_info.panel_force_dead)
			goto status_dead;
	}

	return;

status_dead:
	mdss_fb_report_panel_dead(pstatus_data->mfd);
}
