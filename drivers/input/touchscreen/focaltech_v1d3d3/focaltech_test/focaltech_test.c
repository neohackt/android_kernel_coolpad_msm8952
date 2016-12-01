/*
 *
 * FocalTech TouchScreen driver.
 * 
 * Copyright (c) 2010-2016, FocalTech Systems, Ltd., all rights reserved.
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
 */

 /************************************************************************
*
* File Name: focaltech_test.c
*
* Author:	  Software Department, FocalTech
*
* Created: 2016-03-24
*   
* Modify:
*
* Abstract: create char device and proc node for  the comm between APK and TP
*
************************************************************************/

/*******************************************************************************
* Included header files
*******************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <asm/uaccess.h>

#include <linux/i2c.h>//iic
#include <linux/delay.h>//msleep

//#include "../focaltech_comm.h"
#include "../focaltech_core.h"
#include "focaltech_test_main.h"
#include "focaltech_test_ini.h"

/*******************************************************************************
* Private constant and macro definitions using #define
*******************************************************************************/
#define FOCALTECH_TEST_INFO  "File Version of  focaltech_test.c:  V1.0.0 2016-03-24"

//�����ļ����Ŀ¼����
#define FTS_INI_FILE_PATH "/mnt/sdcard/"  

#define FTS_TEST_BUFFER_SIZE		5*1024
#define FTS_TEST_PRINT_SIZE		128
/*******************************************************************************
* Private enumerations, structures and unions using typedef
*******************************************************************************/


/*******************************************************************************
* Static variables
*******************************************************************************/
static int  openshort_test = 0;

/*******************************************************************************
* Global variable or extern global variabls/functions
*******************************************************************************/


/*******************************************************************************
* Static function prototypes
*******************************************************************************/
static int fts_test_get_ini_size(char *config_name);
static int fts_test_read_ini_data(char *config_name, char *config_buf);
static int fts_test_save_test_data(char *file_name, char *data_buf, int iLen);
static int fts_test_get_testparam_from_ini(char *config_name);
static int fts_test_entry(char *ini_file_name);


/*******************************************************************************
* functions body
*******************************************************************************/

//��ȡ�����ļ���С, ���ڷ����ڴ��ȡ����
static int fts_test_get_ini_size(char *config_name)
{
	struct file *pfile = NULL;
	struct inode *inode = NULL;
	//unsigned long magic;
	off_t fsize = 0;
	char filepath[128];
	memset(filepath, 0, sizeof(filepath));

	sprintf(filepath, "%s%s", FTS_INI_FILE_PATH, config_name);

	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);

	if (IS_ERR(pfile)) {
		FTS_DBG("error occured while opening file %s.",  filepath);
		return -EIO;
	}

	inode = pfile->f_dentry->d_inode;
	//magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	filp_close(pfile, NULL);

	return fsize;
}
//��ȡ���õ��ڴ�
static int fts_test_read_ini_data(char *config_name, char *config_buf)
{
	struct file *pfile = NULL;
	struct inode *inode = NULL;
	//unsigned long magic;
	off_t fsize = 0;
	char filepath[128];
	loff_t pos = 0;
	mm_segment_t old_fs;

	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s%s", FTS_INI_FILE_PATH, config_name);
	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(pfile)) {
		FTS_DBG("error occured while opening file %s.",  filepath);
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
//����������ݵ�SD�� etc.
static int fts_test_save_test_data(char *file_name, char *data_buf, int iLen)
{
	struct file *pfile = NULL;
	
	char filepath[128];
	loff_t pos;
	mm_segment_t old_fs;

	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s%s", FTS_INI_FILE_PATH, file_name);
	if (NULL == pfile)
		pfile = filp_open(filepath, O_CREAT|O_RDWR, 0);
	if (IS_ERR(pfile)) {
		FTS_DBG("error occured while opening file %s.",  filepath);
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

//��ȡ,���������ļ�,��ʼ�����Ա���
static int fts_test_get_testparam_from_ini(char *config_name)
{
	char *filedata = NULL;
	int ret = 0;

	int inisize = fts_test_get_ini_size(config_name);

	FTS_DBG("ini_size = %d ", inisize);
	if (inisize <= 0) {
		FTS_DBG("%s ERROR:Get firmware size failed",  __func__);
		return -EIO;
	}

	filedata = kmalloc(inisize + 1, GFP_ATOMIC);
		
	if (fts_test_read_ini_data(config_name, filedata)) {
		FTS_DBG("%s() - ERROR: request_firmware failed",  __func__);
		kfree(filedata);
		return -EIO;
	} else {
		FTS_DBG("fts_test_read_ini_data successful");
	}

	ret = set_param_data(filedata);
	if(ret < 0)
		return ret;
	
	return 0;
}

/////////////////////////////////
//���Կ���������
///////////////////////////////////
static int fts_test_entry(char *ini_file_name)
{
	/* place holder for future use */
    	char cfgname[128];
	char *testdata = NULL;
	char *printdata = NULL;
	int iTestDataLen=0;//���в�������ʵ�ʳ���,���ڱ��浽�ļ�
	int ret = 0;
	int icycle = 0, i =0;
	int print_index = 0;
	

	FTS_DBG("");
	/*���ڻ�ȡ����ڿ��еĲ�������,ע�����ռ��С.*/
	FTS_DBG("Allocate memory, size: %d", FTS_TEST_BUFFER_SIZE);
	testdata = kmalloc(FTS_TEST_BUFFER_SIZE, GFP_ATOMIC);
	if(NULL == testdata)
	{
		//printk("kmalloc failed in function:%s",  __func__);
		FTS_DBG("kmalloc failed in function:%s",  __func__);
		return -1;
	}
	printdata = kmalloc(FTS_TEST_PRINT_SIZE, GFP_ATOMIC);
	if(NULL == printdata)
	{
		//printk("kmalloc failed in function:%s",  __func__);
		FTS_DBG("kmalloc failed in function:%s",  __func__);
		return -1;
	}
	/*��ʼ��ƽ̨��ص�I2C��д����*/
	init_i2c_write_func(fts_i2c_write);
	init_i2c_read_func(fts_i2c_read);

	/*��ʼ��ָ���ڴ�*/
	ret = focaltech_test_main_init();
	if(ret < 0)
	{
		FTS_DBG("focaltech_test_main_init() error.");
		goto TEST_ERR;
	}		

	/*��ȡ���������ļ�*/
	memset(cfgname, 0, sizeof(cfgname));
	sprintf(cfgname, "%s", ini_file_name);
	FTS_DBG("ini_file_name = %s", cfgname);
	if(fts_test_get_testparam_from_ini(cfgname) <0)
	{
		FTS_DBG("get testparam from ini failure");
		ret = -1;
		goto TEST_ERR;
	}

	/*���ݲ������ÿ�ʼ����*/
	if(true == start_test_tp())
	{
		FTS_DBG("tp test pass");
		openshort_test = 1;
	}
	else
	{
		FTS_DBG("tp test failure");
		openshort_test = 0;
	}
		
	/*��ȡ���Կ��еĲ�������������*/
	iTestDataLen = get_test_data(testdata);

	icycle = 0;
	/*��ӡ�������ݰ� */
	FTS_DBG("print test data: \n");
	for(i = 0; i < iTestDataLen; i++)
	{
		if(('\0' == testdata[i])//����������
			||(icycle == FTS_TEST_PRINT_SIZE -2)//�����ӡ�ַ�������Ҫ��
			||(i == iTestDataLen-1)//�������һ���ַ�
		)
		{
			if(icycle == 0)
			{
				print_index++;
			}	
			else
			{
				memcpy(printdata, testdata + print_index, icycle);
				printdata[FTS_TEST_PRINT_SIZE-1] = '\0';
				printk("%s", printdata);
				print_index += icycle;
				icycle = 0;
			}
		}
		else
		{
			icycle++;
		}
	}
	printk("\n");		

	fts_test_save_test_data("testdata.csv", testdata, iTestDataLen);

	/*�ͷ��ڴ��... */
	focaltech_test_main_exit();
	
	//mutex_unlock(&g_device_mutex);
	ret = 0;
	
TEST_ERR:
	if(NULL != testdata) kfree(testdata);
	if(NULL != printdata) kfree(printdata);	
	return ret;
}


/************************************************************************
* Name: fts_test_show
* Brief:  no
* Input: device, device attribute, char buf
* Output: no
* Return: EPERM
***********************************************************************/
static ssize_t fts_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	/* place holder for future use */
	return sprintf(buf, "%d\n",openshort_test);
}

/************************************************************************
* Name: fts_test_store
* Brief:  upgrade from app.bin
* Input: device, device attribute, char buf, char count
* Output: no
* Return: char count
***********************************************************************/
static ssize_t fts_test_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	char fwname[128];
	struct i2c_client *client = fts_i2c_client;
	memset(fwname, 0, sizeof(fwname));
	sprintf(fwname, "%s", buf);
	fwname[count-1] = '\0';

	mutex_lock(&fts_input_dev->mutex);
	
	disable_irq(client->irq);
	fts_test_entry( fwname);
	enable_irq(client->irq);
	
	mutex_unlock(&fts_input_dev->mutex);

	return count;
}
/*  upgrade from app.bin
*    example:echo "***.ini" > fts_test
*/
static DEVICE_ATTR(fts_test, S_IRUGO|S_IWUSR, fts_test_show, fts_test_store);

/* add your attr in here*/
static struct attribute *fts_test_attributes[] = {
	&dev_attr_fts_test.attr,
	NULL
};

static struct attribute_group fts_test_attribute_group = {
	.attrs = fts_test_attributes
};

int fts_test_init(void)
{
	int err=0;
	
	FTS_DBG("[focal] %s ",  FOCALTECH_TEST_INFO);	//show version
	FTS_DBG("");//default print: current function name and line number
	
	err = sysfs_create_group(&fts_i2c_client->dev.kobj, &fts_test_attribute_group);
	if (0 != err) 
	{
		FTS_DBG( "[focal] %s() - ERROR: sysfs_create_group() failed.",  __func__);
		sysfs_remove_group(&fts_i2c_client->dev.kobj, &fts_test_attribute_group);
		return -EIO;
	} 
	else 
	{
		FTS_DBG("[focal] %s() - sysfs_create_group() succeeded.", __func__);
	}
	//fts_protocol_windows_to_android(client);
	return err;
}
int fts_test_exit(void)
{	
	FTS_DBG("");//default print: current function name and line number
	sysfs_remove_group(&fts_i2c_client->dev.kobj, &fts_test_attribute_group);
	
	return 0;
}

