/*
 *
 * FocalTech fts TouchScreen driver.
 * 
 * Copyright (c) 2010-2015, Focaltech Ltd. All rights reserved.
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

 /*******************************************************************************
*
* File Name: Focaltech_Gestrue.c
*
* Author: Xu YongFeng
*
* Created: 2015-01-29
*   
* Modify by mshl on 2015-04-30
*
* Abstract:
*
* Reference:
*
*******************************************************************************/

/*******************************************************************************
* 1.Included header files
*******************************************************************************/
#include <linux/input/touchscreen_yl.h>
#include "focaltech_core.h"

/*******************************************************************************
* Private constant and macro definitions using #define
*******************************************************************************/
#define  KEY_GESTURE_U		KEY_U
#define  KEY_GESTURE_UP		KEY_UP
#define  KEY_GESTURE_DOWN		KEY_DOWN
#define  KEY_GESTURE_LEFT		KEY_LEFT 
#define  KEY_GESTURE_RIGHT		KEY_RIGHT
#define  KEY_GESTURE_O		KEY_O
#define  KEY_GESTURE_E		KEY_E
#define  KEY_GESTURE_M		KEY_M 
#define  KEY_GESTURE_L		KEY_L
#define  KEY_GESTURE_W		KEY_W
#define  KEY_GESTURE_S		KEY_S 
#define  KEY_GESTURE_V		KEY_V
#define  KEY_GESTURE_Z		KEY_Z

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
#define GESTURE_L		    0x44
#define GESTURE_S		    0x46
#define GESTURE_V		    0x54
#define GESTURE_Z		    0x41
#define FTS_GESTRUE_POINTS 255
#define FTS_GESTRUE_POINTS_ONETIME  62
#define FTS_GESTRUE_POINTS_HEADER 8
#define FTS_GESTURE_OUTPUT_ADRESS 0xD3
#define FTS_GESTURE_OUTPUT_UNIT_LENGTH 4

/*******************************************************************************
* Private enumerations, structures and unions using typedef
*******************************************************************************/
extern u32 support_gesture;
extern int doze_status;
/*******************************************************************************
* Static variables
*******************************************************************************/
short pointnum = 0;
unsigned short coordinate_x[150] = {0};
unsigned short coordinate_y[150] = {0};
char wakeup_slide[32];

/*******************************************************************************
* Global variable or extern global variabls/functions
*******************************************************************************/

/*******************************************************************************
* Static function prototypes
*******************************************************************************/


/*******************************************************************************
* Name: fts_Gesture_init
* Brief:
* Input:
* Output: None
* Return: None
*******************************************************************************/
int fts_Gesture_init(struct input_dev *input_dev)
{
	input_set_capability(input_dev, EV_KEY, KEY_POWER);
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_U); 
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_UP); 
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_DOWN);
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_LEFT); 
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_RIGHT); 
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_O);
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_E); 
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_M); 
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_L);
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_W);
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_S); 
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_V);
	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_Z);
		
	__set_bit(KEY_GESTURE_RIGHT, input_dev->keybit);
	__set_bit(KEY_GESTURE_LEFT, input_dev->keybit);
	__set_bit(KEY_GESTURE_UP, input_dev->keybit);
	__set_bit(KEY_GESTURE_DOWN, input_dev->keybit);
	__set_bit(KEY_GESTURE_U, input_dev->keybit);
	__set_bit(KEY_GESTURE_O, input_dev->keybit);
	__set_bit(KEY_GESTURE_E, input_dev->keybit);
	__set_bit(KEY_GESTURE_M, input_dev->keybit);
	__set_bit(KEY_GESTURE_W, input_dev->keybit);
	__set_bit(KEY_GESTURE_L, input_dev->keybit);
	__set_bit(KEY_GESTURE_S, input_dev->keybit);
	__set_bit(KEY_GESTURE_V, input_dev->keybit);
	__set_bit(KEY_GESTURE_Z, input_dev->keybit);

	return 0;
}

//yulong add for gesture

int fts_get_wakeup_gesture(char*  gesture)
{
    return sprintf(gesture, "%s", (char *)wakeup_slide);
}

int fts_gesture_ctrl(const char*  gesture_buf)
{
    char *gesture;
    int buf_len;
    char *gesture_p;
    char *temp_p;


    buf_len = strlen(gesture_buf);


    FTS_DBG("%s buf_len = %d.\n", __func__, buf_len);
    FTS_DBG("%s gesture_buf:%s.\n", __func__, gesture_buf);

    gesture_p = kzalloc(buf_len + 1, GFP_KERNEL);
    if(gesture_p == NULL)
    {
        FTS_DBG("%s: alloc mem error.\n", __func__);
        return -1;
    }
    temp_p = gesture_p;
    strlcpy(gesture_p, gesture_buf, buf_len + 1);

   while(gesture_p)
    {
        gesture = strsep(&gesture_p, ",");
        FTS_DBG("%s gesture:%s.\n", __func__, gesture);

        if (!strncmp(gesture, "up=",3))
        {
            if(!strncmp(gesture+3, "true", 4))
            {
                FTS_DBG("%s: enable up slide wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_UP_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+3, "false", 5))
            {
                FTS_DBG("%s: disable up slide wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_UP_SLIDE_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "down=",5))
        {
            if(!strncmp(gesture+5, "true", 4))
            {
                FTS_DBG("%s: enable down slide wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_DOWN_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+5, "false", 5))
            {
                FTS_DBG("%s: disable down slide wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_DOWN_SLIDE_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "left=",5))
        {
            if(!strncmp(gesture+5, "true", 4))
            {
                FTS_DBG("%s: enable left slide wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_LEFT_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+5, "false", 5))
            {
                FTS_DBG("%s: disable left slide wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_LEFT_SLIDE_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "right=",6))
        {
            if(!strncmp(gesture+6, "true", 4))
            {
                FTS_DBG("%s: enable right slide wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_RIGHT_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+6, "false", 5))
            {
                FTS_DBG("%s: disable right slide wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_RIGHT_SLIDE_WAKEUP;
            }
            continue;
        }

	 if (!strncmp(gesture, "double_click=",13))
        {
            if(!strncmp(gesture+13, "true", 4))
            {
                FTS_DBG("%s: enable double click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_DOUBLE_CLICK_WAKEUP;
            }
            else if(!strncmp(gesture+13, "false", 5))
            {
                FTS_DBG("%s: disable double click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_DOUBLE_CLICK_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "e=",2))
        {
            if(!strncmp(gesture+2, "true", 4))
            {
                FTS_DBG("%s: enable e click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_E_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+2, "false", 5))
            {
                FTS_DBG("%s: disable e click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_E_SLIDE_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "o=",2))
        {
            if(!strncmp(gesture+2, "true", 4))
            {
                FTS_DBG("%s: enable o click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_O_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+2, "false", 5))
            {
                FTS_DBG("%s: disable o click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_O_SLIDE_WAKEUP;
            }
            continue;
        }
	 if (!strncmp(gesture, "w=",2))
        {
            if(!strncmp(gesture+2, "true", 4))
            {
                FTS_DBG("%s: enable w click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_W_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+2, "false", 5))
            {
                FTS_DBG("%s: disable w click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_W_SLIDE_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "c=",2))
        {
            if(!strncmp(gesture+2, "true", 4))
            {
                FTS_DBG("%s: enable c click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_C_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+2, "false", 5))
            {
                FTS_DBG("%s: disable c click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_C_SLIDE_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "m=",2))
        {
            if(!strncmp(gesture+2, "true", 4))
            {
                FTS_DBG("%s: enable m click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_M_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+2, "false", 5))
            {
                FTS_DBG("%s: disable m click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_M_SLIDE_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "l=",2))
        {
            if(!strncmp(gesture+2, "true", 4))
            {
                FTS_DBG("%s: enable l click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_L_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+2, "false", 5))
            {
                FTS_DBG("%s: disable l click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_L_SLIDE_WAKEUP;
            }
            continue;
        }
        if (!strncmp(gesture, "s=",2))
        {
            if(!strncmp(gesture+2, "true", 4))
            {
                FTS_DBG("%s: enable s click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_S_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+2, "false", 5))
            {
                FTS_DBG("%s: disable s click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_S_SLIDE_WAKEUP;
            }
            continue;
        }

        if (!strncmp(gesture, "v=",2))
        {
            if(!strncmp(gesture+2, "true", 4))
            {
                FTS_DBG("%s: enable v click wakeup func.\n", __func__);
                support_gesture |= TW_SUPPORT_V_SLIDE_WAKEUP;
            }
            else if(!strncmp(gesture+2, "false", 5))
            {
                FTS_DBG("%s: disable v click wakeup func.\n", __func__);
                support_gesture &= ~TW_SUPPORT_V_SLIDE_WAKEUP;
            }
            continue;
        }
    }
    kfree(temp_p);
    return 1;
}
//yulong add end

/*******************************************************************************
* Name: fts_check_gesture
* Brief:
* Input:
* Output: None
* Return: None
*******************************************************************************/
extern struct device *touchscreen_get_dev(void);
static void fts_check_gesture(struct input_dev *input_dev,int gesture_id)
{
       // int ret = 1;
        char *none_wakeup[2] = {"GESTURE=NONE",NULL};
        char *left_wakeup[2] = {"GESTURE=LEFT",NULL};
        char *right_wakeup[2] = {"GESTURE=RIGHT",NULL};
        char *up_wakeup[2] = {"GESTURE=UP",NULL};
        char *down_wakeup[2] = {"GESTURE=DOWN",NULL};
        char *doubleClick_wakeup[2] = {"GESTURE=DOUBLE_CLICK",NULL};
        char *c_wakeup[2] = {"GESTURE=C",NULL};
        char *e_wakeup[2] = {"GESTURE=E",NULL};
        char *m_wakeup[2] = {"GESTURE=M",NULL};
        char *o_wakeup[2] = {"GESTURE=O",NULL};
        char *w_wakeup[2] = {"GESTURE=W",NULL};
        char *l_wakeup[2] = {"GESTURE=L",NULL};
        char *s_wakeup[2] = {"GESTURE=S",NULL};
        char *v_wakeup[2] = {"GESTURE=V",NULL};
        char **envp;
        envp = none_wakeup;
        sprintf(wakeup_slide,"null");
	doze_status = DOZE_WAKEUP;
	switch(gesture_id)
	{
	        case GESTURE_LEFT:
				if(support_gesture & TW_SUPPORT_LEFT_SLIDE_WAKEUP){
	                        FTS_DBG("slide left\n");
                               sprintf(wakeup_slide,"left");
                               envp = left_wakeup;
				}
	                break;
	        case GESTURE_RIGHT:
				if(support_gesture & TW_SUPPORT_RIGHT_SLIDE_WAKEUP){
	                        FTS_DBG("slide right\n");
                               sprintf(wakeup_slide,"right");
                               envp = right_wakeup;
				}
	                break;
	        case GESTURE_UP:
				if(support_gesture & TW_SUPPORT_UP_SLIDE_WAKEUP){
	                        FTS_DBG("slide up\n");
                               sprintf(wakeup_slide,"up");
                               envp = up_wakeup;
				}
	                break;
	        case GESTURE_DOWN:
				if(support_gesture & TW_SUPPORT_DOWN_SLIDE_WAKEUP){
	                        FTS_DBG("slide down\n");
                               sprintf(wakeup_slide,"down");
                               envp = down_wakeup;
				}
	                break;
	        case GESTURE_DOUBLECLICK:
				if(support_gesture & TW_SUPPORT_DOUBLE_CLICK_WAKEUP){
	                        FTS_DBG("slide double_click\n");
                               sprintf(wakeup_slide,"double_click");
                               envp = doubleClick_wakeup;
				}
	                break;
	        case GESTURE_O:
				if(support_gesture & TW_SUPPORT_O_SLIDE_WAKEUP){
	                        FTS_DBG("slide o\n");
                               sprintf(wakeup_slide,"o");
                               envp = o_wakeup;
				}
	                break;
	        case GESTURE_W:
				if(support_gesture & TW_SUPPORT_W_SLIDE_WAKEUP){
	                         FTS_DBG("slide w\n");
                               sprintf(wakeup_slide,"w");
                               envp = w_wakeup;
				}
	                break;
	        case GESTURE_M:
				if(support_gesture & TW_SUPPORT_M_SLIDE_WAKEUP){
	                        FTS_DBG("slide m\n");
                               sprintf(wakeup_slide,"m");
                               envp = m_wakeup;
				}
	                break;
	        case GESTURE_E:
				if(support_gesture & TW_SUPPORT_E_SLIDE_WAKEUP){
	                        FTS_DBG("slide e\n");
                               sprintf(wakeup_slide,"e");
                               envp = e_wakeup;
				}
	                break;
                case GESTURE_C:
				if(support_gesture & TW_SUPPORT_C_SLIDE_WAKEUP){
	                        FTS_DBG("slide C\n");
                               sprintf(wakeup_slide,"c");
                               envp = c_wakeup;
				}
	                break;
	        case GESTURE_L:
				if(support_gesture & TW_SUPPORT_L_SLIDE_WAKEUP){
	                        FTS_DBG("slide l\n");
                               sprintf(wakeup_slide,"l");
                               envp = l_wakeup;
				}
	                break;
	        case GESTURE_S:
				if(support_gesture & TW_SUPPORT_S_SLIDE_WAKEUP){
	                        FTS_DBG("slide s\n");
                               sprintf(wakeup_slide,"s");
                               envp = s_wakeup;
				}
	                break;
	        case GESTURE_V:
				if(support_gesture & TW_SUPPORT_V_SLIDE_WAKEUP){
	                        FTS_DBG("slide v\n");
                               sprintf(wakeup_slide,"v");
                               envp = v_wakeup;
				}
	                break;
	      /* case GESTURE_Z:
				if(support_gesture & TW_SUPPORT_LEFT_SLIDE_WAKEUP){
	                        TPD_GESTURE_DBG("slide left\n");
                               sprintf(wakeup_slide,"left");
                               envp = left_wakeup;
				}
	                break;*/
	        default:
                     envp = none_wakeup;
                     sprintf(wakeup_slide,"null");
	             doze_status = DOZE_ENABLED;
	                break;
	}

       if ((doze_status == DOZE_WAKEUP)&&(envp != none_wakeup))
        {
            struct device *touchscreen_dev;
             touchscreen_dev = touchscreen_get_dev();
             kobject_uevent_env(&touchscreen_dev->kobj, KOBJ_CHANGE, envp);
             FTS_DBG("send kobject uevent!\n");
             doze_status = DOZE_ENABLED;
        }
}

 /************************************************************************
* Name: fts_read_Gestruedata
* Brief: read data from TP register
* Input: no
* Output: no
* Return: fail <0
***********************************************************************/
int fts_read_Gestruedata(void)
{
	unsigned char buf[FTS_GESTRUE_POINTS * 3] = { 0 };
	int ret = -1;
	int i = 0;
	int gestrue_id = 0;

	buf[0] = 0xd3;
	pointnum = 0;

	ret = fts_i2c_read(fts_i2c_client, buf, 1, buf, FTS_GESTRUE_POINTS_HEADER);

	if (ret < 0)
	{
		printk( "%s read touchdata failed.\n", __func__);
		return ret;
	}

	 if (fts_updateinfo_curr.CHIP_ID==0x54 || fts_updateinfo_curr.CHIP_ID==0x58)
	 {
		 gestrue_id = buf[0];
		 pointnum = (short)(buf[1]) & 0xff;
		 buf[0] = 0xd3;

	 if((pointnum * 4 + 8)<255)
	 {
	    	 ret = fts_i2c_read(fts_i2c_client, buf, 1, buf, (pointnum * 4 + 8));
	 }
	 else
	 {
	        ret = fts_i2c_read(fts_i2c_client, buf, 1, buf, 255);
	        ret = fts_i2c_read(fts_i2c_client, buf, 0, buf+255, (pointnum * 4 + 8) -255);
	 }
	 if (ret < 0)
	 {
	       printk( "%s read touchdata failed.\n", __func__);
	       return ret;
	 }

	 fts_check_gesture(fts_input_dev,gestrue_id);
	 for(i = 0;i < pointnum;i++)
	 {
	    	coordinate_x[i] =  (((s16) buf[0 + (4 * i)]) & 0x0F) <<
	        	8 | (((s16) buf[1 + (4 * i)])& 0xFF);
	    	coordinate_y[i] = (((s16) buf[2 + (4 * i)]) & 0x0F) <<
	        	8 | (((s16) buf[3 + (4 * i)]) & 0xFF);
	 }
	 return 0;
	}
	else
	{
		if (0x24 == buf[0])
		{
			gestrue_id = 0x24;
			fts_check_gesture(fts_input_dev,gestrue_id);
			printk( "%d check_gesture gestrue_id.\n", gestrue_id);
			return -1;
		}

		pointnum = (short)(buf[1]) & 0xff;
		buf[0] = 0xd3;
		if((pointnum * 4 + 8)<255)
		{
			ret = fts_i2c_read(fts_i2c_client, buf, 1, buf, (pointnum * 4 + 8));
		}
		else
		{
			ret = fts_i2c_read(fts_i2c_client, buf, 1, buf, 255);
			ret = fts_i2c_read(fts_i2c_client, buf, 0, buf+255, (pointnum * 4 + 8) -255);
		}
		if (ret < 0)
		{
			printk( "%s read touchdata failed.\n", __func__);
			return ret;
		}

		// gestrue_id = fetch_object_sample(buf, pointnum); // not use
		fts_check_gesture(fts_input_dev,gestrue_id);
		printk( "%d read gestrue_id.\n", gestrue_id);

		for(i = 0;i < pointnum;i++)
		{
		    coordinate_x[i] =  (((s16) buf[0 + (4 * i)]) & 0x0F) <<
		        8 | (((s16) buf[1 + (4 * i)])& 0xFF);
		    coordinate_y[i] = (((s16) buf[2 + (4 * i)]) & 0x0F) <<
		        8 | (((s16) buf[3 + (4 * i)]) & 0xFF);
		}
		return 0;
	}
}
