/*
 * Fingerprint sensor device driver
 *
 * This driver will control the platform resources that the FPC fingerprint
 * sensor needs to operate. The major things are probing the sensor to check
 * that it is actually connected and let the Kernel know this and with that also
 * enabling and disabling of regulators, enabling and disabling of platform
 * clocks, controlling GPIOs such as SPI chip select, sensor reset line, sensor
 * IRQ line, MISO and MOSI lines.
 *
 * The driver will expose most of its available functionality in sysfs which
 * enables dynamic control of these features from eg. a user space process.
 *
 * The sensor's IRQ events will be pushed to Kernel's event handling system and
 * are exposed in the drivers event node. This makes it possible for a user
 * space process to poll the input node and receive IRQ events easily. Usually
 * this node is available under /dev/input/eventX where 'X' is a number given by
 * the event system. A user space process will need to traverse all the event
 * nodes and ask for its parent's name (through EVIOCGNAME) which should match
 * the value in device tree named input-device-name.
 *
 * This driver will NOT send any SPI commands to the sensor it only controls the
 * electrical parts.
 *
 *
 * Copyright (c) 2015 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <soc/qcom/scm.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/types.h>
#include <linux/semaphore.h>
#include <linux/pinctrl/pinctrl.h>
#include <asm/io.h>
#include <linux/io.h>
#include <linux/fpsensor.h>
#include <linux/msm-bus.h>
#include <linux/msm-bus-board.h>
#include <linux/wakelock.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#endif

/*********************************************************/
#include <linux/qpnp/qpnp-adc.h>//yulong add
#include <linux/qpnp/pin.h>
#include <linux/hwmon.h>
/*********************************************************/

#define FPSENSOR_CFG_SPI_GPIO

#define FPSENSOR_RESET_LOW_US 3000
#define FPSENSOR_RESET_HIGH1_US 2000

enum msm_spi_clk_path_vec_idx {
	MSM_SPI_CLK_PATH_SUSPEND_VEC = 0,
	MSM_SPI_CLK_PATH_RESUME_VEC  = 1,
};
#define MSM_SPI_CLK_PATH_AVRG_BW(dd) (76800000)
#define MSM_SPI_CLK_PATH_BRST_BW(dd) (76800000)

#if defined(CONFIG_BOARD_CP3600I)
bool LCD_FPC_ID = false;
#endif

static const struct file_operations fpsensor_fops;
static const char * const pctl_names[] = {
	"fpsensor_reset_reset",
	"fpsensor_reset_active",
	"fpsensor_irq_active",
	"fpsensor_irq_default",
	"fpsensor_vdd_enable",
	"fpsensor_vdd_disable",
#ifdef FPSENSOR_CFG_SPI_GPIO
	"fpsensor_spi_active",
	"fpsensor_spi_sleep",
#endif	
};

#define MAX_NAME_LENGTH 20
struct module_id_info//yulong add 2015.03.06
{
	const char* name; // name of chip module
	u32 voltage;      // voltage of module_id pin mpp_2;unit mv
};

struct fpsensor_vreg_t {
	struct regulator *reg_ptr;
	const char *reg_name;
	int min_voltage;
	int max_voltage;
	int op_mode;
	int enable;	
};

struct qup_i2c_clk_path_vote {
	u32                         client_hdl;
	struct msm_bus_scale_pdata *pdata;
	bool                        reg_err;
};

struct fpsensor_data {
	struct platform_device *pdev;
	struct device *dev;
	struct input_dev *idev;
	struct class   *class;
	struct device  *device;
	struct cdev  cdev;
	dev_t   devno;	
	struct mutex lock;
	struct semaphore mutex;
	struct qpnp_vadc_chip *vadc_dev;
	wait_queue_head_t wq_irq_return;
	struct wakeup_source irq_ws;
	struct wakeup_source ioc_ws;
	struct wakeup_source ioc_cmd_ws;
	void __iomem *addr;	
	bool prepared;
	bool  interrupt_done;	
	int irq_enabled;	
	struct qup_i2c_clk_path_vote clk_path_vote;
	u32  master_id;
	struct pinctrl *fingerprint_pinctrl;
	struct pinctrl_state *pinctrl_state[ARRAY_SIZE(pctl_names)];
	struct clk *iface_clk;
	struct clk *core_clk;
	u32	speed_hz;
	struct fpsensor_vreg_t *vreg;	
	uint16_t num_vreg;
	int irq_num;
	int irq_gpio;
	int rst_gpio;
	int irq_status;
   	int boot_irq_status;
	int vdd_en;
#if defined(CONFIG_BOARD_CP3600I)
    int vdd_id_en;
#endif
	const char* sensor_name;
	struct module_id_info *modules;
	int module_size;
	u32 vadc_channel;	
	u32 *keycode;
	int keycode_size;
	bool use_wlock;
};

struct qpnp_vadc_chip *fpsensor_vadc_dev;//wangyadong changed
//add start for read vendor id
static struct fpsensor_data *g_fpsensor = NULL;
static int get_module_name(struct  fpsensor_data *fpsensor, char *buffer);

static int get_vendor(char *buf, struct kernel_param *kp)
{
    int rc = 0;
    char buffer[MAX_NAME_LENGTH]="";

    if(NULL == g_fpsensor) {
        printk(KERN_ERR "%s:g_fpsensor is NULL.\n", __func__);
        return sprintf(buf, "%s", "Unknow ID");
    }

    rc = get_module_name(g_fpsensor, buffer);
    if (0 == rc) {
        printk(KERN_ERR "%s:ID voltage is error.\n", __func__);
        return sprintf(buf, "%s", "Unknow ID");
    }

    return sprintf(buf, "%s", buffer);
}
module_param_call(vendor, NULL, get_vendor, NULL, 0444);
// add end
static int fpsensor_clk_path_postponed_register(struct fpsensor_data *fpsensor)
{
	fpsensor->clk_path_vote.client_hdl = msm_bus_scale_register_client(
						fpsensor->clk_path_vote.pdata);

	if (fpsensor->clk_path_vote.client_hdl) {
		if (fpsensor->clk_path_vote.reg_err) {
			/* log a success message if an error msg was logged */
			fpsensor->clk_path_vote.reg_err = false;
			dev_info(fpsensor->dev,
				"msm_bus_scale_register_client(mstr-id:%d "
				"actv-only:%d):0x%x",
				fpsensor->master_id, 0,
				fpsensor->clk_path_vote.client_hdl);
		}

	} else {
		/* guard to log only one error on multiple failure */
		if (!fpsensor->clk_path_vote.reg_err) {
			fpsensor->clk_path_vote.reg_err = true;

			dev_info(fpsensor->dev,
				"msm_bus_scale_register_client(mstr-id:%d "
				"actv-only:%d):0",
				fpsensor->master_id, 0);
		}
	}

	return fpsensor->clk_path_vote.client_hdl ? 0 : -EAGAIN;
}

/**
 * fpsensor_clk_path_init_structs: internal impl detail of msm_spi_clk_path_init
 *
 * allocates and initilizes the bus scaling vectors.
 */
static int fpsensor_clk_path_init_structs(struct fpsensor_data *fpsensor)
{
	struct msm_bus_vectors *paths    = NULL;
	struct msm_bus_paths   *usecases = NULL;

	dev_dbg(fpsensor->dev, "initialises path clock voting structs");

	paths = devm_kzalloc(fpsensor->dev, sizeof(*paths) * 2, GFP_KERNEL);
	if (!paths) {
		dev_err(fpsensor->dev,
		"msm_bus_paths.paths memory allocation failed");
		return -ENOMEM;
	}

	usecases = devm_kzalloc(fpsensor->dev, sizeof(*usecases) * 2, GFP_KERNEL);
	if (!usecases) {
		dev_err(fpsensor->dev,
		"msm_bus_scale_pdata.usecases memory allocation failed");
		goto path_init_err;
	}

	fpsensor->clk_path_vote.pdata = devm_kzalloc(fpsensor->dev,
					    sizeof(*fpsensor->clk_path_vote.pdata),
					    GFP_KERNEL);
	if (!fpsensor->clk_path_vote.pdata) {
		dev_err(fpsensor->dev,
		"msm_bus_scale_pdata memory allocation failed");
		goto path_init_err;
	}

	paths[MSM_SPI_CLK_PATH_SUSPEND_VEC] = (struct msm_bus_vectors) {
		.src = fpsensor->master_id,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	};

	paths[MSM_SPI_CLK_PATH_RESUME_VEC]  = (struct msm_bus_vectors) {
		.src = fpsensor->master_id,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = MSM_SPI_CLK_PATH_AVRG_BW(fpsensor),
		.ib  = MSM_SPI_CLK_PATH_BRST_BW(fpsensor),
	};

	usecases[MSM_SPI_CLK_PATH_SUSPEND_VEC] = (struct msm_bus_paths) {
		.num_paths = 1,
		.vectors   = &paths[MSM_SPI_CLK_PATH_SUSPEND_VEC],
	};

	usecases[MSM_SPI_CLK_PATH_RESUME_VEC] = (struct msm_bus_paths) {
		.num_paths = 1,
		.vectors   = &paths[MSM_SPI_CLK_PATH_RESUME_VEC],
	};

	*fpsensor->clk_path_vote.pdata = (struct msm_bus_scale_pdata) {
		.active_only  = 0,
		.name         = dev_name(fpsensor->dev),
		.num_usecases = 2,
		.usecase      = usecases,
	};

	return 0;

path_init_err:
	devm_kfree(fpsensor->dev, paths);
	devm_kfree(fpsensor->dev, usecases);
	devm_kfree(fpsensor->dev, fpsensor->clk_path_vote.pdata);
	fpsensor->clk_path_vote.pdata = NULL;
	return -ENOMEM;
}

static void msm_spi_clk_path_init(struct fpsensor_data *fpsensor)
{
	/*
	 * bail out if path voting is diabled (master_id == 0) or if it is
	 * already registered (client_hdl != 0)
	 */
	if (!fpsensor->master_id || fpsensor->clk_path_vote.client_hdl)
		return;

	/* if fail once then try no more */
	if (!fpsensor->clk_path_vote.pdata && fpsensor_clk_path_init_structs(fpsensor)) {
		fpsensor->master_id = 0;
		return;
	};

	/* on failure try again later */
	if (fpsensor_clk_path_postponed_register(fpsensor))
		return;
}

/**
 * Prepare or unprepare the SPI master that we are soon to transfer something
 * over SPI.
 *
 * Please see Linux Kernel manual for SPI master methods for more information.
 *
 * @see Linux SPI master methods
 */
static int spi_set_fabric(struct fpsensor_data *fpsensor, bool active)
{
	int rc = -1;
	unsigned int index;
	if(fpsensor->clk_path_vote.client_hdl == 0)
	{
		dev_err(fpsensor->dev, "%s: client_hdl is 0\n", __func__);
		return rc;
	}
	index = active ?MSM_SPI_CLK_PATH_RESUME_VEC:MSM_SPI_CLK_PATH_SUSPEND_VEC;

	rc = msm_bus_scale_client_update_request(fpsensor->clk_path_vote.client_hdl,index);
	if (rc)
		dev_err(fpsensor->dev, "%s: rc %d fail\n", __func__, rc);
	else
		dev_dbg(fpsensor->dev, "%s: %d ok\n", __func__, active);
	
	return rc;
}

static long spi_clk_max_rate(struct clk *clk, unsigned long rate)
{
	long lowest_available, nearest_low, step_size, cur;
	long step_direction = -1;
	long guess = rate;
	int  max_steps = 10;

	cur =  clk_round_rate(clk, rate);
	if (cur == rate)
		return rate;

	/* if we got here then: cur > rate */
	lowest_available =  clk_round_rate(clk, 0);
	if (lowest_available > rate)
		return -EINVAL;

	step_size = (rate - lowest_available) >> 1;
	nearest_low = lowest_available;

	while (max_steps-- && step_size) {
		guess += step_size * step_direction;
		cur =  clk_round_rate(clk, guess);

		if ((cur < rate) && (cur > nearest_low))
			nearest_low = cur;
		/*
		 * if we stepped too far, then start stepping in the other
		 * direction with half the step size
		 */
		if (((cur > rate) && (step_direction > 0))
		 || ((cur < rate) && (step_direction < 0))) {
			step_direction = -step_direction;
			step_size >>= 1;
		 }
	}
	return nearest_low;
}

static int config_clks(struct fpsensor_data *fpsensor, bool enable)
{
	int rc;
	if (enable) {
		rc = clk_set_rate(fpsensor->core_clk,
				fpsensor->speed_hz);
		if (rc) {
			dev_err(fpsensor->dev,
					"%s: Error setting clk_rate: %u, %d\n",
					__func__, fpsensor->speed_hz,
					rc);
			return rc;
		}
		rc = clk_prepare_enable(fpsensor->core_clk);
		if (rc) {
			dev_err(fpsensor->dev,
					"%s: Error enabling core clk: %d\n",
					__func__, rc);
			return rc;
		}
		rc = clk_prepare_enable(fpsensor->iface_clk);
		if (rc) {
			dev_err(fpsensor->dev,
					"%s: Error enabling iface clk: %d\n",
					__func__, rc);
			clk_disable_unprepare(fpsensor->core_clk);
			return rc;
		}
		dev_dbg(fpsensor->dev, "%s ok. clk rate %u hz\n", __func__,
				fpsensor->speed_hz);
	} else {
		clk_disable_unprepare(fpsensor->iface_clk);
		clk_disable_unprepare(fpsensor->core_clk);
		rc = 0;
	}
	return rc;
}

/**
 * Will try to select the set of pins (GPIOS) defined in a pin control node of
 * the device tree named @p name.
 *
 * The node can contain several eg. GPIOs that is controlled when selecting it.
 * The node may activate or deactivate the pins it contains, the action is
 * defined in the device tree node itself and not here. The states used
 * internally is fetched at probe time.
 *
 * @see pctl_names
 * @see fpsensor_probe
 */
static int select_pin_ctl(struct fpsensor_data *fpsensor, const char *name)
{
	size_t i;
	int rc;
	struct device *dev = fpsensor->dev;
	if(fpsensor->fingerprint_pinctrl == NULL){
		dev_err(dev, "fingerprint_pinctrl is NULL'\n");
		return rc;
	}
	
	for (i = 0; i < ARRAY_SIZE(fpsensor->pinctrl_state); i++) {
		const char *n = pctl_names[i];
		if (!strncmp(n, name, strlen(n))) {
			rc = pinctrl_select_state(fpsensor->fingerprint_pinctrl,
					fpsensor->pinctrl_state[i]);
			if (rc)
				dev_err(dev, "cannot select '%s'\n", name);
			else
				dev_dbg(dev, "Selected '%s'\n", name);
			goto err;
		}
	}
	rc = -EINVAL;
	dev_err(dev, "%s:'%s' not found\n", __func__, name);
err:
	return rc;
}


static int hw_reset(struct  fpsensor_data *fpsensor)
{
	int irq_gpio = -1;
//	struct device *dev = fpsensor->dev;
	int rc;
#ifdef FPSENSOR_CFG_SPI_GPIO
	if(fpsensor->prepared == true)
		rc = select_pin_ctl(fpsensor, "fpsensor_spi_sleep");
#endif
//	rc = select_pin_ctl(fpsensor, "fpsensor_reset_active");
//	if (rc)
//		goto err;
//	usleep_range(FPSENSOR_RESET_HIGH1_US, FPSENSOR_RESET_HIGH1_US + 100);
	fpsensor->irq_status = -1;
   fpsensor->boot_irq_status = 0;
	rc = select_pin_ctl(fpsensor, "fpsensor_reset_reset");
	if (rc)
		goto err;
	usleep_range(FPSENSOR_RESET_LOW_US, FPSENSOR_RESET_LOW_US + 100);

#ifdef FPSENSOR_CFG_SPI_GPIO
	rc = select_pin_ctl(fpsensor, "fpsensor_spi_active");
#endif
	rc = select_pin_ctl(fpsensor, "fpsensor_reset_active");
	if (rc)
		goto err;
	usleep_range(FPSENSOR_RESET_HIGH1_US, FPSENSOR_RESET_HIGH1_US + 100);

#ifdef FPSENSOR_CFG_SPI_GPIO
	if(fpsensor->prepared == false)
		rc = select_pin_ctl(fpsensor, "fpsensor_spi_sleep");
#endif
	irq_gpio = gpio_get_value(fpsensor->irq_gpio)?0:-1;
	if(!irq_gpio){
		fpsensor->irq_status = 1;	
		printk(KERN_INFO "%s OK ! fpsensor->irq_status =%d \n", __func__,fpsensor->irq_status);
	}else{
	
		fpsensor->irq_status = 0;	
		printk(KERN_INFO "%s timed out,retrying ..fpsensor->irq_status = %d \n",__func__,fpsensor->irq_status);	
	}
	return irq_gpio;
err:
	return rc;
}
//add for fastmmi power suspend by jiangyuelong

static int hw_reset_state(struct  fpsensor_data *fpsensor)
{
        int rc;
        printk("###%s fingerprint for fastmmi test\n",__func__);	

        rc = select_pin_ctl(fpsensor, "fpsensor_reset_reset");
        if (rc)
            printk(KERN_INFO "###%sselect_pin_ctl fail rc =%d!\n",__func__,rc);	                
        printk(KERN_INFO "###%s ok!\n",__func__);	
        
        return rc;
}


static int hw_power_ctrl(struct  fpsensor_data *fpsensor,unsigned int enable)
{
        int rc;
        printk("###%s fingerprint for fastmmi test enable =%d \n",__func__,enable);	
        if(enable){
            rc = select_pin_ctl(fpsensor,"fpsensor_vdd_enable");
            if (rc)
                pr_err("cannot get pinctrl_select_state pinctrl state rc =%d\\n",rc);
        }else{

            rc = select_pin_ctl(fpsensor,"fpsensor_vdd_disable");
            if (rc) 
                pr_err("cannot get fpsensor_vdd_disable pinctrl state rc =%d\n",rc);
        }
        printk(KERN_INFO "###%s ok!\n",__func__);	
        
        return rc;
}
//end jyl

static int config_vreg(struct device *dev, struct fpsensor_vreg_t *vreg, int enable)
{
	int rc = 0;
	if (!dev || !vreg) {
		dev_err(dev,"%s: get failed NULL parameter\n", __func__);
		goto vreg_get_fail;
	}
	if(enable == vreg->enable) {
		dev_err(dev,"%s: already %s\n", __func__,enable?"enabled":"disable");
		return 0;
	}
	if (enable) {
		dev_dbg(dev,"%s enable %s\n", __func__, vreg->reg_name);
		vreg->reg_ptr = regulator_get(dev, vreg->reg_name);
		if (IS_ERR_OR_NULL(vreg->reg_ptr)) {
			dev_err(dev,"%s: %s get failed\n", __func__,
				vreg->reg_name);
			vreg->reg_ptr = NULL;
			goto vreg_get_fail;
		}
		if (regulator_count_voltages(vreg->reg_ptr) > 0) {
			rc = regulator_set_voltage(
				vreg->reg_ptr, vreg->min_voltage,
				vreg->max_voltage);
			if (rc < 0) {
				dev_err(dev,"%s: %s set voltage failed\n",
					__func__, vreg->reg_name);
				goto vreg_set_voltage_fail;
			}
			if (vreg->op_mode >= 0) {
				rc = regulator_set_optimum_mode(vreg->reg_ptr,
					vreg->op_mode);
				if (rc < 0) {
					dev_err(dev,
					"%s: %s set optimum mode failed\n",
					__func__, vreg->reg_name);
					//goto vreg_set_opt_mode_fail;
				}
			}
		}
		rc = regulator_enable(vreg->reg_ptr);
		if (rc < 0) {
			dev_err(dev,"%s: %s enable failed\n",
				__func__, vreg->reg_name);
			goto vreg_unconfig;
		}
		vreg->enable = enable;
	} else {
		if (vreg->reg_ptr) {
			dev_dbg(dev,"%s disable %s\n", __func__, vreg->reg_name);
			regulator_disable(vreg->reg_ptr);
			if (regulator_count_voltages(vreg->reg_ptr) > 0) {
				if (vreg->op_mode >= 0)
					regulator_set_optimum_mode(vreg->reg_ptr, 0);
				regulator_set_voltage(
					vreg->reg_ptr, 0, vreg->max_voltage);
			}
			regulator_put(vreg->reg_ptr);
			vreg->reg_ptr = NULL;
			vreg->enable = 0;
		}
	}
	return 0;

vreg_unconfig:
if (regulator_count_voltages(vreg->reg_ptr) > 0)
	regulator_set_optimum_mode(vreg->reg_ptr, 0);

//vreg_set_opt_mode_fail:
if (regulator_count_voltages(vreg->reg_ptr) > 0)
	regulator_set_voltage(vreg->reg_ptr, 0, vreg->max_voltage);

vreg_set_voltage_fail:
	regulator_put(vreg->reg_ptr);
	vreg->reg_ptr = NULL;

vreg_get_fail:
	return -ENODEV;
}

/**
 * Will setup clocks, GPIOs, and regulators to correctly initialize the touch
 * sensor to be ready for work.
 *
 * In the correct order according to the sensor spec this function will
 * enable/disable regulators, SPI platform clocks, and reset line, all to set
 * the sensor in a correct power on or off state "electrical" wise.
 *
 * @see  spi_prepare_set
 * @note This function will not send any commands to the sensor it will only
 *       control it "electrically".
 */
static int device_prepare(struct  fpsensor_data *fpsensor, bool enable)
{
	int rc;

	mutex_lock(&fpsensor->lock);
	if (enable && !fpsensor->prepared) {
		fpsensor->prepared = true;

#ifdef FPSENSOR_CFG_SPI_GPIO
		rc = select_pin_ctl(fpsensor, "fpsensor_spi_active");
#endif
		rc = spi_set_fabric(fpsensor, true);
		if (rc)
			goto err_1;
		rc = config_clks(fpsensor, true);
		if (rc)
			goto err_2;

		usleep_range(100, 200);

#ifdef SET_PIPE_OWNERSHIP
		rc = set_pipe_ownership(fpsensor, true);
		if (rc)
			goto err_3;
#endif
	} else if (!enable && fpsensor->prepared) {
		rc = 0;
#ifdef SET_PIPE_OWNERSHIP
		(void)set_pipe_ownership(fpsensor, false);
err_3:
#endif
		(void)config_clks(fpsensor, false);
err_2:
		(void)spi_set_fabric(fpsensor, false);
err_1:

#ifdef FPSENSOR_CFG_SPI_GPIO
		rc = select_pin_ctl(fpsensor, "fpsensor_spi_sleep");
#endif
		fpsensor->prepared = false;
	} else {
		rc = 0;
	}
	mutex_unlock(&fpsensor->lock);
	return rc;
}
#if defined(CONFIG_BOARD_CP3600I)
static int get_fpc_id_voltage(struct  fpsensor_data *fpsensor)
{
    struct qpnp_vadc_result result;
    int rc=0;
    if (IS_ERR(fpsensor->vadc_dev)) {
        rc = PTR_ERR(fpsensor->vadc_dev);
        if (rc == -EPROBE_DEFER)
            dev_err(fpsensor->dev,"vadc not found - defer probe rc=%d\n", rc);
        else
            dev_err(fpsensor->dev,"vadc property missing, rc=%d\n", rc);
        return rc;
    }
    dev_err(fpsensor->dev,"vadc property missing, rc=%d \
            pmi8950 vadc_channel = %d\n", rc, fpsensor->vadc_channel);
    rc = qpnp_vadc_read(fpsensor_vadc_dev, fpsensor->vadc_channel, &result);

    return (result.physical >> 10);
}
#endif

int get_id_voltage(void)
{
        struct qpnp_vadc_result result;
        int id_voltage=0;

        qpnp_vadc_read(fpsensor_vadc_dev,P_MUX2_1_1,&result);

        id_voltage = (int)result.physical/1000; /* uV to mV */ 

        return id_voltage;
}

#if 0
static int get_module_name(struct  fpsensor_data *fpsensor, char *buffer)
{
	int i = 0;
	int voltage_mv = 0;
	
  	voltage_mv = get_id_voltage(fpsensor);
	dev_err(fpsensor->dev,"vadc voltage=%d",voltage_mv);
	if(voltage_mv < 0)
		return 0;
	
	for (i = 0;i < fpsensor->module_size;i++)
	{
		if ((voltage_mv < fpsensor->modules[i].voltage  + 100)&&(voltage_mv > fpsensor->modules[i].voltage - 100))
		{
			return sprintf(buffer, "%s",fpsensor->modules[i].name);	
		}
	}

	return 0;
}
#endif

static int get_module_name(struct  fpsensor_data *fpsensor, char *buffer)
{
	int i = 0;
	int voltage_mv = 0;

#if defined(CONFIG_BOARD_CP3600I)
    if(LCD_FPC_ID)
        voltage_mv = get_fpc_id_voltage(fpsensor);
    else
#endif
        voltage_mv = get_id_voltage();
	printk(KERN_ERR "wangyadong voltage_mv is %d\n", voltage_mv);
	if(voltage_mv < 0)
		return 0;
	
	for (i = 0;i < fpsensor->module_size;i++)
	{
		if ((voltage_mv < fpsensor->modules[i].voltage  + 100)&&(voltage_mv > fpsensor->modules[i].voltage - 100))
		{
	printk(KERN_ERR "wangyadong modules.name is %s\n", fpsensor->modules[i].name);
			return sprintf(buffer, "%s",fpsensor->modules[i].name);	
		}
	}

	return sprintf(buffer, "%s","unknownID");
}

static irqreturn_t fpsensor_irq_handler(int irq, void *handle)
{
	struct fpsensor_data *fpsensor = handle;
   if( fpsensor->boot_irq_status == 0){
         fpsensor->boot_irq_status = 1;
         dev_err(fpsensor->dev, "%s boot_irq_status=%d\n", __func__,  fpsensor->boot_irq_status);
    }
	if (gpio_get_value(fpsensor->irq_gpio)) {
		fpsensor->interrupt_done = true;
		wake_up_interruptible(&fpsensor->wq_irq_return);
		__pm_wakeup_event(&fpsensor->irq_ws, 1000);
		return IRQ_HANDLED;
	}
	return IRQ_NONE; 
}

static int fpsensor_request_named_gpio(struct fpsensor_data *fpsensor,
		const char *label, int *gpio)
{
	struct device *dev = fpsensor->dev;
	struct device_node *np = dev->of_node;
	int rc = of_get_named_gpio(np, label, 0);
	if (rc < 0) {
		dev_err(fpsensor->dev, "failed to get '%s'\n", label);
		return rc;
	}
	*gpio = rc;
	rc = devm_gpio_request(dev, *gpio, label);
	if (rc) {
		dev_err(fpsensor->dev, "failed to request gpio %d\n", *gpio);
		return rc;
	}
	dev_dbg(fpsensor->dev, "%s %d\n", label, *gpio);
	return 0;
}

static int fpsensor_get_dt_vreg_data(struct device_node *of_node, struct fpsensor_data * fpsensor)
{
	int rc = 0, i = 0;
	uint32_t count = 0;
	uint32_t *vreg_array = NULL;
	struct fpsensor_vreg_t *vreg;
	struct device *dev = fpsensor->dev;

	count = of_property_count_strings(of_node, "fpsensor-vreg-name");
	dev_dbg(dev,"%s fpsensor-vreg-name count %d\n", __func__, count);

	if (!count)
		return 0;

	vreg = kzalloc(sizeof(*vreg) * count, GFP_KERNEL);
	if (!vreg) {
		dev_err(fpsensor->dev,"%s failed %d\n", __func__, __LINE__);
		return -ENOMEM;
	}
	
	for (i = 0; i < count; i++) {
		rc = of_property_read_string_index(of_node,
			"fpsensor-vreg-name", i,
			&vreg[i].reg_name);
		dev_dbg(dev,"%s reg_name[%d] = %s\n", __func__, i,
			vreg[i].reg_name);
		if (rc < 0) {
			dev_err(fpsensor->dev,"%s failed %d\n", __func__, __LINE__);
			goto ERROR1;
		}
	}

	vreg_array = kzalloc(sizeof(uint32_t) * count, GFP_KERNEL);
	if (!vreg_array) {
		dev_err(fpsensor->dev,"%s failed %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto ERROR1;
	}

	rc = of_property_read_u32_array(of_node, "fpsensor-vreg-min-voltage",
		vreg_array, count);
	if (rc < 0) {
		dev_err(fpsensor->dev,"%s failed %d\n", __func__, __LINE__);
		goto ERROR2;
	}
	for (i = 0; i < count; i++) {
		vreg[i].min_voltage = vreg_array[i];
		dev_dbg(dev,"%s vreg[%d].min_voltage = %d\n", __func__,
			i, vreg[i].min_voltage);
	}

	rc = of_property_read_u32_array(of_node, "fpsensor-vreg-max-voltage",
		vreg_array, count);
	if (rc < 0) {
		dev_err(fpsensor->dev,"%s failed %d\n", __func__, __LINE__);
		goto ERROR2;
	}
	for (i = 0; i < count; i++) {
		vreg[i].max_voltage = vreg_array[i];
		dev_dbg(dev,"%s vreg[%d].max_voltage = %d\n", __func__,
			i, vreg[i].max_voltage);
	}

	rc = of_property_read_u32_array(of_node, "fpsensor-vreg-op-mode",
		vreg_array, count);
	if (rc < 0) {
		dev_err(fpsensor->dev,"%s failed %d\n", __func__, __LINE__);
		goto ERROR2;
	}
	for (i = 0; i < count; i++) {
		vreg[i].op_mode = vreg_array[i];
		dev_dbg(dev,"%s vreg[%d].op_mode = %d\n", __func__, i,
			vreg[i].op_mode);
	}
	fpsensor->vreg = vreg;
	fpsensor->num_vreg = count;	
	kfree(vreg_array);
	return rc;
ERROR2:
	kfree(vreg_array);
ERROR1:
	kfree(vreg);
	fpsensor->vreg = NULL;
	fpsensor->num_vreg = 0;
	return rc;
}

//static int fpsensor_cleanup(struct fpsensor_data *fpsensor)
static int fpsensor_cleanup(struct device *dev,struct fpsensor_data *fpsensor)
{
	int i;
	dev_dbg(fpsensor->dev, "%s\n", __func__);
	if(fpsensor->devno)
	{
		cdev_del(&fpsensor->cdev);
		unregister_chrdev_region(fpsensor->devno, 1);
	}
	
	if(fpsensor->idev != NULL) {
		input_unregister_device(fpsensor->idev);
		input_free_device(fpsensor->idev);
	}
	
	if(fpsensor->keycode)
		kfree(fpsensor->keycode);

	select_pin_ctl(fpsensor, "fpsensor_reset_reset");
	select_pin_ctl(fpsensor, "fpsensor_irq_default");
	if (fpsensor->irq_num > 0)
		free_irq(fpsensor->irq_num, fpsensor);

	if (gpio_is_valid(fpsensor->irq_gpio))
		gpio_free(fpsensor->irq_gpio);

	if (gpio_is_valid(fpsensor->rst_gpio))
		gpio_free(fpsensor->rst_gpio);

	if (gpio_is_valid(fpsensor->vdd_en))
		gpio_free(fpsensor->vdd_en);

	for(i=0;i < fpsensor->num_vreg;i++)
	{
		config_vreg(fpsensor->dev, &fpsensor->vreg[i], 0);
	}

	if(fpsensor->modules)
		kfree(fpsensor->modules);
	
	if(fpsensor->addr)
		iounmap(fpsensor->addr);
	
	if(fpsensor->use_wlock) {
		wakeup_source_trash(&fpsensor->irq_ws);
		wakeup_source_trash(&fpsensor->ioc_ws);
		wakeup_source_trash(&fpsensor->ioc_cmd_ws);
	}
	platform_set_drvdata(fpsensor->pdev, NULL);
	devm_kfree(dev,fpsensor);
	pr_err("%s exit\n",__func__);
	return 0;
}

static int fpsensor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int rc = 0;
	size_t i;
	struct device_node *np = dev->of_node;
	struct device_node *pp;
	u32 __iomem phy_addr;	/*  physical address */
	struct fpsensor_data *fpsensor;
	struct pinctrl_state *vdd_on;
#if defined(CONFIG_BOARD_CP3600I)
    struct pinctrl_state *vdd_id_on;
#endif
	//struct qpnp_vadc_chip *vadc_dev;
	const  __be32 *addrp;

printk(KERN_ERR "%s: wangyadong\n", __func__);

	if (!np) {
		dev_err(dev, "no of node found\n");
		return -EINVAL;
	}

#if 0
	vadc_dev = qpnp_get_vadc(&pdev->dev, "fpsensor");
	if (IS_ERR(vadc_dev)) {
		rc = PTR_ERR(vadc_dev);
		if (rc == -EPROBE_DEFER)
			dev_err(dev,"vadc not found - defer probe rc=%d\n", rc);
		else
			dev_err(dev,"vadc property missing, rc=%d\n", rc);
		return rc;
    }
#endif

    fpsensor_vadc_dev = qpnp_get_vadc(&pdev->dev, "fpsensor");

    if (IS_ERR(fpsensor_vadc_dev)) {
        rc = PTR_ERR(fpsensor_vadc_dev);
        if (rc == -EPROBE_DEFER)
            pr_err("vadc not found - defer probe rc=%d\n", rc);
        else
            pr_err("vadc property missing, rc=%d\n", rc);

        return rc;
    }

	fpsensor = devm_kzalloc(dev, sizeof(*fpsensor), GFP_KERNEL);
	if (!fpsensor) {
		dev_err(dev,
			"failed to allocate memory for struct fpsensor_data\n");
		return -ENOMEM;
	}
	
	fpsensor->dev = dev;
	platform_set_drvdata(pdev, fpsensor);
	fpsensor->pdev = pdev;
	fpsensor->vadc_dev = fpsensor_vadc_dev;

	rc = of_property_count_strings(np, "reg-names");
	if (rc != 1) 
	{
		dev_err(dev, "must have serialnum register,please check data sheet:%d\n",rc);
		rc = -EINVAL;
		goto err;
	}
	addrp = of_get_address(np, 0, NULL, NULL);
	if(addrp == NULL) {
		dev_err(dev, "must specify serialnum address\n");
		rc = -EINVAL;
		goto err;
	}else {
		phy_addr = be32_to_cpu(*addrp);
		if(phy_addr)
		{
			fpsensor->addr = ioremap(phy_addr,4);
			if (!fpsensor->addr) {
				dev_err(dev, "Failed to map in CC registers.\n");
				rc = -ENOMEM;
				goto err;
			}
			dev_dbg(dev, "fingerprint mapped %16.16llx->%p\n",(unsigned long long)phy_addr,fpsensor->addr);
		}
	}

	rc = of_property_read_string(np, "sensor-name", &(fpsensor->sensor_name));
	if(rc){
		dev_err(dev, "must specify sensor-name\n");
		rc = -EINVAL;
		goto err;
	}
	rc = of_property_read_u32(np, "id-vadc-channel", &fpsensor->vadc_channel);
	if(rc){
		dev_err(dev, "must specify id-vadc-channel\n");
		rc = -EINVAL;
		goto err;
	}

	fpsensor->module_size = of_get_child_count(np);
	if (fpsensor->module_size == 0) {
		dev_err(dev, "At least have a module\n");
		rc = -ENODEV;
		goto err;
	}

	fpsensor->modules = kzalloc(fpsensor->module_size * (sizeof(*fpsensor->modules)),GFP_KERNEL);
	if (!fpsensor->modules) {
		dev_err(dev, "Malloc for modules fail\n");
		rc = -ENOMEM;
		goto err;
	}

	i = 0;
	for_each_child_of_node(np, pp) {
		rc  = of_property_read_string(pp, "module-name", &(fpsensor->modules[i].name));
		if (rc ) {
			dev_err(dev, "no module-name found\n");
		}

		rc  = of_property_read_u32(pp, "voltage", &(fpsensor->modules[i].voltage));
		if (rc ) {
			dev_err(dev, "no voltage found\n");
		}
		i++;
	}
	
	rc = fpsensor_get_dt_vreg_data(np,fpsensor);
	if(rc){
		dev_err(dev, "parse regulator fail\n");
		rc = -EINVAL;
		goto err;
	}

	for(i=0;i < fpsensor->num_vreg;i++)
	{
		rc = config_vreg(fpsensor->dev, &fpsensor->vreg[i], 1);
		if(rc){
			dev_err(dev, "config regulator fail\n");
			rc = -EINVAL;
			goto err;
		}
	}

	rc = fpsensor_request_named_gpio(fpsensor, "gpio_irq",
			&fpsensor->irq_gpio);
	if(rc){
		dev_err(dev, "must specify gpio_irq\n");
		rc = -EINVAL;
		goto err;
	}

	rc = fpsensor_request_named_gpio(fpsensor, "gpio_rst",
			&fpsensor->rst_gpio);
	if(rc){
		dev_err(dev, "must specify gpio_rst\n");
		rc = -EINVAL;
		goto err;
	}

	fpsensor->iface_clk = clk_get(dev, "iface_clk");
	if (IS_ERR(fpsensor->iface_clk)) {
		dev_err(dev, "%s: Failed to get iface_clk\n", __func__);
		rc = -EINVAL;
		goto err;
	}

	fpsensor->core_clk = clk_get(dev, "core_clk");
	if (IS_ERR(fpsensor->core_clk)) {
		dev_err(dev, "%s: Failed to get core_clk\n", __func__);
		rc = -EINVAL;
		goto err;
	}

	rc = of_property_read_u32(np, "spi-max-frequency", &fpsensor->speed_hz);
	if (rc < 0) {
		dev_err(dev, "spi-max-frequency not found\n");
		goto err;
	}	
	dev_err(dev, "default speed_hz is %d",fpsensor->speed_hz);
	
	rc = of_property_read_u32(np, "master-id", &fpsensor->master_id);
	if (rc < 0) {
		dev_err(dev, "master-id not found\n");
		goto err;
	}
	dev_dbg(dev, "master-id %d\n", fpsensor->master_id);
	//msm_spi_clk_path_init(fpsensor); add jiangyuelong
	fpsensor->fingerprint_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(fpsensor->fingerprint_pinctrl)) {
		if (PTR_ERR(fpsensor->fingerprint_pinctrl) == -EPROBE_DEFER) {
			dev_info(dev, "pinctrl not ready\n");
			rc = -EPROBE_DEFER;
			goto err;
		}
		dev_err(dev, "Target does not use pinctrl\n");
		fpsensor->fingerprint_pinctrl = NULL;
		rc = -EINVAL;
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(fpsensor->pinctrl_state); i++) {
		const char *n = pctl_names[i];
		struct pinctrl_state *state =
			pinctrl_lookup_state(fpsensor->fingerprint_pinctrl, n);
		if (IS_ERR(state)) {
			dev_err(dev, "cannot find '%s'\n", n);
			rc = -EINVAL;
			goto err;
		}
		dev_info(dev, "found pin control %s\n", n);
		fpsensor->pinctrl_state[i] = state;
	}

	//fingerprint use its  gpio 131 enble
	rc = fpsensor_request_named_gpio(fpsensor, "gpio_vdd_en",
			&fpsensor->vdd_en);
	if(rc){
		dev_err(dev, "no specify vdd_en\n");
	}else{
		vdd_on = pinctrl_lookup_state(fpsensor->fingerprint_pinctrl,
				"fpsensor_vdd_enable");
		if (IS_ERR(vdd_on)) {
			pr_err("cannot get fpsensor_vdd_enable pinctrl state\n");
			return PTR_ERR(vdd_on);
		}
		rc = pinctrl_select_state(fpsensor->fingerprint_pinctrl, vdd_on);
		if (rc)
			goto err;
	}
#if defined(CONFIG_BOARD_CP3600I)
    rc = fpsensor_request_named_gpio(fpsensor, "gpio_id_en",
            &fpsensor->vdd_id_en);
    if(rc){
        dev_err(dev, "no specify vdd_id_en\n");
    }else{
        vdd_id_on = pinctrl_lookup_state(fpsensor->fingerprint_pinctrl,
                "fpsensor_vdd_id_enable");
        if (IS_ERR(vdd_id_on)) {
            pr_err("cannot get fpsensor_vdd_id_enable pinctrl state\n");
            return PTR_ERR(vdd_id_on);
        }
        rc = pinctrl_select_state(fpsensor->fingerprint_pinctrl, vdd_id_on);
        if (rc)
            goto err;

        LCD_FPC_ID = true;
    }
#endif
	//end
	
	rc = select_pin_ctl(fpsensor, "fpsensor_irq_default");
	if (rc)
		goto err;
	
	rc = hw_reset(fpsensor);
    //if (rc)
		//goto err;

	rc = select_pin_ctl(fpsensor, "fpsensor_irq_active");
	if (rc)
		goto err;

	msm_spi_clk_path_init(fpsensor); //add jiangyuelong
	fpsensor->idev = devm_input_allocate_device(dev);
	if (!fpsensor->idev) {
		dev_err(dev, "failed to allocate input device\n");
		rc = -ENOMEM;
		goto err;
	}

	if (!of_find_property(np, "linux,code", &fpsensor->keycode_size)) {
		dev_err(dev, "unsupport keyevent\n");
		//return -EINVAL;
	}else {	
		fpsensor->keycode_size /= sizeof(u32);
		fpsensor->keycode = kzalloc(fpsensor->keycode_size * sizeof(u32) * 2, GFP_KERNEL);
		if (!fpsensor->keycode)
			return -ENOMEM;

		of_property_read_u32_array(np, "linux,code", fpsensor->keycode, fpsensor->keycode_size);
		for (i = 0; i < fpsensor->keycode_size; i++) {
			__set_bit(fpsensor->keycode[i], fpsensor->idev->keybit);
		}		
	}

	fpsensor->idev->name = "fpsensor";
	fpsensor->idev->phys = "fpsensor/input0";
	fpsensor->idev->dev.parent = fpsensor->dev;
	fpsensor->idev->id.bustype = BUS_HOST;
	fpsensor->idev->id.vendor  = 0x0005;
	fpsensor->idev->id.product = 0x0001;
	fpsensor->idev->id.version = 0x0100;

	fpsensor->idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	__set_bit(INPUT_PROP_POINTER,fpsensor->idev->propbit);
	fpsensor->idev->relbit[BIT_WORD(REL_WHEEL)] = BIT_MASK(REL_X) | BIT_MASK(REL_Y) | BIT_MASK(REL_WHEEL);

	rc = input_register_device(fpsensor->idev);
	if (rc) {
		dev_err(dev, "failed to register input device\n");
		goto err;
	}

	device_init_wakeup(dev, 1);
	
	mutex_init(&fpsensor->lock);
	init_waitqueue_head(&fpsensor->wq_irq_return);
	wakeup_source_init(&fpsensor->irq_ws, "fingerprint-IRQ");
	wakeup_source_init(&fpsensor->ioc_ws, "fingerprint-IOCTL");
	wakeup_source_init(&fpsensor->ioc_cmd_ws, "fingerprint-IOCTL-cmd");
	fpsensor->use_wlock =true;
	fpsensor->irq_num = gpio_to_irq(fpsensor->irq_gpio);
	dev_dbg(dev, "requested irq %d\n", fpsensor->irq_num);
	rc = devm_request_threaded_irq(dev, fpsensor->irq_num,
			NULL, fpsensor_irq_handler, IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND,
			dev_name(dev), fpsensor);
	if (rc) {
		dev_err(dev, "could not request irq %d\n",fpsensor->irq_num);
		goto err;
	}
	rc = enable_irq_wake(fpsensor->irq_num);
	if (rc) {
		dev_err(dev, "enable_irq_wake %d failed\n",fpsensor->irq_num);
		goto err;
	}
	fpsensor->irq_enabled = 1;
	
	sema_init(&fpsensor->mutex, 0);

	rc = alloc_chrdev_region(&fpsensor->devno,0,1,FPSENSOR_DEV_NAME);
	cdev_init(&fpsensor->cdev, &fpsensor_fops);
	fpsensor->cdev.owner = THIS_MODULE;

	rc = cdev_add(&fpsensor->cdev, fpsensor->devno, 1);
	if (rc) {
		dev_err(dev, "cdev_add failed.\n");
		goto err;
	}
	fpsensor->class = class_create(THIS_MODULE, "fingerprint");

	if (IS_ERR(fpsensor->class)) {
		dev_err(dev, "failed to create class.\n");
		rc = PTR_ERR(fpsensor->class);
		goto err;
	}

	fpsensor->device = device_create(fpsensor->class, NULL, fpsensor->devno,
						NULL, "%s", FPSENSOR_DEV_NAME);

	if (IS_ERR(fpsensor->device)) {
		dev_err(dev, "device_create failed.\n");
		rc = PTR_ERR(fpsensor->device);
	}

    g_fpsensor = fpsensor; // add for read vendor id

	up(&fpsensor->mutex);

	if (of_property_read_bool(dev->of_node, "enable-on-boot")) 
	{
		dev_info(dev, "Enabling hardware\n");
		(void)device_prepare(fpsensor, true);
	}

	dev_info(dev, "%s: ok\n", __func__);
	return 0;
err:
	fpsensor_cleanup(dev,fpsensor);
	return rc;
}

static int fpsensor_remove(struct platform_device *pdev)
{
	struct  fpsensor_data *fpsensor = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	mutex_destroy(&fpsensor->lock);
	dev_info(fpsensor->dev, "%s\n", __func__);
	fpsensor_cleanup(dev,fpsensor);
	return 0;
}

/* -------------------------------------------------------------------- */
static int fpsensor_open(struct inode *inode, struct file *file)

{
	struct  fpsensor_data *fpsensor;

	printk(KERN_INFO "%s\n", __func__);

	fpsensor = container_of(inode->i_cdev, struct  fpsensor_data, cdev);

	if (down_interruptible(&fpsensor->mutex))
		return -ERESTARTSYS;

	file->private_data = fpsensor;

	up(&fpsensor->mutex);

	return 0;
}

/* -------------------------------------------------------------------- */
static int fpsensor_release(struct inode *inode, struct file *file)
{
	struct  fpsensor_data *fpsensor = file->private_data;
	int status = 0;

	printk(KERN_INFO "%s\n", __func__);

	if (down_interruptible(&fpsensor->mutex))
		return -ERESTARTSYS;
	
	file->private_data = NULL;

	up(&fpsensor->mutex);

	return status;
}

static unsigned int fpsensor_poll(struct file *file, poll_table *wait)
{
	struct  fpsensor_data *fpsensor = file->private_data;
	poll_wait(file, &fpsensor->wq_irq_return, wait);
	if (gpio_get_value(fpsensor->irq_gpio) || fpsensor->interrupt_done) {
		fpsensor->interrupt_done = false;
		return POLLIN;
	}
	else return 0;
}

static long
fpsensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct  fpsensor_data *fpsensor = file->private_data;
	int rc = 0;
	int temp = 0;
	unsigned int  vdd_value;
	
	switch(cmd) {
		case FPSENSOR_IOC_HW_PREPARE:
			rc = device_prepare(fpsensor, true);
			break;

		case FPSENSOR_IOC_HW_UNPREPARE:
			rc = device_prepare(fpsensor, false);
			break;

		case FPSENSOR_IOC_GET_INTERRUPT:
			if (!arg)
			{
				dev_err(fpsensor->dev, "%s arg is NULL \n", __func__);
				break;
			}
			if (!gpio_is_valid(fpsensor->irq_gpio))
			{	
				dev_err(fpsensor->dev, "fpc irq_gpio was not assigned properly");
			}
			
			rc = gpio_get_value(fpsensor->irq_gpio);
			
			if (put_user(rc,(int *)arg))
			{
				dev_dbg(fpsensor->dev, "fpc put_user (%d) failed " , *(int *)arg);
				rc = -EFAULT;
			}
			break;
			
		case FPSENSOR_IOC_GET_MODULE_NAME:{	
			char buffer[MAX_NAME_LENGTH]="";
			rc = get_module_name(fpsensor,buffer);
			if (rc  == 0) {
				rc = -EFAULT;
				dev_err(fpsensor->dev, "Can't fond matched moduleID");
				break;
			}				
			rc = copy_to_user((char *)arg, buffer, rc+1);
			if (rc ) {
				rc = -EFAULT;
				dev_err(fpsensor->dev, "get module name fail,rc=%d", rc);
			}			 
			};break;

		case FPSENSOR_IOC_MASK_INTERRUPT:
			if (__get_user(temp, (int __user *)arg)) {
				printk("arg is inval");
				return -EINVAL;
			}
			if(temp == 1)
			{
			    if(fpsensor->irq_enabled) {
			        fpsensor->irq_enabled = 0;
			        disable_irq(fpsensor->irq_num);
			        fpsensor->interrupt_done = false;
			    } else {
			        pr_warn("IRQ has been disabled.\n");
			    }			    
			}else {
			    if(fpsensor->irq_enabled) {
			        pr_warn("IRQ has been enabled.\n");
			    } else {
			        enable_irq(fpsensor->irq_num);
			        fpsensor->irq_enabled = 1;
			        fpsensor->interrupt_done = false;
			    }				
			}
			break;

		case FPSENSOR_IOC_HW_RESET:
			rc = hw_reset(fpsensor);
			dev_err(fpsensor->dev, "fpsensor hw_reset rc=%d",rc);
			break;
            
		case FPSENSOR_IOC_RESET_STATE:
			rc = hw_reset_state(fpsensor);
			break;

		case FPSENSOR_IOC_POWER_CTRL:
                    rc = __get_user(vdd_value, (u32 __user*)arg);
                    pr_info("fpsensor get vdd_value = %d ++\n", vdd_value);
                    rc = hw_power_ctrl(fpsensor,vdd_value);
                    break;

            
		case FPSENSOR_IOC_GET_SERIAL_NUM:	
			if(fpsensor->addr == NULL) {
				rc = -EFAULT;
				dev_err(fpsensor->dev, "get sn fail,addr is NULL");
				break;
			}			 
			temp = readl(fpsensor->addr);
			if (put_user(temp,(int *)arg)) {
				rc = -EFAULT;
				dev_err(fpsensor->dev, "get sn fail,rc=%d", rc);
			}			 
			break;	
			
		case FPSENSOR_IOC_SENDKEY:{
			struct fpsensor_key key = {0};
			if(copy_from_user((char *)(&key), (char *)arg, sizeof(key))) {
				pr_warn("Failed to copy data from user space.\n");
				rc = -EFAULT;
				break;
			}
			if(key.keyID >= FPSENSOR_KEY_MAX) {
				dev_err(fpsensor->dev, "input keyID=%d is too large", key.keyID);
				rc = -EFAULT;				
				break;
			}			 
			if(key.keyID == FPSENSOR_KEY_UP)
			{
				input_report_rel(fpsensor->idev, REL_WHEEL, 2);
				input_sync(fpsensor->idev); 
			}else if(key.keyID == FPSENSOR_KEY_DOWN)
			{
				input_report_rel(fpsensor->idev, REL_WHEEL, -2);	
				input_sync(fpsensor->idev); 
			}else if(key.keyID == FPSENSOR_KEY_LONG)
			{
				pr_warn("report key[%d],state[%d]\n",KEY_FN_F12,key.value);
				input_report_key(fpsensor->idev, KEY_FN_F12, key.value);
				input_sync(fpsensor->idev);
			}
#if 0
			else if(key.keyID == FPSENSOR_KEY_LEFT) 
			{
				input_report_key(fpsensor->idev,KEY_MENU, key.value);
				input_sync(fpsensor->idev); 
			}else if(key.keyID == FPSENSOR_KEY_RIGHT)
			{
				input_report_key(fpsensor->idev,KEY_BACK, key.value);
				input_sync(fpsensor->idev); 
			}else if(key.keyID == FPSENSOR_KEY_CLICK)
			{
				input_report_key(fpsensor->idev,BTN_LEFT, key.value);
				input_sync(fpsensor->idev); 
			}else if(key.keyID == FPSENSOR_KEY_DBLCLICK)
			{
				input_report_key(fpsensor->idev,KEY_POWER, key.value);
				input_sync(fpsensor->idev); 
			}
#endif				
		}break;
			
		case FPSENSOR_IOC_SETCLKRATE:{
			u32 rate,speed;
			rc = __get_user(speed, (u32 __user*)arg);
			if(rc ==  0) {
				rate = spi_clk_max_rate(fpsensor->core_clk, speed);
				if (rate < 0) {
					dev_err(fpsensor->dev,"%s: no match found for requested clock frequency:%d",__func__, speed);
					break;
				}
				pr_info("expect rate [%d],round to [%d]\n", speed,rate);
				fpsensor->speed_hz = rate;
				rc = clk_set_rate(fpsensor->core_clk, rate);
			} else {
				pr_warn("Failed to get speed from user. rc = %d\n", rc);
			}
			};break;

		case FPSENSOR_IOC_COOLBOOT:

			break;

		case FPSENSOR_IOC_GET_SENSOR_NAME:
			rc = copy_to_user((char *)arg, fpsensor->sensor_name, strlen(fpsensor->sensor_name)+1);
			if (rc ) {
				rc = -EFAULT;
				dev_err(fpsensor->dev, "get sensor name fail,rc=%d", rc);
			} 
			break;
		case FPSENSOR_IOC_GET_IRQ_STATUS:// add jiangyuelong for fastmmi
            if (fpsensor->boot_irq_status ||fpsensor->irq_status){
                    fpsensor->irq_status = 1;
            }else{
                    fpsensor->irq_status = 0;
            }
             dev_err(fpsensor->dev, " FPSENSOR_IOC_GET_IRQ_STATUS , fpsensor->irq_status=%d",  fpsensor->irq_status);
			rc = copy_to_user((char *)arg, &(fpsensor->irq_status), sizeof(fpsensor->irq_status));
			if (rc ) {
				rc = -EFAULT;
				dev_err(fpsensor->dev, "get FPSENSOR_IOC_GET_IRQ_STATUS fail,rc=%d", rc);
			} 
			break;
            /*
    	        case FPSENSOR_IOC_GET_BOOT_IRQ_STATUS:// add jiangyuelong for fastmmi
			rc = copy_to_user((char *)arg, &(fpsensor->boot_irq_status), sizeof(fpsensor->boot_irq_status));
			if (rc ) {
				rc = -EFAULT;
				dev_err(fpsensor->dev, "get FPSENSOR_IOC_GET_BOOT_IRQ_STATUS fail,rc=%d", rc);
			} 
			dev_err(fpsensor->dev, "get FPSENSOR_IOC_GET_BOOT_IRQ_STATUS  boot_irq_status=%d", fpsensor->boot_irq_status);
			break;
            */
		case FPSENSOR_IOC_WAKEUP_POLL:
			fpsensor->interrupt_done = true;
			wake_up_interruptible(&fpsensor->wq_irq_return);
			break;

		case FPSENSOR_IOC_WAKELOCK:
			if (__get_user(temp, (int __user *)arg)) {
				printk("arg is inval");
				return -EINVAL;
			}
			pr_debug("fpsensor wakelock: %d\n", temp);
			if(temp == 1)
			{
				__pm_stay_awake(&fpsensor->ioc_ws);
			}else {
				__pm_relax(&fpsensor->ioc_ws);
			}
			break;
			
			
	       case FPSENSOR_IOC_CMD_WAKELOCK:
                            if (__get_user(temp, (int __user *)arg)) {
                                     printk("arg is inval");
                                     return -EINVAL;
                            }
                            pr_debug("fpsensor cmd wakelock: %d\n", temp);
                            if(temp == 1)
                            {
                                     __pm_stay_awake(&fpsensor->ioc_cmd_ws);
                            }else {
                                     __pm_relax(&fpsensor->ioc_cmd_ws);
                            }
                            break;


		default:
			pr_warn("Unsupport cmd:0x%x\n", cmd);
			break;
	}

	return rc;
}
#ifdef CONFIG_COMPAT
static long
fpsensor_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return fpsensor_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif //CONFIG_COMPAT

static const struct file_operations fpsensor_fops = {
	.owner          = THIS_MODULE,
	.open           = fpsensor_open,
	.release        = fpsensor_release,
	.poll           = fpsensor_poll,
	.unlocked_ioctl = fpsensor_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = fpsensor_compat_ioctl,
#endif //CONFIG_COMPAT	
};

static struct of_device_id fpsensor_of_match[] = {
	{ .compatible = "yulong,fpsensor", },
	{}
};
MODULE_DEVICE_TABLE(of, fpsensor_of_match);

static struct platform_driver fpsensor_driver = {
	.driver = {
		.name	= FPSENSOR_DEV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = fpsensor_of_match,
	},
	.probe	= fpsensor_probe,	
	.remove	= fpsensor_remove,
};

static int __init fpsensor_init(void)
{
	int rc = platform_driver_register(&fpsensor_driver);
	if (!rc)
		pr_info("%s OK\n", __func__);
	else
		pr_err("%s %d\n", __func__, rc);
	return rc;
}

static void __exit fpsensor_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&fpsensor_driver);
}

module_init(fpsensor_init);
module_exit(fpsensor_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aleksej Makarov");
MODULE_AUTHOR("Henrik Tillman <henrik.tillman@fingerprints.com>");
MODULE_DESCRIPTION("FPSensor Fingerprint sensor device driver.");
