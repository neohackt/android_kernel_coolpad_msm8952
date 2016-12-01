/*
** modified leds-ktd2026.c by zhoumaiyun@yulong.com 2015.02.09
** add function that read led reg and current config in dts
*/
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include<linux/mutex.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#ifdef CONFIG_YL_DEBUG
    #define YL_DEBUG(fmt, args...) pr_info(fmt, ##args)
#else
    #define YL_DEBUG(fmt, args...)
#endif

#define KTD_LEDS_DEV_NAME   "ktd_leds"
#define I2C_RETRY_DELAY     50
#define I2C_RETRIES         5
#define DEBUG_TRICOLOR_LED  1

#define KTD_ENRST   0x00
#define KTD_FP      0x01
#define KTD_FOT1    0x02
#define KTD_FOT2    0x03
#define KTD_CC      0x04
#define KTD_RR      0x05
#define KTD_OUT1    0x06
#define KTD_OUT2    0x07
#define KTD_OUT3    0x08
#define KTD_OUT4    0x09

enum ktd_led_color {
    LED_COLOR_RED,
    LED_COLOR_GREEN,
    LED_COLOR_BLUE,
    LED_COLOR_DEFAULT,
};
    
struct ktd_leds_data {
    struct i2c_client *client;
    struct mutex lock;
    unsigned int ldo_ctl_flag;
    unsigned int led_flag[6];
    u8 chan_ctrl;
    int led_data[3];
    struct led_classdev leds[3];
    struct ktd_leds_platform_data *pdata;
};

struct ktd_leds_data *ktd_data=NULL;

struct ktd_leds_platform_data {
    u32 ktd_red_out;    //red out reg
    u32 ktd_green_out;  //green out reg
    u32 ktd_blue_out;   //blue out reg
    
    u32 ktd_red_current;   //red out current
    u32 ktd_green_current; //red out current
    u32 ktd_blue_current;  //red out current
};

static void red_led_on(struct ktd_leds_data*);
static void green_led_on(struct ktd_leds_data*);
static void blue_led_on(struct ktd_leds_data*);

static int ktd_leds_i2c_write_reg(u8 regnum, u8 value)
{
    int32_t ret = -1;
    uint8_t tries = 0;
    /*
     * i2c_smbus_write_byte_data - SMBus "write byte" protocol
     * @client: Handle to slave device
     * @command: Byte interpreted by slave
     * @value: Byte being written
     *
     * This executes the SMBus "write byte" protocol, returning negative errno
     * else zero on success.
     */

    do{
        ret = i2c_smbus_write_byte_data(ktd_data->client, regnum, value);   
        if(ret < 0){        
            printk(KERN_ERR "KTD2026:YLLOG:error:i2c_write failed in %s, ret is %d\n", __func__, ret);
            msleep_interruptible(I2C_RETRY_DELAY);//continue;
        }
    }while((ret < 0) && (++tries < I2C_RETRIES));

    if(ret < 0)
	printk(KERN_ERR "KTD2026:YLLOG:%s:write reg %d failed\n", __func__, regnum);
    return ret;
}

static void ktd_led_on(u32 led_out_reg, u32 led_current){
    u8 chan_ctrl_mask[] = {0xfc, 0xf3, 0xcf};

    ktd_data->chan_ctrl &= chan_ctrl_mask[led_out_reg-6];
    ktd_data->chan_ctrl = 0x01 << 2 * (led_out_reg -6);

    if(KTD_OUT1 != led_out_reg  &&  KTD_OUT2 != led_out_reg  && KTD_OUT3 != led_out_reg  &&  KTD_OUT4 != led_out_reg){
        printk(KERN_ERR "KTD2026:YLLOG:%s: reg error:%d \n", __func__, led_out_reg);
        return;
    }
    ktd_leds_i2c_write_reg(KTD_CC, 0x00);
    ktd_leds_i2c_write_reg(led_out_reg, led_current);
    ktd_leds_i2c_write_reg(KTD_ENRST, 0x00);
    ktd_leds_i2c_write_reg(KTD_CC, ktd_data->chan_ctrl);
}

static void ktd_led_blink(u32 led_out_reg, u32 led_current){
    u8 chan_ctrl_mask[] = {0xfc, 0xf3, 0xcf};
    
    ktd_data->chan_ctrl &= chan_ctrl_mask[led_out_reg-6];
    ktd_data->chan_ctrl = 0x02 << 2 * (led_out_reg -6);
    
    if(KTD_OUT1 != led_out_reg  && KTD_OUT2 != led_out_reg  && KTD_OUT3 != led_out_reg &&  KTD_OUT4 != led_out_reg){
        printk(KERN_ERR "KTD2026:YLLOG:%s: reg error:%d \n", __func__, led_out_reg);
        return;
    }
    
    ktd_leds_i2c_write_reg(KTD_CC, 0x00);     //set all leds off
    ktd_leds_i2c_write_reg(KTD_ENRST, 0x20);      //set mode:scl and sda high, 2x slower, tslot 1
    ktd_leds_i2c_write_reg(led_out_reg, led_current);
    ktd_leds_i2c_write_reg(KTD_RR, 0x00);     
    ktd_leds_i2c_write_reg(KTD_FP, 0x14);
    ktd_leds_i2c_write_reg(KTD_FOT1, 0x31);
    ktd_leds_i2c_write_reg(KTD_CC, ktd_data->chan_ctrl);
}

static void ktd_led_off(u32 led_out_reg){
    u8 chan_ctrl_mask[] = {0xfc, 0xf3, 0xcf};

    ktd_data->chan_ctrl &= chan_ctrl_mask[led_out_reg - 6];

    ktd_leds_i2c_write_reg(KTD_CC, ktd_data->chan_ctrl);
}

static void red_led_on(struct ktd_leds_data *ktd_data)
{
    printk("...ktd_red_on = %u ...\n",ktd_data->pdata->ktd_red_out); /**pc add debug**/
    ktd_led_on(ktd_data->pdata->ktd_red_out, ktd_data->pdata->ktd_red_current);
}

static void green_led_on(struct ktd_leds_data *ktd_data)
{
    printk("...ktd_green_on = %u ...\n",ktd_data->pdata->ktd_green_out); /**pc add debug**/
    ktd_led_on(ktd_data->pdata->ktd_green_out, ktd_data->pdata->ktd_green_current);
}

static void blue_led_on(struct ktd_leds_data *ktd_data)
{
    printk("...ktd_blue_on = %u ...\n",ktd_data->pdata->ktd_blue_out); /**pc add debug**/
    ktd_led_on(ktd_data->pdata->ktd_blue_out, ktd_data->pdata->ktd_blue_current);
}

static void red_led_blink(struct ktd_leds_data *ktd_data)
{
    printk("...ktd_red_blink = %u ...\n",ktd_data->pdata->ktd_red_out); /**pc add debug**/
    ktd_led_blink(ktd_data->pdata->ktd_red_out, ktd_data->pdata->ktd_red_current);
}

static void green_led_blink(struct ktd_leds_data *ktd_data)
{
    printk("...ktd_green_blink = %u ...\n",ktd_data->pdata->ktd_green_out); /**pc add debug**/
    ktd_led_blink(ktd_data->pdata->ktd_green_out, ktd_data->pdata->ktd_green_current);
}

static void blue_led_blink(struct ktd_leds_data *ktd_data)
{
    printk("...ktd_blue_blink = %u ...\n",ktd_data->pdata->ktd_blue_out); /**pc add debug**/
    ktd_led_blink(ktd_data->pdata->ktd_blue_out, ktd_data->pdata->ktd_blue_current);
}

static void red_led_off(struct ktd_leds_data *ktd_data)
{
    ktd_led_off(ktd_data->pdata->ktd_red_out);
}

static void green_led_off(struct ktd_leds_data *ktd_data)
{
    ktd_led_off(ktd_data->pdata->ktd_green_out);
}

static void blue_led_off(struct ktd_leds_data *ktd_data)
{
    ktd_led_off(ktd_data->pdata->ktd_blue_out);
}

static ssize_t led_blink_solid_store(struct device *dev,
                     struct device_attribute *attr,
                     const char *buf, size_t size)
{
    unsigned long enable=0;
    enum ktd_led_color color = LED_COLOR_DEFAULT;
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct ktd_leds_data *ktd_led_data = NULL;
    if (!strcmp(led_cdev->name, "red")) {
        color = LED_COLOR_RED;
    } else if (!strcmp(led_cdev->name, "green")) {
        color = LED_COLOR_GREEN;
    } else if (!strcmp(led_cdev->name, "blue")) {
        color = LED_COLOR_BLUE;
    } 
    ktd_led_data = container_of(led_cdev, struct ktd_leds_data, leds[color] );
    if(!ktd_led_data)
        printk(KERN_ERR "KTD2026:YLLOG:%s ktd_led_data is NULL ",__func__);
    enable= simple_strtoul(buf,NULL, 10);
    
#if DEBUG_TRICOLOR_LED
    printk(KERN_DEBUG "KTD2026:YLLOG:tricolor %s is %ld\n",led_cdev->name, enable);
#endif

    mutex_lock(&ktd_led_data->lock);
    if(enable){
        switch(color)
        {
            case LED_COLOR_RED:
                ktd_data->led_flag[0]=1;
                red_led_blink(ktd_data);
                break;
            case LED_COLOR_GREEN:
                ktd_data->led_flag[1]=1;
                green_led_blink(ktd_data);
                break;  
                case LED_COLOR_BLUE:
                ktd_data->led_flag[4]=1;
                blue_led_blink(ktd_data);
                break;  
            default:
                break;
            }
        }
        else
        {
            switch(color)
            {
                case LED_COLOR_RED:
                    ktd_data->led_flag[0]=0;
                    red_led_off(ktd_data);
                    break;
                case LED_COLOR_GREEN:
                    ktd_data->led_flag[1]=0;
                    green_led_off(ktd_data);
                    break;      
                case LED_COLOR_BLUE:
                    ktd_data->led_flag[4]=0;
                    blue_led_off(ktd_data);
                    break;      
                default:
            break;
        }
    }
    ktd_led_data->led_data[color]=enable;
    mutex_unlock(&ktd_led_data->lock);
    return size;
}
static ssize_t led_blink_solid_show(struct device *dev,
                    struct device_attribute *attr, char *buf)
{
    ssize_t ret = 0;    
    enum ktd_led_color color = LED_COLOR_DEFAULT;
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct ktd_leds_data *ktd_led_data = NULL;
    if (!strcmp(led_cdev->name, "red")) {
        color = LED_COLOR_RED;
    } else if (!strcmp(led_cdev->name, "green")) {
        color = LED_COLOR_GREEN;
    } else if (!strcmp(led_cdev->name, "blue")) {
        color = LED_COLOR_BLUE;
    }
    ktd_led_data = container_of(led_cdev, struct ktd_leds_data, leds[color]);
    if(!ktd_led_data)
        printk(KERN_ERR "KTD2026:YLLOG:%s tricolor_led is NULL ",__func__);
    ret = sprintf(buf, "%u\n", ktd_led_data->led_data[color]);
    return ret;
}

static void led_brightness_set_tricolor(struct led_classdev *led_cdev,
                   enum led_brightness brightness)
{
    struct ktd_leds_data *ktd_led_data = NULL;
    enum ktd_led_color color = LED_COLOR_DEFAULT;
   
    printk("....led_brightness_set_tricolor...\n"); /**pc add debug**/ 
    if (!strcmp(led_cdev->name, "red")) {
        color = LED_COLOR_RED;
    } else if (!strcmp(led_cdev->name, "green")) {
        color = LED_COLOR_GREEN;
    } else if (!strcmp(led_cdev->name, "blue")) {
        color = LED_COLOR_BLUE;
    } 
    ktd_led_data = container_of(led_cdev, struct ktd_leds_data, leds[color]);
    if(!ktd_led_data)
        printk(KERN_ERR "KTD2026:YLLOG:%s tricolor_led is NULL \n",__func__);
    mutex_lock(&ktd_led_data->lock);    
    if(brightness){
        switch(color) {
            case LED_COLOR_RED:
                printk(KERN_DEBUG "KTD2026:YLLOG:*****RED  OPEN *****\n");
                ktd_data->led_flag[2]=1;
                red_led_on(ktd_data);
                break;
            case LED_COLOR_GREEN:
                printk(KERN_DEBUG "KTD2026:YLLOG:*****GREEN OPEN*****\n");
                ktd_data->led_flag[3]=1;
                green_led_on(ktd_data);
                break;
            case LED_COLOR_BLUE:
                printk(KERN_DEBUG "KTD2026:YLLOG:*****BLUE OPEN*****\n");
                ktd_data->led_flag[5]=1;
                blue_led_on(ktd_data);
                break;
            default:
                break;
        }
    } else {
        switch(color) {
            case LED_COLOR_RED:
                printk(KERN_DEBUG "KTD2026:YLLOG:*****RED  CLOSED*****\n");
                    ktd_data->led_flag[2]=0;
                red_led_off(ktd_data);
                break;
            case LED_COLOR_GREEN:
                printk(KERN_DEBUG "KTD2026:YLLOG:*****GREEN  CLOSED*****\n");   
                ktd_data->led_flag[3]=0;
                green_led_off(ktd_data);
                break;
            case LED_COLOR_BLUE:
                printk(KERN_DEBUG "KTD2026:YLLOG:*****BLUE CLOSED*****\n");
                ktd_data->led_flag[5]=0;
                blue_led_off(ktd_data);
                break;
            default:
                break;
        }
    }
    mutex_unlock(&ktd_led_data->lock);
}

int ktd_led_set_charge(unsigned int led_color,unsigned int on_off)
{
    unsigned int enable = on_off;
    unsigned int color = led_color;
    
    mutex_lock(&ktd_data->lock);
    switch(color)
    {
        case 1:
            if(enable){
                ktd_leds_i2c_write_reg(KTD_CC, 0x01);
            }
            else{
                ktd_leds_i2c_write_reg(KTD_CC,0x00);
            }
            break;
        case 2:
            if(enable){
                ktd_leds_i2c_write_reg(KTD_CC, 0x04);
            }
            else{
                ktd_leds_i2c_write_reg(KTD_CC,0x00);
            }                   
            break;
        case 3:
            if(enable){
                ktd_leds_i2c_write_reg(KTD_CC, 0x10);
            }
            else{
                ktd_leds_i2c_write_reg(KTD_CC,0x00);
            }                   
            break;
        default:
        break;    
    }
    mutex_unlock(&ktd_data->lock);
    return 0;
}
EXPORT_SYMBOL_GPL(ktd_led_set_charge);

static DEVICE_ATTR(blink, 0644, led_blink_solid_show,led_blink_solid_store);

static int ktd_led_resume(struct i2c_client *client)
{
    int i,ret=0;

    for(i=0; i<6; i++)
    {
        if(ktd_data->led_flag[i])
            return 0;
    }
    ret = ktd_leds_i2c_write_reg(KTD_ENRST, 0x00);
    if(ret)
    {
        printk(KERN_ERR "KTD2026:YLLOG:%s:ktd_led resume write error\n", __func__);
        return -1;
    }
    return ret;
}

static int ktd_led_suspend(struct i2c_client *client, pm_message_t mesg)
{
    int ret;
    int i;
    for(i = 0; i < 6; i++)
    {
        if( ktd_data->led_flag[i] )
            return 0;
    }
    ret = ktd_leds_i2c_write_reg(KTD_ENRST, 0x08);//关闭所有LED子模块,低功耗模式
    if(ret)
    {
        printk(KERN_ERR "KTD2026:YLLOG:%s:ktd_led suspend error\n", __func__);
        return -1;
    }
    return ret;
}

static int ktd_leds_parse_dt(struct device * dev, struct ktd_leds_platform_data * pdata){
    
    int ret = 0,index = 0;
    u32 array[10] = {0};
    struct device_node *np = dev->of_node;
    struct property *prop;
    
    printk(KERN_DEBUG "KTD2026:YLLOG:%s\n", __func__);
    
    prop = of_find_property(np, "ktd_leds,cfgs", NULL);
    if (!prop)
        goto OUT;
    if (!prop->value)
        goto OUT;
    
    ret = of_property_read_u32_array(np, "ktd_leds,cfgs", array,  prop->length/sizeof(u32));

    if (!ret){
        pdata->ktd_red_out = array[index++];
        pdata->ktd_green_out = array[index++];
        pdata->ktd_blue_out = array[index++];

        pdata->ktd_red_current = array[index++];
        pdata->ktd_green_current = array[index++];
        pdata->ktd_blue_current = array[index++];
        printk("...get normal ktd dts config..\n"); /**pc add debug**/
        return 0;
    }    
    
OUT:
    pdata->ktd_red_out = KTD_OUT1;
    pdata->ktd_green_out = KTD_OUT2;
    pdata->ktd_blue_out = KTD_OUT3;

    pdata->ktd_red_current = 0x77;
    pdata->ktd_green_current = 0x77;
    pdata->ktd_blue_current = 0x77;

    dev_err(dev, "Looking up %s property in node %s failed\n", "ktd_leds,cfgs", np->full_name);
    dev_err(dev, "use default led config!!!\n");
    return 0;
}

static int ktd_leds_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int ret = 0;
    int i,j;
    struct ktd_leds_platform_data *pdata;
    
    printk(KERN_DEBUG "KTD2026:YLLOG:%s...probe start....\n",__func__); 
    if(!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WRITE_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
    {
        printk(KERN_ERR "KTD2026:YLLOG:%s: KTD_LEDS functionality check failed.\n", __func__);
        ret = -EIO;
        goto exit;
    }
    printk(KERN_DEBUG "KTD2026:YLLOG:ktd_leds i2c check success\n");

    if (client->dev.of_node) {
        pdata= devm_kzalloc(&client->dev, sizeof(struct ktd_leds_platform_data), GFP_KERNEL);
        if (pdata == NULL) {
            dev_err(&client->dev, "Failed to allocate memory\n");
            ret = -ENOMEM;
            goto out;
        }
        ret = ktd_leds_parse_dt(&client->dev, pdata);
        if (ret) {
            dev_err(&client->dev, "Get pdata failed from Device Tree\n");
            return ret;
        }
    } else {
        pdata = client->dev.platform_data;
        if (pdata == NULL) {
            dev_err(&client->dev, "pdata is NULL\n");
            ret = -ENOMEM;
            goto out;
        }
    }

    ktd_data = kzalloc(sizeof(struct ktd_leds_data),GFP_KERNEL);
    if (ktd_data == NULL) {
        printk(KERN_ERR "KTD2026:YLLOG:%s: KTD_LEDS kzalloc failed.\n", __func__);
        ret = -ENOMEM;
        goto out;
    }

    mutex_init(&ktd_data->lock);
    ktd_data->ldo_ctl_flag = 0;
    ktd_data->chan_ctrl = 0x00;
    ktd_data->client = client;
    ktd_data->pdata=pdata;
    i2c_set_clientdata(client, ktd_data);
    ktd_led_set_charge(0,0);    //close led
    
    ktd_data->leds[0].name = "red";
    ktd_data->leds[0].brightness_set = led_brightness_set_tricolor;
    ktd_data->leds[1].name = "green";
    ktd_data->leds[1].brightness_set = led_brightness_set_tricolor;  
    ktd_data->leds[2].name = "blue";
    ktd_data->leds[2].brightness_set = led_brightness_set_tricolor;
    
    for (i=0; i<3; i++){   /* red, green,blue*/
#ifdef CONFIG_SINK_LED_CONTRL // for charge red led by fuzicheng
        if (i == 0)
            continue;
#endif
        ret = led_classdev_register(&client->dev, &ktd_data->leds[i]);
        if (ret<0) {
            printk(KERN_ERR "KTD2026:YLLOG:ktd_leds: led_classdev_register failed\n");
            goto err_led_classdev_register_failed;
        }
    }
    
    for (i=0; i<3; i++){
#ifdef CONFIG_SINK_LED_CONTRL // for charge red led by fuzicheng
        if (i == 0)
            continue;
#endif
        ret = device_create_file(ktd_data->leds[i].dev, &dev_attr_blink);
        if (ret<0) {
            printk(KERN_ERR "KTD2026:YLLOG:tricolor_led: device_create_file failed\n");
            goto err_out_attr_blink;
        }
    }
    ret = i2c_smbus_write_byte_data(ktd_data->client, 0x06, 0x00);//set current is 0.125mA
    ret = i2c_smbus_write_byte_data(ktd_data->client, 0x04, ktd_data->chan_ctrl);//turn off leds   
    if(ret < 0){
        printk(KERN_ERR "KTD2026:YLLOG:can't find ktd2026 led control ic!");
        goto err_out_attr_blink;
    }

    printk(KERN_DEBUG "[ktd_leds]...func:%s...probe end....\n",__func__); 
    goto exit;
      
err_out_attr_blink:
    for (j = 0; j < i; j++)
        device_remove_file(ktd_data->leds[j].dev, &dev_attr_blink);
        
err_led_classdev_register_failed:
    for (j = 0; j < i; j++)
        led_classdev_unregister(&ktd_data->leds[j]);    

out:
    kfree(ktd_data);
exit:
    return ret;
}

static int /*__devexit */ktd_leds_remove(struct i2c_client *client)
{
    int ret = 0;
    
    ret = i2c_smbus_write_byte_data(ktd_data->client, 0x04, 0x00);//turn off leds
    return 0;
}

static const struct i2c_device_id ktd_leds_id[]={ { KTD_LEDS_DEV_NAME, 0 },{ }, };


MODULE_DEVICE_TABLE(i2c,ktd_leds_id);

static const struct of_device_id ktd_of_match[] = {
        { .compatible = "ktd-leds", },
        { },
};
MODULE_DEVICE_TABLE(of, ktd_of_match);

static struct i2c_driver ktd_leds_driver = {
    .driver = {
            .owner = THIS_MODULE,
            .name = KTD_LEDS_DEV_NAME,
            .of_match_table = ktd_of_match,
          },
    .probe = ktd_leds_probe,
    .remove = /*__devexit_p*/(ktd_leds_remove),
    .suspend = ktd_led_suspend,
    .resume = ktd_led_resume,
    .id_table = ktd_leds_id,

};

static int __init ktd_leds_init(void)
{
    return i2c_add_driver(&ktd_leds_driver);
}
static void __exit ktd_leds_exit(void)
{
    i2c_del_driver(&ktd_leds_driver);
    return;
}

late_initcall(ktd_leds_init);
module_exit(ktd_leds_exit);

MODULE_DESCRIPTION("KTD three color leds sysfs driver");
MODULE_AUTHOR("yulong");
MODULE_LICENSE("GPL");
