#!/bin/bash

copy_bsp_to_dest()
{
	PLT_NAME=$1
	BSP_DIR=$2
	DEST_DIR=$3

	echo > $MAIN_PLT_FILES
	echo > $SUB_PLT_FILES

	if [ "$PLT_NAME" = "QC8x16cta" ]; then
		MAIN_POSTFIX="_yulongQC8x16"
	else
		MAIN_POSTFIX="none"
	fi

	dir -A -1 $MYDROID/$BSP_DIR/ | sed "s/.git//" > $ALL_FILES

	cd $MYDROID/$DEST_DIR
	echo "copy files dest dir $MYDROID/$DEST_DIR"
	env -i git config  user.name $NAME
	env -i git config  user.email $EMAIL

	echo ==============copy bsp dir files==============
	#1). copy all file to local dir
	#cp $MYDROID/$BSP_DIR/* . -rf
	ALL_FILE=`cat $ALL_FILES`
	for ff in $ALL_FILE
	{
	       cp  $MYDROID/$BSP_DIR/$ff . -rf
	}
	echo copy all files ok


	#2). use POSTFIX replase file
	POSTFIX="_yulong"$PLT_NAME
	if [ $MAIN_POSTFIX = "none" ];then
		echo "No subplatform."
	else
		find . -type f -name "*$MAIN_POSTFIX.*" | sed "s/^.\/*//g" > $MAIN_PLT_FILES
		find . -type f -name "*$MAIN_POSTFIX" | sed "s/^.\/*//g" >> $MAIN_PLT_FILES
	fi
	find . -type f -name "*$POSTFIX*" | sed "s/^.\/*//g" > $SUB_PLT_FILES
	ALL_FILE=`cat $MAIN_PLT_FILES`
	for ff in $ALL_FILE
	{
		dst=`echo $ff | sed "s/$MAIN_POSTFIX//g"`
	    cp -f $ff $dst 
	}
	
	ALL_FILE=`cat $SUB_PLT_FILES`
	for ff in $ALL_FILE
	{
		dst=`echo $ff | sed "s/$POSTFIX//g"`
	    cp -f $ff $dst 
	}	

	#3). delete all yulongbsp plt files
	find . -type f -name "*yulongQC8x*" -delete
	cd -
}

MYDROID=$1
PL_NAME=$2
GIT_DIR=$3
GIT_GRANCH=$4
TEMP_DIR=~/temp
MAIN_PLT_FILES=$TEMP_DIR/plt_files.txt
SUB_PLT_FILES=$TEMP_DIR/sub_plt_files.txt
ALL_FILES=$TEMP_DIR/all.txt
if [ ! -d "$TEMP_DIR" ] ; then
	mkdir $TEMP_DIR
fi

NAME=`git config  --get user.name`
EMAIL=`git config  --get user.email`

TAG_NAME=$GIT_GRANCH_`date +%Y.%m.%d_%H.%M.%S`
echo add tag $TAG_NAME
env -i git tag -f $TAG_NAME

copy_bsp_to_dest $PL_NAME yulong/$GIT_DIR $GIT_DIR

