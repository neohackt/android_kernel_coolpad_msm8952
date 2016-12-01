#ifndef _SHTSC_H
#define _SHTSC_H

#define SHTSC_DRIVER_NAME      "shtsc"

// hiroshi@sharp
#define OMAP4PANDA_GPIO_SHTSC_IRQ 59 // pin#28 of J6
#define OMAP4PANDA_GPIO_SHTSC_RESET 37 // pin#10 of J6
// pin#28 of J3 is GND

/************************************************************/
/*Log define*/
/************************************************************/
//Log define.yulong add 2015-07-23
#define SHTSC_INFO(fmt,arg...)           printk(KERN_INFO "SHTSC:INFO "fmt"\n",##arg)
#define SHTSC_ERROR(fmt,arg...)          printk(KERN_ERR "SHTSC:ERR "fmt"\n",##arg)
#define SHTSC_DEBUG(fmt,arg...)          do{\
                                         if(SHTSC_DEBUG_ON)\
                                         printk(KERN_ERR "SHTSC:DBG [%d] "fmt"\n",__LINE__, ##arg);\
                                       }while(0)

/***********************************************************************************/


#if 1//2014.11.12 changed
struct shtsc_i2c_pdata
{
	/* touch panel's minimum and maximum coordinates */
	u32 panel_minx;
	u32 panel_maxx;
	u32 panel_miny;
	u32 panel_maxy;

	/* display's minimum and maximum coordinates */
	u32 disp_minx;
	u32 disp_maxx;
	u32 disp_miny;
	u32 disp_maxy;

	unsigned long irqflags;
	bool	i2c_pull_up;
	bool	digital_pwr_regulator;
	int reset_gpio;
	u32 reset_gpio_flags;
	int vcc_i2c_supply_en;
	u32 vcc_i2c_supply_en_flags;
	int irq_gpio;
	u32 irq_gpio_flags;
	int *key_codes;

	int		ts_touch_num_max;
	int		ts_pressure_max;
	int		ts_flip_x;
	int		ts_flip_y;
	int		ts_swap_xy;

	int (*init_hw) (bool);
	int (*power_on) (bool);
};
#else
struct shtsc_i2c_pdata
{
	int reset_pin;		/* Reset pin is wired to this GPIO (optional) */
	int irq_pin;		/* IRQ pin is wired to this GPIO */
};
#endif

#endif /* _SHTSC_H */
