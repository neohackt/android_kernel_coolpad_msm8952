
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>

#include "ft5336_download_lib.h"

//#include <asm/unistd.h>
//#include <asm/uaccess.h>


FTS_I2c_Read_Function ft5336_I2C_Download_Read;
FTS_I2c_Write_Function ft5336_I2C_Download_write;

#define bool int
#define false 0
#define true 1
#define		FT5336_CMD_READ_FLASH			0x02
#define		FT5336_CMD_WRITE_FLASH			0x03
#define		FT5336_CMD_RELEASE_MCU_RESET	0x0A	//系统退出Debug模块并从SRAM开始执行程序
#define		FT5336_CMD_RESET_SYSTEM			0x0B
#define		FT5336_CMD_RESET_EXTERNAL		0x0C	//复位外设
#define		FT5336_CMD_RESET_MCU_EXTERNAL	0x0D	//复位外设与mcu
#define		FT5336_CMD_WRITE_SFR_SINGLE		0x09
#define FLASH_MAIN_MEMORY_MAX_ADDR 0x7FFF
#define FLASH_INFO_MEMORY_MAX_ADDR 0x3FF
#define    FTS_PACKET_LENGTH        128

#define FTS_DBG
#ifdef FTS_DBG
#define DBG(fmt, args...) printk("[FTS]" fmt, ## args)
#else
#define DBG(fmt, args...) do{}while(0)
#endif

#define u8 unsigned char
//#define u16 unsigned short

static unsigned short g_fts_packet_len = FTS_PACKET_LENGTH;
static unsigned short g_fts_delaytime = 20;

int Init_I2C_Read_Func(FTS_I2c_Read_Function fpI2C_Read)
{
	ft5336_I2C_Download_Read = fpI2C_Read;
	return 0;
}

int Init_I2C_Write_Func(FTS_I2c_Write_Function fpI2C_Write)
{
	ft5336_I2C_Download_write = fpI2C_Write;
	return 0;
}

static int ft5336_Lib_ReadFlash(bool bmain, unsigned char flashdata[], unsigned short flashaddr, unsigned short readlen)
{
	unsigned char cmd[6];
//	unsigned short readlen=FTS_PACKET_LENGTH;
	u8 readflag = FT5336_CMD_READ_FLASH;
		readflag = FT5336_CMD_READ_FLASH;
	//while(1)
	{
		//the max length of info flash data is 1024;

		cmd[0] = readflag;
		cmd[1] = ~readflag;
		cmd[2] = ((flashaddr>>8)&0x7f);
		cmd[3] = (unsigned char )flashaddr;
		cmd[4] = ((readlen-1)>>8);
		cmd[5] = readlen-1;

		if(ft5336_I2C_Download_Read(cmd, 6, flashdata, readlen) < 0)
		{
			return -1;
		}

		//flashaddr += readlen;
		DBG("[FTS] Read the 0x%x th byte.\n", flashaddr);
		msleep(2);
	}
	return 0;
}

int ft5336_Lib_Enter_Download_Mode(void)
{
	unsigned char cmd[2];
	int ret = 0;
	cmd[0] = 0x55;
	cmd[1] = 0xAA;

	ret = ft5336_I2C_Download_write(cmd, 2) ;

	return ret;
}

static int ft5336_Lib_Enable_Wirte_Protect(void)
{
	unsigned char cmd[4];
	int ret = 0;

	cmd[0] = FT5336_CMD_WRITE_SFR_SINGLE;
	cmd[1] = ~FT5336_CMD_WRITE_SFR_SINGLE;
	cmd[2] = 0xfb;
	cmd[3] = 0xa5;

	ret = ft5336_I2C_Download_write(cmd, 4);

	if (ret >= 0) {
		cmd[0] = FT5336_CMD_WRITE_SFR_SINGLE;
		cmd[1] = ~FT5336_CMD_WRITE_SFR_SINGLE;
		cmd[2] = 0xfb;
		cmd[3] = 0x0f;
		ret = ft5336_I2C_Download_write(cmd, 4);
	} else
		return ret;

	if (ret >= 0) {
		cmd[0] = FT5336_CMD_WRITE_SFR_SINGLE;
		cmd[1] = ~FT5336_CMD_WRITE_SFR_SINGLE;
		cmd[2] = 0xfb;
		cmd[3] = 0x6a;
		ret = ft5336_I2C_Download_write(cmd, 4);
	}  else
		return ret;

	return 0;
}
static int ft5336_Lib_EraseFlash(bool bmain)
{
	unsigned char cmd[25];cmd[24] = 0x00;
	cmd[0] = 0x09;
	cmd[1] = 0xF6;
	cmd[2] = 0xF3;
	cmd[3] = 0x00;

	//MainFlash
	cmd[4] = 0x07;
	cmd[5] = 0xF8;
	cmd[6] = 0x79;
	cmd[7] = 0x00;
	cmd[8] = 0x00;
	cmd[9] = 0x06;
	cmd[10] = 0x00;
	cmd[11] = 0x14;
	cmd[12] = 0x54;
	cmd[13] = 0x55;

	cmd[14] = 0x15;
	cmd[15] = 0x14;
	cmd[16] = 0x00;
	if(ft5336_I2C_Download_write(cmd, 14) >= 0) {
		msleep(30);
		if(ft5336_I2C_Download_write(cmd+14, 1) >= 0) {
			msleep(1);
			return ft5336_I2C_Download_write(cmd+15, 2);
		} else {
			DBG("----FTS-------earse main flash failed\n");
			return -1;
		}
	} else {
		DBG("----FTS-------earse main flash failed\n");
		return -1;
	}

	return 0;
}

static int ft5336_Lib_WriteFlash(bool bmain, unsigned char flashdata[],
unsigned short datalen)
{
	unsigned char cmd[6+FTS_PACKET_LENGTH];
	unsigned short writelen = g_fts_packet_len;
	unsigned short flashaddr=0;
	int i=0;

	//download data to mainflash
	DBG("----------FTS-------Download main flash datalen=%d\n", datalen);
	if(datalen > (FLASH_MAIN_MEMORY_MAX_ADDR+1))
	{
		DBG("data is more than FLASH_MAIN_MEMORY_MAX_ADDR!\n");
		return -1;
	}

	DBG("----------FTS-------Write data len=%d\n", datalen);
	while(1)
	{
		if(datalen == (flashaddr))
			break;
		if((datalen-flashaddr)>=g_fts_packet_len)
			writelen = g_fts_packet_len;
		else
		{
			//pr_info("-------FTS---------datalen - flashaddr=%d\n", datalen - flashaddr);
			writelen = datalen - flashaddr;
		}

		cmd[0] = FT5336_CMD_WRITE_FLASH;
		cmd[1] = ~FT5336_CMD_WRITE_FLASH;
		cmd[2] = ((flashaddr>>8)&0x7f);

		cmd[3] = (unsigned char )flashaddr;
		cmd[4] = ((writelen-1)>>8);
		cmd[5] = writelen-1;
		for(i=0; i<writelen; i++)
			cmd[6+i] = flashdata[flashaddr+i];

		if(ft5336_I2C_Download_write(cmd, 6+writelen) < 0)
		{
			return -1;
		}

		flashaddr += writelen;
		//DBG("[FTS] download the 0x%x th byte.\n", flashaddr);
		msleep(g_fts_delaytime);
	}
	return 0;
}

int ft5336_Lib_Reset(void)
{
	unsigned char cmd[2];
	int ret = 0;
	cmd[0] = FT5336_CMD_RESET_SYSTEM;
	cmd[1] = ~FT5336_CMD_RESET_SYSTEM;
	ret = ft5336_I2C_Download_write(cmd, 2) ;
	return ret;
}
int ft5336_Lib_DownloadMain(unsigned char MainBuf[], unsigned short fwlen)
{
	//u8 * pbt_buf = NULL;
	u8 pbt_buf[FTS_PACKET_LENGTH];
	unsigned short flashaddr = 0;
	unsigned short readlen = g_fts_packet_len;
	int i=0;
		DBG("-------FTS-----------ft5336_Lib_DownloadMain!");

	if(pbt_buf == NULL)
	{
		DBG("-------FTS-----------alloc failed!");
		return -1;
	}

	DBG("-----FTS-------start enable write\n");
	if (ft5336_Lib_Enable_Wirte_Protect() < 0) {
		DBG("enable write protect failed!");
		goto DOWNLOADERR;
	}

	DBG("-----FTS-------start erase main\n");
	if(ft5336_Lib_EraseFlash(true) < 0) {
		DBG("erase main failed!");
		goto DOWNLOADERR;
	}
	msleep(150);
	DBG("-----FTS-------start Download fwlen=%d\n", fwlen);
	if(ft5336_Lib_WriteFlash(true, MainBuf, fwlen) >= 0) {
		DBG("----------FTS----start read flash\n");
		if(fwlen > (FLASH_MAIN_MEMORY_MAX_ADDR+1))
		{
			DBG("data is more than FLASH_MAIN_MEMORY_MAX_ADDR!");
			return -1;
		}
		while(1) {
			if(fwlen == (flashaddr))
				break;
			if((fwlen-flashaddr)>=g_fts_packet_len)
				readlen = g_fts_packet_len;
			else
				readlen = fwlen - flashaddr;

			if(ft5336_Lib_ReadFlash(true, pbt_buf, flashaddr, readlen) >= 0)
			{
				for(i=0; i<readlen; i++)
				{
					if(pbt_buf[i] != MainBuf[flashaddr+i]) {
						DBG("different. buf[%d]=0x%02x MainBuf = 0x%02x\n",
						flashaddr+i, pbt_buf[i], MainBuf[flashaddr+i]);
						goto DOWNLOADERR;
					}
				}

				//
			}
			else
			{
				goto DOWNLOADERR;
			}
			flashaddr += readlen;
		}
		DBG("download app to flash successful!\n");
	}
	else
	{
		goto DOWNLOADERR;
	}

	ft5336_Lib_Reset();

	msleep(150);

	return 0;

DOWNLOADERR:
	DBG("download app to flash failed!\n");

	msleep(150);
	return -1;
}

int ft5336_Lib_ReadMainFlash(unsigned char MainBuf[], unsigned short buflen)
{
	unsigned short flashaddr = 0;
	unsigned short readlen = g_fts_packet_len;
	int i = 0;
	u8 pbt_buf[FTS_PACKET_LENGTH];
	while(1) {
		if(buflen == (flashaddr))
			break;
		if((buflen-flashaddr)>=FTS_PACKET_LENGTH)
			readlen = FTS_PACKET_LENGTH;
		else
			readlen = buflen - flashaddr;

		if(ft5336_Lib_ReadFlash(true, pbt_buf, flashaddr, readlen) >= 0)
		{
			for(i=0; i<readlen; i++)
				MainBuf[flashaddr+i] = pbt_buf[i];
		} else
			goto DOWNLOADERR;
		flashaddr += readlen;
	}
	ft5336_Lib_Reset();
	return 0;
DOWNLOADERR:
	DBG("download app to flash failed!\n");

	return -1;

}

