#ifndef _FOCALTECH_5446_FW_H_
#define _FOCALTECH_5446_FW_H_

/* firmware info */
struct touch_panel_info
{
    unsigned char  tp_id;
    unsigned char *tp_name;
    unsigned char *tp_fw;
    unsigned int   tp_fw_size;
};

static unsigned char CTPM_FW_OFILM[] =
{
    #include "coolpad_8681_0x51_001_V1c_D01_20150801_app.i"
};

static unsigned char CTPM_FW_YEJI[] =
{
    #include "coolpad_8681_0x80_V12_D01_20150423_app.i"
};

static unsigned char CTPM_FW_BOEN[] =
{
    #include "coolpad_8681_0x3B_001_V1c_D01_20150801_app.i"
};

struct touch_panel_info yl_fw[] =
{
    {0x51, "ofilm", CTPM_FW_OFILM, sizeof(CTPM_FW_OFILM)},
    {0x80, "yeji" , CTPM_FW_YEJI , sizeof(CTPM_FW_YEJI )},
    {0x3B, "boen" , CTPM_FW_BOEN , sizeof(CTPM_FW_BOEN )},
};

#endif
