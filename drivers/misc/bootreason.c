/*************************************************************************/
/*                                                                       */
/* Copyright (c) 2009-2010  YULONG Company             ¡¡¡¡¡¡¡¡¡¡¡¡¡¡    */
/*                 ÓîÁúŒÆËã»úÍšÐÅ¿ÆŒŒ£šÉîÛÚ£©ÓÐÏÞ¹«ËŸ  °æÈšËùÓÐ 2005-2007*/
/*                                                                       */
/* PROPRIETARY RIGHTS of YULONG Company are involved in the  ¡¡¡¡¡¡¡¡¡¡¡¡*/
/* subject matter of this material.  All manufacturing, reproduction, use*/
/* ,and sales rights pertaining to this subject matter are governed by   */
/* the license agreement.  The recipient of this software implicitly     */
/* accepts the terms of the license.                                     */
/* ±ŸÈíŒþÎÄµµ×ÊÁÏÊÇÓîÁú¹«ËŸµÄ×Ê²ú,ÈÎºÎÈËÊ¿ÔÄ¶ÁºÍÊ¹ÓÃ±Ÿ×ÊÁÏ±ØÐë»ñµÃ       */
/* ÏàÓŠµÄÊéÃæÊÚÈš,³Ðµ£±£ÃÜÔðÈÎºÍœÓÊÜÏàÓŠµÄ·šÂÉÔŒÊø.                      */
/*                                                                       */
/*************************************************************************/
/*************************************************************************
* Copyright (C), 2005-2006, Yulong Co., Ltd.
*
* File name:    board-cp9130-bootreason.c
* Description:  Íâ²¿œÓ¿Ú
* Others:

* Department:  system Software Development 
* Author:      
* Version:     V1.00 
* Date:        2009-7-27

* Function List:  
1. ...



2. Author:      yeruiquan
   Date:        2009/7/20
   Modificaton: optimize the handle of callback
*************************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
//#include <plat/board.h>


static char bootreason[30];
static char append_boot_reason[30];
static int ftm = 0;
static int cts=0;

static int __init yl_cts_setup( char *line )
{
	cts = 1;
	return 1;
}
__setup("yl.cts",yl_cts_setup);

static int __init boot_reason_setup( char *line )
{
	memset(bootreason,0,20);
	strlcpy(bootreason,line,sizeof(bootreason));

	return 1;
}
__setup("yulong.boot_event=",boot_reason_setup);

static int __init append_boot_reason_setup( char *line )
{
	memset(append_boot_reason,0,20);
	strlcpy(append_boot_reason, line, sizeof(append_boot_reason));

	return 1;
}
__setup("append_reason=", append_boot_reason_setup);

int yl_set_bootreason( char *line)
{
	if( line == NULL ){
		return -1;
	}

	memset(bootreason,0,20);
	strlcpy(bootreason,line,sizeof(bootreason));

	return 1;
}

int yl_get_bootreason( char *out, unsigned int len )
{
	if( out == NULL ){
		return -1;
	}

	if( sizeof(bootreason) <= len ){
		return -1;
	}

	memcpy( out, bootreason, len );

	return 0;
}
EXPORT_SYMBOL(yl_get_bootreason);

int yl_get_appendbootreason(char *out, unsigned int len )
{
	if (out == NULL)
		return -1;

	if (sizeof(append_boot_reason) <= len )
		return -1;

	memcpy(out, append_boot_reason, len);
	return 0;
}
EXPORT_SYMBOL(yl_get_appendbootreason);

static int __init ftm_setup( char *line )
{
	ftm = 1;

	return 1;
}
__setup("androidboot.mode=ffbm-02",ftm_setup);


int yl_get_ftm(void)
{
    return ftm;

}
EXPORT_SYMBOL(yl_get_ftm);

/*static int yl_bootreason_read_proc(char *page, char **start, off_t off,
					 int count, int *eof, void *data)
{
	int len;
	char *reason = data;

	len = sprintf(page, "%s\n", reason);

	*start = page + off;

	if (len > off)
		len -= off;
	else
		len = 0;

	return len < count ? len  : count;
}*/

static ssize_t yl_cts_read_proc(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	u16 data_len = 0;
	s32 ret;
	u8 buf[32] = {0};

	data_len = scnprintf(buf, sizeof(buf), "%d\n", cts);

	pr_err("%s[%d]---%s\n",__func__, __LINE__, buf);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, data_len);

	return ret;
}

static ssize_t yl_bootreason_read_proc(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	u16 data_len = 0;
	s32 ret;
	u8 buf[32] = {0};

	data_len = scnprintf(buf, sizeof(buf), "%s\n", bootreason);

	pr_err("%s[%d]---%s\n",__func__, __LINE__, buf);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, data_len);

	return ret;
}

static ssize_t yl_append_bootreason_read_proc(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	u16 data_len = 0;
	s32 ret;
	u8 buf[32] = {0};

	data_len = scnprintf(buf, sizeof(buf), "%s\n", append_boot_reason);

	pr_err("%s[%d]---%s\n",__func__, __LINE__, buf);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, data_len);

	return ret;
}

static struct proc_dir_entry *yl_cts_proc_entry;
static const struct file_operations yl_cts_proc_fops = {
	.read = yl_cts_read_proc,
	.owner = THIS_MODULE,
};


static struct proc_dir_entry *yl_bootreason_proc_entry;
static const struct file_operations yl_bootreason_proc_fops = {
	.read = yl_bootreason_read_proc,
	.owner = THIS_MODULE,
};

static struct proc_dir_entry *yl_append_bootreason_proc_entry;
static const struct file_operations yl_append_bootreason_proc_fops = {
	.read = yl_append_bootreason_read_proc,
	.owner = THIS_MODULE,
};


static int __init yl_bootreason_init( void )
{
	if (!strlen(bootreason))
		strcpy(bootreason,"unkown");
	
	printk(KERN_ERR "yulong cts: %d\n", cts);
	printk(KERN_ERR "Bootup reason: %s\n", bootreason);
	printk(KERN_ERR "append_boot_reason: %s\n", append_boot_reason);

//	if (!create_proc_read_entry("bootreason", S_IRUGO, NULL, yl_bootreason_read_proc, bootreason))
//		return -ENOMEM;
//	if (!create_proc_read_entry("append_reason", S_IRUGO, NULL, yl_bootreason_read_proc, append_boot_reason))
//		return -ENOMEM;

	yl_cts_proc_entry = proc_create("yl_cts", S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP,
		yl_cts_proc_entry, &yl_cts_proc_fops);

	yl_bootreason_proc_entry = proc_create("bootreason", S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP,
		yl_bootreason_proc_entry, &yl_bootreason_proc_fops);

	yl_append_bootreason_proc_entry = proc_create("append_reason", S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP,
		yl_append_bootreason_proc_entry, &yl_append_bootreason_proc_fops);

	return 0;
}

late_initcall(yl_bootreason_init);

