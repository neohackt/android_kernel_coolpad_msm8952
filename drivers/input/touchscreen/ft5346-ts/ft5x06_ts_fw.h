#ifndef __LINUX_FT5X0X_TS_FW_H__
#define __LINUX_FT5X0X_TS_FW_H__

#if defined(CONFIG_BOARD_K2_01)||defined(CONFIG_BOARD_K2_02)
#define FT8606
#elif defined(CONFIG_BOARD_CPSK3_I01)\
	||defined(CONFIG_BOARD_CPSK3_I01_CT)\
	||defined(CONFIG_BOARD_CPSK3_S00)\
    ||defined(CONFIG_BOARD_CPY92_900) || defined(CONFIG_BOARD_CPY91_S00) || defined(CONFIG_BOARD_CPY91_S00_U00) || defined(CONFIG_BOARD_CPY91_U00)\
	||defined(CONFIG_BOARD_SK3_01)||defined(CONFIG_BOARD_CPSK3_U00) || defined(CONFIG_BOARD_CP3600I) || defined(CONFIG_BOARD_CPY92_9_CU)
#endif

#define FT_UPGRADE_AA	                0xAA
#define FT_UPGRADE_55 	                0x55
#define FT_APP_INFO_ADDR	            0xd7f8
#define FT_UPGRADE_EARSE_DELAY		    2000
#define FTS_UPGRADE_LOOP	20

/*芯片型号，在移植时根据具体IC的型号来修改,只能定义其中一款*/
//#define IC_FT6208	0
//#define IC_FT5X06	1
//#define IC_FT5606	2
//#define IC_FT5X16	3
//#define IC_FT5X36	4
#define IC_FT5X46	5
#define FT5X46     1
//#define IC_FT8606 6


#ifdef FT8606
#define DEVICE_IC_TYPE      IC_FT8606
#define		MAX_R_FLASH_SIZE	256
#define    FTS_PACKET_LENGTH          128
#define    FTS_SETTING_BUF_LEN        128
#define ERROR_CODE_OK 	0
/***********************0705 mshl*/
#define    BL_VERSION_LZ4        0
#define    BL_VERSION_Z7         1
#define    BL_VERSION_GZF        2

#define LEN_FLASH_ECC_MAX 0xFFFE  // 一次ECC校验的最大长?

#elif defined(FT5X46)
#define DEVICE_IC_TYPE  IC_FT5X46

#else
#define DEVICE_IC_TYPE  IC_FT5X06
#endif

/*芯片型号，在移植时根据具体IC的型号来修改*/
#if defined(CONFIG_BOARD_K2_01)||defined(CONFIG_BOARD_K2_02)
#define FOCALTECH_SOC  "ft8606"	/*chipset version */
#elif defined(CONFIG_BOARD_CPSK3_I01)\
	||defined(CONFIG_BOARD_CPSK3_I01_CT)\
	||defined(CONFIG_BOARD_CPSK3_S00)\
    ||defined(CONFIG_BOARD_CPY92_900) || defined(CONFIG_BOARD_CPY91_S00) || defined(CONFIG_BOARD_CPY91_S00_U00) || defined(CONFIG_BOARD_CPY91_U00)\
	||defined(CONFIG_BOARD_SK3_01)||defined(CONFIG_BOARD_CPSK3_U00) || defined(CONFIG_BOARD_CP3600I) || defined(CONFIG_BOARD_CPY92_9_CU)
#define FOCALTECH_SOC  "ft5346"
#else
#define FOCALTECH_SOC  "ft5x06"

#endif

/*COB/COF 开关，如果使用COB 方案，为1，否则为0 */
#define FOCALTECH_COB	0

//X,Y轴最大值
#define TP_MAX_Y    1920    //modify TW max coords by jingyuantao@yulong.com
#define TP_MAX_X    1080

struct touch_panel_info {

	unsigned char tp_id;	/*touch panel factory id */

	unsigned char *tp_name;	/*touch panel factory name */

	unsigned char *firmware;	/*firmware for this factory's touch panel */

	unsigned int firmware_size;

};
#ifdef FT8606
static unsigned char aucFW_PRAM_BOOT[] =
{
#include "FT8606_Pramboot_V0.6_20150304.fw"
};

#endif
/****************down模式，升级bootloader用*************/

#if defined(CONFIG_BOARD_CPY92_900) || defined(CONFIG_BOARD_CPY91_S00) || defined(CONFIG_BOARD_CPY91_S00_U00) || defined(CONFIG_BOARD_CPY91_U00) || defined(CONFIG_BOARD_CP3600I) || defined(CONFIG_BOARD_CPY92_9_CU)
static unsigned char CTPM_FW_YEJI[] = {
    #include "coolpad_3600i_0x80_001_V05_D01_20160729_app.i"
};
static unsigned char CTPM_FW_SHENYUE[] = {
    #include "coolpad_3600i_0xA0_001_V05_D01_20160905_app.i"
};
static unsigned char CTPM_FW_OFILM[] = {
    //just test 20160708
    #include "coolpad_3600i_0x51_001_V08_D01_20160812_app.i"
};
static unsigned char CTPM_FW_OFILM_HX8398[] = {
    #include "coolpad_Y92_0x51_001_V05_D01_20160412_app.i"
};
struct touch_panel_info yl_fw[] = {
	{0x80, "Yeji", CTPM_FW_YEJI, sizeof(CTPM_FW_YEJI)},
	{0xa0, "Shenyue", CTPM_FW_SHENYUE, sizeof(CTPM_FW_SHENYUE)},
	{0x51, "Ofilm", CTPM_FW_OFILM, sizeof(CTPM_FW_OFILM)},
    {0x51, "Ofilm_hx8398", CTPM_FW_OFILM_HX8398, sizeof(CTPM_FW_OFILM_HX8398)},
};
#else

static unsigned char CTPM_FW_TIANMA[] = {
};

static unsigned char CTPM_FW_BOYI[] = {
};

static unsigned char CTPM_FW_BOEN[] = {
#include "coolpad_SK3_FT5346_0x3B_001_V17_D01_20151214_app.i"
};
static unsigned char CTPM_FW_BOEN_GF[] = {
#include "coolpad_SK3_5446i_0x3B_001_V0B_D0C_20160114_app.i"
};

static unsigned char CTPM_FW_SHENYUE[] = {
#include "coolpad_SK3_FT5346_0xA0_001_V17_D01_20151214_app.i"
};
static unsigned char CTPM_FW_SHENYUE_GF[] = {
#include "coolpad_SK3_5446i_0xA0_001_V0B_D0C_20160114_app.i"
};


struct touch_panel_info yl_fw[] = {
	{0xe8, "BOYI", CTPM_FW_BOYI, sizeof(CTPM_FW_BOYI)}
	,
	{0x8D, "TIANMA", CTPM_FW_TIANMA, sizeof(CTPM_FW_TIANMA)}
	,
	{0x3B, "BOEN", CTPM_FW_BOEN, sizeof(CTPM_FW_BOEN)}
	,
	{0x3B, "boen", CTPM_FW_BOEN_GF, sizeof(CTPM_FW_BOEN_GF)}
	,
	{0xA0, "SHENYUE", CTPM_FW_SHENYUE, sizeof(CTPM_FW_SHENYUE)}
	,
	{0xA0, "shenyue", CTPM_FW_SHENYUE_GF, sizeof(CTPM_FW_SHENYUE_GF)}
	,
};
#endif

#if FOCALTECH_COB
struct pin_to_id {

	unsigned char pin;	/*gpio state */

	unsigned char id;	/*touch panel factory id */

};

/*COB方案，ID 识别的GPIO 状态对应到具体的模组ID */
static const struct pin_to_id PIN_ID[] = {
	{0x00, 0x51},
	{0x01, 0x80},
	{0x02, 0x85},
	{0x03, 0x85},
};

#endif

/*以下部分不要修改*/
/* this blow don't modify except yl_fw */
const int fw_array_size = ARRAY_SIZE(yl_fw);

struct touch_panel_info *tp_info = yl_fw;

unsigned char *current_main_fw_to_tp = NULL;	//add for download mode
unsigned int current_main_fw_size = 0;	//add for download mode

unsigned char *current_fw_to_tp = NULL;

unsigned int current_fw_size = 0;

unsigned char *current_tp_name = NULL;

unsigned char current_tp_id;

unsigned int current_tp_version;

bool current_tp_supported = false;

#endif
