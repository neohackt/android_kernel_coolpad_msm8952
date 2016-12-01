#ifndef _SYNA_CONFIG_H
#define _SYNA_CONFIG_H

#define POWER_ON_REPAIR 0

struct touch_panel_info
{
    unsigned char  tp_id;          /*touch panel factory id*/
    unsigned char  *tp_name;       /*touch panel factory name */
    unsigned char  *firmware;      /*firmware for this factory's touch panel*/
    unsigned int   firmware_size;
};

#if 1
static unsigned char SYNA_FW_OFILM[]=
{
    #include "PR2072589-s3606r_Y91_PFF_ForOfilm_A00A0001.h"
};

static unsigned char SYNA_FW_EACH[]=
{
    #include "PR2072589-s3606r_Y91_PFF_each_A10A0003.h"
};

static unsigned char SYNA_FW_JUNDA[]=
{
    #include "PR2072589-s3606r_Y91_junda_AB0A0002.h"
};

static unsigned char SYNA_FW_BOEN[]=
{
    #include "PR2072589-s3606r_Y91_Biel_AC0A0002.h"
};

struct touch_panel_info syna_tw_fw[] = {
    {0xa0, "Ofilm", SYNA_FW_OFILM, sizeof(SYNA_FW_OFILM)},
    {0xa1, "Each", SYNA_FW_EACH, sizeof(SYNA_FW_EACH)},
    {0xab, "Junda", SYNA_FW_JUNDA, sizeof(SYNA_FW_JUNDA)},
    {0xac, "Boen", SYNA_FW_BOEN, sizeof(SYNA_FW_BOEN)}
};
#endif

#endif
