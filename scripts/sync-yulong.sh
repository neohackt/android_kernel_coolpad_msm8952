#!/bin/bash

YL_BRANCH=`cat .git/HEAD | sed "s/ref: refs\/heads\///"`

CONFIG_FILE=`pwd`/scripts/plt_config.txt

if [ ! -e "$CONFIG_FILE" ] ; then
        echo "plt_config.txt isn't exist!"
        exit 0
fi

REPO_SVR=`grep '^REPO_SVR=' $CONFIG_FILE | awk -F = '{print $2}'`
REPO_PORT=`grep '^REPO_PORT=' $CONFIG_FILE | awk -F = '{print $2}'`
QCOM=`grep '^QCOM=' $CONFIG_FILE | awk -F = '{print $2}'`

CONFIG_FILE0=`pwd`
if echo $CONFIG_FILE0 |grep -q 'kernel'; then
GIT_DIR=kernel
else
GIT_DIR=bootable\/bootloader\/lk
fi

MYDROID=` pwd | sed "s%${GIT_DIR}$%%"`
GDIR=`echo $GIT_DIR | tr -d '\'`
#some path
SRC_DIR=$MYDROID/yulong/$GDIR
DEST_DIR=$MYDROID/$GDIR

#1. cd to yulong kernel,update it and get its version number  
	cd $SRC_DIR

	NAME=`git config  --get user.name`
	EMAIL=`git config  --get user.email`
		
	env -i git config  user.name $NAME
	env -i git config  user.email $EMAIL

	env -i git reset --hard HEAD && git clean -xdf 
	env -i git checkout $YL_BRANCH && git remote update && git reset --hard origin/$YL_BRANCH

#get the Old version number 
	TAG_FILE=$MYDROID/tag_list.txt
	git tag -l ${YL_BRANCH}* > $TAG_FILE
	OLD_TAG=`awk '{a=$0} END {print a}' $TAG_FILE`
	OLD_CID=`env -i git log $OLD_TAG -1 --format=%h`
	rm -rf $TAG_FILE

#git pull the new yulong/kernel from $REPO_SVR:$REPO_PORT and get the new version number 
	env -i git pull ssh://$REPO_SVR:$REPO_PORT/$QCOM/yulong/$GDIR/.git $YL_BRANCH
	NEW_CID=`env -i git log HEAD -1 --format=%h`

#update the tag,just a record
	TAG_NAME=${YL_BRANCH}_`date +%Y.%m.%d_%H.%M.%S`
	echo add tag $TAG_NAME
	env -i git tag -f $TAG_NAME

	if [ "$OLD_CID" = "$NEW_CID" ]; then
		echo -e "\nno update in $YL_BRANCH...\n"
		CI=`env -i git log --format=%s%n%n%b -n 10`
	else
		CI=`env -i git log $OLD_CID..$NEW_CID --format=%s%n%n%b`
	fi
	
	YL_FILES=$MYDROID/yulong_file.txt
	dir -A -1 | sed "s/.git//"| sed '/^$/d' > $YL_FILES

	DEL_FILES=$MYDROID/yulong_del_file.txt
	env -i git diff $OLD_CID HEAD --name-only --diff-filter=D > $DEL_FILES

#2. save local change to stash,cp bsp update,then pop stash
	cd $DEST_DIR

	env -i git add .
	env -i git stash save "${TAG_NAME}_stash"

	echo -e "==============synchronizing bsp dir files=============="

# copy all file to local dir	
	while read line
		do
			cp -rf $SRC_DIR/$line $DEST_DIR > /dev/null
			
		done < $YL_FILES
		
#		rm -rf $YL_FILES
		
# delete files according to git 
	while read line
		do					
			echo -e "rm $ff"		
			git	 rm $DEST_DIR/$ff	
		done < $DEL_FILES
		
		rm -rf $DEL_FILES
	echo -e "synchronizing bsp dir files ok"

#3.commit the changes
	#need to get user.name and user.email from ~/.gitconfig and push to .git/config 
	#or you cannot commit
	NAME=`git config  --get user.name`
	EMAIL=`git config  --get user.email`
		
	env -i git config  user.name $NAME
	env -i git config  user.email $EMAIL
		
	env -i git add .
	env -i git commit -s -m "$CI"

#4.stash pop,maybe need merge	
	#env -i git stash pop
	echo -e "\nnote: some uncommit local files are put in stash \n"

