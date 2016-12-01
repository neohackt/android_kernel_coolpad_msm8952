#!/bin/bash

if [ "$#" != "2" ] ; then
  echo -e "\n  $0  <commit-id> <config file>"
  echo  "  e.g: $0 HEAD  set_env.txt"
exit 1
fi

COMMIT_ID=$1
CONFIG_FILE=$2

GIT_DIR=`grep '^GIT_DIR=' $CONFIG_FILE | awk -F = '{print $2}'`
PLT_NAME=`grep '^PLT_NAME=' $CONFIG_FILE | awk -F = '{print $2}'`
YULONG_BRANCH=`grep '^YULONG_BRANCH=' $CONFIG_FILE | awk -F = '{print $2}'`

REPO_SVR=`grep '^REPO_SVR=' $CONFIG_FILE | awk -F = '{print $2}'`
GDIR=`echo $GIT_DIR | tr -d '\'`
MYDROID=`pwd | sed "s/${GIT_DIR}$//"`
BSP_SVR_REPO=ssh://${REPO_SVR}:29418/QCOM5.1/yulong/${GDIR}/.git
SRC_DIR=$MYDROID/$GDIR
TEMP_DIR=~/temp

if [ ! -d "$TEMP_DIR" ] ; then
	mkdir $TEMP_DIR
fi

if [ "$PLT_NAME" = "QC8x16" ] || [ "$PLT_NAME" = "QC8x26" ] || [ "$PLT_NAME" = "QC8x74ab" ] \
  || [ "$PLT_NAME" = "QC8x52_5.1" ] || [ "$PLT_NAME" = "QC8x52" ] \
  || [ "$PLT_NAME" = "QC8x39_5.1" ] || [ "$PLT_NAME" = "QC8x39" ] || [ "$PLT_NAME" = "QC8x74ac" ]; then
	POSTFIX="_yulong"$PLT_NAME
else
	echo "Unsupported $PLT_NAME,please check!"
	exit 2
fi

NAME=`git config  --get user.name`
EMAIL=`git config  --get user.email`

CI=`env -i git log -1 $COMMIT_ID --format=%s%n%n%b`

env -i git diff $COMMIT_ID^ $COMMIT_ID --name-only > $TEMP_DIR/file.lst
CHANGE_LST=$TEMP_DIR/change.lst
echo > $TEMP_DIR/del.lst
echo > $TEMP_DIR/patch_fail.txt
echo > $CHANGE_LST

ALL_FILE=`cat $TEMP_DIR/file.lst`
for ff in $ALL_FILE
do
     basename=`basename $ff`
	 export filename=`echo $basename |grep  '\.'`
	 #echo filename=$filename
     if [ $filename ] ;then
	   baseposix=${filename##*.}
	 #  echo baseposix=$baseposix
	   posix=`echo $baseposix | sed   "s/$POSTFIX//g"`
     else
	   posix=""
     fi 
	 #echo posix=$posix
	 
     name=${basename%.*}
	 nofix_name=`echo $name | sed   "s/$POSTFIX//g"`
	 nofix_ff=`echo $ff | sed   "s/$POSTFIX//g"`
	 #echo nofix_ff=$nofix_ff
	 #echo nofix_name=$nofix_name

     if [ $posix ] ;then
	     ori="$nofix_name.$posix"	 
	     postfix="$nofix_name$POSTFIX.$posix"
     else
	     ori="$nofix_name"
         postfix="$nofix_name$POSTFIX"
     fi
	 #echo transfer:$ori to $postfix
	 
	 dir=`dirname $ff`
	 
	 postfix_name=$dir/$postfix	
		
	env -i git show $COMMIT_ID  $ff > $TEMP_DIR/$file_name.patch
	if [ "$?" != "0" ] ; then
		echo $postfix_name >> $TEMP_DIR/del.lst
	else
		echo $ff >> $CHANGE_LST
	fi
done

#echo ==================cd dest dir and commit=============
cd ${MYDROID}/yulong/${GDIR}

env -i git config  user.name $NAME
env -i git config  user.email $EMAIL

#1.update local branch
env -i git checkout $YULONG_BRANCH
env -i git pull $BSP_SVR_REPO $YULONG_BRANCH

RESULT=0
#2. process modified files
ALL_FILE=`cat $CHANGE_LST`
for ff in $ALL_FILE
do
     basename=`basename $ff`
	 export filename=`echo $basename |grep  '\.'`
	 #echo filename=$filename
     if [ $filename ] ;then
	   baseposix=${filename##*.}
	 #  echo baseposix=$baseposix
	   posix=`echo $baseposix | sed   "s/$POSTFIX//g"`
     else
	   posix=""
     fi 
	 #echo posix=$posix
	 
     name=${basename%.*}
	 nofix_name=`echo $name | sed   "s/$POSTFIX//g"`
	 nofix_ff=`echo $ff | sed   "s/$POSTFIX//g"`
	 #echo nofix_ff=$nofix_ff
	 #echo nofix_name=$nofix_name

     if [ $posix ] ;then
	     ori="$nofix_name.$posix"	 
	     postfix="$nofix_name$POSTFIX.$posix"
     else
	     ori="$nofix_name"
         postfix="$nofix_name$POSTFIX"
     fi
	 #echo transfer:$ori to $postfix
	 
	 dir=`dirname $ff`
	 
	 postfix_name=$dir/$postfix	
	checkout_ff=0
	need_merge=1
	if [ ! -e ${ff} -a  ! -e $postfix_name ] ; then
		#. if ff and ffpostfix not exist,just rename ff to ffpostfix
		need_merge=0
		dir=`dirname $ff`
		mkdir -p $dir
		cp -l $SRC_DIR/$ff $postfix_name
		env -i git add $postfix_name
	elif [ -e ${ff} -a ! -e $postfix_name ] ; then
		# ff and no ffpostfix,need checkout ff
		checkout_ff=1
	elif [ ! -e ${ff} -a -e $postfix_name ] ; then
		# no ff have ffpostfix, no need checkout ff
		mv $postfix_name $ff	
	else
		# both ff and ffpostfix exist
		checkout_ff=1
		mv $postfix_name $ff
	fi

	if [ $need_merge = "1" ] ; then
	#	env -i git apply $TEMP_DIR/$file_name.patch
		patch -p1 -f < $TEMP_DIR/$file_name.patch	
		if [ "$?" != "0" ] ; then
			echo ">>>>>>>>>>>>>>>>patch fail at $ff<<<<<<<<<<<<<<<<<<<<<"
			RESULT=1
			echo $postfix_name >> $TEMP_DIR/patch_fail.txt
		fi
		cp -l $SRC_DIR/$ff $postfix_name
		rm $ff
		env -i git add $postfix_name
	fi

	if [ $checkout_ff = "1" ]; then
		env -i git checkout HEAD $ff
	fi
done


#3. remove deleted files
ALL_FILE=`cat $TEMP_DIR/del.lst`
for ff in $ALL_FILE
do
	env -i git rm $ff
done

#4. commit 
env -i git commit  -m "$CI"

#5. merge 
echo "result $RESULT"
if [ "$RESULT" = "1" ] ; then
	ALL_FILE=`cat $TEMP_DIR/patch_fail.txt`
	for ff in $ALL_FILE
	{
		#env -i git checkout $ff
		dst=`echo $ff | sed "s/$POSTFIX//g"`
		echo "merge $SRC_DIR/$dst into $ff by yourself !!!"
	}
fi	

