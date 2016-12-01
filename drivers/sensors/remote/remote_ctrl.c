#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
//#include <mach/gpio.h>
#include <asm/ioctls.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>

#include <linux/sensors/remote_ctrl.h>
//#include <mach/rpm-regulator.h>
//#include <mach/rpm-regulator-smd.h>

/*
	2014. 7. 16.
		: reset_gpio initial(default) condition changed to output/high. (Reset pin is active low)
 */
 
/*
 * remote_ctrl_write() or remote_ctrl_part_write() for misc write function.
 */
#define		USE_PART_WRITE				0

#define 	MAX_IRBUF_SIZE				2048

//#define		IR_DBG(args...)				
//#define		IR_DBG(args...)				pr_info(args)

#define IR_TAG                  "[remote_ctrl] "
#define IR_FUN(f)               printk(KERN_ERR IR_TAG"%s\n", __FUNCTION__)
#define IR_ERR(fmt, args...)    printk(KERN_ERR IR_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
#define IR_LOG(fmt, args...)    printk(KERN_ERR IR_TAG fmt, ##args)
#define IR_DBG(fmt, args...)    printk(KERN_ERR IR_TAG fmt, ##args)                 


static struct remote_ctrl_data *gDeviceData  = NULL;


static DEFINE_MUTEX(rx_lock);
static DEFINE_MUTEX(tx_lock);

static int _i2c_gpio_direction 	= RC_GPIO_MODE_OUTPUT;
static int _i2c_gpio_value		= 0;


static unsigned char _i2c_buf[MAX_IRBUF_SIZE];


// -------------------------------------------------------------------------------------------
// Function Prototypes
// -------------------------------------------------------------------------------------------
static void remote_ctrl_checkdata( const unsigned char * buf, int bufsize ) __attribute__ ((unused));

static ssize_t remote_ctrl_part_write(struct file *file,
				const char __user *buf,
				size_t count,
				loff_t *ppos) __attribute__ ((unused));


static ssize_t remote_ctrl_write(struct file *file,
				const char __user *buf,
				size_t count,
				loff_t *ppos) __attribute__ ((unused));


/* ------------------------------------------------------------------------------------------- */
/*
int remote_ctrl_enable_lvs1() 
{
    struct rpm_regulator *vreg;

	vreg = rpm_regulator_get( &pdata->dev, "8941_lvs1");
	if( vreg ) {
		pr_info("[remote_ctrl] enabling 8941_lvs1...\n" );
		rpm_regulator_enable(vreg);
	}
	
	return 0;
}
*/

static int raon_rc_parse_dt(struct device *dev, struct remote_ctrl_data *pdata)
{
	struct device_node *np = dev->of_node;
	
	/* irq info */
	pdata->init_gpio = of_get_named_gpio_flags(np, "raon,init-gpio",	0, &pdata->init_gpio_flags);

	/* pin for reset */
	pdata->reset_gpio = of_get_named_gpio_flags(np, "raon,reset-gpio",	0, &pdata->reset_gpio_flags);
				
	return 0;
}

static int remote_ctrl_probe(struct i2c_client *client,const struct i2c_device_id *id )
{
    struct remote_ctrl_data *deviceData = NULL;
    //struct rpm_regulator *vreg;
    
	//u8 test[4] = {0xff,0xff,0xff,0xff};
	
	pr_info("[remote_ctrl] %s \n", __func__);
	//msleep(50);
	//gpio_set_value(TEST_GPIO, 0);
	if(!i2c_check_functionality(client->adapter,I2C_FUNC_I2C)) 
	{
		pr_err("i2c_check_functionality failed.");
		return -ENODEV;
	}

	deviceData = kzalloc(sizeof(struct remote_ctrl_data),GFP_KERNEL);
    if (!deviceData) {
        return -ENOMEM;
    }

	deviceData->client 		= client;
	deviceData->dev                 = &client->dev;
	deviceData->init_gpio	= 0;
	deviceData->reset_gpio	= 0;

	raon_rc_parse_dt(&client->dev, deviceData);

	gDeviceData = deviceData;

        deviceData->remote_pinctrl = devm_pinctrl_get(deviceData->dev);
	deviceData->remote_state_active
		= pinctrl_lookup_state(deviceData->remote_pinctrl, "default");
	if (IS_ERR_OR_NULL(deviceData->remote_state_active)) {
		pr_err(	"err,Can not get remote default pinstate\n");
	}

       pinctrl_select_state(deviceData->remote_pinctrl, deviceData->remote_state_active);

	if( deviceData->init_gpio ) {

  		gpio_request(deviceData->init_gpio, "raon_rc");
  		
		gpio_direction_output(deviceData->init_gpio, 1);
		gpio_set_value(deviceData->init_gpio, 1);
		
		_i2c_gpio_direction = RC_GPIO_MODE_OUTPUT;
		_i2c_gpio_value	 	= 1;
	}


	if( deviceData->reset_gpio ) {

  		gpio_request(deviceData->reset_gpio, "ir_reset");
  		
		gpio_direction_output(deviceData->reset_gpio, 1);
		gpio_set_value(deviceData->reset_gpio, 1);			// default level (high)
	}
	
  		gpio_request(893, "remote_power");// 893-890 need modify for project
  		
		gpio_direction_output(893, 1);
		gpio_set_value(893, 1);			// default level (high)
	//vreg = rpm_regulator_get( NULL, "8941_lvs1");
	
	IR_LOG("[remote_ctrl] %s done. init_gpio=%d\n", __func__, deviceData->init_gpio );
	
	//i2c_smbus_write_i2c_block_data(gDeviceData->client, 0xb, 4,test );
	return 0;
}


	






static int remote_ctrl_remove(struct i2c_client *client)
{
	return 0;
}

static int remote_ctrl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int remote_ctrl_release(struct inode *inode, struct file *file)
{
	return 0;	
}



static ssize_t remote_ctrl_read(struct file *file, char __user *buf,
			 size_t count, loff_t *pos)
{
	int32_t rc = 0;

	struct i2c_msg msgs[] = {
		/*
		{
			.addr  = REMOTE_CTRL_SLAVE_ADDR,
			.flags = 0,
			.len   = 2,
			.buf   = buf,
		},
		*/
		{
			.addr  = REMOTE_CTRL_SLAVE_ADDR,
			.flags = I2C_M_RD,
			.len   = count,
			.buf   = _i2c_buf,
		},
	};
	pr_info("remote_ctrl_read enter:count=%ld\r\n",count);
       rc = copy_from_user(_i2c_buf, buf, count);
	pr_info("remote_ctrl_read copy from user:rc=%d\r\n",rc);
        if(rc<0)
	{
           pr_info("copy from user error\r\n");
           return rc;
	}
	rc = i2c_transfer(gDeviceData->client->adapter, msgs, 1);
	if (rc < 0) {
		pr_info("remote_ctrl_i2c_rxdata rc=%d\n", rc);
	}
	else
	{
		rc = copy_to_user(buf,_i2c_buf, count) ? -EFAULT : rc;
	}
	pr_info("remote_ctrl_read exit:ret = rc\r\n");
	return rc;
}



/*
static ssize_t remote_ctrl_read(struct file *file, char __user *buf,
			 size_t count, loff_t *pos)
{
	int32_t rc = 0;

	if( count > MAX_IRBUF_SIZE ) {
		count = MAX_IRBUF_SIZE;
	}

	rc = i2c_master_recv(gDeviceData->client, _i2c_buf,	count);
	IR_DBG("[remote_ir] %s : try to read %d bytes from addr 0x%02x...ret:%d\n", __func__, count, gDeviceData->client->addr, rc );
	
	if( rc >= 0 ) {
		int ret = copy_to_user(buf, _i2c_buf, count);
		if( ret != 0 ) {
			IR_DBG("[remote_ir] %s : copy_to_user failed. (%d)\n", __func__, ret );
		}
	}

	return rc;
}
*/


static void remote_ctrl_checkdata( const unsigned char * buf, int bufsize )
{
	int i, pktno, len, chksum, chksum_calc;

	if( bufsize >= 255 ) {
		IR_LOG("[remote_ir] packet_type: RAW (total size:%d :exceed 255 bytes.)\n", bufsize );
		return;
	}
	
		
	pktno = buf[0];
	len   = buf[1];		// packet length except packet no and length field.
	
	chksum  = buf[ bufsize - 2 ]; chksum <<= 8;
	chksum |= buf[ bufsize - 1 ];
	
	chksum_calc = 0;
	for(i = 0; i < bufsize-2 ; i++) {
		chksum_calc += buf[i];
	}
	
	if( (len+2) != bufsize ) {
		IR_LOG("[remote_ir] packet_type: RAW (length mismatch) (pktno:%d len:%d chksum:%d total_size:%d)\n", pktno, len, chksum, bufsize );
	}
	else if(  chksum != chksum_calc) {
		IR_LOG("[remote_ir] packet_type: RAW (checksum mismatch) (pktno:%d len:%d chksum:%d/%d total_size:%d)\n", pktno, len, chksum, chksum_calc, bufsize );
	}
	else {
		IR_LOG("[remote_ir] packet_type: SEQ_PKT (pktno:%d len:%d chksum:%d/%d total_size:%d)\n", pktno, len, chksum, chksum_calc, bufsize );
	}

}


// ----------
#define MAX_PART_SIZE 250
static ssize_t remote_ctrl_part_write(struct file *file,
				const char __user *buf,
				size_t count,
				loff_t *ppos)
{
	int rc=0, i, written, num_to_write, chksum;
	int pktNo;
//	int pktNo;
//	int pktNo;
	unsigned char _tmpbuf[MAX_PART_SIZE + 4]; 	// add 4 for pktNo, Length, checksum16

	//IR_DBG( "[remote_ir] %s", __func__ );
	
	if( gDeviceData == NULL ) return 0;
		

	if( gDeviceData->init_gpio ) {
		
		rc = gpio_get_value(gDeviceData->init_gpio);
		IR_DBG( "[remote_ir] current gpio value=%d/%d", _i2c_gpio_value, rc );
		
		//if( (_i2c_gpio_direction != RC_GPIO_MODE_OUTPUT) || (rc != 1) ) 
		{
			gpio_direction_output(gDeviceData->init_gpio, 0);
			msleep(10);
			gpio_set_value(gDeviceData->init_gpio, 1);
			msleep(10);
			
			_i2c_gpio_direction = RC_GPIO_MODE_OUTPUT;
			_i2c_gpio_value		= 1;
		}
	}

	
	mutex_lock(&tx_lock);

	if( count > 2048 ) count = 2048;
	rc = copy_from_user(_i2c_buf, buf, count);
	if (rc != 0) {
		IR_LOG("copy_from_user failed. (rc=%d)", rc);
		mutex_unlock(&tx_lock);
		return 0;
	}
		

	rc = 0;
	num_to_write = 0;
	written = 0;
	pktNo = 0;
	
	while( count > 0 ) 
	{
		num_to_write = (count > MAX_PART_SIZE) ? MAX_PART_SIZE : count;
			
		// build unit packet
		_tmpbuf[0] = pktNo;
		_tmpbuf[1] = (unsigned char)(num_to_write + 2);			// packet length except packet no and length field.
		memcpy( &_tmpbuf[2], &_i2c_buf[written], num_to_write );
		
		chksum = pktNo;
		for(i=1; i < (num_to_write+2); i++) {
			chksum += (int)_tmpbuf[i];
		}
		_tmpbuf[num_to_write + 2] = (unsigned char)((chksum >> 8) & 0xff);
		_tmpbuf[num_to_write + 3] = (unsigned char)(chksum & 0xff);
		
		//rc += i2c_master_send(gDeviceData->client,	&_i2c_buf[written],	num_to_write);
		rc += i2c_master_send(gDeviceData->client, _tmpbuf, num_to_write+4);
		
		count -= num_to_write;
		written += num_to_write;
		pktNo ++;

		if( count > 0 ) {
			msleep(1);
		}
		
	}
	
	// check data length of last packet
	if( num_to_write == 250 ) {
		
		msleep(1);

		_tmpbuf[0] = pktNo;	// packet No.
		_tmpbuf[1] = 2;		// Length
		_tmpbuf[2] = 0;		// checksum
		_tmpbuf[3] = (unsigned char)(pktNo + 2);
		
		//rc += i2c_master_send(gDeviceData->client,	&_i2c_buf[written],	num_to_write);
		i2c_master_send(gDeviceData->client, _tmpbuf, 4);
	}
	
	mutex_unlock(&tx_lock);

	return rc;
}
// ----------



// ----------
static ssize_t remote_ctrl_write(struct file *file,
				const char __user *buf,
				size_t count,
				loff_t *ppos)
{
	int rc=0;

	pr_info( "[remote_ir] %s,count=%ld \n", __func__ ,count );
	
	if( gDeviceData == NULL ) {
		IR_DBG( "[remote_ir] gDeviceData is NULL!" );
		return 0;
	}


	//if( gDeviceData->init_gpio && (_i2c_gpio_direction != RC_GPIO_MODE_OUTPUT) ) {
	if( gDeviceData->init_gpio ) {
		
		rc = gpio_get_value(gDeviceData->init_gpio);
		//IR_DBG( "[remote_ir] current gpio value=%d/%d", _i2c_gpio_value, rc );
		
		//if( (_i2c_gpio_direction != RC_GPIO_MODE_OUTPUT) || (rc != 1) ) 
		{
			//IR_LOG("[remote_ctrl] %s : triggering init_gpio(%d)\n", __func__, gDeviceData->init_gpio );	
			gpio_direction_output(gDeviceData->init_gpio, 0);
			msleep(10);
			gpio_set_value(gDeviceData->init_gpio, 1);
			msleep(10);
			
			_i2c_gpio_direction = RC_GPIO_MODE_OUTPUT;
			_i2c_gpio_value		= 1;
		}

	}


	mutex_lock(&tx_lock);

	if( count > MAX_IRBUF_SIZE ) {
		count = MAX_IRBUF_SIZE;
	}

	
	rc = copy_from_user(_i2c_buf, buf, count);
	if (rc != 0) {
		IR_LOG("copy_from_user failed. (rc=%d)", rc);
		mutex_unlock(&tx_lock);
		return 0;
	}

	
	// DEBUG ****
	//remote_ctrl_checkdata( _i2c_buf, count );
	// DEBUG ****
	rc = i2c_master_send(gDeviceData->client, _i2c_buf,	count);
	pr_info( "[remote_ir] i2c_master_send rc =0x%x \n", rc );

	mutex_unlock(&tx_lock);

	return rc;
}
// ----------



static ssize_t remote_ctrl_read_msg (void *i2cMsg)
{
	struct i2c_msg *msg = (struct i2c_msg *)i2cMsg;
	int rc;
	int count;

	struct i2c_msg msgs[] = {
		{
			.addr  = REMOTE_CTRL_SLAVE_ADDR,
			.flags = 0,
			.len   = 2,
			.buf   = _i2c_buf,
		},
		{
			.addr  = REMOTE_CTRL_SLAVE_ADDR,
			.flags = I2C_M_RD,
			.len   = 0,
			.buf   = _i2c_buf,
		},
	};


	if( gDeviceData == NULL || msg == NULL|| msg->buf == NULL ) {
		return -1;
	}

	count = msg->len;
	if( count > MAX_IRBUF_SIZE ) {
		count = MAX_IRBUF_SIZE;
	}

	msgs[1].len = count;


	mutex_lock(&tx_lock);

	rc = copy_from_user(_i2c_buf, msg->buf, count);

	rc = i2c_transfer(gDeviceData->client->adapter, msgs, 2);
	if (rc < 0) {
		IR_LOG("read_msg: i2c_transfer failed 0x%x\n", rc);
	}
	else {
		rc = copy_to_user(msg->buf, _i2c_buf, count) ? -EFAULT : rc;
	}
	
	mutex_unlock(&tx_lock);
	
	return rc;
}



static ssize_t remote_ctrl_write_msg (void *i2cMsg)
{
	struct i2c_msg *msg = (struct i2c_msg *)i2cMsg;
	int rc;
	int count;
	char *buf;
	
	if( gDeviceData == NULL || msg == NULL|| msg->buf == NULL ) {
		return -1;
	}
	
	count = msg->len;
	buf = msg->buf;
	
		
	mutex_lock(&tx_lock);

	if( count > MAX_IRBUF_SIZE ) {
		count = MAX_IRBUF_SIZE;
	}

	
	rc = copy_from_user(_i2c_buf, buf, count);
	if (rc != 0) {
		IR_LOG("copy_from_user failed. (rc=%d)", rc);
		mutex_unlock(&tx_lock);
		return 0;
	}

	rc = i2c_master_send(gDeviceData->client, _i2c_buf,	count);
	
	mutex_unlock(&tx_lock);

	return rc;
}




/*
 * serport_ldisc_ioctl() allows to set the port protocol, and device ID
 */

static long remote_ctrl_ioctl(struct file * file, unsigned int cmd, unsigned long arg)
{
	long err = 0;
	
	if( gDeviceData == NULL || !gDeviceData->init_gpio ) 
	{
		return -EINVAL;
	}
	
	switch(cmd)
	{
	case REMOTE_CTRL_CTL_GET_GPIO_MODE:
		err = _i2c_gpio_direction;	// 0:input 1:output mode
		break;

	case REMOTE_CTRL_CTL_SET_GPIO_MODE:

		if( arg == RC_GPIO_MODE_INPUT )
		{
			gpio_direction_input(gDeviceData->init_gpio);
			_i2c_gpio_direction = RC_GPIO_MODE_INPUT;
			
			err = gpio_get_value(gDeviceData->init_gpio);
		}
		else if( arg == RC_GPIO_MODE_OUTPUT )
		{
			gpio_direction_output(gDeviceData->init_gpio, (int)arg);
			_i2c_gpio_direction = RC_GPIO_MODE_OUTPUT;
			
			err = 0;
		}
		break;
	
	case REMOTE_CTRL_CTL_GET_GPIO_VALUE:
		err = gpio_get_value(gDeviceData->init_gpio);
		_i2c_gpio_value = err;
		break;

	case REMOTE_CTRL_CTL_SET_GPIO_VALUE:
		gpio_set_value(gDeviceData->init_gpio, (int)arg);
		_i2c_gpio_value = (int)arg;
		//IR_DBG( "[remote_ir] set gpio value=%d", _i2c_gpio_value );
		err = 0;
		break;
		
	case REMOTE_CTRL_CTL_SET_RESET_VALUE:
		if( gDeviceData->reset_gpio ) {
			int value = ((int)arg) ? 1: 0;
			gpio_set_value(gDeviceData->reset_gpio, value);
			err = 0;
		}
		else {
			err = -1;
		}
	
		break;
		
		
	case REMOTE_CTRL_WRITE:
		err = remote_ctrl_write_msg( (void *)arg );
		break;

	case REMOTE_CTRL_READ:
		err = remote_ctrl_read_msg( (void *)arg );
		break;

	/* FR116C reads firstly P00/WAKE pin level at F/W startup to determine operating mode of user IR mode or bootloader mode. */
			
	case REMOTE_CTRL_CTL_MODE_BOOTLOADER:
		/* bootloader mode at P00/WAKE HIGH */
		if( gDeviceData->reset_gpio && gDeviceData->init_gpio ) {
			
			/* make P00/WAKE pin to high */
			if(_i2c_gpio_direction != RC_GPIO_MODE_OUTPUT ) {
				gpio_direction_output(gDeviceData->init_gpio, 1);
			}
			gpio_set_value(gDeviceData->init_gpio, 1);
			msleep(1);

			/* RESET (Active LOW) */
			gpio_set_value(gDeviceData->reset_gpio, 0);
			msleep(10); 	/* reset input should be asserted low at least for 8us(typically) for normal reset function */

			gpio_set_value(gDeviceData->reset_gpio, 1);
			/* When the external reset input goes high, the internal reset is released after 64ms of 
			   stability time in case external clock frequency is 8MHz.
			*/
			msleep(64);
			
			
			/* Restore P00/WAKE pin status */
			if(_i2c_gpio_direction != RC_GPIO_MODE_OUTPUT ) 
			{
				gpio_direction_input(gDeviceData->init_gpio);
			}
			else 
			{
				gpio_set_value(gDeviceData->init_gpio, _i2c_gpio_value);
			}
			
			err = 0;
		}
		else {
			err = -1;
		}
	
		break;
		
	case REMOTE_CTRL_CTL_MODE_USERIR:
		/* user IR mode at P00/WAKE LOW */
		if( gDeviceData->reset_gpio && gDeviceData->init_gpio ) {
			
			/* make P00/WAKE pin to low */
			if(_i2c_gpio_direction != RC_GPIO_MODE_OUTPUT ) {
				gpio_direction_output(gDeviceData->init_gpio, 0);
			}
			gpio_set_value(gDeviceData->init_gpio, 0);
			msleep(1);

			/* RESET (Active LOW) */
			gpio_set_value(gDeviceData->reset_gpio, 0);
			msleep(10); 	/* reset input should be asserted low at least for 8us(typically) for normal reset function */

			gpio_set_value(gDeviceData->reset_gpio, 1);
			/* When the external reset input goes high, the internal reset is released after 64ms of 
			   stability time in case external clock frequency is 8MHz.
			*/
			msleep(64);
			
			
			/* Restore P00/WAKE pin status */
			if(_i2c_gpio_direction != RC_GPIO_MODE_OUTPUT ) 
			{
				gpio_direction_input(gDeviceData->init_gpio);
			}
			else 
			{
				gpio_set_value(gDeviceData->init_gpio, _i2c_gpio_value);
			}
			
			err = 0;
		}
		else {
			err = -1;
		}
		break;
		
	}
	

	return err;
}


static struct file_operations remote_ctrl_fops = {
	.owner			= THIS_MODULE,
	.open			= remote_ctrl_open,
	.release		= remote_ctrl_release,
	.read 			= remote_ctrl_read,
#if USE_PART_WRITE
	.write 			= remote_ctrl_part_write,
#else	
	.write 			= remote_ctrl_write,
#endif //USE_PART_WRITE	
	.unlocked_ioctl	= remote_ctrl_ioctl,
};

static struct miscdevice remote_ctrl_miscdev = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= "remote_ctrl_dev",
	.fops 	= &remote_ctrl_fops
};


static int remote_ctrl_misc_device_init(void)
{
    int result=0;
    result = misc_register(&remote_ctrl_miscdev);
    return result;
}

static int remote_ctrl_misc_device_deinit(void)
{
    int result=0;
    result = misc_deregister(&remote_ctrl_miscdev);
    return result;
}

static int remote_ctrl_suspend(struct device *dev)
{

	return 0;
}

static int remote_ctrl_resume(struct device *dev)
{

	return 0;
}

static const struct i2c_device_id RC_ID[] = 
{
  {"raon_remote_ctrl", 0},
  {}
};


MODULE_DEVICE_TABLE(i2c, RC_ID);

const static struct dev_pm_ops i2c_device_rc_pm_ops = {
	.suspend = remote_ctrl_suspend,
	.resume = remote_ctrl_resume,
};

static struct of_device_id rc_match_table[] = {
	{ .compatible = "raon,remote-ctrl",},
	{ },
};


static struct i2c_driver rc_i2c_driver = 
{
    .remove   	= (remote_ctrl_remove),
    .id_table 	= RC_ID,
    .probe 	  	= remote_ctrl_probe,
    .driver 	= 
    {
	    .name 	= "raon_remote_ctrl",
	    .owner 	= THIS_MODULE,
		.pm 	= &i2c_device_rc_pm_ops,
		.of_match_table = rc_match_table,
    },
};


static int __init remote_ctrl_init(void)
{
	int32_t rc = 0;
     #ifdef CONFIG_LPM_MODE
      extern unsigned int poweroff_charging;
      extern unsigned int recovery_mode;
      if (1 == poweroff_charging || 1 == recovery_mode) {
      printk(KERN_ERR"%s: probe exit, lpm=%d recovery=%d\n", __func__, poweroff_charging, recovery_mode);
      return -ENODEV;
}
#endif	
	pr_info("[remote_ctrl] %s \n", __func__);
	rc = i2c_add_driver(&rc_i2c_driver);
	
	return remote_ctrl_misc_device_init();
}

static void __exit remote_ctrl_exit(void)
{
	pr_info("[remote_ctrl] %s \n", __func__);
	i2c_del_driver(&rc_i2c_driver);

	remote_ctrl_misc_device_deinit();
}


//module_i2c_driver(rc_i2c_driver);
/*
module_driver(rc_i2c_driver, i2c_add_driver,	i2c_del_driver)

#define module_driver(__driver, __register, __unregister, ...) \
static int __init __driver##_init(void) \
{ \
	return __register(&(__driver) , ##__VA_ARGS__); \
} \
module_init(__driver##_init); \
static void __exit __driver##_exit(void) \
{ \
	__unregister(&(__driver) , ##__VA_ARGS__); \
} \
module_exit(__driver##_exit);

*/

module_init(remote_ctrl_init);
module_exit(remote_ctrl_exit);

MODULE_AUTHOR("Remote Control");
