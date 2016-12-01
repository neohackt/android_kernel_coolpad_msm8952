#!/bin/bash

echo "target_product name is " ${TARGET_PRODUCT}
if [ -z "${TARGET_PRODUCT}" ]; then
	TARGET_PRODUCT=CPY1
fi
echo TARGET_PRODUCT is ${TARGET_PRODUCT}

if [ -z "${HARDWARE_VER}" ]; then
	HARDWARE_VER=P0
fi
echo HARDWARE_VER is ${HARDWARE_VER}

if [ "$ARCH" = "arm" ]
then
  DEST_CONF_PATH=${srctree}/arch/arm/configs
  PROD_CONF_PATH=${srctree}/arch/arm/mach-msm/board-${TARGET_PRODUCT}
else
  DEST_CONF_PATH=${srctree}/arch/arm64/configs
  PROD_CONF_PATH=${srctree}/arch/arm64/mach-msm/board-${TARGET_PRODUCT}
fi

DEFAULT_CONFIG=$1
echo DEFAULT_CONFIG is ${DEFAULT_CONFIG}

BASE_CONF=${DEST_CONF_PATH}/${DEFAULT_CONFIG}
DEST_CONF=${DEST_CONF_PATH}/.${DEFAULT_CONFIG}

PROD_CONF=${PROD_CONF_PATH}/yl_product_configs
BOARD_CONF=${PROD_CONF_PATH}/board_config_${HARDWARE_VER}
DEBUG_CONF=${PROD_CONF_PATH}/board_config_debug
YL_COMMON__CONF=${DEST_CONF_PATH}/yl_common_config

cat ${BASE_CONF} > ${DEST_CONF}
echo ""  >> ${DEST_CONF}

echo "#yulong common configs" >> ${DEST_CONF}
cat ${YL_COMMON__CONF} >> ${DEST_CONF}
echo ""  >> ${DEST_CONF}
echo "#yulong product configs " >> ${DEST_CONF}
cat ${PROD_CONF} >> ${DEST_CONF}
echo ""  >> ${DEST_CONF}
echo CONFIG_BOARD_${TARGET_PRODUCT}=y >> ${DEST_CONF}
echo CONFIG_BOARD_VER_${HARDWARE_VER}=y >> ${DEST_CONF}
if [ -f ${BOARD_CONF} ] 
then
	echo ""  >> ${DEST_CONF}
	echo "#yulong board configs " >> ${DEST_CONF}
	cat ${BOARD_CONF}>>${DEST_CONF}
fi
if [ "$TARGET_BUILD_VARIANT" = "eng" -a -f ${DEBUG_CONF} ]
then
	echo ""  >> ${DEST_CONF}
	echo "#yulong debug configs " >> ${DEST_CONF}
	cat ${DEBUG_CONF} >> ${DEST_CONF}
fi
