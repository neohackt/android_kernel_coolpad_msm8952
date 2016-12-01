/*
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver.
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION      	DATE			AUTHOR        Note
 *    1.0		  2010-01-05			WenFS    only support mulititouch	Wenfs 2010-10-01
 *    2.0          2011-09-05                   Duxx      Add touch key, and project setting update, auto CLB command
 *    3.0		  2011-09-09			Luowj
 *

 */
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
 
#include "ft5x06_ts_fw.h" /* modidy firmware you will use*/
#include "ft5x0x_ts.h"
 
#include <linux/input/touchscreen_yl.h>
#include <linux/pm_runtime.h> //add by anxufeng for runtime suspend



#ifdef CONFIG_OF

#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>

#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>

#endif

//#define FT_openshort_test
#ifdef FT_openshort_test
#include "lib/test_lib.h"
#endif // Focaltech断线测试新方法

#define TW_TAG  "TouchScreen:"
#define TW_DEBUG //调试时打开，传CC时关闭
#ifdef TW_DEBUG
#define TW_DBG(fmt, ...) \
			if(debug_flag)printk(KERN_ERR TW_TAG pr_fmt(fmt),##__VA_ARGS__)
#else
#define TW_DBG(fmt, ...) do{ \
		 }while(0)
#endif

static struct i2c_client *this_client;
static int touch_flag =0;

static int debug_flag = 0;//maleijie  0; changed by taokai

#if defined(CONFIG_BOARD_K2_01)||defined(CONFIG_BOARD_K2_02)
extern char lcd_name[];		//add read panel name by zdd
static int gesture_flag;
#endif

/* yulong add */
/* author: tanliyu */
/* 2012.10.18 */
static int SCREEN_MAX_X;
static int SCREEN_MAX_Y;

#if CFG_SUPPORT_TOUCH_KEY
static int key_flags = 0;
static int DEBOUNCE;
static bool inDebounce;
#endif
/* yulong end */

int result = 0;
unsigned int file_version;
static int current_mode_flag=MODE_NORMAL;
static int backlight_on=0;

#ifdef CONFIG_TOUCHSCREEN_GESTURE_WAKEUP
extern struct device *touchscreen_get_dev(void);
#define GESTURE_LEFT		0x20
#define GESTURE_RIGHT		0x21
#define GESTURE_UP		    0x22
#define GESTURE_DOWN		0x23
#define GESTURE_DOUBLECLICK	0x24
#define GESTURE_O		    0x30
#define GESTURE_W		    0x31
#define GESTURE_M		    0x32
#define GESTURE_E		    0x33
#define GESTURE_C		    0x34


#define FTS_GESTURE_POINTS 255
#define FTS_GESTURE_POINTS_ONETIME  62
#define FTS_GESTURE_POINTS_HEADER 8
#define FTS_GESTURE_OUTPUT_ADRESS 0xD3
#define FTS_GESTURE_OUTPUT_UNIT_LENGTH 4

//short pointnum = 0;
//unsigned short coordinate_x[150] = {0};
//unsigned short coordinate_y[150] = {0};

enum support_gesture_e {
	TW_SUPPORT_NONE_SLIDE_WAKEUP   = 0x0,
	TW_SUPPORT_UP_SLIDE_WAKEUP     = 0x1,
	TW_SUPPORT_DOWN_SLIDE_WAKEUP   = 0x2,
	TW_SUPPORT_LEFT_SLIDE_WAKEUP   = 0x4,
	TW_SUPPORT_RIGHT_SLIDE_WAKEUP  = 0x8,
	TW_SUPPORT_E_SLIDE_WAKEUP      = 0x10,
	TW_SUPPORT_O_SLIDE_WAKEUP      = 0x20,
	TW_SUPPORT_W_SLIDE_WAKEUP      = 0x40,
	TW_SUPPORT_C_SLIDE_WAKEUP      = 0x80,
	TW_SUPPORT_M_SLIDE_WAKEUP      = 0x100,
	TW_SUPPORT_DOUBLE_CLICK_WAKEUP = 0x200,

	TW_SUPPORT_GESTURE_IN_ALL = (
		TW_SUPPORT_UP_SLIDE_WAKEUP    |
		TW_SUPPORT_DOWN_SLIDE_WAKEUP  |
		TW_SUPPORT_LEFT_SLIDE_WAKEUP  |
		TW_SUPPORT_RIGHT_SLIDE_WAKEUP |
		TW_SUPPORT_E_SLIDE_WAKEUP |
		TW_SUPPORT_O_SLIDE_WAKEUP |
		TW_SUPPORT_W_SLIDE_WAKEUP |
		TW_SUPPORT_C_SLIDE_WAKEUP |
		TW_SUPPORT_M_SLIDE_WAKEUP |
		TW_SUPPORT_DOUBLE_CLICK_WAKEUP)
};

u32 support_gesture = TW_SUPPORT_NONE_SLIDE_WAKEUP;
char wakeup_slide[32];
struct slide_wakeup
{
	unsigned char code;
	unsigned int mask;
	char *evp[2];
	char *gesture;
};
struct slide_wakeup tp_slide_wakeup[]={
	{GESTURE_RIGHT,TW_SUPPORT_RIGHT_SLIDE_WAKEUP,{"GESTURE=RIGHT",NULL}, "right"},
	{GESTURE_LEFT,TW_SUPPORT_LEFT_SLIDE_WAKEUP,{"GESTURE=LEFT",NULL}, "left"},
	{GESTURE_UP,TW_SUPPORT_UP_SLIDE_WAKEUP,{"GESTURE=UP",NULL}, "up"},
	{GESTURE_DOWN,TW_SUPPORT_DOWN_SLIDE_WAKEUP,{"GESTURE=DOWN",NULL}, "down"},
	{GESTURE_DOUBLECLICK,TW_SUPPORT_DOUBLE_CLICK_WAKEUP,{"GESTURE=DOUBLE_CLICK",NULL}, "double_click"},
	{GESTURE_C,TW_SUPPORT_C_SLIDE_WAKEUP,{"GESTURE=C",NULL}, "c"},
	{GESTURE_E,TW_SUPPORT_E_SLIDE_WAKEUP,{"GESTURE=E",NULL}, "e"},
	{GESTURE_M,TW_SUPPORT_M_SLIDE_WAKEUP,{"GESTURE=M",NULL}, "m"},
	{GESTURE_O,TW_SUPPORT_O_SLIDE_WAKEUP,{"GESTURE=O",NULL}, "o"},
	{GESTURE_W,TW_SUPPORT_W_SLIDE_WAKEUP,{"GESTURE=W",NULL}, "w"},
};

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
extern void touch_register_charger_notify(void (*fn)(void));
extern unsigned int touch_get_charger_status(void);
void yl_chg_status_changed(void);
#endif


void TW_enable_irq_wake(struct ft5x0x_ts_data *ts, u8 enable)
{
	if (enable) {
		enable_irq_wake(ts->client->irq);
	} else {
		disable_irq_wake(ts->client->irq);
	}
}
#endif

#if 1
struct virtual_keys_button virtual_keys_button[] = {
                   [0]={
                     .x = 540,
                     .y = 1350,
                     .width = 160,
                     .height = 50,
                     .code = KEY_BACK,
                     },
                   [1]={
			.x = 360,
                     .y = 1350,
                     .width = 160,
                     .height =50,
                     .code = KEY_HOME,
                     },
                   [2]={
                     .x = 180,
                     .y = 1350,
                     .width = 160,
                     .height =50,
                     .code = KEY_MENU,
                     },
};
#endif
//end



/***********************************************************************************************
Name	:	ft5x0x_i2c_rxdata

Input	:	*rxdata
                     *length

Output	:	ret

function	:

***********************************************************************************************/
static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
	  int ret;

	  struct i2c_msg msgs[] = {
		 {
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
		 },
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

    //msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		    pr_err("msg %s i2c read error: %d\n", __func__, ret);

	return ret;
}
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/

static int ft5x0x_i2c_txdata(char *txdata, int length)
{
	  int ret;

	  struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
		},
	};

//msleep(1);
    ret = i2c_transfer(this_client->adapter, msg, 1);
    if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	  return ret;
}
/***********************************************************************************************
Name	:	 ft5x0x_write_reg

Input	:	addr -- address
                     para -- parameter

Output	:

function	:	write register of ft5x0x

***********************************************************************************************/
static int ft5x0x_write_reg(u8 addr, u8 para)
{
    u8 buf[3];
    int ret = -1;

    buf[0] = addr;
    buf[1] = para;
    ret = ft5x0x_i2c_txdata(buf, 2);
    if (ret < 0)
    {
        pr_err("write reg failed! %#x ret: %d", buf[0], ret);
        return -1;
    }

    return 0;
}


/***********************************************************************************************
Name	:	ft5x0x_read_reg

Input	:	addr
                     pdata

Output	:

function	:	read register of ft5x0x

***********************************************************************************************/
static int ft5x0x_read_reg(u8 addr, u8 *pdata)
{
    int ret;
    u8 buf[2];
    struct i2c_msg msgs[2];
    //
    buf[0] = addr;    //register address

    msgs[0].addr = this_client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = buf;
    msgs[1].addr = this_client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].buf = buf;

    ret = i2c_transfer(this_client->adapter, msgs, 2);
    if (ret < 0)
        pr_err("msg %s i2c read error: %d\n", __func__, ret);
    *pdata = buf[0];
    return ret;
}
/***********************************************************************************************
Name	:	 ft5x0x_read_fw_ver

Input	:	 void

Output	:	 firmware version

function	:	 read TP firmware version

***********************************************************************************************/

static unsigned char ft5x0x_read_fw_ver(void)
{
    unsigned char ver;
    ft5x0x_read_reg(FT5X0X_REG_FIRMID, &ver);
    return(ver);
}


#if 1 //upgrade related

typedef enum
{
    ERR_OK,
    ERR_MODE,
    ERR_READID,
    ERR_ERASE,
    ERR_STATUS,
    ERR_ECC,
    ERR_DL_ERASE_FAIL,
    ERR_DL_PROGRAM_FAIL,
    ERR_DL_VERIFY_FAIL
}E_UPGRADE_ERR_TYPE;

typedef unsigned char         FTS_BYTE;     //8 bit
typedef unsigned short        FTS_WORD;    //16 bit
typedef unsigned int          FTS_DWRD;    //16 bit
typedef unsigned char         FTS_BOOL;    //8 bit

typedef struct _FTS_CTP_PROJECT_SETTING_T
{
    unsigned char uc_i2C_addr;             //I2C slave address (8 bit address)
    unsigned char uc_io_voltage;           //IO Voltage 0---3.3v;	1----1.8v
    unsigned char uc_panel_factory_id;     //TP panel factory ID
}FTS_CTP_PROJECT_SETTING_T;

#define FTS_NULL                0x0
#define FTS_TRUE                0x01
#define FTS_FALSE              0x0

#define I2C_CTPM_ADDRESS       0x70


int ftxxxx_i2c_Read(struct i2c_client *client, char *writebuf,
	int writelen, char *readbuf, int readlen)

{
	int ret;

	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
				.addr = client->addr,
					.flags = 0,
					.len = writelen,
					.buf = writebuf,
			},
			{
				.addr = client->addr,
					.flags = I2C_M_RD,
					.len = readlen,
					.buf = readbuf,
				},
		};
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret < 0)
			dev_err(&client->dev, "f%s: i2c read error.\n", __func__);
	} else {
		struct i2c_msg msgs[] = {
			{
				.addr = client->addr,
					.flags = I2C_M_RD,
					.len = readlen,
					.buf = readbuf,
			},
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			dev_err(&client->dev, "%s:i2c read error.\n", __func__);
	}
	return ret;
}

int ftxxxx_i2c_Write(struct i2c_client *client, char *writebuf, int writelen)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
				.flags = 0,
				.len = writelen,
				.buf = writebuf,
		},
	};

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0)
		dev_err(&client->dev, "%s i2c write error.\n", __func__);

	return ret;
}


#ifdef FT_openshort_test

static DEFINE_MUTEX(g_device_mutex);
struct i2c_client *G_Client = NULL;

int focal_i2c_Read(unsigned char *writebuf,
		    int writelen, unsigned char *readbuf, int readlen)
{
		if(NULL == G_Client)
	{
		return -1;
	}

	return ftxxxx_i2c_Read(G_Client, writebuf, writelen, readbuf, readlen);
}
/*write data by i2c*/
int focal_i2c_Write(unsigned char *writebuf, int writelen)
{
	int ret;

	struct i2c_msg msg[] = {
		{
		 .addr = G_Client->addr,
		 .flags = 0,
		 .len = writelen,
		 .buf = writebuf,
		 },
	};

	if(NULL == G_Client->adapter) //全局IIC适配器指针,使用前必须初始化
	{
		printk("i2c_adapter = NULL in function:%s\n", __func__);
		return -1;
	}
	else if(NULL == writebuf)
	{
		printk("writebuf = NULL in function:%s\n", __func__);
		return -1;
	}
	else if(writelen<=0)
	{
		printk("writelen <= 0 in function:%s\n", __func__);
		return -1;
	}

	ret = i2c_transfer(G_Client->adapter, msg, 1);//write data only
	if (ret < 0)
		printk("%s i2c write error.\n", __func__);

	return ret;
}


#endif	//FT_openshort_test

#ifdef FT_openshort_test
#define FTXXXX_INI_FILEPATH "/system/etc/"  //配置文件存放目录定义
static int  openshort_test = 0;

//获取配置文件大小, 用于分配内存读取配置
static int ftxxxx_GetInISize(char *config_name)
{
	struct file *pfile = NULL;
	struct inode *inode = NULL;
	//unsigned long magic;
	off_t fsize = 0;
	char filepath[128];
	memset(filepath, 0, sizeof(filepath));

	sprintf(filepath, "%s%s", FTXXXX_INI_FILEPATH, config_name);

	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);

	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}

	inode = pfile->f_dentry->d_inode;
	//magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	filp_close(pfile, NULL);

	return fsize;
}
//读取配置到内存
static int ftxxxx_ReadInIData(char *config_name, char *config_buf)
{
	struct file *pfile = NULL;
	struct inode *inode = NULL;
	//unsigned long magic;
	off_t fsize = 0;
	char filepath[128];
	loff_t pos = 0;
	mm_segment_t old_fs;

	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s%s", FTXXXX_INI_FILEPATH, config_name);
	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}

	inode = pfile->f_dentry->d_inode;
	//magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	vfs_read(pfile, config_buf, fsize, &pos);
	filp_close(pfile, NULL);
	set_fs(old_fs);

	return 0;
}
//保存测试数据到SD卡 etc.
static int ftxxxx_SaveTestData(char *file_name, char *data_buf, int iLen)
{
	struct file *pfile = NULL;
	
	char filepath[128];
	loff_t pos;
	mm_segment_t old_fs;

	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s%s", "/mnt/sdcard/", file_name);
	if (NULL == pfile)
		pfile = filp_open(filepath, O_CREAT|O_RDWR, 0);
	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	vfs_write(pfile, data_buf, iLen, &pos);
	filp_close(pfile, NULL);
	set_fs(old_fs);

	return 0;
}

//读取,解析配置文件,初始化测试变量
static int ftxxxx_get_testparam_from_ini(char *config_name)
{
	char *filedata = NULL;

	int inisize = ftxxxx_GetInISize(config_name);

	pr_info("inisize = %d \n ", inisize);
	if (inisize <= 0) {
		pr_err("%s ERROR:Get firmware size failed\n", __func__);
		return -EIO;
	}

	filedata = kmalloc(inisize + 1, GFP_KERNEL);
	if(NULL==filedata)
	{
		printk("kmalloc failed in fuction:%s\n",__func__);
		return -1;
	}
		
	if (ftxxxx_ReadInIData(config_name, filedata)) {
		pr_err("%s() - ERROR: request_firmware failed\n", __func__);
		kfree(filedata);
		return -EIO;
	} else {
		pr_info("ftxxxx_ReadInIData successful\n");
	}

	set_param_data(filedata);
	return 0;
}

static ssize_t ftxxxx_ftsscaptest_store(struct device *dev,
struct device_attribute *attr,
	const char *buf, size_t count)
{
	/* place holder for future use */
	char cfgname[128];
	char *testdata = NULL;
	int iTestDataLen=0;//库中测试数据实际长度,用于保存到文件
	struct i2c_client *client = to_i2c_client(dev);
	testdata = kmalloc(1024*32, GFP_ATOMIC);//用于获取存放在库中的测试数据,注意分配空间大小.
	if(NULL == testdata)
	{
		printk("kmalloc failed in function:%s\n", __func__);
		return -1;
	}
	
	G_Client=client;
	memset(cfgname, 0, sizeof(cfgname));
	sprintf(cfgname, "%s", buf);
	cfgname[count-1] = '\0';

	mutex_lock(&g_device_mutex);
    ///********************************************************************************
	init_i2c_write_func(focal_i2c_Write);//初始化平台相关的I2C写函数(以上实现只是示例), 传递给测试库进行写操作
	init_i2c_read_func(focal_i2c_Read);//初始化平台相关的I2C读函数(以上实现只是示例), 传递给测试库进行读操作
	///********************************************************************************
	
	if(ftxxxx_get_testparam_from_ini(cfgname) <0)//第一步,读取解析配置文件.
		printk("get testparam from ini failure\n");
	else {		
		if(true == start_test_tp()) //第二步,根据测试配置开始测试
			{
			printk("tp test pass\n");
			openshort_test = 1;
			}
		else
			{
			printk("tp test failure\n");
			openshort_test = 0;
			}
		iTestDataLen = get_test_data(testdata);//第三步,获取测试库中的测试数据
		//printk("%s\n", testdata);
		ftxxxx_SaveTestData("testdata.txt", testdata, iTestDataLen);
		free_test_param_data();//第四步,释放内存等...
	}
	ft5x0x_write_reg(0x00, 0x00);
	mutex_unlock(&g_device_mutex);
	if(NULL != testdata) kfree(testdata);

	return 1;
}


static ssize_t ftxxxx_ftsscaptest_show(struct device *dev,
struct device_attribute *attr, char *buf)
{	

	return sprintf(buf, "%d\n",openshort_test);
	
}

#endif  //FT_openshort_test

void delay_qt_ms(unsigned long  w_ms)
{
    unsigned long i;
    unsigned long j;

    for (i = 0; i < w_ms; i++)
    {
        for (j = 0; j < 1000; j++)
        {
            udelay(1);
        }
    }
}
#endif
#if 1
/*
[function]:
    callback: read data from ctpm by i2c interface,implemented by special user;
[parameters]:
    bt_ctpm_addr[in]    :the address of the ctpm;
    pbt_buf[out]        :data buffer;
    dw_lenth[in]        :the length of the data buffer;
[return]:
    FTS_TRUE     :success;
    FTS_FALSE    :fail;
*/
FTS_BOOL i2c_read_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)





{
    int ret;

    ret=i2c_master_recv(this_client, pbt_buf, dw_lenth);

    if(ret<=0)
    {
        printk(KERN_ERR "[FTS]i2c_read_interface error\n");

        return FTS_FALSE;
    }

    return FTS_TRUE;
}

/*
[function]:
    callback: write data to ctpm by i2c interface,implemented by special user;

[parameters]:
    bt_ctpm_addr[in]    :the address of the ctpm;
    pbt_buf[in]        :data buffer;
    dw_lenth[in]        :the length of the data buffer;



[return]:
    FTS_TRUE     :success;
    FTS_FALSE    :fail;
*/
FTS_BOOL i2c_write_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    int ret;
    ret=i2c_master_send(this_client, pbt_buf, dw_lenth);
    if(ret<=0)
    {
        printk(KERN_ERR "[FTS]i2c_write_interface error line = %d, ret = %d\n", __LINE__, ret);
        return FTS_FALSE;
    }

    return FTS_TRUE;
}
#endif
/*
[function]:
    send a command to ctpm.
[parameters]:
    btcmd[in]        :command code;
    btPara1[in]    :parameter 1;
    btPara2[in]    :parameter 2;
    btPara3[in]    :parameter 3;
    num[in]        :the valid input parameter numbers, if only command code needed and no parameters followed,then the num is 1;
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL cmd_write(FTS_BYTE btcmd,FTS_BYTE btPara1,FTS_BYTE btPara2,FTS_BYTE btPara3,FTS_BYTE num)
{
    FTS_BYTE write_cmd[4] = {0};

    write_cmd[0] = btcmd;
    write_cmd[1] = btPara1;
    write_cmd[2] = btPara2;
    write_cmd[3] = btPara3;
    return i2c_write_interface(I2C_CTPM_ADDRESS, write_cmd, num);
}

/*
[function]:
    write data to ctpm , the destination address is 0.
[parameters]:
    pbt_buf[in]    :point to data buffer;
    bt_len[in]        :the data numbers;
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL byte_write(FTS_BYTE* pbt_buf, FTS_DWRD dw_len)
{

    return i2c_write_interface(I2C_CTPM_ADDRESS, pbt_buf, dw_len);
}

/*
[function]:
    read out data from ctpm,the destination address is 0.
[parameters]:
    pbt_buf[out]    :point to data buffer;
    bt_len[in]        :the data numbers;
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL byte_read(FTS_BYTE* pbt_buf, FTS_BYTE bt_len)
{
    return i2c_read_interface(I2C_CTPM_ADDRESS, pbt_buf, bt_len);
}


#ifdef CONFIG_OF

/**********************************************************************/
//#define FOCALTECH_TS_GPIO_RESET	        (12 + 902)
//#define FOCALTECH_TS_GPIO_IRQ		(13 + 902)
#define FOCALTECH_TS_GPIO_RESET	        (64 + 890)
#define FOCALTECH_TS_GPIO_IRQ		(65 + 890)
#define FOCALTECH_TS_GPIO_ID1   38
#define FOCALTECH_TS_GPIO_ID2   49
#define FTP_PWR_EN      1  		/* the tw power enable switch */
#define FTP_SLEEP_PWR_EN 0		/* the tw sleep power enable switch */
#if FTP_PWR_EN
struct regulator *ftp_ts_vdd;
struct regulator *ftp_ts_vcc_i2c;
static unsigned int gtp_gpio_vcc_i2c_en;
#define FTP_VDD		"vdd"
#define FTP_VCC_IO	"vcc_i2c"
#endif
/**********************************************************************/
static int focaltech_power_init(bool on)
{
#if FTP_PWR_EN
	int rc;
	if (!on)
		goto pwr_deinit;
	ftp_ts_vdd = regulator_get(&this_client->dev, FTP_VDD);
	if (IS_ERR(ftp_ts_vdd)) {
		rc = PTR_ERR(ftp_ts_vdd);
		pr_err("Touchscreen Regulator get failed vdd rc=%d\n", rc);
		return rc;
	}
	if (regulator_count_voltages(ftp_ts_vdd) > 0) {
		rc = regulator_set_voltage(ftp_ts_vdd, 2600000,3300000);
		if (rc) {
			pr_err("Touchscreen Regulator set failed vdd rc=%d\n", rc);
			goto reg_vdd_put;
		}
	}
	ftp_ts_vcc_i2c = regulator_get(&this_client->dev, FTP_VCC_IO);
	if (IS_ERR(ftp_ts_vcc_i2c)) {
		rc = PTR_ERR(ftp_ts_vcc_i2c);
		pr_err("Touchscreen Regulator get failed vcc_i2c rc=%d\n", rc);
		goto reg_vdd_set_vtg;
	}
	if (regulator_count_voltages(ftp_ts_vcc_i2c) > 0) {
		rc = regulator_set_voltage(ftp_ts_vcc_i2c, 1800000,1800000);
		if (rc) {
			pr_err("Touchscreen Regulator set_vtg failed vcc_i2c rc=%d\n", rc);
			goto reg_vcc_i2c_put;
		}
	}
	return 0;
reg_vcc_i2c_put:
	regulator_put(ftp_ts_vcc_i2c);
reg_vdd_set_vtg:
	if (regulator_count_voltages(ftp_ts_vdd) > 0)
		regulator_set_voltage(ftp_ts_vdd, 0, 3300000);
reg_vdd_put:
	regulator_put(ftp_ts_vdd);
	return rc;
pwr_deinit:
	if (regulator_count_voltages(ftp_ts_vdd) > 0)
		regulator_set_voltage(ftp_ts_vdd, 0, 3300000);
	regulator_put(ftp_ts_vdd);
	if (regulator_count_voltages(ftp_ts_vcc_i2c) > 0)
		regulator_set_voltage(ftp_ts_vcc_i2c, 0, 1800000);
	regulator_put(ftp_ts_vcc_i2c);
#endif
	return 0;
}
static int focaltech_ts_power(int on)
{
#if FTP_PWR_EN
	int rc;
	if (!on)
		goto power_off;
	rc = regulator_enable(ftp_ts_vdd);
	if (rc) {
		pr_err("Touchscreen Regulator vdd enable failed rc=%d\n", rc);
		return rc;
	}
	rc = regulator_enable(ftp_ts_vcc_i2c);
	if (rc) {
		pr_err("Touchscreen Regulator vdd_i2c enable failed rc=%d\n", rc);
		regulator_disable(ftp_ts_vdd);
	}
	gpio_set_value(gtp_gpio_vcc_i2c_en,1);
	return rc;
power_off:
	rc = regulator_disable(ftp_ts_vdd);
	if (rc) {
		pr_err("Touchscreen Regulator vdd disable failed rc=%d\n", rc);
		return rc;
	}
	rc = regulator_disable(ftp_ts_vcc_i2c);
	if (rc) {
		pr_err("Touchscreen Regulator vdd_i2c disable failed rc=%d\n", rc);
		rc = regulator_enable(ftp_ts_vdd);
	}
	return rc;
#endif
	return 0;
}
static int focaltech_ts_gpio_init(bool on)
{
	int ret = 0;
	if(!on)
		goto ftp_gpio_deinit;

#if FTP_PWR_EN
        if (gpio_is_valid(gtp_gpio_vcc_i2c_en)) {
                ret = gpio_request(gtp_gpio_vcc_i2c_en, "focaltech-ts-vcc-i2c-en");
                if (ret){
                        pr_err("TOUCH:%s:Failed to request GPIO %d\n",__func__, gtp_gpio_vcc_i2c_en);
                        return ret;
                }
                gpio_direction_output(gtp_gpio_vcc_i2c_en, 1);
        } else {
                pr_err("TOUCH:%s:irq gpio not provided\n",__func__);
                return -1;
        }
#endif

	ret = gpio_request(FOCALTECH_TS_GPIO_RESET, "focaltech-ts-reset");
	if (ret){
		pr_err("TOUCH:YLLOG:%s: Failed to request GPIO %d\n",__func__, FOCALTECH_TS_GPIO_RESET);
		return ret;
	}
	ret = gpio_direction_output(FOCALTECH_TS_GPIO_RESET, 0);
             /***************************************************/
		ret = gpio_request(FOCALTECH_TS_GPIO_IRQ, "focaltech-ts-irq");
		if (ret){
			pr_err("TOUCH:%s: Failed to request GPIO %d\n",__func__, FOCALTECH_TS_GPIO_IRQ);
			gpio_free(FOCALTECH_TS_GPIO_RESET);
			return ret;
		}
		gpio_direction_input(FOCALTECH_TS_GPIO_IRQ);
		/******************TW ID****************************/
	/*	ret = gpio_request(FOCALTECH_TS_GPIO_ID1, "focaltech-ts-id1");
		if (ret){
			pr_err("TOUCH:%s: Failed to request GPIO %d\n",__func__, FOCALTECH_TS_GPIO_ID1);
			gpio_free(FOCALTECH_TS_GPIO_RESET);
			gpio_free(FOCALTECH_TS_GPIO_IRQ);
			return ret;
		}
		gpio_direction_input(FOCALTECH_TS_GPIO_ID1);
		ret = gpio_request(FOCALTECH_TS_GPIO_ID2, "focaltech-ts-id1");
		if (ret){
			pr_err("TOUCH:%s: Failed to request GPIO %d\n",__func__, FOCALTECH_TS_GPIO_ID2);
			gpio_free(FOCALTECH_TS_GPIO_RESET);
			gpio_free(FOCALTECH_TS_GPIO_IRQ);
			gpio_free(FOCALTECH_TS_GPIO_ID1);
			return ret;
		}
		gpio_direction_input(FOCALTECH_TS_GPIO_ID2);*/
		/**************************************************/
	return 0;
ftp_gpio_deinit:
	gpio_free(FOCALTECH_TS_GPIO_RESET);
	gpio_free(FOCALTECH_TS_GPIO_IRQ);
	gpio_free(FOCALTECH_TS_GPIO_ID1);
	gpio_free(FOCALTECH_TS_GPIO_ID2);
	return 0;
}
/*
static unsigned char focaltech_ts_get_id_pin(void)
{
	unsigned char  pin_status=0x00;
	unsigned char  pin_status1=0x00;
	unsigned char  pin_status2=0x00;
	pin_status1=gpio_get_value(FOCALTECH_TS_GPIO_ID1);
	pin_status2=gpio_get_value(FOCALTECH_TS_GPIO_ID2);
	pin_status=(pin_status1)|(pin_status2<<1);
	printk("focaltech_ts_get_id_pin pin_status1=0x%x,pin_status2=0x%x,pin_status=0x%x\n",pin_status1,pin_status2,pin_status);
	return pin_status;
}*/
static int focaltech_ts_init_resource(void)
{
	int ret = 0;
	ret = focaltech_ts_gpio_init(1);
	if(ret)
	{
		pr_err("Touchscreen GPIO init failed!\n");
		return ret;
	}
	ret = focaltech_power_init(1);
	if(ret)
	{
		pr_err("Touchscreen Power init failed!\n");
		focaltech_ts_gpio_init(0);
		return ret;
	}
	pr_debug("TOUCH:YLLOG:requst and init resource complete\n");
	return 0;
}
static void focaltech_ts_release_resource(void)
{
	focaltech_ts_gpio_init(0);
	focaltech_power_init(0);
}
static int focaltech_ts_reset(int ms)
{
	gpio_direction_output(FOCALTECH_TS_GPIO_RESET, 0);
	mdelay(3);
	gpio_direction_output(FOCALTECH_TS_GPIO_RESET, 1);
	mdelay(ms);
	return 0;
}
static int focaltech_ts_suspend(void){
#if FTP_SLEEP_PWR_EN
	focaltech_ts_power(0);
#endif
	/////////////////focaltech_i2c_pin_config(0); //add by anxufeng    //maleijie
       return 0;
}
static int focaltech_ts_resume(void){
#if FTP_SLEEP_PWR_EN
	focaltech_ts_power(1);
#endif
	////////////////focaltech_i2c_pin_config(1);//maleijie
	focaltech_ts_reset(50);
       return 0;
}



static int goodix_ts_parse_dt(struct device *dev, struct tw_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	int ret;
	enum of_gpio_flags flags;

	/* focaltech,vcc_i2c_supply_en */
        ret = of_get_named_gpio_flags(np, "focaltech,vcc_i2c_supply_en", 0, &flags);
        if (ret < 0){
                dev_err(dev, "Looking up %s property in node %s failed",
                                "focaltech,vcc_i2c_supply_en", np->full_name);
                return -ENODEV;
        }
	gtp_gpio_vcc_i2c_en = ret;
	
	/* reset, irq gpio info */
	pdata->gpio_reset = of_get_named_gpio_flags(np, "focaltech,reset-gpio",
				0, &flags);
	printk("reset-gpio = %d\n", pdata->gpio_reset);

	pdata->gpio_irq = of_get_named_gpio_flags(np, "focaltech,irq-gpio",
				0, &flags);
	printk("irq-gpio = %d\n", pdata->gpio_irq);

	ret = of_property_read_u32(np, "focaltech,irq_flags", (unsigned int *)&pdata->irqflag);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"focaltech,irq_flags", np->full_name);
		return -ENODEV;
	}
	printk("irq-flag = %d\n", (int)pdata->irqflag);
	//pdata->irqflag = 2;

	ret = of_property_read_u32(np, "focaltech,screen_x", &pdata->screen_x);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"focaltech,screen_x", np->full_name);
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "focaltech,screen_y", &pdata->screen_y);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"focaltech,screen_y", np->full_name);
		return -ENODEV;
	}
	printk("screen x,y = %d, %d\n", pdata->screen_x, pdata->screen_y);

	ret = of_property_read_u32(np, "focaltech,pwr_en", &pdata->pwr_en);
	ret = of_property_read_u32(np, "focaltech,sleep_pwr_en", &pdata->sleep_pwr_en);
	ret = of_property_read_string(np, "focaltech,vcc_i2c_supply", &pdata->ts_vcc_i2c);
	ret = of_property_read_string(np, "focaltech,vdd_supply", &pdata->ts_vdd);

	printk("pwr_en=%d, sleep_pwr_en=%d, vcc_i2c=%s, vdd=%s\n", pdata->pwr_en,
				pdata->sleep_pwr_en, pdata->ts_vcc_i2c, pdata->ts_vdd);

//added by maleijie
#if 1
	printk("pdata=%p, pdata->buttons=%p\n", pdata,pdata->buttons);
	if(pdata->buttons==NULL){
       pdata->buttons=virtual_keys_button;
	   pdata->nbuttons=ARRAY_SIZE(virtual_keys_button);
}
#endif
//
	pdata->init = focaltech_ts_init_resource;//goodix_ts_hw_init;
	pdata->release = focaltech_ts_release_resource;//goodix_ts_release;
	pdata->reset = focaltech_ts_reset;//goodix_ts_reset;
	pdata->power = focaltech_ts_power;//goodix_ts_power;
	pdata->suspend = focaltech_ts_suspend;//goodix_ts_sleep;
	pdata->resume = focaltech_ts_resume;//goodix_ts_wakeup;
//	pdata->get_id_pin=focaltech_ts_get_id_pin;

	return 0;
}
#endif



#define    FTS_PACKET_LENGTH        128

/*fanhui xiabiao*/
int fts_check_tp_id(unsigned char tp_id)
{
	int i = 0;

	for(i=0; i< fw_array_size; i++)
	{
		if (tp_id == tp_info[i].tp_id)
			 return i;
	}

	return -1;//no match tp_id return -1;
}

#if defined(FT8606) || defined(FT5X46)
struct Upgrade_Info{
	u16		delay_aa;		/*delay of write FT_UPGRADE_AA*/
	u16		delay_55;		/*delay of write FT_UPGRADE_55*/
	u8		upgrade_id_1;	/*upgrade id 1*/
	u8		upgrade_id_2;	/*upgrade id 2*/
	u16		delay_readid;	/*delay of read id*/
};

struct Upgrade_Info upgradeinfo;

/*
*get upgrade information depend on the ic type
*/
static void fts_get_upgrade_info(struct Upgrade_Info * upgrade_info)
{
	switch(DEVICE_IC_TYPE)
	{
		/*case IC_ftxxxx:
		upgrade_info->delay_55 = ftxxxx_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = ftxxxx_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = ftxxxx_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = ftxxxx_UPGRADE_ID_2;
		upgrade_info->delay_readid = ftxxxx_UPGRADE_READID_DELAY;
		break;
		case IC_FT5606:
		upgrade_info->delay_55 = FT5606_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5606_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5606_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5606_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5606_UPGRADE_READID_DELAY;
		break;
		case IC_FT5316:
		upgrade_info->delay_55 = FT5316_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5316_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5316_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5316_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5316_UPGRADE_READID_DELAY;
		break;
		case IC_FT5X36:
		upgrade_info->delay_55 = FT5X36_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5X36_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5X36_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5X36_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5X36_UPGRADE_READID_DELAY;
		break;*/
	case IC_FT5X46:
		upgrade_info->delay_55 = FT5X46_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5X46_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5X46_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5X46_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5X46_UPGRADE_READID_DELAY;
		break;
	case IC_FT8606:
		upgrade_info->delay_55 = FT8606_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT8606_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT8606_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT8606_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT8606_UPGRADE_READID_DELAY;
		break;

	default:
		break;
	}
}
#endif


#ifdef FT5X46
int hid_to_i2c(struct i2c_client * client)
{
	u8 auc_i2c_write_buf[5] = {0};
	int bRet = 0;

	auc_i2c_write_buf[0] = 0xeb;
	auc_i2c_write_buf[1] = 0xaa;
	auc_i2c_write_buf[2] = 0x09;

	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 3);

	msleep(10);

	auc_i2c_write_buf[0] = auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = 0;

	ftxxxx_i2c_Read(client, auc_i2c_write_buf, 0, auc_i2c_write_buf, 3);
	printk("%s, auc_i2c_write_buf=0x%x, 0x%x, 0x%x\n", __func__, 
		auc_i2c_write_buf[0], auc_i2c_write_buf[1], auc_i2c_write_buf[2]);
	if(0xeb==auc_i2c_write_buf[0] && 0xaa==auc_i2c_write_buf[1] && 0x08==auc_i2c_write_buf[2])
		bRet = 1;		
	else
		bRet = 0;

	return bRet;
	
}


int  fts_ctpm_fw_upgrade(struct i2c_client * client, u8* pbt_buf, u32 dw_lenth)
{
	u8 reg_val[4] = {0};
	u32 i = 0;
	u32 packet_number;
	u32 j;
	u32 temp;
	u32 lenght;
	u8 packet_buf[FTS_PACKET_LENGTH + 6];
	u8 auc_i2c_write_buf[10];
	u8 bt_ecc;
	int i_ret;
	for(i=0;i<5;i++)
	{
		i_ret = hid_to_i2c(client);
		if(i_ret == 1)
		{
			printk("sucesss hid_to_i2c 1");
			break;
		}
		else
			pr_err("hid_to_i2c fail ! \n");
	}
	if (i >= 5 )
		return -EIO;
	fts_get_upgrade_info(&upgradeinfo);
	for (i = 0; i < FTS_UPGRADE_LOOP; i++) {
		/*********Step 1:Reset  CTPM *****/
		ft5x0x_write_reg(0xfc, FT_UPGRADE_AA);
		msleep(upgradeinfo.delay_aa);
		ft5x0x_write_reg(0xfc, FT_UPGRADE_55);
		msleep(200);
		/*********Step 2:Enter upgrade mode *****/
		i_ret = hid_to_i2c(client);

		if(i_ret == 0)
		{
			pr_err("hid_to_i2c fail ! \n");
			continue;
		}
		msleep(10);
		auc_i2c_write_buf[0] = FT_UPGRADE_55;
		auc_i2c_write_buf[1] = FT_UPGRADE_AA;
		i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 2);
		if(i_ret < 0)
		{
			pr_err("failed writing  0x55 and 0xaa ! \n");
			continue;
		}

		/*********Step 3:check READ-ID***********************/
		msleep(1);
		auc_i2c_write_buf[0] = 0x90;
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] = 0x00;

		reg_val[0] = reg_val[1] = 0x00;

		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);

		if (reg_val[0] == upgradeinfo.upgrade_id_1
			&& reg_val[1] == upgradeinfo.upgrade_id_2) {
				pr_err("[FTS] Step 3: READ OK CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0], reg_val[1]);
				break;
		} else {
			dev_err(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0], reg_val[1]);
			continue;
		}
	}
	if (i >= FTS_UPGRADE_LOOP )
		return -EIO;

	/*Step 4:erase app and panel paramenter area*/
	pr_err("Step 4:erase app and panel paramenter area\n");
	auc_i2c_write_buf[0] = 0x61;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);     //erase app area 
	msleep(1650);

	for(i = 0;i < 25;i++)
	{
		auc_i2c_write_buf[0] = 0x6a;
		reg_val[0] = reg_val[1] = 0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

		if(0xF0==reg_val[0] && 0xAA==reg_val[1])
		{
			break;
		}
		msleep(50);
	}

       //write bin file length to FW bootloader.
	auc_i2c_write_buf[0] = 0xB0;
	auc_i2c_write_buf[1] = (u8) ((dw_lenth >> 16) & 0xFF);
	auc_i2c_write_buf[2] = (u8) ((dw_lenth >> 8) & 0xFF);
	auc_i2c_write_buf[3] = (u8) (dw_lenth & 0xFF);

	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 4);

	/*********Step 5:write firmware(FW) to ctpm flash*********/
	bt_ecc = 0;
	pr_err("Step 5:write firmware(FW) to ctpm flash\n");

	//dw_lenth = dw_lenth - 8;
	temp = 0;
	//packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
	packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
	packet_buf[0] = 0xbf;
	packet_buf[1] = 0x00;

	pr_err("focaltech dw_lenth:%d,FTS_PACKET_LENGTH:%d,packet_number:%d\n",dw_lenth,FTS_PACKET_LENGTH,packet_number);
	for (j = 0; j < packet_number; j++) {
		temp = j * FTS_PACKET_LENGTH;
		packet_buf[2] = (u8) (temp >> 8);
		packet_buf[3] = (u8) temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (u8) (lenght >> 8);
		packet_buf[5] = (u8) lenght;
	
		for (i = 0; i < FTS_PACKET_LENGTH; i++) {
			packet_buf[6 + i] = pbt_buf[j * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}
		i_ret = ftxxxx_i2c_Write(client, packet_buf, FTS_PACKET_LENGTH + 6);
		//msleep(20);
		//pr_err("focaltech_i2c_Write result %x\n",i_ret);
		
		for(i = 0;i < 30;i++)
		{
			auc_i2c_write_buf[0] = 0x6a;
			reg_val[0] = reg_val[1] = 0x00;
			i_ret =ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);
			//pr_err("focaltech_i2c_Write result %x\n",i_ret);
			if ((j + 0x1000) == (((reg_val[0]) << 8) | reg_val[1]))
			{
				break;
			}
			msleep(2);
		}
		//pr_err("focaltech j= %d,i=%d \n",j,i);
		if (i == 30)
		{
			pr_err("focaltech_packet_number j + 0x1000 = %d,reg_val=%d \n",(j + 0x1000),((reg_val[0]) << 8) | reg_val[1]);
			break;
		}
		
	}
	//pr_err("focaltech_packet_number %x\n",j);
	if ((dw_lenth) % FTS_PACKET_LENGTH > 0) {
		temp = packet_number * FTS_PACKET_LENGTH;
		packet_buf[2] = (u8) (temp >> 8);
		packet_buf[3] = (u8) temp;
		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (u8) (temp >> 8);
		packet_buf[5] = (u8) temp;

		for (i = 0; i < temp; i++) {
			packet_buf[6 + i] = pbt_buf[packet_number * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}        
		ftxxxx_i2c_Write(client, packet_buf, temp + 6);

		for(i = 0;i < 30;i++)
		{
			auc_i2c_write_buf[0] = 0x6a;
			reg_val[0] = reg_val[1] = 0x00;
			ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

			if ((j + 0x1000) == (((reg_val[0]) << 8) | reg_val[1]))
			{
				break;
			}
			msleep(1);
		}
	}

	msleep(50);

	/*********Step 6: read out checksum***********************/
	/*send the opration head */
	pr_err("Step 6: read out checksum\n");
	auc_i2c_write_buf[0] = 0x64;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1); 
	msleep(300);

	temp = 0;
	auc_i2c_write_buf[0] = 0x65;
	auc_i2c_write_buf[1] = (u8)(temp >> 16);
	auc_i2c_write_buf[2] = (u8)(temp >> 8);
	auc_i2c_write_buf[3] = (u8)(temp);
	temp = dw_lenth;
	auc_i2c_write_buf[4] = (u8)(temp >> 8);
	auc_i2c_write_buf[5] = (u8)(temp);
	i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 6); 
	msleep(dw_lenth/256);

	for(i = 0;i < 100;i++)
	{
		auc_i2c_write_buf[0] = 0x6a;
		reg_val[0] = reg_val[1] = 0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

		if (0xF0==reg_val[0] && 0x55==reg_val[1])
		{
			break;
		}
		msleep(1);

	}

	auc_i2c_write_buf[0] = 0x66;
	ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 1);
	if (reg_val[0] != bt_ecc) 
	{
		dev_err(&client->dev, "[FTS]--ecc error! FW=%02x bt_ecc=%02x\n",
			reg_val[0],
			bt_ecc);

		return -EIO;
	}
	printk(KERN_WARNING "checksum %X %X \n",reg_val[0],bt_ecc);       
	/*********Step 7: reset the new FW***********************/
	pr_err("Step 7: reset the new FW\n");
	auc_i2c_write_buf[0] = 0x07;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(200);   //make sure CTP startup normally 
	i_ret = hid_to_i2c(client);//Android to Std i2c.
	if(i_ret == 0)
	{
		pr_err("hid_to_i2c fail ! \n");
	}
	return 0;
}


/************************************************************************
* Name: fts_ctpm_fw_upgrade_ReadVendorID
* Brief:  read vendor ID
* Input: i2c client, vendor ID
* Output: no
* Return: fail <0
***********************************************************************/
static int fts_ctpm_fw_upgrade_ReadVendorID(struct i2c_client *client, u8 *ucPVendorID)
{
	u8 reg_val[4] = {0};
	u32 i = 0;
	u8 auc_i2c_write_buf[10];
	int i_ret;

	*ucPVendorID = 0;
	i_ret = hid_to_i2c(client);
	if (i_ret == 0) 
	{
		printk("%s HidI2c change to StdI2c fail ! \n",__func__);
		return -EIO;
	}
	fts_get_upgrade_info(&upgradeinfo);
	for (i = 0; i < FTS_UPGRADE_LOOP; i++) 
	{
		/*********Step 1:Reset  CTPM *****/
		ft5x0x_write_reg(0xfc, FT_UPGRADE_AA);
		msleep(upgradeinfo.delay_aa);
		ft5x0x_write_reg(0xfc, FT_UPGRADE_55);
		msleep(200);
		/*********Step 2:Enter upgrade mode *****/
		i_ret = hid_to_i2c(client);
		if (i_ret == 0) 
		{
			printk("%s HidI2c change to StdI2c fail ! \n",__func__);
			continue;
		}
		msleep(10);
		auc_i2c_write_buf[0] = FT_UPGRADE_55;
		auc_i2c_write_buf[1] = FT_UPGRADE_AA;
		i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 2);
		if (i_ret < 0) {
			printk("%s failed writing  0x55 and 0xaa ! \n",__func__);
			continue;
		}
		/*********Step 3:check READ-ID***********************/
		msleep(10);
		auc_i2c_write_buf[0] = 0x90;
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] = 0x00;
		reg_val[0] = reg_val[1] = 0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);
		if (reg_val[0] == upgradeinfo.upgrade_id_1 && reg_val[1] == upgradeinfo.upgrade_id_2){
			printk("[FTS] Step 3: %s READ OK CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",__func__, reg_val[0], reg_val[1]);
			break;
		} 
		else 
		{
			dev_err(&client->dev, "[FTS] Step 3:%s CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",__func__, reg_val[0], reg_val[1]);
			continue;
		}
	}
	if (i >= FTS_UPGRADE_LOOP)
		return -EIO;
	/*********Step 4: read vendor id from app param area***********************/
	msleep(10);
	auc_i2c_write_buf[0] = 0x03;
	auc_i2c_write_buf[1] = 0x00;
	auc_i2c_write_buf[2] = 0xd7;
	auc_i2c_write_buf[3] = 0x84;
	for (i = 0; i < FTS_UPGRADE_LOOP; i++) 
	{
		ftxxxx_i2c_Write(client, auc_i2c_write_buf, 4);		
		msleep(5);
		reg_val[0] = reg_val[1] = 0x00;
		i_ret = ftxxxx_i2c_Read(client, auc_i2c_write_buf, 0, reg_val, 2);
		if (0 == reg_val[0]) 
		{
			*ucPVendorID = 0;
			printk("In upgrade Vendor ID Mismatch, REG1 = 0x%x, REG2 = 0x%x, Definition:0x%x, i_ret=%d\n", reg_val[0], reg_val[1], 0, i_ret);
		} 
		else 
		{
			*ucPVendorID = reg_val[0];
			printk("In upgrade Vendor ID,%s REG1 = 0x%x, REG2 = 0x%x\n",__func__, reg_val[0], reg_val[1]);
			break;
		}
	}
	msleep(50);
	/*********Step 5: reset the new FW***********************/
	printk("Step 5:%s reset the new FW\n",__func__);
	auc_i2c_write_buf[0] = 0x07;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(200);	
	i_ret = hid_to_i2c(client);	
	if (i_ret == 0) 
	{
		printk("%s HidI2c change to StdI2c fail ! \n",__func__);
	}
	msleep(10);
	return 0;
}



/************************************************************************
* Name: fts_ctpm_fw_upgrade_ReadTpStruct
* Brief:  read vendor ID
* Input: i2c client, vendor ID
* Output: no
* Return: fail <0
***********************************************************************/
static int fts_ctpm_fw_upgrade_ReadTpStruct(struct i2c_client *client, u8 *TpStuct)
{
	u8 reg_val[4] = {0};
	u32 i = 0;
	u8 auc_i2c_write_buf[10];
	int i_ret;

	*TpStuct= 0;
	i_ret = hid_to_i2c(client);
	if (i_ret == 0) 
	{
		printk("%s HidI2c change to StdI2c fail ! \n",__func__);
		return -EIO;
	}
	//fts_get_upgrade_info(&upgradeinfo);
	for (i = 0; i < FTS_UPGRADE_LOOP; i++) 
	{
		/*********Step 1:Reset  CTPM *****/
		ft5x0x_write_reg(0xfc, FT_UPGRADE_AA);
		msleep(upgradeinfo.delay_aa);
		ft5x0x_write_reg(0xfc, FT_UPGRADE_55);
		msleep(200);
		/*********Step 2:Enter upgrade mode *****/
		i_ret = hid_to_i2c(client);
		if (i_ret == 0) 
		{
			printk("%s HidI2c change to StdI2c fail ! \n",__func__);
			continue;
		}
		msleep(10);
		auc_i2c_write_buf[0] = FT_UPGRADE_55;
		auc_i2c_write_buf[1] = FT_UPGRADE_AA;
		i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 2);
		if (i_ret < 0) {
			printk("%s failed writing  0x55 and 0xaa ! \n",__func__);
			continue;
		}
		/*********Step 3:check READ-ID***********************/
		msleep(10);
		auc_i2c_write_buf[0] = 0x90;
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] = 0x00;
		reg_val[0] = reg_val[1] = 0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);
		if (reg_val[0] == upgradeinfo.upgrade_id_1 && reg_val[1] == upgradeinfo.upgrade_id_2){
			printk("[FTS] Step 3: %s READ OK CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",__func__, reg_val[0], reg_val[1]);
			break;
		} 
		else 
		{
			dev_err(&client->dev, "[FTS] Step 3:%s CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",__func__, reg_val[0], reg_val[1]);
			continue;
		}
	}
	if (i >= FTS_UPGRADE_LOOP)
		return -EIO;
	/*********Step 4: read vendor id from app param area***********************/
	msleep(10);
	auc_i2c_write_buf[0] = 0x03;
	auc_i2c_write_buf[1] = 0x00;
	auc_i2c_write_buf[2] = 0xd7;
	auc_i2c_write_buf[3] = 0xc2;
	for (i = 0; i < FTS_UPGRADE_LOOP; i++) 
	{
		ftxxxx_i2c_Write(client, auc_i2c_write_buf, 4);		
		msleep(5);
		reg_val[0] = reg_val[1] = 0x00;
		i_ret = ftxxxx_i2c_Read(client, auc_i2c_write_buf, 0, reg_val, 2);
		if (0 == reg_val[0]) 
		{
			*TpStuct = 0;
			printk("In upgrade Vendor ID Mismatch, REG1 = 0x%x, REG2 = 0x%x, Definition:0x%x, i_ret=%d\n", reg_val[0], reg_val[1], 0, i_ret);
		} 
		else 
		{
			*TpStuct = reg_val[1];
			printk("In upgrade Vendor ID,%s REG1 = 0x%x, REG2 = 0x%x\n",__func__, reg_val[0], reg_val[1]);
			break;
		}
	}
	msleep(50);
	/*********Step 5: reset the new FW***********************/
	printk("Step 5:%s reset the new FW\n",__func__);
	auc_i2c_write_buf[0] = 0x07;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(200);	
	i_ret = hid_to_i2c(client);	
	if (i_ret == 0) 
	{
		printk("%s HidI2c change to StdI2c fail ! \n",__func__);
	}
	msleep(10);
	return 0;
}

static int fts_ctpm_chk_upg(void)
{
	unsigned int ui_sz;
	unsigned char uc_if_ver;
	unsigned char uc_fw_ver;
	unsigned char check_byte1;
	unsigned char check_byte2;
	
	printk("enter %s\n",__FUNCTION__);

	if (!current_tp_supported)
		return 0;

	uc_fw_ver = ft5x0x_read_fw_ver();
	printk("[FT]:%s,host  firmware version = %x\n",__func__,uc_fw_ver);
	if (ui_sz > 2)
		uc_if_ver = current_fw_to_tp[ui_sz - 2];
	else
		uc_if_ver = 0x00;

	ft5x0x_read_reg(0xA3, &check_byte1);
	ft5x0x_read_reg(0x9F, &check_byte2);
	if(check_byte1!=0x54||check_byte2!=0x22)
	{
		printk("reg0xA3!=0x54 or 0x9F!=0x22\n");
		return 1;
	}

	if( uc_fw_ver < uc_if_ver)
	{
		printk("host version is smaller.\n");
		return 1;
	}
		
	else
		return 0;
 }

#else

static void ft5x0x_reset(int ms)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);//tanliyu add
	if(data->pdata->reset)
		data->pdata->reset(ms);
}

E_UPGRADE_ERR_TYPE  fts_ctpm_fw_upgrade(FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    FTS_BYTE reg_val[2] = {0};
    FTS_DWRD i = 0;
    FTS_BYTE is_5336_new_bootloader = 0;
    FTS_DWRD try_count = 0;
    FTS_DWRD  packet_number;
    FTS_DWRD  j;
    FTS_DWRD  temp;
    FTS_DWRD  lenght;
    FTS_BYTE  packet_buf[FTS_PACKET_LENGTH + 6];
    FTS_BYTE  auc_i2c_write_buf[10];
    FTS_BYTE bt_ecc;
    int      i_ret;

retryupdate:
    /*********Step 1:Reset  CTPM *****/
/* yulong modify */
/* author: tanliyu*/
/* time: 2012.10.20 */

/*write 0xaa to register 0xfc*/
/* soft reset */
#if 1
    ft5x0x_write_reg(0xfc,0xaa);
    msleep(DELAY_AA);
     /*write 0x55 to register 0xfc*/
    ft5x0x_write_reg(0xfc,0x55);
    printk("[FTS] Step 1: Reset CTPM test\n");

    msleep(DELAY_55);
#endif

#if 0
/* hardware reset */
    ft5x0x_reset(35);
/* yulong end */
#endif

    /*********Step 2:Enter upgrade mode *****/
    auc_i2c_write_buf[0] = 0x55;
    auc_i2c_write_buf[1] = 0xaa;
    do
    {
        i ++;
        i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
        msleep(5);
    }while(i_ret <= 0 && i < 5 );

    msleep(DELAY_ID);

    /*********Step 3:check READ-ID***********************/
    cmd_write(0x90,0x00,0x00,0x00,4);
    byte_read(reg_val,2);
    if (reg_val[0] == 0x79 && reg_val[1] == CTPM_ID2)
    {
        printk(KERN_INFO "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
    }
    else
    {
	printk(KERN_ERR "[FTS] Check ctpm id failed, need try again. CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
       while (try_count < 20)
       {
	try_count ++;
         goto retryupdate;
       }

	ft5x0x_reset(300);
	return ERR_READID;
      //i_is_new_protocol = 1;
    }

    cmd_write(0xcd,0x0,0x00,0x00,1);
    byte_read(reg_val,1);
/**litao3 modify ft5x06 serises chip's bootloader id must be 0*/
    if (reg_val[0] > 4)
	is_5336_new_bootloader = 0;
    printk(KERN_INFO "[FTS] bootloader version = 0x%x\n", reg_val[0]);

     /*********Step 4:erase app and panel paramenter area ********************/
    cmd_write(0x61,0x00,0x00,0x00,1);  //erase app area
    msleep(DELAY_EARSE);
    printk(KERN_DEBUG "[FTS] Step 4: erase. \n");

    /*********Step 5:write firmware(FW) to ctpm flash*********/
    bt_ecc = 0;
    printk(KERN_DEBUG "[FTS] Step 5: start upgrade. \n");
    dw_lenth = dw_lenth - 8;
    packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
    packet_buf[0] = 0xbf;
    packet_buf[1] = 0x00;
    for (j=0;j<packet_number;j++)
    {
        temp = j * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        lenght = FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(lenght>>8);
        packet_buf[5] = (FTS_BYTE)lenght;

        for (i=0;i<FTS_PACKET_LENGTH;i++)
        {
            packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i];
            bt_ecc ^= packet_buf[6+i];
        }

        byte_write(&packet_buf[0],FTS_PACKET_LENGTH + 6);
        delay_qt_ms(FTS_PACKET_LENGTH/6 + 1);
        if ((j * FTS_PACKET_LENGTH % 1024) == 0)
        {
              printk(KERN_INFO "[FTS] upgrade the 0x%x th byte.\n", ((unsigned int)j) * FTS_PACKET_LENGTH);
        }
    }

    if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
    {
        temp = packet_number * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;

        temp = (dw_lenth) % FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;

        for (i=0;i<temp;i++)
        {
            packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i];
            bt_ecc ^= packet_buf[6+i];
        }

        byte_write(&packet_buf[0],temp+6);
        delay_qt_ms(20);
    }

    //send the last six byte
    for (i = 0; i<6; i++)
    {
	if (is_5336_new_bootloader && DEVICE_IC_TYPE == IC_FT5X06)
		temp = 0x7bfa + i;
	else
		temp = 0x6ffa + i;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        temp =1;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;
        packet_buf[6] = pbt_buf[ dw_lenth + i];
        bt_ecc ^= packet_buf[6];

        byte_write(&packet_buf[0],7);
        delay_qt_ms(20);
    }

    /*********Step 7: reset the new FW***********************/
    cmd_write(0x07,0x00,0x00,0x00,1);

    msleep(300);  //make sure CTP startup normally

    return ERR_OK;
}


#if	FOCALTECH_COB
int fts_cob_ctpm_fw_upgrade_with_i_file(void)
{
	int ret = 0;
	//=========FW upgrade========================*/
	ret = fts_ctpm_fw_upgrade(current_fw_to_tp, current_fw_size);
	if (ret != 0)
		printk("[FTS] upgrade %s failed ret = %d.\n", current_tp_name, ret);
	else
	{
		printk("[FTS] upgrade %s successfully.\n", current_tp_name);
	}
	return ret;
}

/***********************************************************************************************
function	:	read tp calibrate state.
			0,  calibrated
			1,  none calibrated.
***********************************************************************************************/
static unsigned int ft5x0x_read_tp_calibrated(void)
{
	FTS_BYTE reg_val[2] = {0};
	FTS_BYTE  auc_i2c_write_buf[10];
	FTS_DWRD i = 0;
	int	i_ret = 0;
	int	ret=0;
	int try_count=0;

	//struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);

	/*********Step 1:Reset  CTPM *****/
READID:

#if 1
    ft5x0x_write_reg(0xfc,0xaa);
    msleep(DELAY_AA);
     /*write 0x55 to register 0xfc*/
    ft5x0x_write_reg(0xfc,0x55);
    printk("[FTS] Step 1: Reset CTPM test\n");

    msleep(DELAY_55);
#else
    /* hardware reset */
    ft5x0x_reset(35);
#endif

    /*********Step 2:Enter upgrade mode *****/
    auc_i2c_write_buf[0] = 0x55;
    auc_i2c_write_buf[1] = 0xaa;
    do
    {
        i ++;
        i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
        msleep(5);
    }while(i_ret <= 0 && i < 5 );

    msleep(DELAY_ID);

    /*********Step 3:check READ-ID***********************/
    cmd_write(0x90,0x00,0x00,0x00,4);
    byte_read(reg_val,2);
    if (reg_val[0] == 0x79 && reg_val[1] == CTPM_ID2)
    {
        printk(KERN_INFO "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
    }
    else
    {
	printk(KERN_ERR "[FTS] Check ctpm id failed, need try again. CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
       while (try_count < 20)
       {
	try_count ++;
         goto READID;
       }

	ft5x0x_reset(300);
	return ERR_READID;
      //i_is_new_protocol = 1;
    }

	/*********Step 4:read clb area start addr:0x7C00***********************/
	cmd_write(0x03,0x00,0x7c,0x00,4);
	printk("cmd_write\n");
	mdelay(5);
	i_ret = byte_read(reg_val,1);
	if (i_ret == 0)
	{
		printk( "check clb failed!\r\n");
		return 1;
	}

	printk("the value reg_val[0]=0x%x \n",reg_val[0]);
	if(0xFF == reg_val[0])
	{
		printk("None calibration.\n");
		ret = 1;
	}
	else
	{
		printk("Calibrated. \n");
		ret = 0;
	}
	printk("Step 4:read clb area start addr:0x7C00\n");

	/**********Step 5:reset FW to return work mode**************/
	cmd_write(0x07,0x00,0x00,0x00,1);

	msleep(300);

	printk("[FTS]: fts_ctpm_check_calibration over.\n");

	return ret;
}

#endif

#endif 

#if	FOCALTECH_COB
static int ft5x0x_read_tp_type(void)
{
	unsigned char type, pin_status;
	int i;

	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	if(data->pdata->get_id_pin)
		pin_status = data->pdata->get_id_pin();
	else
	{
	    	return (-1);
	}

	for(i=0; i < ARRAY_SIZE(PIN_ID); i++)
	{
		if(pin_status == PIN_ID[i].pin)
		{
			type = PIN_ID[i].id;
			printk("ft5x0x_read_tp_type cob: 0x%x",type);
	    		return (type);
		}
	}

	return (-1);
}

#else
static int ft5x0x_read_tp_type(void)
{

    unsigned char type;
    int ret;
    type=0;
    ret=ft5x0x_read_reg(0xA8, &type);
    if(ret < 0) return -1;
#if defined(CONFIG_BOARD_K2_01)||defined(CONFIG_BOARD_K2_02)
	if(type!=0xE8&&type!=0x8D)
	{
		if(0==strcmp("LCD_TYPE_FT8606_BOYI_AUO_HD_50",lcd_name))
			type=0xE8;
		else if((0==strcmp("LCD_TYPE_FT8608_TIANMA_HD_50",lcd_name))||(0==strcmp("LCD_TYPE_FT8608_TIANMA2_HD_50",lcd_name)))
			type=0x8D;
	}
#elif defined(CONFIG_BOARD_SK3_01)||defined(CONFIG_BOARD_SK3_02)||defined(CONFIG_BOARD_SK3_01_CT)
	if(type!=0x3B&&type!=0xA0)
		fts_ctpm_fw_upgrade_ReadVendorID(this_client, &type);
#endif
    return (type);
}
#endif


void fts_get_current_tp_info(int num)
{
	int index=num;
#if defined(CONFIG_BOARD_SK3_01)\
	||defined(CONFIG_BOARD_SK3_01_CT)\
	||defined(CONFIG_BOARD_SK3_02)
	u8 buf;
	if(current_tp_id==0xA0) //shenyue have two diff structure
	{
		ft5x0x_read_reg(0xA0,&buf);
	
		if(0x0C==buf)//0x0Cis GF structure
		{
			index++;
		}
	}
	else if(current_tp_id==0x3B) //BOEN have two diff structure
	{
		ft5x0x_read_reg(0xA0,&buf);
		if(0x0C==buf)//0x0C is BOEN  GF structure
		{
			index++;
		}
	}
	if(0x0c!=buf&&0x0a!=buf)
	{
		fts_ctpm_fw_upgrade_ReadTpStruct(this_client, &buf);
		if(0x43==buf)//0x0Cis GF structure
		{
			index++;
		}
	}	
#endif
	current_tp_supported = true;
	current_tp_name  = tp_info[index].tp_name;
	current_fw_to_tp = tp_info[index].firmware;
	current_fw_size  = tp_info[index].firmware_size;
}

int fts_ctpm_auto_clb(void)
{
#if defined(FT8606) || defined(FT5X46)
	printk("[FTS] FT8606 and FT5X46 IC will auto CLB,don't need it.\n");
	return 0;
#else
    unsigned char uc_temp;
    unsigned char i ;

    printk("[FTS] start auto CLB.\n");
    msleep(200);
    ft5x0x_write_reg(0, 0x40);
    delay_qt_ms(100);   //make sure already enter factory mode
    ft5x0x_write_reg(2, 0x4);  //write command to start calibration
    delay_qt_ms(300);
    if (DEVICE_IC_TYPE == IC_FT5X06) {
	for(i=0;i<100;i++)
	{
		ft5x0x_read_reg(0x02, &uc_temp);
		if (0x02 == uc_temp || 0xFF == uc_temp)
			{
				/*if 0x02, then auto clb ok, else 0xff, auto clb failure*/
			    break;
			}
			delay_qt_ms(20);
		}
    }
    else {
		for(i=0;i<100;i++)
		{
			ft5x0x_read_reg(0, &uc_temp);
			if (0x0 == ((uc_temp&0x70)>>4))  /*return to normal mode, calibration finish*/
			{
			    break;
			}
			delay_qt_ms(20);
		}
    }
    printk("[FTS] calibration OK.\n");

    msleep(300);
    ft5x0x_write_reg(0, 0x40);  //goto factory mode
    delay_qt_ms(200);   //make sure already enter factory mode
    ft5x0x_write_reg(2, 0x5);  //store CLB result
    delay_qt_ms(300);
    ft5x0x_write_reg(0, 0x0); //return to normal mode
    msleep(300);
    printk("[FTS] store CLB result OK.\n");
    return 0;
#endif
}


int fts_ctpm_fw_upgrade_with_i_file(void)
{
	int ret = -1;
	//=========FW upgrade========================*/
	if(0==current_fw_size)
		return ret;
	#ifdef FT5X46
	ret = fts_ctpm_fw_upgrade(this_client,current_fw_to_tp, current_fw_size);
	#else
	ret = fts_ctpm_fw_upgrade(current_fw_to_tp, current_fw_size);
	#endif
	if (ret != 0)
		printk("[FTS] upgrade %s failed ret = %d.\n", current_tp_name, ret);
	else
	{
		printk("[FTS] upgrade %s successfully.\n", current_tp_name);
		fts_ctpm_auto_clb();  //start auto CLB
	}
	return ret;
}

/* force update firmware */
int fts_ctpm_fw_upgrade_with_tp_id(unsigned char tp_id)
{
	   int ret = 0;
	   int num = fts_check_tp_id(tp_id);
	   if (num < 0)
	   	   return -1;
	   fts_get_current_tp_info(num);
	   ret = fts_ctpm_fw_upgrade_with_i_file();
	   if (ret == 0)
	      current_tp_id = tp_id; /*use to check, the id is right*/

          return ret;

}

unsigned char fts_ctpm_get_i_file_ver(unsigned char tp_id)
{
	unsigned char uc_if_ver;
	unsigned int ui_sz;
	int num = fts_check_tp_id(tp_id);
	if (num < 0)
		return -1;
	fts_get_current_tp_info(num);
	ui_sz = current_fw_size;
	if (ui_sz > 2)
		uc_if_ver = current_fw_to_tp[ui_sz - 2];
	else
		uc_if_ver = 0xff;

	return uc_if_ver;
}
#define    FTS_SETTING_BUF_LEN        128

#if  CFG_SUPPORT_TOUCH_KEY
 /* yulong add */
 /* reason : key debounce */
 /* author : tanliyu */
 /* time : 2012-10-04 */
static void ft_keys_timer(unsigned long _ft_data)
{
	   	inDebounce = false;
	   	TW_DBG("TOUCH:YLLOG:timer inDebounce false\n");
}


/******************************/
int fts_ctpm_chk_upg_probe(void)
{
	unsigned char uc_tp_fm_ver=0;
	unsigned char IC_tp_id=0;
	unsigned char uc_if_ver=0;
	unsigned int ui_sz=0;
	printk("**enter in %s **\n",__func__);

	if(!current_tp_id)
	{
		printk("[FTS]: current_tp_id is null,Get tp vid error!\n");
		return 1;
	}
	printk("[fts]:tp_vendor_id = %x.\n",current_tp_id);
	IC_tp_id=ft5x0x_read_tp_type();
	// check tp id
	if(IC_tp_id== 0xa8)
	{
		printk("[FTS]: current_tp_id is 0xa8,Get tp vid error!\n");
		return 1;
	}
	printk("[fts]:IC_tp_id = %x.\n",IC_tp_id);
	if(IC_tp_id!=current_tp_id)
	{
		printk("[FTS]: current_tp_id and IC_tp_id is not coherent!\n");
		return 1;

	}
	else{

	uc_tp_fm_ver = ft5x0x_read_fw_ver();

	ui_sz = current_fw_size;
	if (ui_sz > 2)
		uc_if_ver = current_fw_to_tp[ui_sz - 2];


	printk("[fts]:IC_fm_ver = 0x%02x,I_file_fw_ver= 0x%02x\n",  uc_tp_fm_ver,uc_if_ver);

	// < 0x0a = original version > default version 0x0a
	if ( uc_tp_fm_ver == 0xa6  ||  uc_tp_fm_ver  < 0x10 || uc_tp_fm_ver < uc_if_ver)//the firmware in host flash is new, need upgrade//add for auto update
	//if ( uc_tp_fm_ver == 0xa6  ||  uc_tp_fm_ver  < 0x10)
		return 1;
	else
		return 0;
	}
}


int fts_ctpm_auto_upg(void)
{
	unsigned char uc_host_fm_ver;
	int           i_ret = 0;
	unsigned char i;

	/*try to update firmware if fails*/
	for(i=0;i<1;i++)
	{
		// get tp_id
//		vid_TP = ft5x06_read_vendor_id_TP();
	//	mdelay(100);
		i_ret = fts_ctpm_fw_upgrade_with_i_file();
		if (i_ret == 0)
		{
			uc_host_fm_ver = ft5x0x_read_fw_ver();
			printk("[FTS] upgrade to new version 0x%x\n", uc_host_fm_ver);
			return 0;
		}
		mdelay(5);
	}
	printk("[FTS] upgrade failed ret=%d.\n", i_ret);
	return (-1);
}
/***********************************/
static void key_release(int index)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);//tanliyu add
	int i = index;

	key_flags = 0;
	data->pdata->buttons[i].key_status = KEY_RELEASE;
	input_report_key(data->input_dev, data->pdata->buttons[i].code, 0);
	input_sync(data->input_dev);
//     printk("maleijie:key_release %d\n",data->pdata->buttons[i].code);//changed by taokai
	/* yulong add */
	/* reason : key debounce */
	/* author : tanliyu */
	/* time : 2012-10-04 */
	if (DEBOUNCE)
		mod_timer(&data->timer, jiffies + msecs_to_jiffies(DEBOUNCE));
	/* yulong end */

}

static void key_press(int index)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);//tanliyu add
	int i = index;
// printk("maleijie:key_press\n");//changed by taokai
	/* yulong modify */
	/* reason : key debounce */
	/* author : tanliyu */
	/* time : 2012-10-04 */
	if (DEBOUNCE && inDebounce)
	{
		TW_DBG("TOUCH:YLLOG:indebouce should return %d\n", i);
	}else{
		TW_DBG("TOUCH:YLLOG:indebouce changes to true%d\n", i);
		key_flags = i + 1;
	// 	printk("maleijie:YLLOG: data->pdata->buttons %d,%d\n", i,data->pdata->buttons[i].code);//changed by taokai
		data->pdata->buttons[i].key_status = KEY_PRESS;
		input_report_key(data->input_dev, data->pdata->buttons[i].code, 1);
		input_sync(data->input_dev);
		if(DEBOUNCE)
			inDebounce = true;//tanliyu add
	}

}


/* yulong end */
#endif


/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/
static void ft5x0x_ts_release(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
#if  CFG_SUPPORT_TOUCH_KEY
    if (key_flags != 0)
	key_release(key_flags-1);
#endif
//Modified by taokai
	if (touch_flag) {
		touch_flag = 0;
    input_mt_sync(data->input_dev);
    input_sync(data->input_dev);

	}
}

#ifdef CONFIG_TOUCHSCREEN_GESTURE_WAKEUP
static void check_gesture(int gesture_id)
{
	struct device *touchscreen_dev;
	int i;
	char **envp;
	for (i = 0; i < ARRAY_SIZE(tp_slide_wakeup); i++) {
		if ((gesture_id == tp_slide_wakeup[i].code) && (support_gesture&tp_slide_wakeup[i].mask)) {
			TW_DBG("Slide(0x%x) To Light up the screen!", gesture_id);
			sprintf(wakeup_slide,tp_slide_wakeup[i].gesture);
			envp = tp_slide_wakeup[i].evp;
			touchscreen_dev = touchscreen_get_dev();
			kobject_uevent_env(&touchscreen_dev->kobj, KOBJ_CHANGE, envp);
			printk("send < %s slide wakeup > kobject uevent!", wakeup_slide);
			break;
		}
	}
}
static int fts_read_Gesturedata(void)
{
	unsigned char buf[FTS_GESTURE_POINTS * 3] = { 0 };
	int ret = -1;
	//int i = 0;
	int gesture_id = 0;
	//int pointnum = 0;

	buf[0] = 0xd3;
	ret = ftxxxx_i2c_Read(this_client, buf, 1, buf, FTS_GESTURE_POINTS_HEADER);
	TW_DBG( "tpd read FTS_GESTURE_POINTS_HEADER.buf[0]=%d\n",buf[0]);// change by zdd
	if (ret < 0) {
		TW_DBG( "%s read touchdata failed.\n", __func__);
		return ret;
	}

	if (GESTURE_DOUBLECLICK == buf[0]) {
		gesture_id = GESTURE_DOUBLECLICK;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_LEFT == buf[0]) {
		gesture_id = GESTURE_LEFT;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_RIGHT == buf[0]) {
		gesture_id = GESTURE_RIGHT;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_UP == buf[0]) {
		gesture_id = GESTURE_UP;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_DOWN == buf[0]) {
		gesture_id = GESTURE_DOWN;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_O == buf[0]) {
		gesture_id = GESTURE_O;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_W == buf[0]) {
		gesture_id = GESTURE_W;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_M == buf[0]) {
		gesture_id = GESTURE_M;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_E == buf[0]) {
		gesture_id = GESTURE_E;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
		if (GESTURE_C == buf[0]) {
		gesture_id = GESTURE_C;
		check_gesture(gesture_id);
		TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
		return 0;
	}
#if	0	//don't need static library
 
	pointnum = (short)(buf[1]) & 0xff;
	buf[0] = 0xd3;
	if ((pointnum * 4 + 8) < 255) {
		ret = ftxxxx_i2c_Read(this_client, buf, 1, buf, (pointnum * 4 + 8));
	} else {
		ret = ftxxxx_i2c_Read(this_client, buf, 1, buf, 255);
		ret = ftxxxx_i2c_Read(this_client, buf, 0, buf+255, (pointnum * 4 + 8) -255);
	}
	if (ret < 0) {
		TW_DBG( "%s read touchdata failed.\n", __func__);
		return ret;
	}
	gesture_id = fetch_object_sample(buf, pointnum);
	TW_DBG( "tpd %d check_gesture gesture_id.\n", gesture_id);
	check_gesture(gesture_id);
	for (i = 0; i < pointnum; i++) {
		coordinate_x[i] =  (((s16) buf[0 + (4 * i)]) & 0x0F) <<
			8 | (((s16) buf[1 + (4 * i)])& 0xFF);
		coordinate_y[i] = (((s16) buf[2 + (4 * i)]) & 0x0F) <<
			8 | (((s16) buf[3 + (4 * i)]) & 0xFF);
	}

#endif
	return 0;
}
#endif
static int ft5x0x_read_data(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
    struct ts_event *event = &data->event;
    u8 buf[CFG_POINT_READ_BUF] = {0};
    int ret = -1;
    int i;

#ifdef CONFIG_TOUCHSCREEN_GESTURE_WAKEUP
	u8 state = 0;
	ft5x0x_read_reg(0xd0, &state);
	TW_DBG("Gesturedata state=%d\n",state);
	if (state == 1) {
		fts_read_Gesturedata();
		return 1;
	}
#endif

    ret = ft5x0x_i2c_rxdata(buf, CFG_POINT_READ_BUF);
    if (ret < 0)
    {
		printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		    return ret;
	  }

    memset(event, 0, sizeof(struct ts_event));
	
	#if 1
	{
		event->touch_point = buf[2];
	}
	#else
	{
	    event->touch_point = buf[2]>>4;
	}
    #endif
	 //event->touch_point = buf[2]&&0x07;
//    printk("ft5x0x_read_data2 touch_point: %d\n",event->touch_point);//maleijie//changed bu taokai
    if (event->touch_point > CFG_MAX_TOUCH_POINTS)
    {
        event->touch_point = CFG_MAX_TOUCH_POINTS;
    }

    for (i = 0; i < event->touch_point; i++)
    {
        event->au16_x[i] = (s16)(buf[3 + 6*i] & 0x0F)<<8 | (s16)buf[4 + 6*i];
        event->au16_y[i] = (s16)(buf[5 + 6*i] & 0x0F)<<8 | (s16)buf[6 + 6*i];
	event->au16_x[i] = (SCREEN_MAX_X - event->au16_x[i])<0? 0:(SCREEN_MAX_X - event->au16_x[i]);
	event->au16_y[i] = (SCREEN_MAX_Y - event->au16_y[i])<0? 0:(SCREEN_MAX_Y - event->au16_y[i]);

        event->au8_touch_event[i] = buf[0x3 + 6*i] >> 6;
        event->au8_finger_id[i] = (buf[5 + 6*i])>>4;
        event->pressure[i]     = buf[7 + 6*i];
        event->touch_size[i]   = buf[8 + 6*i];
        if (event->au16_x[i] > SCREEN_MAX_X || event->au16_y[i] > SCREEN_MAX_Y)
		TW_DBG("TOUCH:YLLOG:Error pointer x = %d y = %d event = %d\n", event->au16_x[i], event->au16_y[i], event->au8_touch_event[i]);


    }
   /*One point and active up, release it*/
    if (event->touch_point == 1)
    {
       if (event->au8_touch_event[0]== 1){
		   if (event->au16_y[0] < SCREEN_MAX_Y){
			     touch_flag = 1;
		   }
           ft5x0x_ts_release();
           return 1;
       }

    }
    //event->pressure = 200;
//	printk("ft5x0x_read_data end..\n");//maleijie    //chenged by taokai
    return 0;
}

/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/


#if CFG_SUPPORT_TOUCH_KEY
int ft5x0x_touch_key_process(struct input_dev *dev, u16 x, u16 y, int touch_event)
{
	int i;
	int nbuttons;
	int nx0, ny0, nx1, ny1;
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);//tanliyu add
	nbuttons = data->pdata->nbuttons;
//  printk("maleijie ft5x0x_touch_key_process :%d,%d\n",x,y);//maleijie  //changed by taokai
	for (i = 0; i < nbuttons; i++){

       nx0 = data->pdata->buttons[i].x;
       ny0 = data->pdata->buttons[i].y;
       nx1 = data->pdata->buttons[i].x + data->pdata->buttons[i].width;
       ny1 = data->pdata->buttons[i].y + data->pdata->buttons[i].height;
//	   printk("maleijie ft5x0x_touch_key_process nbuttons :%d,%d,%d,%d\n",nx0,ny0,nx1,ny1);//maleijie
       if ( x >=nx0 && x <=nx1 && y >=ny0 && y<= ny1){
//	   	printk("maleijie ft5x0x_touch_key_process touch_event :%d,%d,%d\n",x,y,touch_event);//maleijie
	         if (data->pdata->buttons[i].key_status)
		  {
	              if (touch_event == 2)		//this key is contact
	                	break;
	              else if(touch_event == 1)	//this key is up
		  	{
		  		key_release(i);
				break;
	              	}
		      else if (touch_event == 0) //this key is down
			{
				key_release(i);
				key_press(i);
				break;
			}
	         }
		  else
		  {
		  	if(touch_event == 0)  //this key is down
	            	{
				key_press(i);
				break;
			}
	        }
       }
   }

   return 0;
}

#endif

void report_point(struct input_dev *dev, u16 x, u16 y, u16 t, u16 p, u8 id)
{
	input_report_abs(dev, ABS_MT_POSITION_X, x);
	input_report_abs(dev, ABS_MT_POSITION_Y, y);
	input_report_abs(dev, ABS_MT_TOUCH_MAJOR, t);
	input_report_abs(dev, ABS_MT_WIDTH_MAJOR, 1);
	input_report_abs(dev, ABS_MT_PRESSURE, p);
	input_report_abs(dev, ABS_MT_TRACKING_ID, id);
	input_mt_sync(dev);
//	printk/*maleijie TW_DBG*/( "maleijie pointer%d x=%d y=%d\n", id, x, y);   //changed by taokai
}


static void ft5x0x_report_value(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
    struct ts_event *event = &data->event;
    int i;
    int release[CFG_MAX_TOUCH_POINTS] = {0};
    int rel = 1;

#if  CFG_SUPPORT_TOUCH_KEY
    if ((key_flags != 0) && (event->au16_y[0] < SCREEN_MAX_Y))
		key_release(key_flags-1);
#endif

    for (i  = 0; i < event->touch_point; i++)
    {
   // 	printk( KERN_ERR"reprot value: au16_x[i] = %d, au16_y[i] = %d\n",event->au16_x[i],event->au16_y[i]);
		if (event->au16_x[i] < SCREEN_MAX_X)
		{
			if (event->au16_y[i] < (SCREEN_MAX_Y))
			{
		/* finger is in bounds and its status is down or moving */
				if (event->au8_touch_event[i]== 0 || event->au8_touch_event[i] == 2)
				{
					touch_flag = 1;
					if (event->pressure[i] == 0)
						event->pressure[i] = 10;
					report_point	(data->input_dev, event->au16_x[i], event->au16_y[i],
                              event->touch_size[i], event->pressure[i], event->au8_finger_id[i]);
				}
                        else     /* finger's status is up, we should release it */
				release[i] = 1;
			}
				else if (event->au16_y[i] == SCREEN_MAX_Y)
					release[i] = 1;
		        else
				{
				/* finger is not in bounds, we should release it */
					if (event->au8_touch_event[i]== 0 || event->au8_touch_event[i] == 2)
					{
						touch_flag = 1;
                 	}
		            	//printk("maleijie ft5x0x_report_value else :%d\n",i);//maleijie
					if (i != 0)
			           	release[i] = 1;
					else
					{
						#if  CFG_SUPPORT_TOUCH_KEY
		         		      /* here if we check the first finger touch the keyboard area, we should report the key code.
		        		      then if there is other fingers touch AA area, the event would not be reported.*/
							ft5x0x_touch_key_process(data->input_dev, event->au16_x[i], event->au16_y[i],
			      	        event->au8_touch_event[i]);

			              	if (key_flags == 0 && event->au8_touch_event[i] != 0)
			              	{
			      	       	//no key report or key release, event->au8_touch_event[i] != 0 for debouce return
				            	release[i] = 1;
			                }
			              	else
			              	{
					           //have key pressed
					            return;
				            }
		              #else
                      	release[i] = 1;
		              #endif
					}

		          }
		}
	}

    /* check all fingers is released or should release, if so we report release event */
    for (i = 0; i < event->touch_point; i++)
    {
            rel = rel & release[i];
			//printk("maleijie ft5x0x_report_value for :%d,%d\n",i,release[i]);//maleijie
    }
    if (rel)
    {
        ft5x0x_ts_release();
        return;
    }
    else
	input_sync(data->input_dev);

}


/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/
static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
    int ret = -1;
    ret = ft5x0x_read_data();
  //  	printk(KERN_ERR"maleijie ft5x0x_ts_pen_irq_work(ret=%d)\n",ret);//maleijie
    if (ret == 0)
    {
        ft5x0x_report_value();
    }
    enable_irq(this_client->irq);

}
/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/
static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
    struct ft5x0x_ts_data *ft5x0x_ts = dev_id;
    disable_irq_nosync(this_client->irq);
//    printk(KERN_ERR"xuliang:int.....\n");
    if (!work_pending(&ft5x0x_ts->pen_event_work))
    {
        queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
    }
    else
    {
	enable_irq(this_client->irq);
    }
    return IRQ_HANDLED;
}


#if defined(CONFIG_HAS_EARLYSUSPEND)||defined(CONFIG_FB)
/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/

static void ft5x0x_ts_suspend(struct device *dev)
{

    struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(this_client);
    struct input_dev *input_dev = ft5x0x_ts->input_dev;
    struct irq_desc *desc = irq_to_desc(ft5x0x_ts->irq);
	int ret=-1;
		
	mutex_lock(&input_dev->mutex);
	ft5x0x_ts->tp_is_suspend=1;

#ifdef CONFIG_TOUCHSCREEN_GESTURE_WAKEUP
#if defined(CONFIG_BOARD_K2_01)||defined(CONFIG_BOARD_K2_02)
	if (1==gesture_flag)
#else
	if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
#endif
	{
		disable_irq_nosync(ft5x0x_ts->irq);
		ft5x0x_write_reg(0xd1, 0x3F);//add by zdd 
		ft5x0x_write_reg(0xd2, 0x1F);//add by zdd 
		mdelay(1);
		ft5x0x_write_reg(0xd0, 0x01);
		TW_enable_irq_wake(ft5x0x_ts, 1);
		enable_irq(ft5x0x_ts->irq);
		mutex_unlock(&input_dev->mutex);
		printk(KERN_ERR"TW had suspended with gesture.\n");
		return;
	} else 
	#endif
	{

		disable_irq_nosync(ft5x0x_ts->irq);
		cancel_work_sync(&ft5x0x_ts->pen_event_work);
		flush_workqueue(ft5x0x_ts->ts_workqueue);
	}

#if CFG_SUPPORT_TOUCH_KEY
    /* yulong add  */
    /* reason : key debounce */
    /* author : tanliyu */
    /* time : 2012-10-04 */
	if(DEBOUNCE)
	{
		del_timer_sync(&ft5x0x_ts->timer);
	    inDebounce = false;
	}
    /* yulong end */
#endif

    if(unlikely ( 0 == desc->depth ) )
    {
        mdelay(3);
        printk(KERN_ERR "%s:%d,desc->depth=%d\n",__func__,__LINE__,desc->depth);
        disable_irq_nosync(ft5x0x_ts->irq);
    }

    ret = ft5x0x_write_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);
	if(ret < 0)
	{
		printk("[FTS]:failed write 03 into a5,ret=%d\n",ret);
	}
	else
	{
		printk("[FTS]:write 0xa5 to 03 sucess.ret=%d\n",ret);
	}
    ft5x0x_ts_release();
    if(ft5x0x_ts->pdata->suspend)
	ft5x0x_ts->pdata->suspend();
    mutex_unlock(&input_dev->mutex);
}
/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/
static void ft5x0x_ts_resume(struct device *dev)
{
    struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(this_client);
    struct input_dev *input_dev = ft5x0x_ts->input_dev;
    struct irq_desc *desc = irq_to_desc(ft5x0x_ts->irq);
#ifdef CONFIG_TOUCHSCREEN_GLOVE
	u8 auc_i2c_write_buf[2] =	{0xc0,0x00};
	int ret=0;
#endif
    disable_irq_nosync(this_client->irq);// litao3 add for resolve unbalanced irq
    mutex_lock(&input_dev->mutex);	
    ft5x0x_ts->tp_is_suspend=0;
    if(ft5x0x_ts->pdata->resume)
	ft5x0x_ts->pdata->resume();
#ifdef CONFIG_TOUCHSCREEN_GESTURE_WAKEUP 
	if (support_gesture & TW_SUPPORT_GESTURE_IN_ALL) {

		TW_enable_irq_wake(ft5x0x_ts, 0);

		} 
#endif
	 printk(KERN_ERR "%s:Start %d,desc->depth=%d\n",__func__,__LINE__,desc->depth);
	while( desc->depth >= 1  )
	{
		enable_irq(ft5x0x_ts->irq);
		mdelay(5);
	}
	 printk(KERN_ERR "%s:End %d,desc->depth=%d\n",__func__,__LINE__,desc->depth);
#ifdef CONFIG_TOUCHSCREEN_GLOVE
	if(current_mode_flag==MODE_GLOVE)
	{
		auc_i2c_write_buf[1] = 0x01; 
		ret = ft5x0x_i2c_txdata(auc_i2c_write_buf,2);
		if (ret < 0) 
		{
		 	pr_err("%s write reg 0xc0 failed!\n",__func__);
		}	
		
	}
#endif
	 mutex_unlock(&input_dev->mutex);
#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
	yl_chg_status_changed();
#endif
}
#endif  //CONFIG_HAS_EARLYSUSPEND
//add by taokai
#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct ft5x0x_ts_data *ft5x0x_data =
		container_of(self, struct ft5x0x_ts_data, fb_notif);
	if (evdata && evdata->data && event == FB_EVENT_BLANK &&
			ft5x0x_data && ft5x0x_data->client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK) {
			backlight_on = 1;
			printk(KERN_ERR"%s: reume,  backlight_on = %d\n",__func__,backlight_on );
			ft5x0x_ts_resume(&ft5x0x_data->client->dev);
		}
		else if (*blank == FB_BLANK_POWERDOWN) {
			backlight_on = 0;
			printk(KERN_ERR"%s: suspend, backlight_on = %d\n",__func__,backlight_on);	
			ft5x0x_ts_suspend(&ft5x0x_data->client->dev);
		}
	}
	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void ft5x0x_ts_early_suspend(struct early_suspend *handler)
{
	struct ft5x0x_ts_data *data = container_of(handler,
						   struct ft5x0x_ts_data,
						   early_suspend);

	ft5x0x_ts_suspend(&data->client->dev);
}
static void ft5x0x_ts_late_resume(struct early_suspend *handler)
{
	struct ft5x0x_ts_data *data = container_of(handler,
						   struct ft5x0x_ts_data,
						   early_suspend);
	ft5x0x_ts_resume(&data->client->dev);
}
#endif
//add by taokai


static const struct dev_pm_ops ft5x0x_ts_pmops={
#if (!defined(CONFIG_FB)&&!defined(CONFIG_HAS_EARLYSUSPEND))
	.suspend=ft5x0x_ts_suspend;
	.resume =ft5x0x_ts_resume;
#endif
};
		

/***********************************************************************************************
Name	:fts_ctpm_repair_firemware

Input	:


Output	:

function	:repair_firemware

***********************************************************************************************/
#if  0//maleijie
static int fts_ctpm_repair_firemware(void)
{
    unsigned char uc_i2c_addr;             //I2C slave address (8 bit address)
    unsigned char uc_io_voltage;           //IO Voltage 0---3.3v;	1----1.8v
    unsigned char uc_panel_factory_id;     //TP panel factory ID

    unsigned char buf[FTS_SETTING_BUF_LEN];

    FTS_BYTE reg_val[2] = {0};
    FTS_BYTE  auc_i2c_write_buf[10];
    //FTS_BYTE  packet_buf[FTS_SETTING_BUF_LEN + 6];
    FTS_DWRD i = 0;
    int i_ret;
    int ret;
    int repair_result = 0;
    unsigned char uc_host_fm_ver;

    uc_i2c_addr = 0x70;
    uc_io_voltage = 0x0;
    uc_panel_factory_id = 0x5a;

    /*********Step 1:Reset  CTPM *****/
    /*write 0xaa to register 0xfc*/
    ft5x0x_write_reg(0xfc,0xaa);
    delay_qt_ms(50);
     /*write 0x55 to register 0xfc*/
    ft5x0x_write_reg(0xfc,0x55);
    printk("[FTS] Step 1: Reset CTPM test\n");

    delay_qt_ms(30);

    /*********Step 2:Enter upgrade mode *****/
    auc_i2c_write_buf[0] = 0x55;
    auc_i2c_write_buf[1] = 0xaa;
    do
    {
        i ++;
        i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
        delay_qt_ms(5);
    }while(i_ret <= 0 && i < 5 );

    /*********Step 3:check READ-ID***********************/
    cmd_write(0x90,0x00,0x00,0x00,4);
    byte_read(reg_val,2);
    /*
    check id is different.
    chip:ft5506 0x79:0x6
    chip:ft5306 0x79:0x3
    */
    if (reg_val[0] == 0x79 && reg_val[1] == CTPM_ID2)
    {
        printk("[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
    }
    else
    {
        //return ERR_READID;
        return 0;
    }

    cmd_write(0xcd,0x0,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("bootloader version = 0x%x\n", reg_val[0]);


    /* --------- read current project setting  ---------- */
    //set read start address
    buf[0] = 0x3;
    buf[1] = 0x0;
    buf[2] = 0x78;
    buf[3] = 0x0;
    byte_write(buf, 4);
    byte_read(buf, FTS_SETTING_BUF_LEN);

    printk("[FTS] old setting: uc_i2c_addr = 0x%x, uc_io_voltage = %d, uc_panel_factory_id = 0x%x\n",
        buf[0],  buf[2], buf[4]);
    for (i = 0; i < FTS_SETTING_BUF_LEN; i++)
    {
        if (i % 16 == 0)     printk("\n");
        printk("0x%x, ", buf[i]);

    }
    printk("\n");
    /********* reset the new FW***********************/
    cmd_write(0x07,0x00,0x00,0x00,1);

    msleep(300);
     ret = fts_ctpm_fw_upgrade_with_tp_id(buf[4]);
     if (ret == 0)
        {
                msleep(300);
                uc_host_fm_ver = fts_ctpm_get_i_file_ver(buf[4]);
                pr_info("%s [FTS] upgrade to new version 0x%x\n", __FUNCTION__, uc_host_fm_ver);
                repair_result = 1;
        }
    else
        {
                repair_result = 0;
                pr_err("%s ERROR:[FTS] upgrade failed ret=%d.\n", __FUNCTION__, ret);
         }
     return repair_result;

}
#endif

/*sysfs debug*/

/*
*get firmware size

@firmware_name:firmware name
*note:the firmware default path is sdcard.
	if you want to change the dir, please modify by yourself.
*/
static int ft5x0x_GetFirmwareSize(char *firmware_name)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize = 0;
	char filepath[128];
	memset(filepath, 0, sizeof(filepath));

	sprintf(filepath, "%s", firmware_name);

	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);

	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}

	inode = pfile->f_dentry->d_inode;
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	filp_close(pfile, NULL);
	return fsize;
}

/*
*read firmware buf for .bin file.

@firmware_name: fireware name
@firmware_buf: data buf of fireware

note:the firmware default path is sdcard.
	if you want to change the dir, please modify by yourself.
*/
static int ft5x0x_ReadFirmware(char *firmware_name,
			       unsigned char *firmware_buf)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize;
	char filepath[128];
	loff_t pos;
	mm_segment_t old_fs;

	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s", firmware_name);
	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}

	inode = pfile->f_dentry->d_inode;
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	vfs_read(pfile, firmware_buf, fsize, &pos);
	filp_close(pfile, NULL);
	set_fs(old_fs);

	return 0;
}



/*
upgrade with *.bin file
*/

int fts_ctpm_fw_upgrade_with_app_file(struct i2c_client *client,
				       char *firmware_name)
{
	u8 *pbt_buf = NULL;
	int i_ret;
	int fwsize = ft5x0x_GetFirmwareSize(firmware_name);

	if (fwsize <= 0) {
		dev_err(&client->dev, "%s ERROR:Get firmware size failed\n",
					__func__);
		return -EIO;
	}

	if (fwsize < 8 || fwsize > 32 * 1024) {
		dev_dbg(&client->dev, "%s:FW length error\n", __func__);
		return -EIO;
	}

	/*=========FW upgrade========================*/
	pbt_buf = kmalloc(fwsize + 1, GFP_ATOMIC);

	if (ft5x0x_ReadFirmware(firmware_name, pbt_buf)) {
		dev_err(&client->dev, "%s() - ERROR: request_firmware failed\n",
					__func__);
		kfree(pbt_buf);
		return -EIO;
	}

	if(current_tp_id != pbt_buf[fwsize-1])
	{
		printk(KERN_ERR"current_tp_id=0x%x, bin_file_id=0x%x\n", current_tp_id, pbt_buf[fwsize-1]);
		kfree(pbt_buf);
		return -EIO;
	}

	if ((pbt_buf[fwsize - 8] ^ pbt_buf[fwsize - 6]) == 0xFF
		&& (pbt_buf[fwsize - 7] ^ pbt_buf[fwsize - 5]) == 0xFF
		&& (pbt_buf[fwsize - 3] ^ pbt_buf[fwsize - 4]) == 0xFF) {
		/*call the upgrade function */
		#ifdef FT5X46
		i_ret = fts_ctpm_fw_upgrade(client,pbt_buf, fwsize);
		#else
		i_ret = fts_ctpm_fw_upgrade(pbt_buf, fwsize);
		#endif
		if (i_ret != 0)
			dev_dbg(&client->dev, "%s() - ERROR:[FTS] upgrade failed..\n",
						__func__);
		else {
			fts_ctpm_auto_clb();	/*start auto CLB*/
		 }
		kfree(pbt_buf);
	} else {
		dev_dbg(&client->dev, "%s:FW format error\n", __func__);
		kfree(pbt_buf);
		return -EIO;
	}

	return i_ret;
}


#ifdef FT8606

int ft5x06_reset_touchscreen(void);

bool Upgrade_ReadPram(struct i2c_client * client,unsigned int Addr, unsigned char * pData, unsigned short Datalen)
{
	bool ReCode=false;
	int ret=-1;
	unsigned char pDataSend[16];
	//if (iCommMode == HY_I2C_INTERFACE)
	{
		pDataSend[0] = 0x85;
		pDataSend[1] = 0x00;
		pDataSend[2] = Addr>>8;
		pDataSend[3] = Addr;
		//HY_IIC_IO(hDevice, pDataSend, 4, NULL, 0);
		ftxxxx_i2c_Write(client, pDataSend, 4);
		//HY_IIC_IO(hDevice, NULL, 0, pData, Datalen) == ERROR_CODE_OK ? ReCode = true : ReCode = false;

		ret =ftxxxx_i2c_Read(client, NULL, 0, pData, Datalen);  
		if (ret < 0) 
		{        
			TW_DBG("[FTS] failed Upgrade_ReadPram \n");     
			return ReCode;    
		} 
		ReCode=true;
	}
	
	return ReCode;
}


int  FT_IC_PROGRAM_FT8606_WritePram(struct i2c_client * client, u8* pbt_buf, u32 dw_lenth)
{

	u8 reg_val[4] = {0};
	u32 i = 0;
	u32 packet_number;
	u32 j;
	u32 temp,nowAddress=0,StartFlashAddr=0,FlashAddr=0;
	u32 lenght;
	u8 packet_buf[FTS_PACKET_LENGTH + 6];
	u8 *pCheckBuffer = NULL;
	u8 auc_i2c_write_buf[10];
	u8 bt_ecc;
	int i_ret,ReCode=-1;

	//fts_get_upgrade_info(&upgradeinfo);
	printk("zax 8606 dw_lenth= %d",dw_lenth);
	if(dw_lenth > 0x10000 || dw_lenth ==0)
	{		
		return -EIO;
	}
	pCheckBuffer = kmalloc(dw_lenth + 1, GFP_ATOMIC);
	if(!pCheckBuffer)
	{
		dev_err(&client->dev,"couldn't allocate pCheckBuffer\n");
		return -ENOMEM;
	}
	for (i = 0; i < 20; i++) 
	{
		/*********Step 1:Reset  CTPM *****/
		/*write 0xaa to register 0xfc */
		//ftxxxx_reset_tp(0);
		//msleep(10);
		//ftxxxx_reset_tp(1);
		
		//msleep(10);	//time (5~20ms)


		ft5x0x_write_reg(0xfc, FT_UPGRADE_AA);
		msleep(upgradeinfo.delay_aa);
		ft5x0x_write_reg(0xfc, FT_UPGRADE_55);
		msleep(200);
		/*********Step 2:Enter upgrade mode *****/
		auc_i2c_write_buf[0] = FT_UPGRADE_55;
		i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
		if(i_ret < 0)
		{
			TW_DBG("[FTS] failed writing  0x55 ! \n");
			continue;
		}
		
		/*
		auc_i2c_write_buf[0] = FT_UPGRADE_AA;
		i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
		if(i_ret < 0)
		{
			DBG("[FTS] failed writing  0xaa ! \n");
			continue;
		}
		*/	
		/*********Step 3:check READ-ID***********************/
		msleep(1);
		auc_i2c_write_buf[0] = 0x90;
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] =
			0x00;
		reg_val[0] = reg_val[1] = 0x00;
		
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);
		if (reg_val[0] == 0x86&& reg_val[1] == 0x06) 
		{
			/*
			i_ret = 0x00;
			ftxxxx_read_reg(client, 0xd0, &i_ret);

			if(i_ret == 0)
			{
				DBG("[FTS] Step 3: READ State fail \n");
				continue;
			}

			DBG("[FTS] Step 3: i_ret = %d \n", i_ret);
			*/
			
			TW_DBG("[FTS] Step 3: READ CTPM ID OK,ID1 = 0x%x,ID2 = 0x%x\n",
				reg_val[0], reg_val[1]);
			
			msleep(50);
			break;
		} 
		else 
		{
			dev_err(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",
				reg_val[0], reg_val[1]);
			
			continue;
		}
	}

	if (i >= FTS_UPGRADE_LOOP )
	{
		if(pCheckBuffer)
			kfree(pCheckBuffer);
		return -EIO;
	}
	/*********Step 4:write firmware(FW) to ctpm flash*********/
	bt_ecc = 0;
	TW_DBG("Step 5:write firmware(FW) to ctpm flash\n");

	//dw_lenth = dw_lenth - 8;
	
	temp = 0;
	packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
	packet_buf[0] = 0xae;
	packet_buf[1] = 0x00;

	for (j = 0; j < packet_number; j++) 
	{
		temp = j * FTS_PACKET_LENGTH;
		packet_buf[2] = (u8) (temp >> 8);
		packet_buf[3] = (u8) temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (u8) (lenght >> 8);
		packet_buf[5] = (u8) lenght;

		for (i = 0; i < FTS_PACKET_LENGTH; i++) 
		{
			packet_buf[6 + i] = pbt_buf[j * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}
		ftxxxx_i2c_Write(client, packet_buf, FTS_PACKET_LENGTH + 6);
		nowAddress=nowAddress+FTS_PACKET_LENGTH;
	}

	if ((dw_lenth) % FTS_PACKET_LENGTH > 0) 
	{
		temp = packet_number * FTS_PACKET_LENGTH;
		packet_buf[2] = (u8) (temp >> 8);
		packet_buf[3] = (u8) temp;
		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (u8) (temp >> 8);
		packet_buf[5] = (u8) temp;

		for (i = 0; i < temp; i++) 
		{
			packet_buf[6 + i] = pbt_buf[packet_number * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}		
		ftxxxx_i2c_Write(client, packet_buf, temp + 6);
		nowAddress=nowAddress+temp;
	}
	/*
	temp = FT_APP_INFO_ADDR;
	packet_buf[2] = (u8) (temp >> 8);
	packet_buf[3] = (u8) temp;
	temp = 8;
	packet_buf[4] = (u8) (temp >> 8);
	packet_buf[5] = (u8) temp;
	for (i = 0; i < 8; i++) 
	{
		packet_buf[6+i] = pbt_buf[dw_lenth + i];
		bt_ecc ^= packet_buf[6+i];
	}	
	ftxxxx_i2c_Write(client, packet_buf, 6+8);
	*/
	/*********Step 5: read out checksum***********************/
	/*send the opration head */
	TW_DBG("Step 6: read out checksum\n");
	/*auc_i2c_write_buf[0] = 0xcc;
	//msleep(2);
	ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 1);
	if (reg_val[0] != bt_ecc) 
	{
		dev_err(&client->dev, "[FTS]--ecc error! FW=%02x bt_ecc=%02x\n",reg_val[0],bt_ecc);	
		return -EIO;
	}
	printk(KERN_WARNING "checksum %X %X \n",reg_val[0],bt_ecc);
	DBG("Read flash and compare\n");
		*/
	msleep(100);
	//-----------------------------------------------------------------------------------------------------
	dev_err(&client->dev, "[FTS]--nowAddress=%02x dw_lenth=%02x\n",nowAddress,dw_lenth);	
	if(nowAddress == dw_lenth)  //如果down的数据量大小等于文件大小
	{

	FlashAddr=0;
	while(1)
	{
		StartFlashAddr = FlashAddr;
		if(FlashAddr == dw_lenth)
		{
			break;
		}
		else if(FlashAddr+MAX_R_FLASH_SIZE > dw_lenth)
		{			
			if(!Upgrade_ReadPram(client,StartFlashAddr, pCheckBuffer+FlashAddr, dw_lenth-FlashAddr))
			{
				TW_DBG("read out checksum error\n");
				kfree(pCheckBuffer);
				return -EIO;
				//break;
			}
			ReCode = ERROR_CODE_OK;
			FlashAddr = dw_lenth;
		}
		else
		{
			if(!Upgrade_ReadPram(client,StartFlashAddr, pCheckBuffer+FlashAddr, MAX_R_FLASH_SIZE))
			{
				TW_DBG("read out checksum error\n");
				kfree(pCheckBuffer);
				return -EIO;
				
				//break;
			}
			FlashAddr += MAX_R_FLASH_SIZE;
			ReCode = ERROR_CODE_OK;
		}

		if(ReCode != ERROR_CODE_OK){
			TW_DBG("read out checksum error\n");
			kfree(pCheckBuffer);
				return -EIO;
			//break;
		}
		
	}
	dev_err(&client->dev, "[FTS]--FlashAddr=%02x dw_lenth=%02x\n",FlashAddr,dw_lenth);
	if(FlashAddr == dw_lenth)
	{
		
		TW_DBG("Checking data...\n");
		for(i=0; i<dw_lenth; i++)
		{
			if(pCheckBuffer[i] != pbt_buf[i])
			{
				TW_DBG("read out checksum error\n");
				if(pCheckBuffer)
				{	
					kfree(pCheckBuffer);
					pCheckBuffer = NULL;
				}
				return -EIO;
			}
		}
		if(pCheckBuffer)
		{
			kfree(pCheckBuffer);
			pCheckBuffer = NULL;
		}
		//COMM_FLASH_FT5422_Upgrade_StartApp(bOldProtocol, iCommMode);		//Reset
		TW_DBG("read out checksum successful\n");
		
	}
	else
	{
		if(pCheckBuffer)
		{
			kfree(pCheckBuffer);
			pCheckBuffer = NULL;
		}
		TW_DBG("read out checksum error\n");
	}	

	}
	else
	{
		if(pCheckBuffer)
		{
			kfree(pCheckBuffer);
			pCheckBuffer = NULL;
		}
		TW_DBG("read out checksum error\n");
	}	
	/*********Step 6: start app***********************/
	TW_DBG("Step 6: start app\n");
	auc_i2c_write_buf[0] = 0x08;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(20);

	return 0;
}


int FT_IC_PROGRAM_FT8606_WriteFlash(struct i2c_client *client, u8 *pbt_buf, u32 dw_lenth)
{
	u8 reg_val[4] = {0};
	u8 reg_val_id[4] = {0};
	u32 i = 0;
//	u8 is_5336_new_bootloader = 0;
//	u8 is_5336_fwsize_30 = 0;
	u32 packet_number;
	u32 j=0;
	u32 temp;
	u32 lenght;
	u8 packet_buf[FTS_PACKET_LENGTH + 6];
	u8 auc_i2c_write_buf[10];
	u8 bt_ecc;
	int i_ret;
	int extra_tianma;
	unsigned char cmd[20];
	auc_i2c_write_buf[0] = 0x05;
	reg_val_id[0] = 0x00;
	if(0==strcmp("LCD_TYPE_FT8606_BOYI_AUO_HD_50",lcd_name))//BOYI erase all flash,TIANMA erase the TP of flash
		extra_tianma=0;
	else if((0==strcmp("LCD_TYPE_FT8608_TIANMA_HD_50",lcd_name))||(0==strcmp("LCD_TYPE_FT8608_TIANMA2_HD_50",lcd_name)))
		extra_tianma=0x1000;
	i_ret =ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val_id, 1);
	if(dw_lenth == 0)
	{
		return -EIO;
	}
	if(0x81 == (int)reg_val_id[0]) 
	{
		if(dw_lenth > 1024*64) 
		{
			return -EIO;
		}
	}
	else if(0x80 == (int)reg_val_id[0]) 
	{
		if(dw_lenth > 1024*68) 
		{
			return -EIO;
		}
	}

	/*if(dw_lenth > 1024*68)
	{
		return -EIO;
	}*/
	fts_get_upgrade_info(&upgradeinfo);
	for (i = 0; i < FTS_UPGRADE_LOOP; i++)
	{
		msleep(100);

		pr_warn("[FTS] Download Step 1:Reset  CTPM\n");
		/*********Step 1:Reset  CTPM *****/

		/*write 0xaa to register 0xfc */
		ft5x0x_write_reg(0xfc, FT_UPGRADE_AA);
		msleep(upgradeinfo.delay_aa);

		/*write 0x55 to register 0xfc */
		ft5x0x_write_reg(0xfc, FT_UPGRADE_55);

		if (i<=15)
		{
			msleep(upgradeinfo.delay_55+i*3);
		}
		else
		{
			msleep(upgradeinfo.delay_55-(i-15)*2);
		}

		pr_warn("[FTS] Download Step 2:Enter upgrade mode \n");
		/*********Step 2:Enter upgrade mode *****/

		auc_i2c_write_buf[0] = FT_UPGRADE_55;
		ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
		msleep(5);
		auc_i2c_write_buf[0] = FT_UPGRADE_AA;
		ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);

		/*********Step 3:check READ-ID***********************/
		msleep(upgradeinfo.delay_readid);
		auc_i2c_write_buf[0] = 0x90;
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] =0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);

		pr_warn("[FTS] Download Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0], reg_val[1]);
		/*if (reg_val[0] == fts_updateinfo_curr.download_id_1
		        && reg_val[1] == fts_updateinfo_curr.download_id_2)*/
		if ((reg_val[0] == 0x86)&& (reg_val[1] == 0xA6))//
		{
			pr_err("[FTS] Download Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",
			    reg_val[0], reg_val[1]);
			break;
		}
		else
		{
			pr_err("[FTS] Download Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",
			        reg_val[0], reg_val[1]);

			continue;
		}
	}

	if (i >= FTS_UPGRADE_LOOP)
	{
		if (reg_val[0] != 0x00 || reg_val[1] != 0x00)
		{
			
			#if 1 //sw reset
				ft5x06_reset_touchscreen();//change by zdd 
			#else //hw reset
				fts_ctpm_hw_reset();
			#endif	
			msleep(300);
		}
		return -EIO;
	}
	
 	{		
		cmd[0] = 0x05;
		cmd[1] = reg_val_id[0];//0x80;
		cmd[2] = 0x00;//???
		//ReCode = HY_IIC_IO(hDevice, cmd, 2, NULL, 0);	
		ftxxxx_i2c_Write(client, cmd, 3);
	}
	pr_warn("[FTS] Download Step 4:change to write flash mode\n");
	if(0==strcmp("LCD_TYPE_FT8606_BOYI_AUO_HD_50",lcd_name))//BOYI erase all flash,TIANMA erase the TP of flash
		ft5x0x_write_reg(0x09, 0x0a);
	else if((0==strcmp("LCD_TYPE_FT8608_TIANMA_HD_50",lcd_name))||(0==strcmp("LCD_TYPE_FT8608_TIANMA2_HD_50",lcd_name)))
		ft5x0x_write_reg(0x09, 0x0b);
	msleep(50);
	/*Step 4:erase app and panel paramenter area*/
	pr_warn("[FTS] Download Step 4:erase app and panel paramenter area\n");
	auc_i2c_write_buf[0] = 0x61;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);	/*erase app area */
	msleep(3000/*upgradeinfo.delay_earse_flash*/);
	/*erase panel parameter area */

	for(i = 0; i < 15; i++)
	{
		auc_i2c_write_buf[0] = 0x6a;
		reg_val[0] = reg_val[1] = 0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

		if(0xF0==reg_val[0] && 0xAA==reg_val[1])
		{
			break;
		}
		msleep(50);
	}

	pr_warn("[FTS] Download Step 5:write firmware(FW) to ctpm flash\n");
	/*********Step 5:write firmware(FW) to ctpm flash*********/
	bt_ecc = 0;

	//dw_lenth = dw_lenth - 8;
	temp = 0;
	packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
	packet_buf[0] = 0xbf;
	

	//pr_warn("[FTS] Download Step 5:packet_number: %d\n", packet_number);
	for (j = 0; j < packet_number; j++)
	{
		temp = j * FTS_PACKET_LENGTH+extra_tianma;
		packet_buf[1] = (u8) (temp >> 16);
		packet_buf[2] = (u8) (temp >> 8);
		packet_buf[3] = (u8) temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (u8) (lenght >> 8);
		packet_buf[5] = (u8) lenght;

		for (i = 0; i < FTS_PACKET_LENGTH; i++)
		{
			packet_buf[6 + i] = pbt_buf[j * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}
		ftxxxx_i2c_Write(client, packet_buf, FTS_PACKET_LENGTH + 6);
		//msleep(10);

		for(i = 0; i < 30; i++)
		{
			auc_i2c_write_buf[0] = 0x6a;
			reg_val[0] = reg_val[1] = 0x00;
			ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

			if ((j + 0x1000) == (((reg_val[0]) << 8) | reg_val[1]))
			{
				break;
			}
			//msleep(1);

		}
	}

	if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
	{
		temp = packet_number * FTS_PACKET_LENGTH+extra_tianma;
		packet_buf[1] = (u8) (temp >> 16);
		packet_buf[2] = (u8) (temp >> 8);
		packet_buf[3] = (u8) temp;
		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (u8) (temp >> 8);
		packet_buf[5] = (u8) temp;

		for (i = 0; i < temp; i++)
		{
			packet_buf[6 + i] = pbt_buf[packet_number * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}
		ftxxxx_i2c_Write(client, packet_buf, temp + 6);

		for(i = 0; i < 30; i++)
		{
			auc_i2c_write_buf[0] = 0x6a;
			reg_val[0] = reg_val[1] = 0x00;
			ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

			if ((j + 0x1000) == (((reg_val[0]) << 8) | reg_val[1]))
			{
				break;
			}
			msleep(1);

		}
	}

	msleep(50);


	pr_warn("[FTS] Download Step 6: read out checksum\n");
	/*********Step 6: read out checksum***********************/
	auc_i2c_write_buf[0] = 0x64;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(300);

	temp = extra_tianma;
	auc_i2c_write_buf[0] = 0x65;
	auc_i2c_write_buf[1] = (u8)(temp >> 16);
	auc_i2c_write_buf[2] = (u8)(temp >> 8);
	auc_i2c_write_buf[3] = (u8)(temp);
	if (dw_lenth > LEN_FLASH_ECC_MAX)
	{
		temp = LEN_FLASH_ECC_MAX;
	}
	else
	{
		printk("zax Step 6_1: read out checksum\n");
		temp = dw_lenth;
	}
	
	auc_i2c_write_buf[4] = (u8)(temp >> 8);
	auc_i2c_write_buf[5] = (u8)(temp);
	i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 6);
	msleep(dw_lenth/256);

	for(i = 0; i < 100; i++)
	{
		auc_i2c_write_buf[0] = 0x6a;
		reg_val[0] = reg_val[1] = 0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

		if (0xF0==reg_val[0] && 0x55==reg_val[1])
		{
			break;
		}
		msleep(1);

	}
	//----------------------------------------------------------------------
	if (dw_lenth > LEN_FLASH_ECC_MAX)
	{
		temp=LEN_FLASH_ECC_MAX;
		auc_i2c_write_buf[0] = 0x65;
		auc_i2c_write_buf[1] = (u8)(temp >> 16);
		auc_i2c_write_buf[2] = (u8)(temp >> 8);
		auc_i2c_write_buf[3] = (u8)(temp);
		
		temp = dw_lenth-LEN_FLASH_ECC_MAX;
		
		auc_i2c_write_buf[4] = (u8)(temp >> 8);
		auc_i2c_write_buf[5] = (u8)(temp);
		i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 6);
		msleep(dw_lenth/256);

		for(i = 0; i < 100; i++)
		{
			auc_i2c_write_buf[0] = 0x6a;
			reg_val[0] = reg_val[1] = 0x00;
			ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

			if (0xF0==reg_val[0] && 0x55==reg_val[1])
			{
				break;
			}
			msleep(1);

		}
	}


	
	auc_i2c_write_buf[0] = 0x66;
	ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 1);
	if (reg_val[0] != bt_ecc)
	{
		//pr_err("[FTS] Download Step 6: ecc error! FW=%02x bt_ecc=%02x\n",
		 //       reg_val[0],
		     //   bt_ecc);

		return -EIO;
	}

	printk(KERN_WARNING "checksum %X %X \n",reg_val[0],bt_ecc);    
	pr_warn("[FTS] Download Step 7: reset the new FW\n");
	/*********Step 7: reset the new FW***********************/
#if 1 //sw reset
	auc_i2c_write_buf[0] = 0x07;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
#else //hw reset
	fts_ctpm_hw_reset();
#endif
	msleep(300);	/*make sure CTP startup normally */

	//i_ret = fts_hidi2c_to_stdi2c(client);//Android to Std i2c.

	//if(i_ret == 0)
	//{
	//	pr_err("HidI2c change to StdI2c i_ret = %d ! \n", i_ret);
	//}

	return 0;
}




//#define LEN_FLASH_ECC_MAX 0xFFFE  // 一次ECC校验的最大长?
int  FT_IC_PROGRAM_FT8606_Upgrade(struct i2c_client * client, u8* pbt_buf, u32 dw_lenth)
{
	u8 reg_val[4] = {0};
	u8 reg_val_id[4] = {0};
	u32 i = 0;
	u32 packet_number;
	u32 j;
	u32 temp;
	u32 lenght;
	u8 packet_buf[FTS_PACKET_LENGTH + 6];
	u8 auc_i2c_write_buf[10];
	u8 bt_ecc;
	int i_ret;
	unsigned char cmd[20];
	unsigned char Checksum = 0;
	
	//memcpy(m_DataBuffer, pFileData, m_FileLen);
	//pbt_buf=pFileData;
	auc_i2c_write_buf[0] = 0x05;
	reg_val_id[0] = 0x00;
		
	i_ret =ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val_id, 1);
	if(dw_lenth == 0)
	{
		return -EIO;
	}
	if(0x81 == (int)reg_val_id[0]) 
	{
		if(dw_lenth > 1024*60) 
		{
			return -EIO;
		}
	}
	else if(0x80 == (int)reg_val_id[0]) 
	{
		if(dw_lenth > 1024*64) 
		{
			return -EIO;
		}
	}

		
	/*if(dw_lenth > 1024*64)
	{
			

		return -EIO;
	}*/

	fts_get_upgrade_info(&upgradeinfo);

	for (i = 0; i < FTS_UPGRADE_LOOP; i++) {
		/*********Step 1:Reset  CTPM *****/
		//ftxxxx_write_reg(client, 0xfc, FT_UPGRADE_AA);
		//msleep(upgradeinfo.delay_aa);
		//ftxxxx_write_reg(client, 0xfc, FT_UPGRADE_55);
		//msleep(200);
		/*********Step 2:Enter upgrade mode *****/
		//i_ret = HidI2c_To_StdI2c(client);

		//if(i_ret == 0)
		//{
		//	DBG("HidI2c change to StdI2c fail ! \n");
		//	continue;
		//}
		msleep(10);
		auc_i2c_write_buf[0] = FT_UPGRADE_55;
		auc_i2c_write_buf[1] = FT_UPGRADE_AA;
		i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 2);
		if(i_ret < 0)
		{
			TW_DBG("failed writing  0x55 and 0xaa ! \n");
			continue;
		}

		/*********Step 3:check READ-ID***********************/
		msleep(1);
		auc_i2c_write_buf[0] = 0x90;
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] = 0x00;

		reg_val[0] = reg_val[1] = 0x00;

		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);

		if ((reg_val[0] == upgradeinfo.upgrade_id_1
			&& reg_val[1] == upgradeinfo.upgrade_id_2)|| (reg_val[0] == 0x86 && reg_val[1] == 0xA6)) {
			TW_DBG("[FTS] Step 3: READ OK CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",
				reg_val[0], reg_val[1]);
				break;
		} else {
			dev_err(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",
				reg_val[0], reg_val[1]);

			continue;
		}
	}
	//printk("zax uuuuuuu  %d\n",i);
	if (i >= FTS_UPGRADE_LOOP )
		return -EIO;
	//printk("zax 1111uuuuuuu \n");
	/*Step 4:erase app and panel paramenter area*/
	TW_DBG("Step 4:erase app and panel paramenter area\n");
	//zax 20150115

	//选择Flash型号，命令： W 05 Mode。 Mode：FT5003：0x81， Winbond：0x80
	//ucAddr = 0x05;
	//ucMode = 0x80;
	
	

	{		
		cmd[0] = 0x05;
		cmd[1] = reg_val_id[0];//0x80;
		cmd[2] = 0x00;//???
		//ReCode = HY_IIC_IO(hDevice, cmd, 2, NULL, 0);	
		ftxxxx_i2c_Write(client, cmd, 3);
	}
	
	//Set pramboot download mode
	//COMM_FLASH_FT5422_Upgrade_SetFlashMode(0x0B, iCommMode);
	{
		cmd[0] = 0x09;
		cmd[1] = 0x0B;
		//HY_IIC_IO(hDevice, cmd, 2, NULL, 0);
		ftxxxx_i2c_Write(client, cmd, 2);
	}	
	for(i=0; i<dw_lenth ; i++)
	{
		
		Checksum ^= pbt_buf[i];
	}
	msleep(50);

	//erase app area 



	//zax enable
	//auc_i2c_write_buf[0] = 0x06;
	//ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);

	
	auc_i2c_write_buf[0] = 0x61;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);     
	msleep(1350);

	for(i = 0;i < 15;i++)
	{
		auc_i2c_write_buf[0] = 0x6a;
		reg_val[0] = reg_val[1] = 0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

		if(0xF0==reg_val[0] && 0xAA==reg_val[1])
		{
			break;
		}
		msleep(50);
	}
	//zax disable
	//auc_i2c_write_buf[0] = 0x04;
	//ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
	
       //write bin file length to FW bootloader.
	//auc_i2c_write_buf[0] = 0xB0;
	//auc_i2c_write_buf[1] = (u8) ((dw_lenth >> 16) & 0xFF);
	//auc_i2c_write_buf[2] = (u8) ((dw_lenth >> 8) & 0xFF);
	//auc_i2c_write_buf[3] = (u8) (dw_lenth & 0xFF);

	//ftxxxx_i2c_Write(client, auc_i2c_write_buf, 4);

	/*********Step 5:write firmware(FW) to ctpm flash*********/
	bt_ecc = 0;
	TW_DBG("Step 5:write firmware(FW) to ctpm flash\n");

	//dw_lenth = dw_lenth - 8;
	temp = 0;
	packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
	packet_buf[0] = 0xbf;
	//packet_buf[1] = 0x00;

	for (j = 0; j < packet_number; j++) {
		temp = 0x1000+j * FTS_PACKET_LENGTH;
		packet_buf[1] = (u8) (temp >> 16);
		packet_buf[2] = (u8) (temp >> 8);
		packet_buf[3] = (u8) temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (u8) (lenght >> 8);
		packet_buf[5] = (u8) lenght;

		for (i = 0; i < FTS_PACKET_LENGTH; i++) 
		{
			packet_buf[6 + i] = pbt_buf[j * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}
		//DBG("wirte start\n");
		ftxxxx_i2c_Write(client, packet_buf, FTS_PACKET_LENGTH + 6);
		//msleep(10);
		//DBG("wirte start 1\n");
		for(i = 0;i < 30;i++)
		{
			auc_i2c_write_buf[0] = 0x6a;
			reg_val[0] = reg_val[1] = 0x00;
			ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);
			
			if ((j +0x20+ 0x1000) == (((reg_val[0]) << 8) | reg_val[1]))
			{
			
				break;
			}
			msleep(1);

		}
		//DBG("55 reg_val[0]= %d\n",reg_val[0]);
			//DBG("55  reg_val[1]= %d\n",reg_val[1]);
		//DBG("wirte end 2\n");
	}

	if ((dw_lenth) % FTS_PACKET_LENGTH > 0) {
		temp = 0x1000+packet_number * FTS_PACKET_LENGTH;
		packet_buf[1] = (u8) (temp >> 16);
		packet_buf[2] = (u8) (temp >> 8);
		packet_buf[3] = (u8) temp;
		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (u8) (temp >> 8);
		packet_buf[5] = (u8) temp;

		for (i = 0; i < temp; i++) {
			packet_buf[6 + i] = pbt_buf[packet_number * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}        
		TW_DBG("wirte start II\n");
		ftxxxx_i2c_Write(client, packet_buf, temp + 6);
		
		for(i = 0;i < 30;i++)
		{
			auc_i2c_write_buf[0] = 0x6a;
			reg_val[0] = reg_val[1] = 0x00;
			ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

			if ((j +0x20+ 0x1000) == (((reg_val[0]) << 8) | reg_val[1]))
			{
				break;
			}
			msleep(1);

		}
		TW_DBG("wirte end II\n");
	}

	msleep(50);

	/*********Step 6: read out checksum***********************/
	/*send the opration head */
	TW_DBG("Step 6: read out checksum\n");
	auc_i2c_write_buf[0] = 0x64;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1); 
	msleep(300);
	
	temp = 0x1000+0;
	
	
	auc_i2c_write_buf[0] = 0x65;
	auc_i2c_write_buf[1] = (u8)(temp >> 16);
	auc_i2c_write_buf[2] = (u8)(temp >> 8);
	auc_i2c_write_buf[3] = (u8)(temp);
	
	if (dw_lenth > LEN_FLASH_ECC_MAX)
	{
		temp = LEN_FLASH_ECC_MAX;
	}
	else
	{
		temp = dw_lenth;
		TW_DBG("Step 6_1: read out checksum\n");
	}
	auc_i2c_write_buf[4] = (u8)(temp >> 8);
	auc_i2c_write_buf[5] = (u8)(temp);
	i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 6); 
	msleep(dw_lenth/256);

	for(i = 0;i < 100;i++)
	{
		auc_i2c_write_buf[0] = 0x6a;
		reg_val[0] = reg_val[1] = 0x00;
		ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

		if (0xF0==reg_val[0] && 0x55==reg_val[1])
		{
			break;
		}
		msleep(1);

	}
	//----------------------------------------------------------------------
	if (dw_lenth > LEN_FLASH_ECC_MAX)
	{
		temp = LEN_FLASH_ECC_MAX;//??? 0x1000+LEN_FLASH_ECC_MAX
		auc_i2c_write_buf[0] = 0x65;
		auc_i2c_write_buf[1] = (u8)(temp >> 16);
		auc_i2c_write_buf[2] = (u8)(temp >> 8);
		auc_i2c_write_buf[3] = (u8)(temp);
		temp = dw_lenth-LEN_FLASH_ECC_MAX;
		auc_i2c_write_buf[4] = (u8)(temp >> 8);
		auc_i2c_write_buf[5] = (u8)(temp);
		i_ret = ftxxxx_i2c_Write(client, auc_i2c_write_buf, 6); 

		msleep(dw_lenth/256);

		for(i = 0;i < 100;i++)
		{
			auc_i2c_write_buf[0] = 0x6a;
			reg_val[0] = reg_val[1] = 0x00;
			ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 2);

			if (0xF0==reg_val[0] && 0x55==reg_val[1])
			{
				break;
			}
			msleep(1);

		}
	}
	auc_i2c_write_buf[0] = 0x66;
	ftxxxx_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 1);
	if (reg_val[0] != bt_ecc) 
	{
		dev_err(&client->dev, "[FTS]--ecc error! FW=%02x bt_ecc=%02x\n",
			reg_val[0],
			bt_ecc);

		return -EIO;
	}
	printk(KERN_WARNING "checksum %X %X \n",reg_val[0],bt_ecc);       
	/*********Step 7: reset the new FW***********************/
	TW_DBG("Step 7: reset the new FW\n");
	auc_i2c_write_buf[0] = 0x07;
	ftxxxx_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(200);   //make sure CTP startup normally 
	//i_ret = HidI2c_To_StdI2c(client);//Android to Std i2c.

	//if(i_ret == 0)
	//{
	//	DBG("HidI2c change to StdI2c fail ! \n");
	//}

	return 0;
}


int fts_ctpm_fw_upgrade_with_config(struct i2c_client * client)//, unsigned char * firmware_name)
{	
	//downlaod, write lcd config
	//u8* pbt_buf = NULL;
	int i_ret;
	/*
	int fwsize = ft5x0x_GetFirmwareSize(firmware_name);
	int fwsize=current_fw_size;
	
	pbt_buf = (u8 *) kmalloc(fwsize+1,GFP_ATOMIC);
	pbt_buf=firmware_name;
	
	
	if(ft5x0x_ReadFirmware(firmware_name, pbt_buf))
	{
		dev_err(&client->dev, "%s() - ERROR: request_firmware failed\n", __FUNCTION__);
		kfree(pbt_buf);
		return -EIO;
	}
	*/

	i_ret = FT_IC_PROGRAM_FT8606_WritePram(client, aucFW_PRAM_BOOT, sizeof(aucFW_PRAM_BOOT));
	printk("zax FT_IC_PROGRAM_FT8606_WritePram=  %d\n",i_ret);

	
	if (i_ret != 0)
	{
		dev_err(&client->dev, "%s:upgrade failed. err.\n",__func__);
		return -EIO;
	}

	printk("zax 3== %d",current_fw_size);
	
	i_ret =  FT_IC_PROGRAM_FT8606_WriteFlash(client, current_fw_to_tp, current_fw_size);
	
	//i_ret =  fts_ctpm_8606_fw_WriteLcdConfig(client, pbt_buf, fwsize);
	if (i_ret != 0)
	{
		dev_err(&client->dev, "%s() - ERROR:[FTS] upgrade failed i_ret = %d.\n",__FUNCTION__,  i_ret);
	}
	printk("zax success %d\n",i_ret);
	//kfree(pbt_buf);
	
	return i_ret;
	

	// upgrade follow
	/*
	u8 * pbt_buf = NULL;
	int i_ret;
	int fw_len = sizeof(CTPM_FW);

	pbt_buf = CTPM_FW;

	i_ret = FT_IC_PROGRAM_FT8606_WritePram(client, aucFW_PRAM_BOOT, sizeof(aucFW_PRAM_BOOT));
	printk("FT_IC_PROGRAM_FT8606_WritePram=  %d\n",i_ret);
	if (i_ret != 0)
	{
		dev_err(&client->dev, "%s:upgrade failed. err.\n",__func__);
		return -EIO;
	}

	printk("zax upgrade== %d",fw_len);
		
		i_ret =  FT_IC_PROGRAM_FT8606_Upgrade(client, pbt_buf, sizeof(CTPM_FW));
		if (i_ret != 0)
		{
			dev_err(&client->dev, "%s() - ERROR:[FTS] upgrade failed i_ret = %d.\n",__FUNCTION__,  i_ret);
		}

	
	printk("upgrade success %d\n",i_ret);
	return i_ret;
	*/
	
}


#endif

/*upgrade from app.bin*/
static ssize_t ft5x0x_fwupgradeapp_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	char fwname[128];
	struct i2c_client *client = this_client;

	memset(fwname, 0, sizeof(fwname));
	sprintf(fwname, "%s", buf);
	fwname[count - 1] = '\0';

	disable_irq(client->irq);

	fts_ctpm_fw_upgrade_with_app_file(client, fwname);

	enable_irq(client->irq);

	return count;
}

/*upgrade from app.bin
*example:echo "*_app.bin" > ftsfwupgradeapp
*/
static DEVICE_ATTR(ftsfwupgradeapp, S_IRUGO | S_IWUSR, NULL,
			ft5x0x_fwupgradeapp_store);

#ifdef	FT_openshort_test
static DEVICE_ATTR(ftsscaptest, S_IRUGO|S_IWUSR, ftxxxx_ftsscaptest_show, ftxxxx_ftsscaptest_store);
#endif  
/*add your attr in here*/
static struct attribute *ft5x0x_attributes[] = {
	&dev_attr_ftsfwupgradeapp.attr,
	#ifdef	FT_openshort_test
	&dev_attr_ftsscaptest.attr,
	#endif	//FT_openshort_test
	NULL
};

static struct attribute_group ft5x0x_attribute_group = {
	.attrs = ft5x0x_attributes
};

#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
void yl_chg_status_changed(void)
{
	unsigned int chg_status;
	s32 ret = -1;
	u8 i2c_control_buf[2] = {0x8b};
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(this_client);
	struct input_dev *input_dev = ft5x0x_ts->input_dev;
	chg_status = touch_get_charger_status();
	printk("tw get charger status is: %d", chg_status);

	mutex_lock(&input_dev->mutex);
	if(ft5x0x_ts->tp_is_suspend){
		mutex_unlock(&input_dev->mutex);
		return;
	}
	
	if(chg_status == 0)
	{
		i2c_control_buf[1] =0x00;
		ret=ftxxxx_i2c_Write(this_client, i2c_control_buf, 2);
		if(ret < 0)
		{
			printk(KERN_ERR "%s: write 0x00 to 0x8b error\n",__func__);
		}
	}
	else
	{
		i2c_control_buf[1] =0x01;
		ret=ftxxxx_i2c_Write(this_client, i2c_control_buf, 2);
		if(ret < 0)
		{
			printk(KERN_ERR "%s:write 0x01 to 0x8b error\n",__func__);
		}
	}
	mutex_unlock(&input_dev->mutex);
}
#endif


/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/

/*******************************************************
   ********  1--active  0--not active***********
*******************************************************/
int ft5x06_active(void)
{
	//printk("enter %s\n",__FUNCTION__);
	return 1;
}
/*******************************************************
***********check firmware if need update***********
*******************************************************/
int ft5x06_firmware_need_update(void)
{
	unsigned char uc_fw_ver;
	unsigned char uc_if_ver;
	unsigned int ui_sz;
#if 	FOCALTECH_COB
	unsigned char uc_clb;
#endif

#if defined(CONFIG_BOARD_SK3_01)\
	||defined(CONFIG_BOARD_SK3_01_CT)\
	||defined(CONFIG_BOARD_SK3_02)
	unsigned char check_byte1;
	unsigned char check_byte2;
#endif

	printk("enter %s\n",__FUNCTION__);

	if (!current_tp_supported)
		return 0;

	uc_fw_ver = ft5x0x_read_fw_ver();
	printk("[FT]:host  firmware version = %x\n",uc_fw_ver);
	ui_sz = current_fw_size;
#ifdef FT8606
	if(uc_fw_ver>0x80)
	{
		uc_fw_ver = 0x01;
	}
	if(0==strcmp("LCD_TYPE_FT8606_BOYI_AUO_HD_50",lcd_name))//BOYI use all.i and TIANMA app.i
		uc_if_ver =current_fw_to_tp[4362];
	else if((0==strcmp("LCD_TYPE_FT8608_TIANMA_HD_50",lcd_name))||(0==strcmp("LCD_TYPE_FT8608_TIANMA2_HD_50",lcd_name)))
		uc_if_ver =current_fw_to_tp[266];
	printk("[FT]:uc_if_ver = %x\n",uc_if_ver);
#else
	if (ui_sz > 2)
		uc_if_ver = current_fw_to_tp[ui_sz - 2];
	else
		uc_if_ver = 0x00;
#endif
	printk("[FT]:new firmware version = %x\n",uc_if_ver);

#if defined(CONFIG_BOARD_SK3_01)\
	||defined(CONFIG_BOARD_SK3_01_CT)\
	||defined(CONFIG_BOARD_SK3_02)
	ft5x0x_read_reg(0xA3, &check_byte1);
	ft5x0x_read_reg(0x9F, &check_byte2);
	if(check_byte1!=0x54||check_byte2!=0x22)
	{
		printk("0xA3 !=0x54 or 0x9F!=0x22\n");
		return 1;
	}
#endif

#if 	FOCALTECH_COB
	uc_clb = ft5x0x_read_tp_calibrated();
//	      if(uc_clb)
//		return 1;      //changed by taokai


#endif

	if( uc_fw_ver < uc_if_ver || debug_flag)
		return 1;
	else
		return 0;
 }





/***********add for download*********/
/*******************************************************
*********************do firmware update ***************
*******************************************************/
int ft5x06_firmware_do_update(void)
{
	int ret = 0;
	printk("enter %s\n",__FUNCTION__);
	if (!current_tp_supported)
		return -1;
	disable_irq(this_client->irq);
	#ifdef FT8606
	ret=fts_ctpm_fw_upgrade_with_config(this_client);
	#else
	ret = fts_ctpm_fw_upgrade_with_i_file();
	#endif
	enable_irq(this_client->irq);
	if (ret != 0)
		return -1;
	else
		return 0;
}

/*******************************************************
*******************check if need calibrate***********
*******************************************************/

int ft5x06_need_calibrate(void)
{
	TW_DBG("enter %s\n",__FUNCTION__);
	return 1;
}

/*******************************************************
 ******************system write "calibrate"************
*******************************************************/

int ft5x06_calibrate(void)
{
	TW_DBG("enter %s\n",__FUNCTION__);
	mdelay(2000);

	if(!fts_ctpm_auto_clb())
		return 0;
	else
		return -1;
}

/*******************************************************
 ******************get firmware version **************
*******************************************************/
int ft5x06_get_firmware_version(char * version )
{
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(this_client);
	TW_DBG("enter %s\n",__FUNCTION__);


	if (!current_tp_supported)
		return sprintf(version, "%s%x:%s","invalid touch panel id:",current_tp_id, "NA");

	if (0==ft5x0x_ts->tp_is_suspend)
		current_tp_version= ft5x0x_read_fw_ver();
	return sprintf(version, "%s:%s:0x%x:0x%x", current_tp_name, FOCALTECH_SOC, current_tp_version,current_tp_version);
}

/*******************************************************
  ******************system write "reset"***************
*******************************************************/
int ft5x06_reset_touchscreen(void)
{
	TW_DBG(KERN_ERR "enter %s\n",__FUNCTION__);

	disable_irq_nosync(this_client->irq);

	/*write 0xaa to register 0xfc*/
	ft5x0x_write_reg(0xfc,0xaa);
	delay_qt_ms(50);
	/*write 0x55 to register 0xfc*/
	ft5x0x_write_reg(0xfc,0x55);
	delay_qt_ms(30);
	printk(KERN_ERR "[FTS]: Reset CTPM ok!\n");

	enable_irq(this_client->irq);
	return 1;
}

/*******************************************************
  ******************"handwrite" "normal" *************
*******************************************************/

touch_mode_type ft5x06_get_mode(void)
{
	printk("enter %s\n",__FUNCTION__);
	return current_mode_flag;
}

int ft5x06_set_mode(touch_mode_type work_mode)
{
#ifdef CONFIG_TOUCHSCREEN_GLOVE
	int ret = -1;	 
	u8 auc_i2c_write_buf[2] =	{0xc0,0x00};
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(this_client);
	if (this_client== NULL)
		return -1;

	if(current_mode_flag == work_mode)
		return 1;
	mutex_lock(&ft5x0x_ts->device_mode_mutex);
	current_mode_flag=work_mode;
	if(current_mode_flag==MODE_GLOVE)
	{
		auc_i2c_write_buf[1] = 0x01;
	}
	 
	ret = ft5x0x_i2c_txdata(auc_i2c_write_buf,2);
	if (ret < 0) 
	{
		 pr_err("%s write reg 0xc0 failed!\n",__func__);
	}	 
	mutex_unlock(&ft5x0x_ts->device_mode_mutex);
#endif
	printk("%s: glove_switch  = %d\n", __func__,current_mode_flag);
	return 1;

}

/*******************************************************
  ****************get "oreitation:X" ************
*******************************************************/
/* litao3 modify for compile
enum touch_oreintation_type ft5x06_get_oreitation(void)
{
	printk("enter %s\n",__FUNCTION__);
	return 1;
}


int ft5x06_set_oreitation(enum touch_oreintation_type oreitate)
{
	printk("enter %s\n",__FUNCTION__);
	return 1;
}
litao3 end */

/*******************************************************
  **********read buf[256]: ef:ab [???????]?êo??ì  ********
*******************************************************/
int ft5x06_read_regs(char * buf)
{
	int ret;
	u8 reg, val;
	int i = 0;

	printk("enter %s\n",__FUNCTION__);

	for(i=0; i<0xD8; i++)
	{
		reg = i;
		ret = ft5x0x_read_reg(reg, &val);
		printk("CP_Touchscreen: reg0x%x = 0x%x.\n", reg, val);
	}

	return 1;
}

/*******************************************************
  **********write buf[256]: ef:ab [???????]?êo??ì ********
*******************************************************/
int ft5x06_write_regs(const char * buf)
{
	printk("enter %s\n",__FUNCTION__);
	return 1;
}

/*******************************************************
  ***************tw debug on or off *************
*******************************************************/
int ft5x06_debug(int val)
{
	printk("enter %s debug=%d\n",__FUNCTION__,val);
	debug_flag = val;
	return 1;
}
//add by taokai
int focaltech_vendor(char* vendor)
{
	return sprintf(vendor,"%s","focaltech");
}
int focaltech_get_wakeup_gesture(char*  gesture)
{
#ifdef CONFIG_TOUCHSCREEN_GESTURE_WAKEUP
	int i;
	char wakeup_gesture[64]={0};

	if(support_gesture == 0)
		return sprintf(gesture, "%s", "none");

	for (i = 0; i < ARRAY_SIZE(tp_slide_wakeup); i++) {
		if (support_gesture & tp_slide_wakeup[i].mask) {
			if(wakeup_gesture[0] != 0)
				sprintf(wakeup_gesture + strlen(wakeup_gesture), "%s", ",");
			sprintf(wakeup_gesture + strlen(wakeup_gesture) , "%s", tp_slide_wakeup[i].gesture);
		}
	}

	return sprintf(gesture, "%s", wakeup_gesture);
#endif
	return 1;
}
int focaltech_gesture_ctrl(const char*  gesture_buf)
{
#ifdef CONFIG_TOUCHSCREEN_GESTURE_WAKEUP
	char *gesture;
	int buf_len;
	char *gesture_p=NULL;
	char *temp_p;
	char tmp_buf[32]={0};
	int i;
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(this_client);
	struct input_dev *input_dev = ft5x0x_ts->input_dev;
	buf_len = strlen(gesture_buf);
	TW_DBG("%s buf_len = %d.", __func__, buf_len);
	printk("%s gesture_buf:%s.\n", __func__, gesture_buf);
	mutex_lock(&input_dev->mutex);
	gesture_p = kzalloc(buf_len + 1, GFP_KERNEL);
	if(gesture_p == NULL) {
		TW_DBG("%s: alloc mem error.", __func__);
		mutex_unlock(&input_dev->mutex);
		return -1;
	}
	temp_p = gesture_p;
	strlcpy(gesture_p, gesture_buf, buf_len + 1);
	while (gesture_p) {
		gesture = strsep(&gesture_p, ",");
		TW_DBG("%s gesture:%s.", __func__, gesture);

		for (i = 0; i < ARRAY_SIZE(tp_slide_wakeup); i++) {
			memset(tmp_buf, 0, 32);
			strcpy(tmp_buf, tp_slide_wakeup[i].gesture);
			if (!strncmp(gesture, tmp_buf, strlen(tmp_buf))) {
				if (!strncmp(gesture+strlen(tmp_buf), "=true", 5)) {
					TW_DBG("%s: enable up slide wakeup func.", tmp_buf);
					support_gesture |= tp_slide_wakeup[i].mask;
					break;
				} else if (!strncmp(gesture+strlen(tmp_buf), "=false", 6)) {
					TW_DBG("%s: disable up slide wakeup func.", tmp_buf);
					support_gesture &= ~tp_slide_wakeup[i].mask;
					break;
				}
			}
		}
	}
	mutex_unlock(&input_dev->mutex);
#if defined(CONFIG_BOARD_K2_01)||defined(CONFIG_BOARD_K2_02)
	if(1==backlight_on)
	{
		if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
		{
			gesture_flag=1;
		}
		else
		{
			gesture_flag=0;
		}
	}
	else
	{
		if(support_gesture & TW_SUPPORT_GESTURE_IN_ALL)
		{
			gesture_flag=1;
		}
	}
		
#else
	if(0==backlight_on)
	{
		ft5x0x_ts_resume(&ft5x0x_ts->client->dev);
		msleep(5);
		ft5x0x_ts_suspend(&ft5x0x_ts->client->dev);
	}
#endif
	kfree(temp_p);
#endif
	return 1;
}
struct touchscreen_funcs focaltech_ops=
{
	.touch_id					= 0,
	.touch_type			  = 1,
	.active						= ft5x06_active,
	.firmware_need_update		= ft5x06_firmware_need_update,
	.firmware_do_update			= ft5x06_firmware_do_update,
	.need_calibrate				= ft5x06_need_calibrate,
	.calibrate					= ft5x06_calibrate,
	.get_firmware_version		= ft5x06_get_firmware_version,
	.reset_touchscreen			= ft5x06_reset_touchscreen,
	.get_mode					=  ft5x06_get_mode,
	.set_mode					=  ft5x06_set_mode,
	/*litao3 modify for compile
	.get_oreitation				= ft5x06_get_oreitation,
	.set_oreitation				= ft5x06_set_oreitation,
modify end */
	.read_regs					= ft5x06_read_regs,
	.write_regs					= ft5x06_write_regs,
	.debug						= ft5x06_debug,
//add by taokai
  	.get_vendor     =focaltech_vendor,
	.get_wakeup_gesture     = focaltech_get_wakeup_gesture,
	.gesture_ctrl           = focaltech_gesture_ctrl,
};
/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/
static int ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ft5x0x_ts_data *ft5x0x_ts;
    struct input_dev *input_dev;
    struct tw_platform_data *pdata;//maleijie
    int ret = 0;
    int err_time = 2;
    int i=0;
  //  int err=0;
    //unsigned char report;
    //int repair_result = 0;

//    printk("TOUCH:maleijie:probe start\n");   //changed by taokai
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
    {
        ret = -ENODEV;
        goto exit_check_functionality_failed;
    }
#ifdef CONFIG_OF
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct tw_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		ret = goodix_ts_parse_dt(&client->dev, pdata);
		if (ret)
		{
			dev_err(&client->dev, "Failed to parse dt\n");
			devm_kfree(&client->dev, pdata);
			return ret;
		}
	} else
#endif
	pdata = client->dev.platform_data;
    if(!pdata)
    {
        printk("dev platform data is null.");
        return -ENODEV;
    }

    this_client = client;
    ft5x0x_ts = kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
    if (!ft5x0x_ts)
    {
        ret = -ENOMEM;
        goto exit_alloc_data_failed;
    }

    input_dev = input_allocate_device();
    if (!input_dev)
    {
        ret = -ENOMEM;
        dev_err(&client->dev, "failed to allocate input device\n");
        goto exit_input_dev_alloc_failed;
    }
    ft5x0x_ts->input_dev = input_dev;
    ft5x0x_ts->pdata = pdata;//client->dev.platform_data;	  //maleijie
    ft5x0x_ts->irq = gpio_to_irq(ft5x0x_ts->pdata->gpio_irq);
    ft5x0x_ts->client = client;

	 /* init data */
	SCREEN_MAX_X = ft5x0x_ts->pdata->screen_x;
	SCREEN_MAX_Y = ft5x0x_ts->pdata->screen_y;

    if (ft5x0x_ts->pdata->init)
    {
   		ret = ft5x0x_ts->pdata->init();
		if (ret)
		{
			pr_err("TOUCH:YLLOG:failed to init resource in probe\n");
			goto exit_init;
		}
	}

	if (ft5x0x_ts->pdata->power)
	{
		ret = ft5x0x_ts->pdata->power(1);
		if (ret)
		{
			pr_err("TOUCH:YLLOG:failed to power up in probe\n");
			goto exit_power;
		}
	}

	if (ft5x0x_ts->pdata->reset)
	{
		ret = ft5x0x_ts->pdata->reset(200);//the time changed by zdd
		if (ret)
		{
			pr_err("TOUCH:YLLOG:failed to reset in probe\n");
			goto exit_reset;
		}
	}


    i2c_set_clientdata(client, ft5x0x_ts);

#if 1 //wfs
read_id:
    ret = ft5x0x_read_tp_type();
    current_tp_id = ret;
    current_tp_version = ft5x0x_read_fw_ver();
    pr_err("touch screen read firemware tp_id = 0x%x tp_version = 0x%x!\n", current_tp_id, current_tp_version);

    if(ret >= 0)
    {
		ret = fts_check_tp_id(current_tp_id);
    }
    if (ret < 0){
       pr_err("current tp id is not exist in tp firmware info list,and it will not support update firmware\n");
       current_tp_supported = false;
       //fts_ctpm_fw_upgrade_with_tp_id(0x55);
       if (err_time-- > 0)
       {
            goto read_id;
       }
       else{
          if (current_tp_id == 255)
	  {
            goto exit_power;
          }
       }
    }
    else{
      fts_get_current_tp_info(ret);
    }
#endif	//wfs

    input_set_abs_params(input_dev,
		          ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev,
		          ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
    input_set_abs_params(input_dev,
		          ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
    input_set_abs_params(input_dev,
		          ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
    input_set_abs_params(input_dev,
		          ABS_MT_TRACKING_ID, 0, 5, 0, 0);
    input_set_abs_params(input_dev,
		          ABS_MT_PRESSURE, 0, PRESS_MAX, 0, 0);

    set_bit(EV_ABS, input_dev->evbit);
    set_bit(EV_SYN, input_dev->evbit);
    set_bit(INPUT_PROP_DIRECT, input_dev->propbit);


#if CFG_SUPPORT_TOUCH_KEY
    set_bit(EV_KEY, input_dev->evbit);
    //set_bit(BTN_TOUCH, input_dev->keybit);
    for(i = 0; i < ft5x0x_ts->pdata->nbuttons; i++)
    {
        input_set_capability(input_dev, EV_KEY, ft5x0x_ts->pdata->buttons[i].code);
        ft5x0x_ts->pdata->buttons[i].key_status = KEY_RELEASE;
    }

    DEBOUNCE     = ft5x0x_ts->pdata->key_debounce;
    if(DEBOUNCE)
    {
	inDebounce = false;
	setup_timer(&ft5x0x_ts->timer, ft_keys_timer, (unsigned long)ft5x0x_ts);
    }
#endif

    input_dev->name	= FT5X0X_NAME;
    input_dev->dev.parent = &client->dev;//add by anxufeng
    ret = input_register_device(input_dev);
    if (ret)
    {
        dev_err(&client->dev,"ft5x0x_ts_probe: failed to register input device: %s\n",
        dev_name(&client->dev));
        goto exit_input_register_device_failed;
    }
    ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
    if (!ft5x0x_ts->ts_workqueue)
    {
        ret = -ESRCH;
        dev_err(&client->dev, "ft5x0x_probe: create_singlethread_workqueue failed\n");
        goto exit_create_singlethread;
    }

#if defined(CONFIG_FB)
	ft5x0x_ts->fb_notif.notifier_call = fb_notifier_callback;
	ret = fb_register_client(&ft5x0x_ts->fb_notif);
	if (ret)
		dev_err(&client->dev, "Unable to register fb_notifier: %d\n",ret);
	else
		printk("Leon : %s  fb register client suscess.", __func__);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	    ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	    ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_suspend;
	    ft5x0x_ts->early_suspend.resume	= ft5x0x_ts_resume;
	    register_early_suspend(&ft5x0x_ts->early_suspend);
#endif


 /******for fw miss or upgrade fail**********/
/*
	err = fts_ctpm_chk_upg_probe();
	if(err == 1)
	//if(1)
	{
		printk(KERN_ERR "[fts]:down load update firmware.\n");
		ft5x06_firmware_do_update();
		//fts_ctpm_auto_upg();	///open it for update firmware. linronghui. 2012.09.29. modify place 6/6.
	}
*/
 /*************************************/
    mutex_init(&ft5x0x_ts->device_mode_mutex);
    INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);
    ret = request_irq(ft5x0x_ts->irq, ft5x0x_ts_interrupt, ft5x0x_ts->pdata->irqflag, client->dev.driver->name,ft5x0x_ts);
    if (ret < 0)
    {
        dev_err(&client->dev, "ft5x0x_probe: request irq failed\n");
        goto exit_irq_request_failed;
    }
	touch_flag =0;

    touchscreen_set_ops(&focaltech_ops);
	
#ifdef CONFIG_TOUCHSCREEN_CHARGER_STATUS_CHECK
		touch_register_charger_notify(&yl_chg_status_changed);
#endif

    ret = sysfs_create_group(&client->dev.kobj, &ft5x0x_attribute_group);//maleijie
	if(ret<0)
	{
		dev_err(&client->dev, "sysfs create group failed\n");
	}
    ret= ft5x06_firmware_need_update();
	if (0)	//(ret)
	{
		mutex_lock(&ft5x0x_ts->device_mode_mutex);
 		if(0==ft5x06_firmware_do_update())
			dev_err(&client->dev, "probe firmware update sucess\n");
		mutex_unlock(&ft5x0x_ts->device_mode_mutex);
	}
#if defined(CONFIG_BOARD_SK3_01)\
	||defined(CONFIG_BOARD_SK3_01_CT)\
	||defined(CONFIG_BOARD_SK3_02)
	if(fts_ctpm_chk_upg())
	{
		mutex_lock(&ft5x0x_ts->device_mode_mutex);
 		if(0==ft5x06_firmware_do_update())
			dev_err(&client->dev, "probe firmware update sucess\n");
		mutex_unlock(&ft5x0x_ts->device_mode_mutex);		
	}
#endif
	 current_tp_version = ft5x0x_read_fw_ver();
    printk(KERN_ERR"TOUCH:maleijie:probe complete\n");
    return 0;


exit_irq_request_failed:
    destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
    input_unregister_device(input_dev);
exit_input_register_device_failed:
exit_reset:
exit_power:
    ft5x0x_ts->pdata->release();
exit_init:
    input_free_device(input_dev);
exit_input_dev_alloc_failed:
    i2c_set_clientdata(client, NULL);
    kfree(ft5x0x_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
return ret;
}
/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/
static int __exit ft5x0x_ts_remove(struct i2c_client *client)
{
    struct ft5x0x_ts_data *ft5x0x_ts;
    ft5x0x_ts = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ft5x0x_ts->early_suspend);
#endif
#ifdef FT_openshort_test
	mutex_destroy(&g_device_mutex);
#endif //FT_openshort_test
    mutex_destroy(&ft5x0x_ts->device_mode_mutex);
    cancel_work_sync(&ft5x0x_ts->pen_event_work);
    destroy_workqueue(ft5x0x_ts->ts_workqueue);
    free_irq(ft5x0x_ts->irq, ft5x0x_ts);
    input_unregister_device(ft5x0x_ts->input_dev);
    kfree(ft5x0x_ts);
    i2c_set_clientdata(client, NULL);
    return 0;
}
#ifdef CONFIG_OF
static struct of_device_id ft5x06_match_table[] = {
	{ .compatible = "focaltech,fts",},
	{ },
};
#else
#define ft5x06_match_table NULL
#endif

static const struct i2c_device_id ft5x0x_ts_id[] = {
    { FT5X0X_NAME, 0 },{ }
};

MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
    .probe		= ft5x0x_ts_probe,
    .remove	= ft5x0x_ts_remove,
    .id_table	= ft5x0x_ts_id,
    .driver	= {
       .name	= FT5X0X_NAME,
       .owner	= THIS_MODULE,
       .of_match_table = ft5x06_match_table,
       .pm      = &ft5x0x_ts_pmops,
    },
};

/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/
static int __init ft5x0x_ts_init(void)
{
    int ret;
    ret = i2c_add_driver(&ft5x0x_ts_driver);
    return ret;

}

/***********************************************************************************************
Name	:

Input	:

Output	:

function	:

***********************************************************************************************/
static void __exit ft5x0x_ts_exit(void)
{
    i2c_del_driver(&ft5x0x_ts_driver);

}

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
