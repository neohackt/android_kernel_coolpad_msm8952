#!/bin/bash
MYDROID=`pwd`

CONFIG_FILE=`pwd`/plt_config.txt
if [ ! -e "$CONFIG_FILE" ] ; then
	CONFIG_FILE=`pwd`/kernel/scripts/plt_config.txt
        echo "CONFIG_FILE=$CONFIG_FILE"
fi

if [ ! -e "$CONFIG_FILE" ] ; then
        echo "plt_config.txt isn't exist!"
        exit 0
fi

QCOM_BRANCH=`grep '^QCOM_BRANCH=' $CONFIG_FILE | awk -F = '{print $2}'`
YL_BRANCH=`grep '^YL_BRANCH=' $CONFIG_FILE | awk -F = '{print $2}'`
YL_Product=`grep '^YL_Product=' $CONFIG_FILE | awk -F = '{print $2}'`
REPO_SVR=`grep '^REPO_SVR=' $CONFIG_FILE | awk -F = '{print $2}'`
REPO_PORT=`grep '^REPO_PORT=' $CONFIG_FILE | awk -F = '{print $2}'`
QCOM=`grep '^QCOM=' $CONFIG_FILE | awk -F = '{print $2}'`

echo work_dir is $MYDROID

############################the function U may use##################################
function sync-copyfile()
{
	MY_DROID=$1
	DEST_DIR=$2
	YL_BSP_DIR=$3
	GIT_BRANCH=$4

	#just tag the current local branch in yulong kernel,is useful for find its Hash value
	TAG_NAME=${GIT_BRANCH}_`date +%Y.%m.%d_%H.%M.%S`
	echo -e "add tag $TAG_NAME to yulong"
	env -i git tag -f $TAG_NAME		

#1 copy all file to local dir		
	echo -e "\n ==============copy bsp dir files=============="
	echo -e "\n copy files from $MY_DROID/$YL_BSP_DIR to $MY_DROID/$DEST_DIR ..."
	
	YL_FILES=$MY_DROID/yulong_file.txt
	dir -A -1 $MY_DROID/$YL_BSP_DIR/ | sed "s/.git//" | sed '/^$/d' > $YL_FILES	
	
	while read line
	do	
	
		cp -rf $MY_DROID/$YL_BSP_DIR/$line $MY_DROID/$DEST_DIR > /dev/null
		
	done < $YL_FILES
	
	rm -rf $YL_FILES
	
	echo -e "copy all files ok"

#2 commit the changes

	cd $MY_DROID/$DEST_DIR	
	
	#need to get user_name and user_email from ~/.gitconfig and push to .git/config 
	#or you maynot commit
	
	NAME=`git config  --get user.name`
	EMAIL=`git config  --get user.email`
	
	env -i git config  user.name $NAME
	env -i git config  user.email $EMAIL
	
	env -i git add .
	env -i git commit -s -m "sync all from yulong_bsp"
	
    echo -e "commit the changes ok "	
}

#repo清单文件
#2,repo qualcomm kernel/lk files list and clone git 
	cd $MYDROID
	REPO_LIST_Qualcomm="repo_qualcomm_list.txt"
	echo "$QCOM/kernel" > $REPO_LIST_Qualcomm
	echo "$QCOM/bootable/bootloader/lk" >> $REPO_LIST_Qualcomm

	while read line
	do
		echo -e "\n waiting for $line clone ...\n"
		GIT_DIR=`echo $line | sed "s/$QCOM\///"`
		if [ -d $MYDROID/$GIT_DIR ] ; then
			rm -rf $MYDROID/$GIT_DIR
		fi
		
		env -i git clone ssh://$REPO_SVR:$REPO_PORT/$line $MYDROID/$GIT_DIR
		
		cd $MYDROID/$GIT_DIR	
        echo "checkout step..."
		echo "checkout to $YL_BRANCH"
		scp -P $REPO_PORT -p $REPO_SVR:/hooks/commit-msg .git/hooks/
		env -i git fetch origin $QCOM_BRANCH:$YL_BRANCH > /dev/null 2>&1
		env -i git checkout $YL_BRANCH
		
	done < $REPO_LIST_Qualcomm
	
	rm -rf $MYDROID/$REPO_LIST_Qualcomm
	
#3,repo yulong kernel/lk files list and clone git 

	cd $MYDROID
	REPO_LIST_Yulong="repo_yulong_list.txt"
	echo "$QCOM/yulong/kernel" > $REPO_LIST_Yulong
	echo "$QCOM/yulong/bootable/bootloader/lk" >> $REPO_LIST_Yulong
	
		while read line
	do	
		echo -e "\n waiting for $line clone ...\n"
		YL_GIT_DIR=`echo $line | sed "s/$QCOM\///"`
		BASE_DIR=`echo $YL_GIT_DIR | sed 's/yulong\///'`
		if [ -d $MYDROID/$YL_GIT_DIR ] ; then
			rm -rf $MYDROID/$YL_GIT_DIR
		fi
		
		env -i git clone ssh://$REPO_SVR:$REPO_PORT/$line $MYDROID/$YL_GIT_DIR
		
		cd $MYDROID/$YL_GIT_DIR	
        echo "checkout step..."
		scp -P $REPO_PORT -p $REPO_SVR:/hooks/commit-msg .git/hooks/
        env -i git checkout $YL_BRANCH
		sync-copyfile $MYDROID $BASE_DIR $YL_GIT_DIR $YL_BRANCH > /dev/null
	
	done < $REPO_LIST_Yulong
	
	rm -rf $MYDROID/$REPO_LIST_Yulong
	
#4,copy yulong device code
	
	if [ -d $MYDROID/device/yulong ]; then
		rm -rf $MYDROID/device/yulong
	fi
	mkdir $MYDROID/device/yulong
	cd $MYDROID/device/yulong
	
	REPO_yulong_Devices="repo_yulong_devices.txt"
	echo "$QCOM/yulong/device/yulong/$YL_Product" > $REPO_yulong_Devices
	echo "$QCOM/yulong/device/yulong/common" >> $REPO_yulong_Devices
	
		while read line
	do
		echo -e "\n waiting for $line clone ...\n"
		GIT_DIR=${line##*/};
		env -i git clone ssh://$REPO_SVR:$REPO_PORT/$line			
		cd $MYDROID/device/yulong/$GIT_DIR	
        echo "checkout step.."
		scp -P $REPO_PORT -p $REPO_SVR:/hooks/commit-msg .git/hooks/
        env -i git checkout $YL_BRANCH
		
		cd ../
	done < $REPO_yulong_Devices
	
	rm -rf $MYDROID/device/yulong/$REPO_yulong_Devices
	cd $MYDROID
#5 modify some build error and make the bootimage
	
	sed -i 's/(error ALL_PREBUILT contains unexpected files)/(warning ALL_PREBUILT contains unexpected files)/' $MYDROID/build/core/main.mk
	sed -i 's/(error $(LOCAL_PATH): $(module_id) already defined by $($(module_id)))/(warning $(LOCAL_PATH): $(module_id) already defined by $($(module_id)))/' $MYDROID/build/core/base_rules.mk
	
  cd $MYDROID/yulong/kernel
  touch ../Android.mk
  #touch ../../device/qcom/Android.mk


