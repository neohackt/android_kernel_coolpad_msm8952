/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "ov8856.h"

#undef CDBG
#define CDBG(fmt, args...) pr_debug(fmt, ##args)

#define SENSOR_MODULE_INFO
#define SENSOR_MODULE_LENC
//#define SENSOR_MODULE_AF
#define SENSOR_MODULE_AWB

#define OTP_ENABLE_ADDR 0x5001
#define OTP_MODE_ADDR 0x3D84
#define OTP_START_ADDR_H 0X3D88
#define OTP_START_ADDR_L 0X3D89
#define OTP_END_ADDR_H 0X3D8A
#define OTP_END_ADDR_L 0X3D8B
#define OTP_LOADBUFF_ADDR 0x3D81

#define OTP_GROUP_NUM 2

#ifdef SENSOR_MODULE_INFO
#define INFO_FLAG_ADDR 0X7010
#define INFO_START_ADDR_1 0X7011
#define INFO_END_ADDR_1 0X7019
#define INFO_START_ADDR_2 0X701A
#define INFO_END_ADDR_2 0X7022
#endif

#ifdef SENSOR_MODULE_LENC
#define LENC_FLAG_ADDR 0X702C
#define LENC_START_ADDR_1 0X702D
#define LENC_END_ADDR_1 0X711D
#define LENC_START_ADDR_2 0X711E
#define LENC_END_ADDR_2 0X720E
#endif

#ifdef SENSOR_MODULE_AWB
#define AWB_FLAG_ADDR 0X7010
#define AWB_START_ADDR_1 0X7016
#define AWB_END_ADDR_1 0X7018
#define AWB_START_ADDR_2 0X701f
#define AWB_END_ADDR_2 0X7021
#endif

struct otp_struct {
	int flag; // bit[7]: info, bit[6]:wb, bit[5]:vcm, bit[4]:lenc
	uint16_t module_integrator_id;
	uint16_t lens_id;
	uint16_t production_year;
	uint16_t production_month;
	uint16_t production_day;
	uint16_t rg_ratio;
	uint16_t bg_ratio;
	uint16_t light_rg;
	uint16_t light_bg;
	uint16_t lenc[240];
	int checksum;
	uint16_t VCM_start;
	uint16_t VCM_end;
	uint16_t VCM_dir;
};

static struct otp_struct current_otp = {
	.flag =0,
};

static int R_gain, G_gain, B_gain;

static int RG_Ratio_Typical, BG_Ratio_Typical;

#if 0

// index: index of otp group. (1, 2, 3)
// code: 0 for start code, 1 for stop code
// return: 0, group index is empty
// 1, group index has invalid data
// 2, group index has valid data
int check_otp_VCM(struct msm_sensor_ctrl_t *s_ctrl, int index, int code)
{
	uint16_t flag;
	/*begin added by longwei for camera OTP */
	//set 0x5001 to 0x00
	ov8856_write_i2c(s_ctrl, 0x5001, 0x00);
	/*end added by longwei for camera OTP */
	ov8856_write_i2c(s_ctrl, 0x3d84, 0xC0);
	//partial mode OTP write start address
	ov8856_write_i2c(s_ctrl, 0x3d88, 0x70);
	ov8856_write_i2c(s_ctrl, 0x3d89, 0x30);
	//partial mode OTP write end address
	ov8856_write_i2c(s_ctrl, 0x3d8A, 0x70);
	ov8856_write_i2c(s_ctrl, 0x3d8B, 0x30);
	//read otp into buffer
	ov8856_write_i2c(s_ctrl, 0x3d81, 0x01);
	msleep(5);
	//select group
	ov8856_read_i2c(s_ctrl, 0x7030, &flag);

	if (index == 1) {
		flag = (flag>>6) & 0x03;
	} else if (index == 2) {
		flag = (flag>>4) & 0x03;
	} else if (index == 3) {
		flag = (flag>>2) & 0x03;
	}

	CDBG("%s:index = %d,flag = %d, \n", __func__, index, (int)flag);

	//clear otp buffer
	ov8856_write_i2c(s_ctrl, 0x7030, 0x00);
	/*begin added by longwei for camera OTP */
	 //set 0x5001 to 0x08
	ov8856_write_i2c(s_ctrl, 0x5001, 0x08);
	/*end added by longwei for camera OTP */
	if (flag == 0x00) {
		return 0;
	} else if (flag & 0x02) {
		return 1;
	} else {
		return 2;
	}
}

// index: index of otp group. (1, 2, 3)
// code: 0 start code, 1 stop code
// return: 0
int read_otp_VCM(struct msm_sensor_ctrl_t *s_ctrl, int index, struct otp_struct * otp_ptr)
{
	int i;
	uint16_t temp;
	int start_addr = 0, end_addr = 0;

	CDBG("%s:the index of reading is:%d\n", __FUNCTION__, index);

	if (index == 1) {
		start_addr = 0x7031;
		end_addr = 0x7033;
	}
	else if (index == 2) {
		start_addr = 0x7034;
		end_addr = 0x7036;
	}
	else if (index == 3) {
		start_addr = 0x7037;
		end_addr = 0x7039;
	}
	/*begin added by longwei for camera OTP */
	//set 0x5001 to 0x00
	ov8856_write_i2c(s_ctrl, 0x5001, 0x00);
	/*end added by longwei for camera OTP */
	ov8856_write_i2c(s_ctrl, 0x3d84, 0xC0);
	//partial mode OTP write start address
	ov8856_write_i2c(s_ctrl, 0x3d88, (start_addr >> 8) & 0xff);
	ov8856_write_i2c(s_ctrl, 0x3d89, start_addr & 0xff);
	//partial mode OTP write end address
	ov8856_write_i2c(s_ctrl, 0x3d8A, (end_addr >> 8) & 0xff);
	ov8856_write_i2c(s_ctrl, 0x3d8B, end_addr & 0xff);
	// read otp into buffer
	ov8856_write_i2c(s_ctrl, 0x3d81, 0x01);
	msleep(5);
	//flag and lsb of VCM start code
	ov8856_read_i2c(s_ctrl, start_addr+2, &temp);
	(* otp_ptr).VCM_start = (ov8856_read_i2c(s_ctrl, start_addr, &((* otp_ptr).VCM_start))<<2) | ((temp>>6) & 0x03);
	(* otp_ptr).VCM_end = (ov8856_read_i2c(s_ctrl, start_addr + 1, &((* otp_ptr).VCM_end))<< 2) | ((temp>>4) & 0x03);
	(* otp_ptr).VCM_dir = (temp>>2) & 0x03;

	// clear otp buffer
	for (i=start_addr; i<=end_addr; i++) {
		ov8856_write_i2c(s_ctrl, i, 0x00);
	}
	/*begin added by longwei for camera OTP */
	//set 0x5001 to 0x08
	ov8856_write_i2c(s_ctrl, 0x5001, 0x08);
	/*end added by longwei for camera OTP */
	return 0;
}

#else

int32_t ov8856_read_i2c(struct msm_sensor_ctrl_t *s_ctrl, uint32_t addr, uint16_t *data)
{
	int32_t rc = 0;

	s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read(s_ctrl->sensor_i2c_client,
		addr, data,MSM_CAMERA_I2C_BYTE_DATA);

	return rc;
}

int32_t ov8856_write_i2c(struct msm_sensor_ctrl_t *s_ctrl,int addr,int data)
{
	int32_t rc;
	rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(s_ctrl->sensor_i2c_client,
		addr,data,MSM_CAMERA_I2C_BYTE_DATA);
	return rc;
}

// R_gain, sensor red gain of AWB, 0x400 =1
// G_gain, sensor green gain of AWB, 0x400 =1
// B_gain, sensor blue gain of AWB, 0x400 =1
// return 0;
static int update_awb_gain(struct msm_sensor_ctrl_t *s_ctrl, int R_gain, int G_gain, int B_gain)
{
	int32_t rc = 0;
	CDBG("%s:updated R_gain=%d,G_gain=%d,B_gain=%d\n", __FUNCTION__, R_gain, G_gain, B_gain);
	if (R_gain > 0x400) {
		rc |= ov8856_write_i2c(s_ctrl, 0x5019, R_gain>>8);
		rc |= ov8856_write_i2c(s_ctrl, 0x501A, R_gain & 0x00FF);
	}

	if (G_gain > 0x400) {
		rc |= ov8856_write_i2c(s_ctrl, 0x501B, G_gain>>8);
		rc |= ov8856_write_i2c(s_ctrl, 0x501C, G_gain & 0x00FF);
	}

	if (B_gain > 0x400) {
		rc |= ov8856_write_i2c(s_ctrl, 0x501D, B_gain>>8);
		rc |= ov8856_write_i2c(s_ctrl, 0x501E, B_gain & 0x00FF);
	}
	return rc;
}

// otp_ptr: pointer of otp_struct
int update_lenc(struct msm_sensor_ctrl_t *s_ctrl, struct otp_struct *otp_ptr)
{
	int32_t rc = 0;
	uint16_t i, temp;

	rc |= ov8856_read_i2c(s_ctrl, 0x5000, &temp);
	CDBG("%s:origin temp = %d\n", __func__, temp);
	temp |= 0x20;//modify for quarter size green
	rc |= ov8856_write_i2c(s_ctrl, 0x5000, temp);
	CDBG("%s:after temp = %d\n", __func__, temp);
	for (i = 0; i < 240; i++) {
		CDBG("%s:lenc[%d] = %d\n",__func__, i, (int)((*otp_ptr).lenc[i]));
		rc |= ov8856_write_i2c(s_ctrl, 0x5900 + i, (*otp_ptr).lenc[i]);
	}
	return rc;
}

/*
int32_t read_otp_param(struct msm_sensor_ctrl_t *s_ctrl,
	int group, int addr, struct otp_struct *otp_ptr)
{
	int rc = 0;
	int i;
	int start_addr = 0, end_addr = 0;

	CDBG("%s:the index of reading is:%d\n", __FUNCTION__, group);

	switch (group) {
		case 1 :
			start_addr = LENC_START_ADDR_1;
			end_addr = LENC_END_ADDR_1;
			break;
		case 2 :
			start_addr = LENC_START_ADDR_2;
			end_addr = LENC_END_ADDR_2;
		case 3 :
			//start_addr = INFO_START_ADDR_3;
			//end_addr = INFO_END_ADDR_3;
			//break;
		case 4 :
			//start_addr = INFO_START_ADDR_4;
			//end_addr = INFO_END_ADDR_4;
			//break;
		default :
			pr_err("**err group**");
			return rc;
	}

	//set 0x5001 to 0x00
	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x00);
	rc |= ov8856_write_i2c(s_ctrl, OTP_MODE_ADDR, 0xC0);
	//partial mode OTP write start address
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_H, start_addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_L, start_addr&0xff);
	//partial mode OTP write end address
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_H, end_addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_L, end_addr&0xff);
	//read otp into buffer
	rc |= ov8856_write_i2c(s_ctrl, OTP_LOADBUFF_ADDR, 0x01);
	msleep(10);

	for(i = start_addr; i < end_addr; i++) {
		ov8856_read_i2c(s_ctrl, (start_addr + i), &((*otp_ptr).lenc[i]));
		CDBG("%s:lenc[%d] = %d\n",__func__, i, (int)((*otp_ptr).lenc[i]));
	}

	// clear otp buffer
	for (i = start_addr; i <= end_addr; i++) {
		rc |= ov8856_write_i2c(s_ctrl, i, 0x00);
	}

	//set 0x5001 to 0x08
	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x08);

	return rc;
}
*/
int32_t group_from_check_flag(struct msm_sensor_ctrl_t *s_ctrl, int *group, int addr)
{
	int32_t rc = 0;
	uint16_t flag;

	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x00);

	rc |= ov8856_write_i2c(s_ctrl, OTP_MODE_ADDR, 0xC0);
	//partial mode OTP write start address
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_H, addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_L, addr&0xff);
	//partial mode OTP write end address
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_H, addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_L, addr&0xff);
	//read otp into buffer
	rc |= ov8856_write_i2c(s_ctrl, OTP_LOADBUFF_ADDR, 0x01);
	msleep(5);
	rc |= ov8856_read_i2c(s_ctrl, addr, &flag);

	//clear otp buffer
	rc |= ov8856_write_i2c(s_ctrl, INFO_FLAG_ADDR, 0x00);
	//set 0x5001 to 0x08
	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x08);

	if (rc)
		return rc;

	//select group
	if ((flag&0xc0) == 0x40)
		*group = 1;
	else if ((flag&0x30) == 0x10)
		*group = 2;
	else if ((flag&0x0c) == 0x04)
		*group = 3;
	else if ((flag&0x03) == 0x01)
		*group = 4;
	else
		*group = 0;

	if ((*group > OTP_GROUP_NUM) || (*group < 1)){
		pr_err("get wrong group\n");
		return -EFAULT;
	}

	CDBG("%s:group = %d,flag = %d\n", __func__, (int)*group, (int)flag);

	return rc;
}
// index: index of otp group. (1, 2, 3)
// otp_ptr: pointer of otp_struct
// return: 0,
int read_otp_wb(struct msm_sensor_ctrl_t *s_ctrl, int group, struct otp_struct *otp_ptr)
{
	int32_t rc = 0;
	int i;
	int start_addr = 0, end_addr = 0;
	uint16_t awb_lsb, awb_rg_msb, awb_bg_msb;

	CDBG("%s:the index of reading is:%d\n", __FUNCTION__, group);

	switch (group) {
		case 1 :
			start_addr = AWB_START_ADDR_1;
			end_addr = AWB_END_ADDR_1;
			break;
		case 2 :
			start_addr = AWB_START_ADDR_2;
			end_addr = AWB_END_ADDR_2;
		case 3 :
			//start_addr = INFO_START_ADDR_3;
			//end_addr = INFO_END_ADDR_3;
			//break;
		case 4 :
			//start_addr = INFO_START_ADDR_4;
			//end_addr = INFO_END_ADDR_4;
			//break;
		default :
			pr_err("**err group**");
			return -EFAULT;
	}

	//set 0x5001 to 0x00
	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x00);
	/*end added by longwei for camera OTP */
	rc |= ov8856_write_i2c(s_ctrl, OTP_MODE_ADDR, 0xC0);
	//partial mode OTP write start address
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_H, start_addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_L, start_addr&0xff);
	//partial mode OTP write end address
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_H, end_addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_L, end_addr&0xff);
	//read otp into buffer
	rc |= ov8856_write_i2c(s_ctrl, OTP_LOADBUFF_ADDR, 0x01);
	msleep(10);

	rc |= ov8856_read_i2c(s_ctrl, start_addr + 2, &awb_lsb);
	rc |= ov8856_read_i2c(s_ctrl, start_addr + 0, &awb_rg_msb);
	rc |= ov8856_read_i2c(s_ctrl, start_addr + 1, &awb_bg_msb);
	CDBG("%s:awb_lsb = %d\n", __func__, awb_lsb);
	CDBG("%s:awb_rg_msb = %d\n", __func__, awb_rg_msb);
	CDBG("%s:awb_bg_msb = %d\n", __func__, awb_bg_msb);

	(*otp_ptr).rg_ratio = (awb_rg_msb<<2) + ((awb_lsb>>6) & 0x03);
	(*otp_ptr).bg_ratio = (awb_bg_msb<<2) + ((awb_lsb>>4) & 0x03);

	CDBG("%s:rg_ratio = %d,bg_ratio=%d\n", __func__,
		(int)(*otp_ptr).rg_ratio,(int)((*otp_ptr).bg_ratio));

	(*otp_ptr).flag |= 0xc0; // valid info and WB in OTP

	// clear otp buffer
	for (i = start_addr; i <= end_addr; i++) {
		rc |= ov8856_write_i2c(s_ctrl, i, 0x00);
	}
	/*begin added by longwei for camera OTP */
	//set 0x5001 to 0x08
	rc |= ov8856_write_i2c(s_ctrl, 0x5001, 0x08);
	/*end added by longwei for camera OTP */
	return rc;
}

// call this function after OV8856 initialization
// return value: 0 update success
// 1, no OTP
//int update_otp_wb(struct msm_sensor_ctrl_t *s_ctrl)
int prepare_update_otp_wb(struct msm_sensor_ctrl_t *s_ctrl)
{
    int32_t rc;
    int group;
 //   int temp;
	int rg,bg;
	int nR_G_gain, nB_G_gain, nG_G_gain;
	int nBase_gain;

	// R/G and B/G of current camera module is read out from sensor OTP
	// check first OTP with valid data
	rc = group_from_check_flag(s_ctrl, &group, AWB_FLAG_ADDR);
	if (rc){
		pr_err("check wb info fail\n");
	}

	rc = read_otp_wb(s_ctrl, group, &current_otp);
	if (rc){
		pr_err("read wb info fail\n");
	}

	rg = current_otp.rg_ratio;
	bg = current_otp.bg_ratio;

	//calculate G gain
	nR_G_gain = (RG_Ratio_Typical*0x400) / rg;
	nB_G_gain = (BG_Ratio_Typical*0x400) / bg;
	nG_G_gain = 0x400;

	if (R_gain < G_gain) {
		nBase_gain = nR_G_gain;
	} else if(B_gain < G_gain) {
		nBase_gain = nB_G_gain;
	} else {
		nBase_gain = nG_G_gain;
	}

	return 0;
}


// index: index of otp group. (1, 2, 3)
// otp_ptr: pointer of otp_struct
// return: 0,
int read_otp_lenc(struct msm_sensor_ctrl_t *s_ctrl, int group, struct otp_struct *otp_ptr)
{
	int32_t rc = 0;
	int i, lenc_nums;
	int start_addr = 0, end_addr = 0;

	CDBG("%s:the index of reading is:%d\n", __FUNCTION__, group);

	switch (group) {
		case 1 :
			start_addr = LENC_START_ADDR_1;
			end_addr = LENC_END_ADDR_1;
			break;
		case 2 :
			start_addr = LENC_START_ADDR_2;
			end_addr = LENC_END_ADDR_2;
		case 3 :
			//start_addr = INFO_START_ADDR_3;
			//end_addr = INFO_END_ADDR_3;
			//break;
		case 4 :
			//start_addr = INFO_START_ADDR_4;
			//end_addr = INFO_END_ADDR_4;
			//break;
		default :
			pr_err("**err group**");
			return -EFAULT;
	}

	lenc_nums = end_addr - start_addr + 1;

	//set 0x5001 to 0x00
	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x00);
	/*end added by longwei for camera OTP */
	rc |= ov8856_write_i2c(s_ctrl, OTP_MODE_ADDR, 0xC0);
	//partial mode OTP write start address
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_H, start_addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_L, start_addr&0xff);
	//partial mode OTP write end address
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_H, end_addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_L, end_addr&0xff);
	//read otp into buffer
	rc |= ov8856_write_i2c(s_ctrl, OTP_LOADBUFF_ADDR, 0x01);

	msleep(10);

	for(i = 0; i < 240; i++) {
		ov8856_read_i2c(s_ctrl, (start_addr + i), &((*otp_ptr).lenc[i]));
		CDBG("%s:lenc[%d] = %8x\n",__func__, i, (int)((*otp_ptr).lenc[i]));
	}

	(*otp_ptr).flag |= 0x10; // valid lenc in OTP

	// clear otp buffer
	for (i = start_addr; i <= end_addr; i++) {
		rc |= ov8856_write_i2c(s_ctrl, i, 0x00);
	}

	//set 0x5001 to 0x08
	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x08);

	return rc;
}

// call this function after OV8856 initialization
// return value: 0 update success
// 1, no OTP
//int update_otp_lenc(struct msm_sensor_ctrl_t *s_ctrl)
int prepare_update_otp_lenc(struct msm_sensor_ctrl_t *s_ctrl)
{
	int group;
	int32_t rc;

	// check first lens correction OTP with valid data
	rc = group_from_check_flag(s_ctrl, &group, LENC_FLAG_ADDR);
	if (rc){
		pr_err("check lenc info fail\n");
	}

	rc = read_otp_lenc(s_ctrl, group, &current_otp);
	if (rc){
		pr_err("read lenc info fail\n");
	}

	return 0;
}

// index: index of otp group. (1, 2, 3)
// otp_ptr: pointer of otp_struct
// return: 0,
int32_t read_module_info(struct msm_sensor_ctrl_t *s_ctrl, int group, struct otp_struct *otp_ptr)
{
	int32_t rc = 0;
	int i;
	int start_addr = 0, end_addr = 0;

	CDBG("%s:the index of reading is:%d\n", __FUNCTION__, group);

	switch (group) {
		case 1 :
			start_addr = INFO_START_ADDR_1;
			end_addr = INFO_END_ADDR_1;
			break;
		case 2 :
			start_addr = INFO_START_ADDR_2;
			end_addr = INFO_END_ADDR_2;
		case 3 :
			//start_addr = INFO_START_ADDR_3;
			//end_addr = INFO_END_ADDR_3;
			//break;
		case 4 :
			//start_addr = INFO_START_ADDR_4;
			//end_addr = INFO_END_ADDR_4;
			//break;
		default :
			pr_err("**err group**");
			return -EFAULT;
	}

	//set 0x5001 to 0x00
	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x00);
	/*end added by longwei for camera OTP */
	rc |= ov8856_write_i2c(s_ctrl, OTP_MODE_ADDR, 0xC0);
	//partial mode OTP write start address
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_H, start_addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_START_ADDR_L, start_addr&0xff);
	//partial mode OTP write end address
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_H, end_addr>>8);
	rc |= ov8856_write_i2c(s_ctrl, OTP_END_ADDR_L, end_addr&0xff);
	//read otp into buffer
	rc |= ov8856_write_i2c(s_ctrl, OTP_LOADBUFF_ADDR, 0x01);
	msleep(5);

	rc |= ov8856_read_i2c(s_ctrl, start_addr, &((*otp_ptr).module_integrator_id));
	rc |= ov8856_read_i2c(s_ctrl, start_addr + 1, &((*otp_ptr).lens_id));
	rc |= ov8856_read_i2c(s_ctrl, start_addr + 2, &((*otp_ptr).production_year));
	rc |= ov8856_read_i2c(s_ctrl, start_addr + 3, &((*otp_ptr).production_month));
	rc |= ov8856_read_i2c(s_ctrl, start_addr + 4, &((*otp_ptr).production_day));

	CDBG("%s : module_integrator_id=%d\n", __func__, (int)((*otp_ptr).module_integrator_id));
	CDBG("%s : lens_id=%dd\n", __func__, (int)((*otp_ptr).lens_id));
	CDBG("%s : production_year=%d\n", __func__, (int)((*otp_ptr).production_year));
	CDBG("%s : production_month=%d\n", __func__, (int)((*otp_ptr).production_month));
	CDBG("%s : production_day=%d\n", __func__, (int)((*otp_ptr).production_day));

	// clear otp buffer
	for (i = start_addr; i <= end_addr; i++) {
		rc |= ov8856_write_i2c(s_ctrl, i, 0x00);
	}

	//set 0x5001 to 0x08
	rc |= ov8856_write_i2c(s_ctrl, OTP_ENABLE_ADDR, 0x08);

	return rc;
}

int get_module_info(struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t group;
	int32_t rc = 0;

	rc = group_from_check_flag(s_ctrl, &group, INFO_FLAG_ADDR);
	if (rc){
		pr_err("check module info fail\n");
		return rc;
	}

	rc = read_module_info(s_ctrl, group, &current_otp);
	if (rc){
		pr_err("read module info fail\n");
		return rc;
	}

	if(!(s_ctrl->module_id & 0xFF))
		s_ctrl->module_id |= current_otp.module_integrator_id;

	switch(current_otp.module_integrator_id)
	{
		case (MODULE_TECH_OV8856 & 0xFF):
			RG_Ratio_Typical = 0xf5;
			BG_Ratio_Typical = 0x117;
			pr_err("ov8856  Module is qtech\n");
			break;
		case (MODULE_OFILM_OV8856 & 0xFF):
			RG_Ratio_Typical = 0x135;
			BG_Ratio_Typical = 0x160;
			pr_err("ov8856  Module is ofilm\n");
			break;
		case (MODULE_ZQIAO_OV8856 & 0xFF):
			RG_Ratio_Typical = 0x136;
			BG_Ratio_Typical = 0x164;
			pr_err("ov8856  Module is zqiao\n");
			break;
		default:
			RG_Ratio_Typical = 0x11A;
			BG_Ratio_Typical = 0x11E;
			pr_err("ov8856 Module is not defined\n");
			break;
	}
	return 0;
}

#endif
int ov8856_sensor_prepare_otp(struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;

	rc = ov8856_write_i2c(s_ctrl,
			0x0103, 0x01);
	if (rc) {
		pr_err("stream on 0x0103 failed\n");
		return rc;
	}
	msleep(50);

	rc = ov8856_write_i2c(s_ctrl,
			0x0100, 0x01);
	if (rc) {
		pr_err("stream on failed\n");
		return rc;
	}

	rc = get_module_info(s_ctrl);
	if (rc) {
		pr_err("get_otp_info failed\n");
		return rc;
	}

	rc = prepare_update_otp_lenc(s_ctrl);
	if (rc) {
		pr_err("prepare_update_otp_lenc failed\n");
		return rc;
	}

	rc = prepare_update_otp_wb(s_ctrl);
	if (rc) {
		pr_err("prepare_update_otp_wb failed\n");
	}

	rc = ov8856_write_i2c(s_ctrl,
	   0x0100, 0x00);
	if (rc) {
		pr_err("stream off 0x0103 failed\n");
	}

	CDBG("exit ov8856_sensor_prepare_otp\n");

	return rc;
}

int ov8856_update_otp(struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;
	pr_info("enter ov8856_update_otp\n");

	if (current_otp.flag &0x10){
		rc = update_lenc(s_ctrl, &current_otp);
		if (rc){
			pr_err("update lenc fail\n");
		}
	}

	if (current_otp.flag &0x40){
		rc = update_awb_gain(s_ctrl, R_gain, G_gain, B_gain);
		if (rc){
			pr_err("update lenc fail\n");
		}
	}

	pr_info("exit ov8856_update_otp\n");
	return rc;
}

int ov8856_get_interface(struct msm_sensor_ctrl_t *s_ctrl)
{
    if(!s_ctrl  || !s_ctrl->func_tbl ){
        pr_err("%s: s_ctrl is %p \n", __func__, s_ctrl);
        return -1;
    }
    s_ctrl->func_tbl->sensor_prepare_otp = &ov8856_sensor_prepare_otp;
    s_ctrl->func_tbl->sensor_update_otp = &ov8856_update_otp;
    s_ctrl->sensor_prepare_otp = 1;
    return 0;
}
