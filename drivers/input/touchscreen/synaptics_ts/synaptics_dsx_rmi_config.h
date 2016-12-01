#ifndef _SYNA_CONFIG_H
#define _SYNA_CONFIG_H

#define POWER_ON_REPAIR 0


struct touch_panel_info
{
	unsigned char  *tp_id;		/*touch panel factory id*/
	unsigned char *tp_name;		/*touch panel factory name */
	unsigned char *firmware;	/*firmware for this factory's touch panel*/
	unsigned int   firmware_size;
};



unsigned int IC_TYPE_4191 = 1;

#if defined(CONFIG_BOARD_CPY72_821) || defined(CONFIG_BOARD_CPY72_921)
static unsigned char SYNA_FW_boen[]=
{
    #include "SynaImage_boen_00000015.h"
};

static unsigned char SYNA_FW_ofilm[]=
{
     #include "SynaImage_ofilm_00000014.h"
};

struct touch_panel_info syna_tw_fw[] = {
	{"0xac", "boen", SYNA_FW_boen, sizeof(SYNA_FW_boen)},
    {"0xa0", "ofilm", SYNA_FW_ofilm, sizeof(SYNA_FW_ofilm)}
};
#endif
#endif


