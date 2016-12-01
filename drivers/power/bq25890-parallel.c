/*zhangzhe*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

//zhangzhe
//#include <linux/qpnp/pin.h>
#include <linux/pinctrl/consumer.h>

//zhangzhe++
#include <linux/delay.h>
#include <linux/reboot.h>    //poweroff charge reboot
#define YL_DEBUG_SWITCH 1
#define YL_LOG_ENABLE 	1

/**************************************************************
LOG DEFINATION
LOGE(" error !! \n")     //ERROR, important data && status
LOGN(" ID = %d\n", id)   //NOTICE, some useful information
LOGD(" start\n")         //DEBUG, secondary||unmeant debug info

**  <author>       <date>        <version>       <desc>
**  zhangzhe       2014-03-10    1.00            create
***************************************************************/
#define YL_HEAD               "YL:"
#define MY_MODULE             "PSY:CH2:"
#define FUNC_HEAD(fmt)        "%s(): " fmt, __func__


#if YL_LOG_ENABLE
#define LOGE(fmt, ...)\
         printk(KERN_ERR YL_HEAD MY_MODULE  FUNC_HEAD(fmt), ##__VA_ARGS__)
#define LOGN(fmt, ...)\
         printk(KERN_NOTICE YL_HEAD MY_MODULE FUNC_HEAD(fmt), ##__VA_ARGS__)
#define LOGD(fmt, ...)\
         printk(KERN_DEBUG YL_HEAD MY_MODULE FUNC_HEAD(fmt), ##__VA_ARGS__)
#else
#define LOGE(fmt, ...)  ;
#define LOGN(fmt, ...)  ;
#define LOGD(fmt, ...)  ;
#endif
//zhangzhe--

//#define BQ25890_I2C_WAIT_QUEUE

#ifdef BQ25890_I2C_WAIT_QUEUE
static int bq_resume = 1;
static DECLARE_WAIT_QUEUE_HEAD(wait_bq_resume);
#endif

struct bq25890_parallel_device{
	struct i2c_client	*client;
	struct device 		*dev;
	struct power_supply 	charger;
	struct delayed_work 	work;
	//struct delayed_work	heart_beat;
	unsigned int		bq25890_en_gpio;
	unsigned int            bq25890_en_gpio_inited;
	unsigned int		bq25890_en_gpio_valid;
	unsigned int 		vbus_present;
	unsigned int            charge_enable;
	unsigned int            	charge_status;
	int            		ibat_ma;
	int            		ibus_ma;
	int            		vbus_mv;
	int            		vbat_mv;
	int            		ibat_ma_read;
	int            		ibus_ma_read;
	int            		vbus_mv_read;
	int                     vsys_mv_read;
	int            		vbat_mv_read;

#ifdef CONFIG_BOARD_CP3600I                                                                                      
	struct pinctrl          *pinctrl;
	struct pinctrl_state    *int_state_active;
	struct pinctrl_state    *int_state_suspend;
#endif
};


static int bq25890_i2c_read(struct bq25890_parallel_device *chip, int reg, u8 *val)
{
        s32 ret;
#ifdef BQ25890_I2C_WAIT_QUEUE
        int wait_event;

        wait_event = wait_event_interruptible(wait_bq_resume,bq_resume == 1);
        if (wait_event < 0){
                pr_err("%s: wait for i2c bus resume error\n", __func__);
        }
#endif
        ret = i2c_smbus_read_byte_data(chip->client, reg);
        if (ret < 0) {
                pr_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
                return ret;
        } else {
                *val = ret;
        }
        return 0;
}

static int bq25890_i2c_write(struct bq25890_parallel_device *chip, int reg, u8 val)
{
        s32 ret;
#ifdef BQ25890_I2C_WAIT_QUEUE
        int wait_event;

        wait_event = wait_event_interruptible(wait_bq_resume,bq_resume == 1);
        if (wait_event < 0){
                pr_err("%s: wait for i2c bus resume error\n", __func__);
        }
#endif
        ret = i2c_smbus_write_byte_data(chip->client, reg, val);
        if (ret < 0) {
                pr_err("i2c write fail: can't write %02x to %02x: %d\n",
                        val, reg, ret);
                return ret;
        }
        pr_debug("Writing 0x%02x=0x%02x\n", reg, val);
        return 0;
}

static int bq25890_gpio_enable(struct bq25890_parallel_device *chip,unsigned int en)
{
        LOGE("ENABLE(%d),GPIO(%d,%d)\n",en,chip->bq25890_en_gpio_inited,chip->bq25890_en_gpio_valid);
	if(!chip->bq25890_en_gpio_inited||!chip->bq25890_en_gpio_valid)
	{
		printk(KERN_ERR "ljc in %s, bq25890_en_gpio_inited %d, bq25890_en_gpio_valid %d\n", 
			__func__, chip->bq25890_en_gpio_inited, chip->bq25890_en_gpio_valid);
		LOGE("GPIO not ready!\n");
		return -1;
	}

        gpio_set_value(chip->bq25890_en_gpio,(en?1:0));

        return 0;
}


static int bq25890_gpio_init(struct bq25890_parallel_device *chip)
{
	int error = 0;

	if(!chip->bq25890_en_gpio_valid)
	{
		LOGE("GPIO INVALID!\n");
		return -1;
	}


	if(!chip)
	{
		LOGE("NULL POINTER!\n");
	}

	if (gpio_is_valid(chip->bq25890_en_gpio)) {


		error = gpio_request(chip->bq25890_en_gpio, "bq25890_gpio_enable");
		if (error) {
			LOGE("request gpio(%d) fail\n",chip->bq25890_en_gpio);
			return error;
		}

		error = gpio_direction_output(chip->bq25890_en_gpio, 1);
		if (error) {
			LOGE("set gpio(%d) output fail\n",chip->bq25890_en_gpio);
			return error;
		}

		chip->bq25890_en_gpio_inited = 1;

		gpio_set_value(chip->bq25890_en_gpio, 1);
	}
	else
	{
		LOGE("gpio(%d) is invalid\n",chip->bq25890_en_gpio);
		chip->bq25890_en_gpio_inited = 0;
		return -1;
	}

	return 0;
}


static int bq25890_registers_init(struct bq25890_parallel_device *chip)
{
//        int reg;
        int ret;

	LOGE("START\n");
//0x00 - VBUS CURRENT(100mA)
        ret = bq25890_i2c_write(chip, 0x00, 0x00);
	if(ret<0)
	{
		LOGE(" write register(0x00) fail\n");
		return ret;
	}

//0x01 - DPM voltage relative threshold(1100mV blow)
        ret = bq25890_i2c_write(chip, 0x01,0x0B);
        if(ret<0)
        {
                LOGE(" write register(0x01) fail\n");
                return ret;
        }

//ADC(1)	ICO(1)		DPDM(0)		HVDCP(0)
        ret = bq25890_i2c_write(chip, 0x02,0xC0);
        if(ret<0)
        {
                LOGE(" write register(0x02) fail\n");
                return ret;
        }

//BAT_LOAD(0)		WDOG(0)		OTG(0)		CHARGE(1)
//Vsys_min = 3.5V
        ret = bq25890_i2c_write(chip, 0x03,0x0A);
        if(ret<0)
        {
                LOGE(" write register(0x03) fail\n");
                return ret;
        }

//Ibat = 0mA
        ret = bq25890_i2c_write(chip, 0x04,0x00);
        if(ret<0)
        {
                LOGE(" write register(0x04) fail\n");
                return ret;
        }

//PreCharge 64mA //Termination Current 64mA
        ret = bq25890_i2c_write(chip, 0x05,0x00);
        if(ret<0)
        {
                LOGE(" write register(0x) fail\n");
                return ret;
        }

//Vbat(4.4V)	Vlow(3.0V)	VRECHG(-100mV)
        ret = bq25890_i2c_write(chip, 0x06,0x96);
       	if(ret<0)
        {
                LOGE(" write register(0x6) fail\n");
                return ret;
        }

//Iterm_en(1)	STAT_DIS(0) 	WD(0)	TIMER(12H)
        ret = bq25890_i2c_write(chip, 0x07,0x8C);
        if(ret<0)
        {
                LOGE(" write register(0x07) fail\n");
                return ret;
        }

//IR(140mR,224mV)
        ret = bq25890_i2c_write(chip, 0x08,0xFF);
        if(ret<0)
        {
                LOGE(" write register(0x07) fail\n");
                return ret;
        }

//BATFET RST(0)
        ret = bq25890_i2c_write(chip, 0x09,0x00);
        if(ret<0)
        {
                LOGE(" write register(0x09) fail\n");
                return ret;
        }

//BOOST(1.3A@5V)
        ret = bq25890_i2c_write(chip, 0x0a,0x73);
        if(ret<0)
        {
                LOGE(" write register(0x0a) fail\n");
                return ret;
        }

//RELATED DPM VOLTAGE
        ret = bq25890_i2c_write(chip, 0x0d,0x8D);
        if(ret<0)
        {
                LOGE(" write register(0x0d) fail\n");
                return ret;
        }

	LOGE("END\n");

        return 0;
}

static int bq25890_get_vbus(struct bq25890_parallel_device *chip)
{
        int ret=0;
        int vbus=0;
        u8 val=0;

        ret = bq25890_i2c_read(chip, 0x11, &val);
        if (ret < 0)
        {
                LOGE("REG(0x11) read fail!\n");
                return -1;
        }
         else
        {
                if(val&0x80)
                {
                        LOGE("VBUS++\n");
                        vbus=(val&0x7F)*100 + 2600;
                }
                else
                {
                        LOGE("VBUS--\n");
                        vbus=0;
                }
        }

        chip->vbus_mv_read = vbus;

        return 0;
}

static int bq25890_get_vsys(struct bq25890_parallel_device *chip)
{
        int ret=0;
        int vsys=0;
        u8 val=0;

        ret = bq25890_i2c_read(chip, 0x0F, &val);
        if (ret < 0)
        {
                LOGE("REG(0x0F) read fail!\n");
                return -1;
        }
         else
        {
                vsys=(val&0x7F)*20 + 2304;
        }

        chip->vsys_mv_read = vsys;

        return 0;
}

static int bq25890_get_vbat(struct bq25890_parallel_device *chip)
{
        int ret=0;
        int vbat=0;
        u8 val=0;

        ret = bq25890_i2c_read(chip, 0x0E, &val);
        if (ret < 0)
        {
                LOGE("REG(0x0E) read fail!\n");
                return -1;
        }
         else
        {
                vbat=(val&0x7F)*20 + 2304;
        }

        chip->vbat_mv_read = vbat;

        return 0;
}


static int bq25890_get_ibat(struct bq25890_parallel_device *chip)
{
        int ret=0;
        int ibat=0;
        u8 val=0;


        ret = bq25890_i2c_read(chip, 0x12, &val);
        if (ret < 0)
        {
                LOGE("REG(0x12) read fail!\n");
                return -1;
        }
         else
        {
                ibat =(val&0x7F)*50;
        }

        chip->ibat_ma_read =ibat;

        return 0;
}

static int bq25890_get_ibus(struct bq25890_parallel_device *chip)
{
        int ret=0;
        int ibus=0;
        u8 val=0;


        ret = bq25890_i2c_read(chip, 0x13, &val);
        if (ret < 0)
        {
                LOGE("REG(0x13) read fail!\n");
                return -1;
        }
        else
        {
                ibus =(val&0x3F)*50 + 100;
        }

        chip->ibus_ma_read =ibus;

        return 0;
}


static int bq25890_set_ibat(struct bq25890_parallel_device *chip,int ma)
{
        int ret,reg;

	LOGE("ibat(%dmA)\n",ma);

	reg = ma/64;
	reg&=0xFF;
        ret = bq25890_i2c_write(chip, 0x04,reg);
        if(ret<0)
        {
                LOGE(" write register(0x04) fail\n");
                return ret;
        }

        return ret;
}

static int bq25890_set_ibus(struct bq25890_parallel_device *chip,int ma)
{
        int ret,reg;
	u8 val;

	LOGE("ibus(%dmA)\n",ma);

        ret = bq25890_i2c_read(chip, 0x00, &val);
        if (ret < 0)
        {
                LOGE("REG(0x0B) read registers fail!\n");
                return ret;
        }


	if(ma<100) 		{ ma =  100; }
	else if(ma>3000)	{ ma = 3000; }

	reg = (ma-100)/50;
	reg= (reg&0x3F)|(val&0xC0);

        ret = bq25890_i2c_write(chip, 0x00,reg);
        if(ret<0)
        {
                LOGE(" write register(0x04) fail\n");
                return ret;
        }

        return ret;
}

static int bq25890_charge_enable(struct bq25890_parallel_device *chip,int en)
{
        int ret;
	int count=0;

//	LOGE("enable(%d)\n",en);
	if(en)
	{
	        ret = bq25890_i2c_write(chip, 0x03,0x1A);
	        if(ret<0)
        	{
        	        LOGE(" write register(0x03) fail1,try(%d)\n",count);
        	       	return ret;
        	}
	}
	else
	{
		if(chip->ibus_ma >=100)
		{
			chip->ibus_ma =0;
			bq25890_set_ibus(chip,chip->ibus_ma);
		}

	        ret = bq25890_i2c_write(chip, 0x03,0x0A);
        	if(ret<0)
		{
               		LOGE(" write register(0x03) fail2,try(%d)\n",count);
                	return ret;
        	}
	}

        return ret;
}



//avoid vbus input current
static void bq25890_vbus_keeper(struct bq25890_parallel_device *chip)
{
	LOGE("present(%d), ibus(%dmA)\n",chip->vbus_present,chip->ibus_ma);
	if(!chip)
	{
		LOGE("null pointer\n");
	}

	if((chip->vbus_present)&&(chip->ibus_ma>=100)&&(chip->charge_enable))
	{
		bq25890_gpio_enable(chip, 1);
	}
	else
	{
		bq25890_gpio_enable(chip, 0);
	}
}

static int bq25890_set_vbat(struct bq25890_parallel_device *chip,int mv)
{
        int ret,reg;
	u8 val;

	LOGE("vbat(%dmA)\n",mv);

        ret = bq25890_i2c_read(chip, 0x06, &val);
        if (ret < 0)
        {
                LOGE("REG(0x0B) read registers fail!\n");
                return ret;
        }

	if(mv<3840) 		{ mv =  3840; }
	else if(mv>4608)	{ mv =  4608; }

	reg = ((mv-3840)/16)<<2;
	reg= (reg&0xFC)|(val&0x03);

        ret = bq25890_i2c_write(chip, 0x06,0x8E);
       	if(ret<0)
        {
                LOGE(" write register(0x6) fail\n");
                return ret;
        }

        return ret;
}

static int bq25890_get_charge_state(struct bq25890_parallel_device *chip)
{
        int ret=0;
	int status = POWER_SUPPLY_STATUS_UNKNOWN;
	u8 val=0;

        ret = bq25890_i2c_read(chip, 0x0B, &val);
        if (ret < 0)
        {
		LOGE("REG(0x0B) read registers fail!\n");
		return status;
	}

	val=(val&0x18)>>3;

	if(val == 0x00)
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	else if(val == 0x01)
        	status = POWER_SUPPLY_STATUS_CHARGING;
        else if(val == 0x02)
        	status = POWER_SUPPLY_STATUS_CHARGING;
        else if(val == 0x03)
                status = POWER_SUPPLY_STATUS_FULL;
	else
		status = POWER_SUPPLY_STATUS_UNKNOWN;

	chip->charge_status = status;
	LOGE(" charge state(%d)\n",chip->charge_status);

        return status;
}

static void bq25890_dump_registers(struct bq25890_parallel_device *chip)
{
	int ret=0;
	int reg_addr=0;
	u8 val=0;

	if(1)
	{
		for(reg_addr=0x00;reg_addr<=0x14;reg_addr++)
		{
        		ret = bq25890_i2c_read(chip, reg_addr, &val);
        		if (ret < 0)
        		{
        		        LOGE("REG(0x%02x) read fail!\n",reg_addr);
        		}
			else
			{
				LOGE("REG(0x%02x) = 0x%2x\n",reg_addr,val);
			}
		}
	}
	else
	{
		for(reg_addr=0x14;reg_addr<=0x14;reg_addr++)
                {
                        ret = bq25890_i2c_read(chip, reg_addr, &val);
                        if (ret < 0)
                        {
				LOGE("REG(0x%02x) read fail!\n",reg_addr);
                        }
                        else
                        {
                                LOGE("REG(0x%02x) = 0x%2x\n",reg_addr,val);
                        }
                }
	}
	return;
}
/*
static void bq25890_heart_beat_work(struct work_struct *work)
{
	struct bq25890_parallel_device *chip = container_of(work, struct bq25890_parallel_device,heart_beat.work);

//        bq25890_dump_registers(chip);

        bq25890_i2c_write(chip, 0x02,0xC0);

	mdelay(1);

        bq25890_get_ibat(chip);
        bq25890_get_ibus(chip);
        bq25890_get_vbus(chip);
        bq25890_get_vsys(chip);
        bq25890_get_vbat(chip);


	LOGE("CRT(%4d,%4d),VTG(%4d,%4d,%4d),STS(%d)\n",
		chip->ibus_ma_read,chip->ibat_ma_read,
			chip->vbus_mv_read,chip->vsys_mv_read,chip->vbat_mv_read,
				bq25890_get_charge_state(chip));


	schedule_delayed_work(&chip->heart_beat,msecs_to_jiffies(30000));
}
*/

static enum power_supply_property bq25890_power_supply_props[] = {
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_VENDOR,
};

static int bq25890_power_supply_get_property(struct power_supply *psy,
						     enum power_supply_property prop,
							     union power_supply_propval *val)
{
	int ret = 0;
	struct bq25890_parallel_device *chip = container_of(psy, struct bq25890_parallel_device,charger);

	switch (prop) {

        case POWER_SUPPLY_PROP_PRESENT:
		val->intval = chip->vbus_present;
                break;

	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = chip->charge_enable;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = chip->ibat_ma*1000;
//		LOGE("POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX(%d)\n",val->intval/1000);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		bq25890_get_ibat(chip);
		val->intval = chip->ibat_ma_read*1000;
//		LOGE("POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT(%d)\n",val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chip->ibus_ma*1000;
//		LOGE("POWER_SUPPLY_PROP_CURRENT_MAX(%d)\n",val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		bq25890_get_ibus(chip);
		val->intval = chip->ibus_ma_read*1000;
//		LOGE("POWER_SUPPLY_PROP_CURRENT_NOW(%d)\n",val->intval);
		break;
//uV
        case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		bq25890_get_vbus(chip);
		bq25890_get_vbat(chip);
		val->intval = chip->vbat_mv_read*1000;
//		LOGE("POWER_SUPPLY_PROP_VOLTAGE_MAX(%d)\n",val->intval);
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bq25890_get_charge_state(chip);
//		LOGE("POWER_SUPPLY_PROP_STATUS(%d)\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_VENDOR:
		val->strval = "bq25890";
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

#define NOT_INIT (-1)
static int bq25890_ibat_regulate	=NOT_INIT;
static int bq25890_ibat_old		=NOT_INIT;
static int bq25890_power_supply_set_property(struct power_supply *psy,
						     enum power_supply_property prop,
							     const union power_supply_propval *val)
{
	int ret = 0;
	int temp = 0;
        struct bq25890_parallel_device *chip = container_of(psy, struct bq25890_parallel_device,charger);
	if(!chip)
	{
		LOGE("null pointer\n");
		return -1;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		LOGE("POWER_SUPPLY_PROP_CHARGING_ENABLED\n");
		chip->charge_enable = (int)(val->intval);
		bq25890_vbus_keeper(chip);
                break;


	case POWER_SUPPLY_PROP_PRESENT:
		LOGE("POWER_SUPPLY_PROP_PRESENT\n");
		temp = (int)(val->intval);
                ret = bq25890_charge_enable(chip,temp);
	        if(ret)
	        {
        	        LOGE(" bq25890 charge enable(%d) fail\n",temp);
       		}
		else
                {
                        chip->vbus_present = temp;
                }
		bq25890_vbus_keeper(chip);
		break;

//uA
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		LOGE("POWER_SUPPLY_PROP_CURRENT_MAX(%dmA)\n",((int)(val->intval)/1000));
		temp = (int)(val->intval)/1000;
                if(temp>3000)
                        temp = 3000;
                else if(temp<0)
                        temp = 0;

                ret = bq25890_set_ibus(chip,temp);
                if(ret)
                {
                        LOGE(" bq25890 set ibus(%dmA) fail\n",temp);
                }
                else
                {
                        chip->ibus_ma = temp;
                }

		bq25890_vbus_keeper(chip);

		break;

//uA
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		LOGE("POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX(%dmA)\n",((int)(val->intval)/1000));
		temp = (int)(val->intval)/1000;

		if(temp!=bq25890_ibat_old)
		{
			bq25890_ibat_old	= temp;
			bq25890_ibat_regulate 	= bq25890_ibat_old;
		}

		if(bq25890_ibat_regulate<0)
		{
			bq25890_ibat_regulate 	= bq25890_ibat_old;
		}

                bq25890_get_vsys(chip);
		if((chip->vsys_mv_read>4550)&&(bq25890_ibat_regulate>500))
		{
			bq25890_ibat_regulate-=200;
		}
		else if((chip->vsys_mv_read<4450)&&(bq25890_ibat_regulate<(temp-200)))
		{
			bq25890_ibat_regulate+=200;
		}

		if(bq25890_ibat_regulate>3000)
			bq25890_ibat_regulate=3000;
		else if(bq25890_ibat_regulate<0)
			bq25890_ibat_regulate=0;

		LOGE("IBAT REGULATE TO (%dmA)\n",bq25890_ibat_regulate);
		ret = bq25890_set_ibat(chip,bq25890_ibat_regulate);
		if(ret)
                {
                        LOGE(" bq25890 set ibat(%dmA) fail\n",bq25890_ibat_regulate);
                }
		else
		{
			chip->ibat_ma = temp;
		}
		break;

//mV
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		LOGE(" bq25890 set Vbat(%dmV)\n",(int)(val->intval));
		temp = (int)(val->intval);
		if(temp>4608)
			temp = 4608;
		else if(temp<3840)
			temp = 3840;

		ret = bq25890_set_vbat(chip,temp);
		if(ret)
                {
                        LOGE(" bq25890 set vbat(%dmv) fail\n",temp);
                }
		else
		{
			chip->vbat_mv = temp;
		}
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int bq25890_power_supply_is_writeable(struct power_supply *psy,
				       enum power_supply_property prop)
{
	int rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}


static void bq25890_psy_changed(struct power_supply *psy)
{
	LOGE("test!\n");
}

static char *bq25890_supplied_to[] = {
	"usb-parallel-disable",
};


static int bq25890_prase_dt(struct i2c_client *client, struct bq25890_parallel_device *chip)
{
        if (NULL != of_find_property(client->dev.of_node, "bq,bq25890-en", NULL)) {
                LOGE("GPIO valid!\n");
		chip->bq25890_en_gpio = of_get_named_gpio_flags(client->dev.of_node,"bq,bq25890-en", 0, NULL);
		chip->bq25890_en_gpio_valid = 1;
        }
	else
	{
		chip->bq25890_en_gpio_valid = 0;
		LOGE("GPIO invalid!\n");
	}

	LOGE("GPIO(%d)\n",chip->bq25890_en_gpio);

	if(of_property_read_u32(client->dev.of_node,"bq,battery-voltage-mv",&chip->vbat_mv))
	{
		LOGE("get vbat voltage limie fail, set default(4.2v)\n");
		chip->vbat_mv = 4400;
	}
	LOGE("VBAT(%dmV)\n",chip->vbat_mv);

	return 0;
}


static void bq25890_param_init(struct bq25890_parallel_device *chip)
{
	chip->vbus_present 		= 0;
	chip->charge_enable             = 1;
	chip->ibus_ma 			= 0;
	chip->ibat_ma 			= 0;
	chip->vbat_mv 			= 4400;
	chip->bq25890_en_gpio 		= 0;
	chip->bq25890_en_gpio_valid 	= 0;
	chip->bq25890_en_gpio_inited	= 0;
	chip->bq25890_en_gpio_inited 	= 0;
}

#ifdef CONFIG_BOARD_CP3600I
static int bq25890_pinctrl_init(struct bq25890_parallel_device *chip)
{
	int retval;

	/* Get pinctrl if target uses pinctrl */
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		dev_err(chip->dev,
				"Target does not use pinctrl\n");
		retval = PTR_ERR(chip->pinctrl);
		chip->pinctrl = NULL;
		return retval;
	}

	chip->int_state_active
		= pinctrl_lookup_state(chip->pinctrl, "int_active");
	if (IS_ERR_OR_NULL(chip->int_state_active)) {
		dev_err(chip->dev,
				"Can not get default pinstate\n");
		retval = PTR_ERR(chip->int_state_active);
		chip->pinctrl = NULL;
		return retval;
	}

	chip->int_state_suspend
		= pinctrl_lookup_state(chip->pinctrl, "int_sleep");
	if (IS_ERR_OR_NULL(chip->int_state_suspend)) {
		dev_err(chip->dev,
				"Can not get sleep pinstate\n");
		retval = PTR_ERR(chip->int_state_suspend);
		chip->pinctrl = NULL;
		return retval;
	}
	retval = pinctrl_select_state(chip->pinctrl, chip->int_state_active);
	if (retval) {
		dev_err(chip->dev,
				"can not set pins\n");
		return retval;
	}

	return 0;
}
#endif

/* main bq25890 parallel probe function */
static int bq25890_parallel_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct bq25890_parallel_device *chip;
	int ret;

	LOGE(" ++\n");
	LOGE("#######################the is charge test ############# \n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		LOGE(" devm_kzalloc fail\n");
		ret = -ENOMEM;
		goto error_1;
	}

	chip->client = client;
	chip->dev = &client->dev;

        i2c_set_clientdata(client, chip);

	LOGE(" i2c addr(0x%x)\n",client->addr);

	bq25890_param_init(chip);
//GPIO
	bq25890_prase_dt(chip->client, chip);

	bq25890_gpio_init(chip);

	bq25890_registers_init(chip);

#ifdef CONFIG_BOARD_CP3600I
        bq25890_pinctrl_init(chip);
#endif

        bq25890_dump_registers(chip);

	chip ->charger.name 			= "usb-parallel";
	chip ->charger.type 			= POWER_SUPPLY_TYPE_USB_PARALLEL;
        chip ->charger.supplied_to       	= bq25890_supplied_to;
        chip ->charger.num_supplicants   	= ARRAY_SIZE(bq25890_supplied_to);
	chip ->charger.properties 		= bq25890_power_supply_props;
	chip ->charger.num_properties 		= ARRAY_SIZE(bq25890_power_supply_props);
	chip ->charger.get_property 		= bq25890_power_supply_get_property;
	chip ->charger.set_property 		= bq25890_power_supply_set_property;
	chip ->charger.property_is_writeable	= bq25890_power_supply_is_writeable;
	chip ->charger.external_power_changed 	= bq25890_psy_changed;
	ret = power_supply_register(chip->dev, &chip->charger);
	if (ret) {
		LOGE(" power supply register usb-parallel fail\n");
		goto error_psy_register_fail;
	}

	//INIT_DELAYED_WORK(&chip->heart_beat,bq25890_heart_beat_work);

        //schedule_delayed_work(&chip->heart_beat,msecs_to_jiffies(10000));

        //LOGE(" --\n");

	return 0;

error_psy_register_fail:
error_1:
        LOGE(" FAIL\n");
	return -1;
}

static int bq25890_parallel_remove(struct i2c_client *client)
{
//	struct bq25890_parallel_device *chip = i2c_get_clientdata(client);

	LOGE(" REMOVED\n");

	return 0;

}



static int bq25890_parallel_suspend(struct device *dev)
{
#ifdef BQ25890_I2C_WAIT_QUEUE
	bq_resume = 0;
	pr_err("%s: bq_resume = %d\n", __func__, bq_resume);
#endif
	return 0;
}


static int bq25890_parallel_resume(struct device *dev)
{
#ifdef BQ25890_I2C_WAIT_QUEUE
	bq_resume = 1;
	wake_up_interruptible(&wait_bq_resume);
        pr_err("%s: bq_resume = %d\n", __func__, bq_resume);
#endif
	return 0;
}

static const struct dev_pm_ops bq25890_parallel_pm_ops = {
	.resume		= bq25890_parallel_resume,
	.suspend	= bq25890_parallel_suspend,
};


static struct of_device_id bq25890_parallel_of_match_table[] = {
	{
		.compatible = "bq25890-parallel",
	},
	{},
};
MODULE_DEVICE_TABLE(of, bq25890i_parallel_of_match_table);

static const struct i2c_device_id bq25890_parallel_i2c_id_table[] = {
        { "bq25890-parallel", 0},
        {},
};
MODULE_DEVICE_TABLE(i2c, bq25890_parallel_i2c_id_table);

static struct i2c_driver bq25890_parallel_driver = {
	.driver = {
		.name = "bq25890-parallel",
		.of_match_table = bq25890_parallel_of_match_table,
		.pm		= &bq25890_parallel_pm_ops,
	},
	.probe = bq25890_parallel_probe,
	.remove = bq25890_parallel_remove,
	.id_table = bq25890_parallel_i2c_id_table,
};

static int __init bq25890_init(void)
{
	int ret;
	LOGE("\n");

	ret = i2c_add_driver(&bq25890_parallel_driver);

	return ret;
}

static void __exit bq25890_exit(void)
{
	i2c_del_driver(&bq25890_parallel_driver);

}

module_init(bq25890_init);
module_exit(bq25890_exit);

MODULE_AUTHOR("zhangzhe");
MODULE_DESCRIPTION("bq25890 parallel driver");
MODULE_LICENSE("GPL");
