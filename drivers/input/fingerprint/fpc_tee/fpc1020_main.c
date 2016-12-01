/* FPC1020 Touch sensor driver
 *
 * Copyright (c) 2013,2014 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#include "fpc1020_common.h"
#include "fpc1020_regs.h"
#include "fpc1020_regulator.h"
#include <linux/pinctrl/consumer.h>

/*********************************************************/
#include <linux/qpnp/qpnp-adc.h>//yulong add
#include <linux/qpnp/pin.h>
#include <linux/hwmon.h>
#include <asm/io.h>
/*********************************************************/


/* -------------------------------------------------------------------- */
/* fpc1020 driver constants						*/
/* -------------------------------------------------------------------- */
#define FPC1020_CLASS_NAME                      "fpsensor"

#define FPC1020_DEV_NAME                        "fpc1020"

/* set '0' for dynamic assignment, or '> 0' for static assignment */
#define FPC1020_MAJOR				0

#define FPC1020_DEFAULT_IRQ_TIMEOUT_MS		(100 * HZ / 1000)

#define SECURITY_CONTROL_BASE_PHYS 0x00058000 
#define SECURITY_CONTROL_BASE SECURITY_CONTROL_BASE_PHYS 
#define SECURITY_CONTROL_CORE_REG_BASE (SECURITY_CONTROL_BASE + 0x00000000) 

#define HWIO_QFPROM_CORR_SERIAL_NUM_ADDR (SECURITY_CONTROL_CORE_REG_BASE + 0x00004008) 

#define FPC1020_SLEEP_RETRIES			5
#define FPC1020_SLEEP_RETRY_TIME_US		1000

#define FPC1020_RESET_RETRIES			2
#define FPC1020_RESET_LOW_US			1000
#define FPC1020_RESET_HIGH1_US			100
#define FPC1020_RESET_HIGH2_US			1250


#define FPC1020_MK_REG_READ_BYTES(__dst, __reg, __count, __ptr) {	\
	(__dst).reg      = FPC1020_REG_TO_ACTUAL((__reg));		\
	(__dst).reg_size = (__count);					\
	(__dst).write    = false;					\
	(__dst).dataptr  = (__ptr); }

#define FPC1020_MK_REG_READ(__dst, __reg, __ptr) {			\
	(__dst).reg      = FPC1020_REG_TO_ACTUAL((__reg));		\
	(__dst).reg_size = FPC1020_REG_SIZE((__reg));			\
	(__dst).write    = false;					\
	(__dst).dataptr  = (u8 *)(__ptr); }

#define FPC1020_MK_REG_WRITE_BYTES(__dst, __reg, __count, __ptr) {	\
	(__dst).reg      = FPC1020_REG_TO_ACTUAL((__reg));		\
	(__dst).reg_size = (__count);					\
	(__dst).write    = true;					\
	(__dst).dataptr  = (__ptr); }

#define FPC1020_MK_REG_WRITE(__dst, __reg, __ptr) {			\
	(__dst).reg      = FPC1020_REG_TO_ACTUAL((__reg));		\
	(__dst).reg_size = FPC1020_REG_SIZE((__reg));			\
	(__dst).write    = true;					\
	(__dst).dataptr  = (u8 *)(__ptr); }

#define FPC1020_STATUS_REG_RESET_VALUE 0x1e

#define FPC1020_STATUS_REG_MODE_MASK ( \
		FPC_1020_STATUS_REG_BIT_MAIN_IDLE_CMD | \
		FPC_1020_STATUS_REG_BIT_SYNC_PWR_IDLE | \
		FPC_1020_STATUS_REG_BIT_PWR_DWN_OSC_HIN)

#define FPC1020_STATUS_REG_IN_DEEP_SLEEP_MODE	0

#define FPC1020_STATUS_REG_IN_SLEEP_MODE	0

#define FPC1020_STATUS_REG_IN_IDLE_MODE ( \
		FPC_1020_STATUS_REG_BIT_MAIN_IDLE_CMD | \
		FPC_1020_STATUS_REG_BIT_SYNC_PWR_IDLE | \
		FPC_1020_STATUS_REG_BIT_PWR_DWN_OSC_HIN)
		
/* -------------------------------------------------------------------- */
/* fpc1020 data types							*/
/* -------------------------------------------------------------------- */
typedef enum {
	FPC_1020_STATUS_REG_BIT_IRQ			= 1 << 0,
	FPC_1020_STATUS_REG_BIT_MAIN_IDLE_CMD		= 1 << 1,
	FPC_1020_STATUS_REG_BIT_SYNC_PWR_IDLE		= 1 << 2,
	FPC_1020_STATUS_REG_BIT_PWR_DWN_OSC_HIN		= 1 << 3,
	FPC_1020_STATUS_REG_BIT_FIFO_EMPTY		= 1 << 4,
	FPC_1020_STATUS_REG_BIT_FIFO_FULL		= 1 << 5,
	FPC_1020_STATUS_REG_BIT_MISO_EDGRE_RISE_EN	= 1 << 6
} fpc1020_status_reg_t;

typedef enum {
	FPC1020_CMD_FINGER_PRESENT_QUERY	= 32,
	FPC1020_CMD_WAIT_FOR_FINGER_PRESENT	= 36,
	FPC1020_CMD_ACTIVATE_SLEEP_MODE		= 40,
	FPC1020_CMD_ACTIVATE_DEEP_SLEEP_MODE	= 44,
	FPC1020_CMD_ACTIVATE_IDLE_MODE		= 52,
	FPC1020_CMD_CAPTURE_IMAGE		= 192,
	FPC1020_CMD_READ_IMAGE			= 196,
	FPC1020_CMD_SOFT_RESET			= 248
} fpc1020_cmd_t;

typedef enum {
	FPC_1020_IRQ_REG_BIT_FINGER_DOWN   = 1 << 0,
	FPC_1020_IRQ_REG_BIT_ERROR         = 1 << 2,
	FPC_1020_IRQ_REG_BIT_FIFO_NEW_DATA = 1 << 5,
	FPC_1020_IRQ_REG_BIT_COMMAND_DONE  = 1 << 7,
	FPC_1020_IRQ_REG_BITS_REBOOT       = 0xff
} fpc1020_irq_reg_t;

/* -------------------------------------------------------------------- */
/* fpc1020 sensor commands and registers				*/
/* -------------------------------------------------------------------- */
typedef enum {
	FPC_1020_ERROR_REG_BIT_FIFO_UNDERFLOW = 1 << 0
} fpc1020_error_reg_t;

typedef struct {
	fpc1020_reg_t reg;
	bool          write;
	u16           reg_size;
	u8            *dataptr;
} fpc1020_reg_access_t;


/* -------------------------------------------------------------------- */
/* global variables							*/
/* -------------------------------------------------------------------- */
extern struct qpnp_vadc_chip *fpc_vadc_dev;// comform qpnp-vm-bms.c

/* -------------------------------------------------------------------- */
/* function prototypes							*/
/* -------------------------------------------------------------------- */

static int  fpc1020_probe(struct spi_device *spi);

static int  fpc1020_remove(struct spi_device *spi);

static int fpc1020_open(struct inode *inode, struct file *file);

static int fpc1020_release(struct inode *inode, struct file *file);

static unsigned int fpc1020_poll(struct file *file, poll_table *wait);

static long fpc1020_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static int fpc1020_cleanup(fpc1020_data_t *fpc1020, struct spi_device *spidev);

static int  fpc1020_param_init(fpc1020_data_t *fpc1020,
					struct fpc1020_platform_data *pdata);

static int  fpc1020_supply_init(fpc1020_data_t *fpc1020);

static int  fpc1020_reset_init(fpc1020_data_t *fpc1020,
					struct fpc1020_platform_data *pdata);

static int  fpc1020_irq_init(fpc1020_data_t *fpc1020,
					struct fpc1020_platform_data *pdata);

static int  fpc1020_spi_setup(fpc1020_data_t *fpc1020,
					struct fpc1020_platform_data *pdata);

static int fpc1020_cmd(fpc1020_data_t *fpc1020,
			fpc1020_cmd_t cmd, u8 wait_irq_mask);

static int fpc1020_reset(fpc1020_data_t *fpc1020);

static int fpc1020_wait_for_irq(fpc1020_data_t *fpc1020, int timeout);

static int fpc1020_check_irq_after_reset(fpc1020_data_t *fpc1020);

static int fpc1020_reg_access(fpc1020_data_t *fpc1020,
		      fpc1020_reg_access_t *reg_data);

static int fpc1020_read_irq(fpc1020_data_t *fpc1020, bool clear_irq);

irqreturn_t fpc1020_interrupt(int irq, void *_fpc1020);

static int fpc1020_get_id_voltage(void);//yulong add

int fpc1020_module_name(char *); 

static int  fpc1020_create_class(fpc1020_data_t *fpc1020);

static int  fpc1020_create_device(fpc1020_data_t *fpc1020);

static int  fpc1020_spi_get_of_pdata(struct device *dev);

static int  fpc1020_get_of_pdata(struct device *dev,
					struct fpc1020_platform_data *pdata);

static int fpc1020_hw_prepare(fpc1020_data_t *fpc1020);
static int fpc1020_hw_unprepare(fpc1020_data_t *fpc1020);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	const bool target_little_endian = true;
#else
	#warning BE target not tested!
	const bool target_little_endian = false;
#endif


/* -------------------------------------------------------------------- */
/* fpc1020 data types							*/
/* -------------------------------------------------------------------- */
struct chip_struct {
	fpc1020_chip_t type;
	u16 hwid;
	u8  revision;
	u8  pixel_rows;
	u8  pixel_columns;
	u8  adc_group_size;
	u16 spi_max_khz;
};


/* -------------------------------------------------------------------- */
/* fpc1020 driver constants						*/
/* -------------------------------------------------------------------- */
#define FPC1150_ROWS		208u
#define FPC1150_COLUMNS		80u

#define FPC1021_ROWS		160u
#define FPC1021_COLUMNS		160u

#define FPC1020_ROWS		192u
#define FPC1020_COLUMNS		192u
#define FPC102X_ADC_GROUP_SIZE	8u

#define FPC1020_EXT_HWID_CHECK_ID1020A_ROWS 5u


static const char *chip_text[] = {
	"N/A",		/* FPC1020_CHIP_NONE */
	"fpc1020a", 	/* FPC1020_CHIP_1020A */
	"fpc1021a", 	/* FPC1020_CHIP_1021A */
	"fpc1021b", 	/* FPC1020_CHIP_1021B */
	"fpc1021f",     /* FPC1020_CHIP_1021F */
	"fpc1150a", 	/* FPC1020_CHIP_1150A */
	"fpc1150b", 	/* FPC1020_CHIP_1150B */
	"fpc1150f", 	/* FPC1020_CHIP_1150F */
	"fpc1155x" 	/* FPC1020_CHIP_1155X */
};

static const struct chip_struct chip_data[] = {
	{FPC1020_CHIP_1020A, 0x020a, 0, FPC1020_ROWS, FPC1020_COLUMNS, FPC102X_ADC_GROUP_SIZE, 8000},
	{FPC1020_CHIP_1021A, 0x021a, 2, FPC1021_ROWS, FPC1021_COLUMNS, FPC102X_ADC_GROUP_SIZE, 8000},
	{FPC1020_CHIP_1021B, 0x021b, 1, FPC1021_ROWS, FPC1021_COLUMNS, FPC102X_ADC_GROUP_SIZE, 8000},
	{FPC1020_CHIP_1021F, 0x021f, 1, FPC1021_ROWS, FPC1021_COLUMNS, FPC102X_ADC_GROUP_SIZE, 8000},
	{FPC1020_CHIP_1150A, 0x150a, 1, FPC1150_ROWS, FPC1150_COLUMNS, FPC102X_ADC_GROUP_SIZE, 8000},
	{FPC1020_CHIP_1150B, 0x150b, 1, FPC1150_ROWS, FPC1150_COLUMNS, FPC102X_ADC_GROUP_SIZE, 8000},
	{FPC1020_CHIP_1150F, 0x150f, 1, FPC1150_ROWS, FPC1150_COLUMNS, FPC102X_ADC_GROUP_SIZE, 8000},
	{FPC1020_CHIP_1155X, 0x9500, 1, FPC1150_ROWS, FPC1150_COLUMNS, FPC102X_ADC_GROUP_SIZE, 5000},
	{FPC1020_CHIP_NONE,  0,      0, 0,            0,               0,                      0}
};

static fpc1020_data_t *gfpc1020 = NULL;



#ifdef CONFIG_OF
static struct of_device_id fpc1020_of_match[] = {
	{ .compatible = "fpc,fpc1020", },
	{},
};

MODULE_DEVICE_TABLE(of, fpc1020_of_match);
#endif

/*************************************************************************/
#define MAX_NAME_LENGTH 20
struct module_id_info//yulong add 2015.03.06
{
	unsigned char Rk;   //chip inner resister, unit k
	char name[MAX_NAME_LENGTH]; // name of chip module
	int volt;         // voltage of module_id pin mpp_2;unit mv
};
struct module_id_info fpc_module_id[]={
{10, "Ofilm" ,163},
{47, "Bonguang",575},
{100, "Dreamtech",900},
{220, "crucialtec",1237}
};
/*************************************************************************/

static struct spi_driver fpc1020_driver = {
	.driver = {
		.name	= FPC1020_DEV_NAME,
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = fpc1020_of_match,
#endif
	},
	.probe	= fpc1020_probe,
	.remove	= fpc1020_remove,
};

static const struct file_operations fpc1020_fops = {
	.owner          = THIS_MODULE,
	.open           = fpc1020_open,
	.release        = fpc1020_release,
	.poll           = fpc1020_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= fpc1020_ioctl,
#endif
	.unlocked_ioctl = fpc1020_ioctl,
};



/* -------------------------------------------------------------------- */
int fpc1020_gpio_reset(fpc1020_data_t *fpc1020)
{
	int error = 0;
	int counter = FPC1020_RESET_RETRIES;

	while (counter) {
		counter--;

		gpio_set_value(fpc1020->reset_gpio, 1);
		udelay(FPC1020_RESET_HIGH1_US);

		gpio_set_value(fpc1020->reset_gpio, 0);
		udelay(FPC1020_RESET_LOW_US);

		gpio_set_value(fpc1020->reset_gpio, 1);
		udelay(FPC1020_RESET_HIGH2_US);

		error = gpio_get_value(fpc1020->irq_gpio) ? 0 : -EIO;

		if (!error) {
			printk(KERN_INFO "%s OK !\n", __func__);
			counter = 0;
		} else {
			printk(KERN_INFO "%s timed out,retrying ...\n",
				__func__);

			udelay(1250);
		}
	}
	return error;
}


/* -------------------------------------------------------------------- */
int fpc1020_spi_reset(fpc1020_data_t *fpc1020)
{
	int error = 0;
	int counter = FPC1020_RESET_RETRIES;

	dev_dbg(&fpc1020->spi->dev, "%s\n", __func__);

	while (counter) {
		counter--;

		error = fpc1020_cmd(fpc1020,
				FPC1020_CMD_SOFT_RESET,
				false);

		if (error >= 0) {
			error = fpc1020_wait_for_irq(fpc1020,
					FPC1020_DEFAULT_IRQ_TIMEOUT_MS);
		}

		if (error >= 0) {
			error = gpio_get_value(fpc1020->irq_gpio) ? 0 : -EIO;

			if (!error) {
				dev_dbg(&fpc1020->spi->dev,
					"%s OK !\n", __func__);

				counter = 0;

			} else {
				dev_dbg(&fpc1020->spi->dev,
					"%s timed out,retrying ...\n",
					__func__);
			}
		}
	}
	return error;
}


/* -------------------------------------------------------------------- */
int fpc1020_reset(fpc1020_data_t *fpc1020)
{
	int error = 0;

	dev_dbg(&fpc1020->spi->dev, "%s\n", __func__);

	error = (fpc1020->soft_reset_enabled) ?
			fpc1020_spi_reset(fpc1020) :
			fpc1020_gpio_reset(fpc1020);

	disable_irq(fpc1020->irq);
	fpc1020->interrupt_done = false;
	enable_irq(fpc1020->irq);

	error = fpc1020_check_irq_after_reset(fpc1020);

	if (error < 0)
		goto out;

	error = (gpio_get_value(fpc1020->irq_gpio) != 0) ? -EIO : 0;

	if (error)
		dev_err(&fpc1020->spi->dev, "IRQ pin, not low after clear.\n");

	error = fpc1020_read_irq(fpc1020, true);

	if (error != 0) {
		dev_err(&fpc1020->spi->dev,
			"IRQ register, expected 0x%x, got 0x%x.\n",
			0,
			(u8)error);

		error = -EIO;
	}

out:
	return error;
}


/* -------------------------------------------------------------------- */
int fpc1020_check_hw_id(fpc1020_data_t *fpc1020)
{
	int error = 0;
	u16 hardware_id;

	fpc1020_reg_access_t reg;
	int counter = 0;
	bool match = false;

	FPC1020_MK_REG_READ(reg, FPC102X_REG_HWID, &hardware_id);

	error = fpc1020_reg_access(fpc1020, &reg);

	if (error)
		return error;

	if(fpc1020->force_hwid > 0) {

		dev_info(&fpc1020->spi->dev,
			"Hardware id, detected 0x%x - forced setting 0x%x\n",
			hardware_id,
			   fpc1020->force_hwid);

		hardware_id = fpc1020->force_hwid;
	}

	while (!match && chip_data[counter].type != FPC1020_CHIP_NONE) {
		if (chip_data[counter].hwid == hardware_id)
			match = true;
		else
			counter++;
	}

	if (match) {
		fpc1020->chip.type     = chip_data[counter].type;
		fpc1020->chip.revision = chip_data[counter].revision;

		fpc1020->chip.pixel_rows     = chip_data[counter].pixel_rows;
		fpc1020->chip.pixel_columns  = chip_data[counter].pixel_columns;
		fpc1020->chip.adc_group_size = chip_data[counter].adc_group_size;
		fpc1020->chip.spi_max_khz    = chip_data[counter].spi_max_khz;

		dev_info(&fpc1020->spi->dev,
				"Hardware id: 0x%x (%s, rev.%d) \n",
						hardware_id,
						chip_text[fpc1020->chip.type],
						fpc1020->chip.revision);
	} else {
		dev_err(&fpc1020->spi->dev,
			"Hardware id mismatch: got 0x%x\n", hardware_id);

		fpc1020->chip.type = FPC1020_CHIP_NONE;
		fpc1020->chip.revision = 0;
		error = -EIO;
	}
	return error;
}


/* -------------------------------------------------------------------- */
const char *fpc1020_hw_id_text(fpc1020_data_t *fpc1020)
{
	return chip_text[fpc1020->chip.type];
}


/* -------------------------------------------------------------------- */
static int fpc1020_check_irq_after_reset(fpc1020_data_t *fpc1020)
{
	int error = 0;
	u8 irq_status;

	fpc1020_reg_access_t reg_clear = {
		.reg = FPC102X_REG_READ_INTERRUPT_WITH_CLEAR,
		.write = false,
		.reg_size = FPC1020_REG_SIZE(
				FPC102X_REG_READ_INTERRUPT_WITH_CLEAR),

		.dataptr = &irq_status
	};
	
	error = fpc1020_reg_access(fpc1020, &reg_clear);

	if(error < 0)
		return error;

	if (irq_status != FPC_1020_IRQ_REG_BITS_REBOOT) {
		dev_err(&fpc1020->spi->dev,
			"IRQ register, expected 0x%x, got 0x%x.\n",
			FPC_1020_IRQ_REG_BITS_REBOOT,
			irq_status);

		error = -EIO;
	}

	return (error < 0) ? error : irq_status;
}


/* -------------------------------------------------------------------- */
int fpc1020_wait_for_irq(fpc1020_data_t *fpc1020, int timeout)
{
	int result = 0;

	if (!timeout) {
		result = wait_event_interruptible(
				fpc1020->wq_irq_return,
				fpc1020->interrupt_done);
	} else {
		result = wait_event_interruptible_timeout(
				fpc1020->wq_irq_return,
				fpc1020->interrupt_done, timeout);
	}

	if (result < 0) {
		dev_err(&fpc1020->spi->dev,
			 "wait_event_interruptible interrupted by signal.\n");

		return result;
	}

	if (result || !timeout) {
		fpc1020->interrupt_done = false;
		return 0;
	}

	return -ETIMEDOUT;
}


/* -------------------------------------------------------------------- */
int fpc1020_read_irq(fpc1020_data_t *fpc1020, bool clear_irq)
{
	int error = 0;
	u8 irq_status;
	fpc1020_reg_access_t reg_read = {
		.reg = FPC102X_REG_READ_INTERRUPT,
		.write = false,
		.reg_size = FPC1020_REG_SIZE(FPC102X_REG_READ_INTERRUPT),
		.dataptr = &irq_status
	};

	fpc1020_reg_access_t reg_clear = {
		.reg = FPC102X_REG_READ_INTERRUPT_WITH_CLEAR,
		.write = false,
		.reg_size = FPC1020_REG_SIZE(
					FPC102X_REG_READ_INTERRUPT_WITH_CLEAR),
		.dataptr = &irq_status
	};

	error = fpc1020_reg_access(fpc1020,
				(clear_irq) ? &reg_clear : &reg_read);

	if(error < 0)
		return error;

	if (irq_status == FPC_1020_IRQ_REG_BITS_REBOOT) {

		dev_err(&fpc1020->spi->dev,
			"%s: unexpected irq_status = 0x%x\n"
			, __func__, irq_status);

		error = -EIO;
	}

	return (error < 0) ? error : irq_status;
}


/* -------------------------------------------------------------------- */
int fpc1020_read_status_reg(fpc1020_data_t *fpc1020)
{
	int error = 0;
	u8 status;
	/* const */ fpc1020_reg_access_t reg_read = {
		.reg = FPC102X_REG_FPC_STATUS,
		.write = false,
		.reg_size = FPC1020_REG_SIZE(FPC102X_REG_FPC_STATUS),
		.dataptr = &status
	};

	error = fpc1020_reg_access(fpc1020, &reg_read);

	return (error < 0) ? error : status;
}


/* -------------------------------------------------------------------- */
int fpc1020_reg_access(fpc1020_data_t *fpc1020,
		      fpc1020_reg_access_t *reg_data)
{
	int error = 0;

	u8 temp_buffer[FPC1020_REG_MAX_SIZE];

	struct spi_message msg;

	struct spi_transfer cmd = {
		//.cs_change = 1,
		.delay_usecs = 0,
		.speed_hz = (u32)fpc1020->spi_freq_khz * 1000u,
		.tx_buf = &(reg_data->reg),
		.rx_buf = NULL,
		.len    = 1 + FPC1020_REG_ACCESS_DUMMY_BYTES(reg_data->reg),
		.tx_dma = 0,
		.rx_dma = 0,
		.bits_per_word = 0,
	};

	struct spi_transfer data = {
		//.cs_change = 1,
		.delay_usecs = 0,
		.speed_hz = (u32)fpc1020->spi_freq_khz * 1000u,
		.tx_buf = (reg_data->write)  ? temp_buffer : NULL,
		.rx_buf = (!reg_data->write) ? temp_buffer : NULL,
		.len    = reg_data->reg_size,
		.tx_dma = 0,
		.rx_dma = 0,
		.bits_per_word = 0,
	};

	if (reg_data->reg_size > sizeof(temp_buffer)) {
		dev_err(&fpc1020->spi->dev,
			"%s : illegal register size\n",
			__func__);

		error = -ENOMEM;
		goto out;
	}

	if (reg_data->write) {
		if (target_little_endian) {
			int src = 0;
			int dst = reg_data->reg_size - 1;

			while (src < reg_data->reg_size) {
				temp_buffer[dst] = reg_data->dataptr[src];
				src++;
				dst--;
			}
		} else {
			memcpy(temp_buffer,
				reg_data->dataptr,
				reg_data->reg_size);
		}
	}

	fpc1020_hw_prepare(fpc1020);
	spi_message_init(&msg);
	spi_message_add_tail(&cmd,  &msg);
	spi_message_add_tail(&data, &msg);

	error = spi_sync(fpc1020->spi, &msg);
	fpc1020_hw_unprepare(fpc1020);

	if (error)
		dev_err(&fpc1020->spi->dev, "%s : spi_sync failed.\n", __func__);

	if (!reg_data->write) {
		if (target_little_endian) {
			int src = reg_data->reg_size - 1;
			int dst = 0;

			while (dst < reg_data->reg_size) {
				reg_data->dataptr[dst] = temp_buffer[src];
				src--;
				dst++;
			}
		} else {
			memcpy(reg_data->dataptr,
				temp_buffer,
				reg_data->reg_size);
		}
	}

	dev_dbg(&fpc1020->spi->dev,
		"%s %s 0x%x/%dd (%d bytes) %x %x %x %x : %x %x %x %x\n",
		 __func__,
		(reg_data->write) ? "WRITE" : "READ",
		reg_data->reg,
		reg_data->reg,
		reg_data->reg_size,
		(reg_data->reg_size > 0) ? temp_buffer[0] : 0,
		(reg_data->reg_size > 1) ? temp_buffer[1] : 0,
		(reg_data->reg_size > 2) ? temp_buffer[2] : 0,
		(reg_data->reg_size > 3) ? temp_buffer[3] : 0,
		(reg_data->reg_size > 4) ? temp_buffer[4] : 0,
		(reg_data->reg_size > 5) ? temp_buffer[5] : 0,
		(reg_data->reg_size > 6) ? temp_buffer[6] : 0,
		(reg_data->reg_size > 7) ? temp_buffer[7] : 0);

out:
	return error;
}


/* -------------------------------------------------------------------- */
int fpc1020_cmd(fpc1020_data_t *fpc1020,
			fpc1020_cmd_t cmd,
			u8 wait_irq_mask)
{
	int error = 0;
	struct spi_message msg;

	struct spi_transfer t = {
		//.cs_change = 1,
		.delay_usecs = 0,
		.speed_hz = (u32)fpc1020->spi_freq_khz * 1000u,
		.tx_buf = &cmd,
		.rx_buf = NULL,
		.len    = 1,
		.tx_dma = 0,
		.rx_dma = 0,
		.bits_per_word = 0,
	};

	fpc1020_hw_prepare(fpc1020);
	spi_message_init(&msg);
	spi_message_add_tail(&t,  &msg);

	error = spi_sync(fpc1020->spi, &msg);
	fpc1020_hw_unprepare(fpc1020);

	if (error)
		dev_err(&fpc1020->spi->dev, "spi_sync failed.\n");

	if ((error >= 0) && wait_irq_mask) {
		error = fpc1020_wait_for_irq(fpc1020,
					FPC1020_DEFAULT_IRQ_TIMEOUT_MS);

		if (error >= 0)
			error = fpc1020_read_irq(fpc1020, true);
	}

	// dev_dbg(&fpc1020->spi->dev, "%s 0x%x/%dd\n", __func__, cmd, cmd);

	return error;
}




/* -------------------------------------------------------------------- */
int fpc1020_wake_up(fpc1020_data_t *fpc1020)
{
	const fpc1020_status_reg_t status_mask = FPC1020_STATUS_REG_MODE_MASK;

	int reset  = fpc1020_reset(fpc1020);
	int status = fpc1020_read_status_reg(fpc1020);
 
	if (reset == 0 && status >= 0 &&
		(fpc1020_status_reg_t)(status & status_mask) ==
		FPC1020_STATUS_REG_IN_IDLE_MODE) {

		dev_dbg(&fpc1020->spi->dev, "%s OK\n", __func__);

		return 0;
	} else {

		dev_err(&fpc1020->spi->dev, "%s FAILED\n", __func__);

		return -EIO;
	}
}


/* -------------------------------------------------------------------- */
int fpc1020_sleep(fpc1020_data_t *fpc1020, bool deep_sleep)
{
	const char *str_deep = "deep";
	const char *str_regular = "regular";

	const fpc1020_status_reg_t status_mask = FPC1020_STATUS_REG_MODE_MASK;

	int error = fpc1020_cmd(fpc1020,
				(deep_sleep) ? FPC1020_CMD_ACTIVATE_DEEP_SLEEP_MODE :
						FPC1020_CMD_ACTIVATE_SLEEP_MODE,
				0);
	bool sleep_ok;

	int retries = FPC1020_SLEEP_RETRIES;

	if (error) {
		dev_dbg(&fpc1020->spi->dev,
			"%s %s command failed %d\n", __func__,
			(deep_sleep)? str_deep : str_regular,
			error);

		return error;
	}

	error = 0;
	sleep_ok = false;

	while (!sleep_ok && retries && (error >= 0)) {

		error = fpc1020_read_status_reg(fpc1020);

		if (error < 0) {
			dev_dbg(&fpc1020->spi->dev,
				"%s %s read status failed %d\n", __func__,
				(deep_sleep)? str_deep : str_regular,
				error);
		} else {
			error &= status_mask;
			sleep_ok = (deep_sleep) ?
				error == FPC1020_STATUS_REG_IN_DEEP_SLEEP_MODE :
				error == FPC1020_STATUS_REG_IN_SLEEP_MODE;
		}
		if (!sleep_ok) {
			udelay(FPC1020_SLEEP_RETRY_TIME_US);
			retries--;
		}
	}

	if (deep_sleep && sleep_ok && gpio_is_valid(fpc1020->reset_gpio))
		gpio_set_value(fpc1020->reset_gpio, 0);

	if (sleep_ok) {
		dev_dbg(&fpc1020->spi->dev,
			"%s %s OK\n", __func__,
			(deep_sleep)? str_deep : str_regular);
		return 0;
	} else {
		dev_err(&fpc1020->spi->dev,
			"%s %s FAILED\n", __func__,
			(deep_sleep)? str_deep : str_regular);

		return (deep_sleep) ? -EIO : -EAGAIN;
	}
}

int fpc1020_get_id_voltage()
{
        struct qpnp_vadc_result result;
        int rc=0;
	if (IS_ERR(fpc_vadc_dev)) {
                rc = PTR_ERR(fpc_vadc_dev);
               if (rc == -EPROBE_DEFER)
                       pr_err("vadc not found - defer probe rc=%d\n", rc);
               else
                        pr_err("vadc property missing, rc=%d\n", rc);
		return rc;
        }
	rc = qpnp_vadc_read(fpc_vadc_dev,P_MUX2_1_1,&result);
	
	return (result.physical >> 10);
}

int fpc1020_module_name(char *buffer)
{
	int i = 0;
	int array = 0;
	int voltage_mv = 0;
	
  	voltage_mv = fpc1020_get_id_voltage();
	if(voltage_mv < 0)
		goto err;
	
	for (i = 0;i < ARRAY_SIZE(fpc_module_id);i++)
	{
		if (voltage_mv - fpc_module_id[i].volt < 100)
			if(voltage_mv - fpc_module_id[i].volt > - 100){
				array = i;		
				break;
			}
	}
	if(array < ARRAY_SIZE(fpc_module_id)){
		return sprintf(buffer, "%s",fpc_module_id[array].name);
	}
err:
 	return sprintf(buffer, "%s ","UnknowID");
}

/* -------------------------------------------------------------------- */
static int  fpc1020_probe(struct spi_device *spi)
{
	struct fpc1020_platform_data *fpc1020_pdata;
	struct fpc1020_platform_data pdata_of;
	struct device *dev = &spi->dev;
	int error = 0;
	fpc1020_data_t *fpc1020 = NULL;

	fpc1020 = kzalloc(sizeof(*fpc1020), GFP_KERNEL);
	if (!fpc1020) {
		dev_err(&spi->dev,
		"failed to allocate memory for struct fpc1020_data\n");

		return -ENOMEM;
	}

	printk(KERN_INFO "%s\n", __func__);

	spi_set_drvdata(spi, fpc1020);
	fpc1020->spi = spi;
	fpc1020->spi_freq_khz = 1000u;

	fpc1020->reset_gpio = -EINVAL;
	fpc1020->irq_gpio   = -EINVAL;

	fpc1020->irq        = -EINVAL;
	fpc1020->use_regulator_for_bezel = 0;
	fpc1020->state = 0;
	fpc1020->irq_state=0;
	gfpc1020 = fpc1020;

	init_waitqueue_head(&fpc1020->wq_irq_return);
	if (error)
		goto err;

	fpc1020_pdata = spi->dev.platform_data;

	if (!fpc1020_pdata) {
		error = fpc1020_get_of_pdata(dev, &pdata_of);
		fpc1020_pdata = &pdata_of;
		fpc1020_spi_get_of_pdata(dev);
		if (error)
			goto err;
	}

	error = fpc1020_param_init(fpc1020, fpc1020_pdata);
	if (error)
		goto err;

	error = fpc1020_supply_init(fpc1020);
	if (error)
		goto err;

	error = fpc1020_reset_init(fpc1020, fpc1020_pdata);
	if (error)
		goto err;

	error = fpc1020_irq_init(fpc1020, fpc1020_pdata);
	if (error)
		goto err;

	error = fpc1020_spi_setup(fpc1020, fpc1020_pdata);
	if (error)
		goto err;

	error = fpc1020_reset(fpc1020);
	if (error)
		goto err;

	error = fpc1020_check_hw_id(fpc1020);
	if (error)
		goto err;

	fpc1020->spi_freq_khz = fpc1020->chip.spi_max_khz;

	dev_info(&fpc1020->spi->dev,
			"Req. SPI frequency : %d kHz.\n",
			fpc1020->spi_freq_khz);

	error = fpc1020_create_class(fpc1020);
	if (error)
		goto err;

	error = fpc1020_create_device(fpc1020);
	if (error)
		goto err;

	sema_init(&fpc1020->mutex, 0);

	cdev_init(&fpc1020->cdev, &fpc1020_fops);
	fpc1020->cdev.owner = THIS_MODULE;

	error = cdev_add(&fpc1020->cdev, fpc1020->devno, 1);
	if (error) {
		dev_err(&fpc1020->spi->dev, "cdev_add failed.\n");
		goto err_chrdev;
	}

	error = fpc1020_sleep(fpc1020, false);
	if (error)
		goto err_cdev;

	up(&fpc1020->mutex);
	return 0;

err_cdev:
	cdev_del(&fpc1020->cdev);

err_chrdev:
	unregister_chrdev_region(fpc1020->devno, 1);

err:
	fpc1020_cleanup(fpc1020, spi);
	return error;
}


/* -------------------------------------------------------------------- */
static int  fpc1020_remove(struct spi_device *spi)
{
	fpc1020_data_t *fpc1020 = spi_get_drvdata(spi);

	printk(KERN_DEBUG "%s\n", __func__);

	fpc1020_sleep(fpc1020, true);

	cdev_del(&fpc1020->cdev);

	unregister_chrdev_region(fpc1020->devno, 1);

	fpc1020_cleanup(fpc1020, spi);

	return 0;
}

/* -------------------------------------------------------------------- */
static int fpc1020_open(struct inode *inode, struct file *file)

{
	fpc1020_data_t *fpc1020;

	printk(KERN_INFO "%s\n", __func__);

	fpc1020 = container_of(inode->i_cdev, fpc1020_data_t, cdev);

	if (down_interruptible(&fpc1020->mutex))
		return -ERESTARTSYS;

	file->private_data = fpc1020;

	up(&fpc1020->mutex);

	return 0;
}



/* -------------------------------------------------------------------- */
static int fpc1020_release(struct inode *inode, struct file *file)
{
	fpc1020_data_t *fpc1020 = file->private_data;
	int status = 0;

	printk(KERN_INFO "%s\n", __func__);

	if (down_interruptible(&fpc1020->mutex))
		return -ERESTARTSYS;

#ifdef CONFIG_PINCTRL
	if(fpc1020->pins_active){
		status = pinctrl_select_state(fpc1020->pinctrl, fpc1020->pins_active);
		if (status) {
			dev_err(&fpc1020->spi->dev, "%s: Can not set %s pins\n",
			__func__, PINCTRL_STATE_DEFAULT);
			return status;
		}
	}
#endif

	up(&fpc1020->mutex);

	return status;
}

static unsigned int fpc1020_poll(struct file *file, poll_table *wait)
{
	fpc1020_data_t *fpc1020 = file->private_data;
	poll_wait(file, &fpc1020->wq_irq_return, wait);
	if (gpio_get_value(fpc1020->irq_gpio)) {
	//if(fpc1020->interrupt_done == true) {
	//	fpc1020->interrupt_done = false;
		return POLLIN;
	}
	else return 0;
}


static inline int fpc1020_spi_request_gpios(fpc1020_data_t *fpc1020)
{
	int result = 0;

	if (!fpc1020->spi_use_pinctrl) {
		gpio_request(fpc1020->spi_gpio_clk,"spi clk");
		gpio_request(fpc1020->spi_gpio_miso,"spi miso");
		gpio_request(fpc1020->spi_gpio_mosi,"spi mosi");
		gpio_request(fpc1020->spi_gpio_cs,"spi cs");		
	} else {
#ifdef CONFIG_PINCTRL
		result = pinctrl_select_state(fpc1020->pinctrl, fpc1020->pins_active);
		if (result) {
			printk("%s: Can not set active pins\n",	__func__);
		}
#endif		
	}
	return result;
}

static inline int fpc1020_spi_free_gpios(fpc1020_data_t *fpc1020)
{
	int result = 0;

	if (!fpc1020->spi_use_pinctrl) {
		gpio_free(fpc1020->spi_gpio_clk);
		gpio_free(fpc1020->spi_gpio_miso);
		gpio_free(fpc1020->spi_gpio_mosi);
		gpio_free(fpc1020->spi_gpio_cs);	
	} else {
#ifdef CONFIG_PINCTRL
		result = pinctrl_select_state(fpc1020->pinctrl, fpc1020->pins_sleep);
		if (result)
			printk("%s: Can not set sleep pins\n",	__func__);
#endif		
	}
	return result;
}

int fpc1020_hw_prepare(fpc1020_data_t *fpc1020)
{
	int rc;
		fpc1020_spi_request_gpios(fpc1020);
		rc = clk_prepare_enable(fpc1020->clk);
		if (rc) {
			printk("%s: unable to enable core_clk\n",__func__);
		}

		rc = clk_prepare_enable(fpc1020->pclk);
		if (rc) {
			printk("%s: unable to enable iface_clk\n",__func__);
		}
		return rc;
}

int fpc1020_hw_unprepare(fpc1020_data_t *fpc1020)
{
	int rc = 0;
	rc = fpc1020_spi_free_gpios(fpc1020);
	clk_disable_unprepare(fpc1020->clk);
	clk_disable_unprepare(fpc1020->pclk);
	return rc;
}
static long fpc1020_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	fpc1020_data_t *fpc1020 = file->private_data;
	char buffer[MAX_NAME_LENGTH];
	void __user *up = (void __user *)arg;
	void *param = (char *)arg;
	void __iomem *addr;
	int rc = 0;
	int sn = 0;
	int mask = 0;
	switch (cmd) {
		case FPC_HW_RESET:
			fpc1020_gpio_reset(fpc1020);
			break;
		case FPC_HW_PREPARE:
			fpc1020_hw_prepare(fpc1020);
			break;

		case FPC_HW_UNPREPARE:
			fpc1020_hw_unprepare(fpc1020);
			break;

		case FPC_GET_INTERRUPT:
			if (!up)
			{
				dev_err(&fpc1020->spi->dev, "%s up is NULL \n", __func__);
			}
			if (!gpio_is_valid(fpc1020->irq_gpio))
			{	
				dev_err(&fpc1020->spi->dev, "fpc irq_gpio was not assigned properly");
			}
			
			rc = gpio_get_value(fpc1020->irq_gpio);
			
			if (put_user(rc,(int *)up))
			{
				dev_dbg(&fpc1020->spi->dev, "fpc put_user (%d) failed " , *(int *)up);
				rc = -EFAULT;
			}
			break;

		case FPC_MASK_INTERRUPT:
			if (__get_user(mask, (int __user *)arg)) {
				printk("arg is inval");
				return -EINVAL;
			}
			if(fpc1020->irq_state == mask){
				printk("already in %d",mask);
				break;
			}
			fpc1020->irq_state=mask;
			if(mask == 1)
			{
				disable_irq(fpc1020->irq);
				fpc1020->interrupt_done = false;
			}else {
				enable_irq(fpc1020->irq);
				fpc1020->interrupt_done = false;
			}
			break;

		case FPC_GET_MODULE_NAME:			
			rc = fpc1020_module_name(buffer);
			rc = copy_to_user(param,buffer, rc+1);
			if (rc ) {
				rc = -EFAULT;
				dev_err(&fpc1020->spi->dev, "get module name fail,rc=%d", rc);
			}			 
			break;
		case FPC_GET_SERIAL_NUM:	
			addr = ioremap(HWIO_QFPROM_CORR_SERIAL_NUM_ADDR,4);
			sn = readl(addr);
			if (put_user(sn,(int *)up)) {
				rc = -EFAULT;
				dev_err(&fpc1020->spi->dev, "get sn fail,rc=%d", rc);
			}			 
			break;
	default:
		dev_dbg(&fpc1020->spi->dev, "ENOIOCTLCMD: cmd=%d ", cmd);
		return -ENOIOCTLCMD;
	}

	return rc;
}

/* -------------------------------------------------------------------- */
static int fpc1020_cleanup(fpc1020_data_t *fpc1020, struct spi_device *spidev)
{
	dev_dbg(&fpc1020->spi->dev, "%s\n", __func__);

	if (!IS_ERR_OR_NULL(fpc1020->device))
		device_destroy(fpc1020->class, fpc1020->devno);

	class_destroy(fpc1020->class);

	if (fpc1020->irq >= 0){
		disable_irq_wake(fpc1020->irq);
		free_irq(fpc1020->irq, fpc1020);
	}

	if (gpio_is_valid(fpc1020->irq_gpio)){
		gpio_free(fpc1020->irq_gpio);
	}

	if (gpio_is_valid(fpc1020->reset_gpio))
		gpio_free(fpc1020->reset_gpio);

	kfree(fpc1020);

	spi_set_drvdata(spidev, NULL);

	return 0;
}


/* -------------------------------------------------------------------- */
static int  fpc1020_param_init(fpc1020_data_t *fpc1020,
					struct fpc1020_platform_data *pdata)
{
	fpc1020->vddtx_mv    = pdata->external_supply_mv;
	fpc1020->txout_boost = pdata->txout_boost;

	if (fpc1020->vddtx_mv > 0) {
		dev_info(&fpc1020->spi->dev,
			"External TxOut supply (%d mV)\n",
			fpc1020->vddtx_mv);
	} else {
		dev_info(&fpc1020->spi->dev,
			"Internal TxOut supply (boost %s)\n",
			(fpc1020->txout_boost) ? "ON" : "OFF");
	}

	fpc1020->force_hwid = pdata->force_hwid;
	fpc1020->use_regulator_for_bezel = pdata->use_regulator_for_bezel;

	return 0;
}


/* ------------------------------------------------------------ */
static int  fpc1020_supply_init(fpc1020_data_t *fpc1020)
{
	int error = 0;

	// Determine is we should use external regulator for
	// power sully to the bezel.
	if( fpc1020->use_regulator_for_bezel )
	{
		error = fpc1020_regulator_configure(fpc1020);
		if (error) {
			dev_err(&fpc1020->spi->dev,
					"fpc1020_probe - regulator configuration failed.\n");
			goto err;
		}

		error = fpc1020_regulator_set(fpc1020, true);
		if (error) {
			dev_err(&fpc1020->spi->dev,
					"fpc1020_probe - regulator enable failed.\n");
			goto err;
		}
	 }

err:
	return error;
}


/* -------------------------------------------------- */
static int  fpc1020_reset_init(fpc1020_data_t *fpc1020,
					struct fpc1020_platform_data *pdata)
{
	int error = 0;

	if (gpio_is_valid(pdata->reset_gpio)) {

		dev_info(&fpc1020->spi->dev,
			"Assign HW reset -> GPIO%d\n", pdata->reset_gpio);

		fpc1020->soft_reset_enabled = false;

		error = gpio_request(pdata->reset_gpio, "fpc1020_reset");

		if (error) {
			dev_err(&fpc1020->spi->dev,
				"gpio_request (reset) failed.\n");
			return error;
		}

		fpc1020->reset_gpio = pdata->reset_gpio;

		error = gpio_direction_output(fpc1020->reset_gpio, 0);

		if (error) {
			dev_err(&fpc1020->spi->dev,
			"gpio_direction_output(reset) failed.\n");
			return error;
		}
	} else {
		dev_info(&fpc1020->spi->dev, "Using soft reset\n");

		fpc1020->soft_reset_enabled = true;
	}

	return error;
}


/* -------------------------------------------------------------------- */
static int  fpc1020_irq_init(fpc1020_data_t *fpc1020,
					struct fpc1020_platform_data *pdata)
{
	int error = 0;

	if (gpio_is_valid(pdata->irq_gpio)) {

		dev_info(&fpc1020->spi->dev,
			"Assign IRQ -> GPIO%d\n",
			pdata->irq_gpio);

		error = gpio_request(pdata->irq_gpio, "fpc1020_irq");

		if (error) {
			dev_err(&fpc1020->spi->dev,
				"gpio_request (irq) failed.\n");

			return error;
		}

		fpc1020->irq_gpio = pdata->irq_gpio;

		error = gpio_direction_input(fpc1020->irq_gpio);

		if (error) {
			dev_err(&fpc1020->spi->dev,
				"gpio_direction_input (irq) failed.\n");
			return error;
		}
	} else {
		return -EINVAL;
	}

	fpc1020->irq = gpio_to_irq(fpc1020->irq_gpio);

	if (fpc1020->irq < 0) {
		dev_err(&fpc1020->spi->dev, "gpio_to_irq failed.\n");
		error = fpc1020->irq;
		return error;
	}

	error = request_irq(fpc1020->irq, fpc1020_interrupt,
			IRQF_TRIGGER_RISING, "fpc1020", fpc1020);

	if (error) {
		dev_err(&fpc1020->spi->dev,
			"request_irq %i failed.\n",
			fpc1020->irq);

		fpc1020->irq = -EINVAL;

		return error;
	}

	error = enable_irq_wake(fpc1020->irq);

        if (error) {
                dev_err(&fpc1020->spi->dev,
                        "enable_irq_wake %i failed.\n",
                        fpc1020->irq);

                fpc1020->irq = -EINVAL;

                return error;
        }

	return error;
}


/* -------------------------------------------------------------------- */
static int  fpc1020_spi_setup(fpc1020_data_t *fpc1020,
					struct fpc1020_platform_data *pdata)
{
	int error = 0;

	printk(KERN_INFO "%s\n", __func__);

	fpc1020->spi->mode = SPI_MODE_0;
	fpc1020->spi->bits_per_word = 8;
	fpc1020->spi->chip_select = 0;

	error = spi_setup(fpc1020->spi);

	if (error) {
		dev_err(&fpc1020->spi->dev, "spi_setup failed\n");
		goto out_err;
	}

out_err:
	return error;
}
/* -------------------------------------------------------------------- */

#ifdef CONFIG_OF
static int  fpc1020_spi_get_of_pdata(struct device *dev)
{
	fpc1020_data_t *fpc1020 = dev_get_drvdata(dev);
	struct device_node *spi_node = dev->of_node;

	fpc1020->spi_use_pinctrl = of_property_read_bool(spi_node,"qcom,use-pinctrl");

	if(fpc1020->spi_use_pinctrl == false) {
		fpc1020->spi_gpio_clk =  of_get_named_gpio(spi_node, "qcom,gpio-clk",0);
		fpc1020->spi_gpio_miso = of_get_named_gpio(spi_node, "qcom,gpio-miso",0);
		fpc1020->spi_gpio_mosi = of_get_named_gpio(spi_node, "qcom,gpio-mosi",0);
		fpc1020->spi_gpio_cs =   of_get_named_gpio(spi_node,"qcom,gpio-cs0" ,0);
	}
	
	fpc1020->clk = clk_get(dev, "core_clk");
	if (IS_ERR(fpc1020->clk)) {
		printk("%s: unable to get core_clk\n", __func__);
	}

	fpc1020->pclk = clk_get(dev, "iface_clk");
	if (IS_ERR(fpc1020->pclk)) {
		printk("%s: unable to get iface_clk\n", __func__);
	}
	return 0;
}
#else
static int  fpc1020_spi_get_of_pdata(struct device *dev)
{
	struct device_node *node = dev->of_node;
	fpc1020_data_t *fpc1020 = dev_get_drvdata(dev);

	fpc1020->spi_use_pinctrl = 1;

	fpc1020->spi_gpio_clk = -EINVAL;
	fpc1020->spi_gpio_miso = -EINVAL;
	fpc1020->spi_gpio_mosi = -EINVAL;
	fpc1020->spi_gpio_cs = -EINVAL;

	return -ENODEV;
}
#endif

/* -------------------------------------------------------------------- */
#ifdef CONFIG_OF
static int  fpc1020_get_of_pdata(struct device *dev,
					struct fpc1020_platform_data *pdata)
{
	struct device_node *node = dev->of_node;
#ifdef CONFIG_PINCTRL
	fpc1020_data_t *fpc1020 = dev_get_drvdata(dev);
#endif
	/* optional properties */
	const void *vddtx_prop = of_get_property(node, "fpc,vddtx_mv", NULL);
	const void *boost_prop =
			of_get_property(node, "fpc,txout_boost_enable", NULL);
	const void *hwid_prop =
			of_get_property(node, "fpc,force_hwid", NULL);
	const void *use_regulator_for_bezel_prop = of_get_property(node, "vdd_tx-supply", NULL);

	if (node == NULL) {
		dev_err(dev, "%s: Could not find OF device node\n", __func__);
		goto of_err;
	}

	pdata->reset_gpio = -EINVAL;
	pdata->irq_gpio   = -EINVAL;

	if (NULL != of_find_property(node, "fpc,gpio_irq", NULL)) {
		pdata->irq_gpio = of_get_named_gpio_flags(node,"fpc,gpio_irq", 0, NULL);
	}

	if (NULL != of_find_property(node, "fpc,gpio_reset", NULL)) {
		pdata->reset_gpio = of_get_named_gpio_flags(node,"fpc,gpio_reset", 0, NULL);
	}

	pdata->external_supply_mv =
			(vddtx_prop != NULL) ? be32_to_cpup(vddtx_prop) : 0;

	pdata->txout_boost = (boost_prop != NULL) ? 1 : 0;

	pdata->force_hwid =
			(hwid_prop != NULL) ? be32_to_cpup(hwid_prop) : 0;

	pdata->use_regulator_for_bezel = use_regulator_for_bezel_prop  ? 1 : 0;
	pdata->use_regulator_for_bezel = 0;

#ifdef CONFIG_PINCTRL
	fpc1020->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(fpc1020->pinctrl)) {
		dev_err(dev, "Failed to get pin ctrl\n");
		return 0;
	}
	fpc1020->pins_active = pinctrl_lookup_state(fpc1020->pinctrl,
				PINCTRL_STATE_DEFAULT);
	if (IS_ERR_OR_NULL(fpc1020->pins_active)) {
		dev_err(dev, "Failed to lookup pinctrl default state\n");
		return PTR_ERR(fpc1020->pins_active);
	}

	fpc1020->pins_sleep = pinctrl_lookup_state(fpc1020->pinctrl,
				PINCTRL_STATE_SLEEP);
	if (IS_ERR_OR_NULL(fpc1020->pins_sleep)) {
		dev_err(dev, "Failed to lookup pinctrl sleep state\n");
		return PTR_ERR(fpc1020->pins_sleep);
	}
#endif	
	return 0;

of_err:
	pdata->reset_gpio = -EINVAL;
	pdata->irq_gpio   = -EINVAL;
	pdata->force_hwid = -EINVAL;

	pdata->external_supply_mv = 0;
	pdata->txout_boost = 0;

	return -ENODEV;
}

#else
static int  fpc1020_get_of_pdata(struct device *dev,
					struct fpc1020_platform_data *pdata)
{
	pdata->reset_gpio = -EINVAL;
	pdata->irq_gpio   = -EINVAL;
	pdata->force_hwid = -EINVAL;

	pdata->external_supply_mv = 0;
	pdata->txout_boost = 0;

	return -ENODEV;
	}
#endif

static ssize_t fpc1020_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	//fpc1020_data_t *fpc1020 = container_of(dev,fpc1020_data_t,device);
	fpc1020_data_t *fpc1020 = gfpc1020;
	return sprintf(buf, "%s\n", fpc1020->state?"enabled":"disabled");
}

static ssize_t fpc1020_state_store(struct device *dev,struct device_attribute *attr,	const char *buf, size_t count)
{
	fpc1020_data_t *fpc1020 = gfpc1020;
    printk(KERN_DEBUG "%s\n", __func__);
	if(strncmp("enabled",buf,7)==0)
	{
		fpc1020->state = 1;
		fpc1020_hw_prepare(fpc1020);
		fpc1020_reset(fpc1020);
	} else if(strncmp("disabled",buf,8)==0)
	{
		fpc1020->state = 0;
		fpc1020_hw_unprepare(fpc1020);
	} else 
		printk("unknow state:%s\n",buf);
	return count;
}

static DEVICE_ATTR(state, 0644, fpc1020_state_show, fpc1020_state_store);

/* -------------------------------------------------------------------- */
static int  fpc1020_create_class(fpc1020_data_t *fpc1020)
{
	int error = 0;

	dev_dbg(&fpc1020->spi->dev, "%s\n", __func__);

	fpc1020->class = class_create(THIS_MODULE, FPC1020_CLASS_NAME);

	if (IS_ERR(fpc1020->class)) {
		dev_err(&fpc1020->spi->dev, "failed to create class.\n");
		error = PTR_ERR(fpc1020->class);
	}

	return error;
}


/* -------------------------------------------------------------------- */
static int  fpc1020_create_device(fpc1020_data_t *fpc1020)
{
	int error = 0;

	dev_dbg(&fpc1020->spi->dev, "%s\n", __func__);

	if (FPC1020_MAJOR > 0) {
		fpc1020->devno = MKDEV(FPC1020_MAJOR, 0);

		error = register_chrdev_region(fpc1020->devno,
						1,
						FPC1020_DEV_NAME);
	} else {
		error = alloc_chrdev_region(&fpc1020->devno,0,1,FPC1020_DEV_NAME);
	}

	if (error < 0) {
		dev_err(&fpc1020->spi->dev,
				"%s: FAILED %d.\n", __func__, error);
		goto out;

	} else {
		dev_info(&fpc1020->spi->dev, "%s: major=%d, minor=%d\n",
						__func__,
						MAJOR(fpc1020->devno),
						MINOR(fpc1020->devno));
	}

	fpc1020->device = device_create(fpc1020->class, NULL, fpc1020->devno,
						NULL, "%s", FPC1020_DEV_NAME);

	if (IS_ERR(fpc1020->device)) {
		dev_err(&fpc1020->spi->dev, "device_create failed.\n");
		error = PTR_ERR(fpc1020->device);
	}
	error = device_create_file(fpc1020->device, &dev_attr_state);
	if (error)
		printk("create file failed\n");
out:
	return error;
}



/* -------------------------------------------------------------------- */
irqreturn_t fpc1020_interrupt(int irq, void *_fpc1020)
{
	fpc1020_data_t *fpc1020 = _fpc1020;
	if (gpio_get_value(fpc1020->irq_gpio)) {
		fpc1020->interrupt_done = true;
		wake_up_interruptible(&fpc1020->wq_irq_return);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

/* -------------------------------------------------------------------- */
/* function definitions							*/
/* -------------------------------------------------------------------- */
static int  fpc1020_init(void)
{
	if (spi_register_driver(&fpc1020_driver))
		return -EINVAL;

	return 0;
}


/* -------------------------------------------------------------------- */
static void  fpc1020_exit(void)
{
	printk(KERN_INFO "%s\n", __func__);

	spi_unregister_driver(&fpc1020_driver);
}

module_init(fpc1020_init);
module_exit(fpc1020_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fingerprint Cards AB <tech@fingerprints.com>");
MODULE_DESCRIPTION("FPC1020 touch sensor driver.");
