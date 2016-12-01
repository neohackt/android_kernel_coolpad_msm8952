#!/bin/sh

PLT_DIR=qcom
PRODUCT_NAME=$1
DIFF_RESULT=~/temp/diff_result.txt
LINK_LIST=~/temp/link.list
LINK_LIST_BAK=~/temp/link_bak.list
LINK_LIST_DTS=~/temp/link_dts.list
LINK_LIST_DTS_BAK=~/temp/link_dts_bak.list
BOARD_DIRS=~/temp/board_dirs.txt
DIFF_RESULT=~/temp/diff_result.txt
BOARD_DTS_OLD=~/temp/zll

#1.del same file from $2 
del_file()
{
	while read line
	do  
 		ff0=$line
		sed -i 's#'$ff0'#EXCLUSIVE#;/EXCLUSIVE/d' $2
		echo zll -----------del-1=$1,----2=$2
	done < $1
}

#2. copy file by read $1
copy_file()
{
   while read line
   do
     echo "zll--------copy==$line..."
     ff=$line
     dir=`dirname $ff`
     file=`basename $ff`
     if [ ! "$dir" = "." ];then
     	echo "zll-<><><><><>---dir=$dir----file=$file..."
	mkdir -p $BOARD_DTS_OLD/$dir
    	 cp -rf $2/$ff $BOARD_DTS_OLD/$dir
     else
     	cp -rf $2/$ff $BOARD_DTS_OLD
     	echo "zll-<><><><else><>---dir=$dir----file=$file..."
     fi
     unset dir
   done < $1
}
git reset --hard | git clean -xdf
if [ $2 -eq 64 ];then
	echo "-------------copy qcom from arm64"
	cp ../../../../../../kernel/arch/arm64/boot/dts/qcom/ . -rf
elif [ $2 -eq 32 ];then
	echo "-------------copy qcom from arm"
	cp ../../../../../../kernel/arch/arm/boot/dts/qcom/ . -rf
else
	echo error --------need 3 params
	exit 1
fi
#1.find board dirs
find . -name Makefile | sed "s/.\///" | grep ^"${PRODUCT_NAME}/"
find . -name Makefile | sed "s/.\///" | grep ^"${PRODUCT_NAME}/" | awk -F '/' '{print $2}' > $BOARD_DIRS
rm -rf $BOARD_DTS_OLD
#.>>000>>>>>>>>>>find the lower files between P0...Pn
while read line
do
	echo ">>>>>>>>>> $line-Diff <<<<<<<<<<"
	BOARD_DTS_CUR=$PRODUCT_NAME/$line
	if [ ! -d "$BOARD_DTS_OLD" ];then
		mkdir $BOARD_DTS_OLD
	fi
	if [ $(ls -al $BOARD_DTS_OLD | wc -l) = 3 ];then
		echo "zll -----BOARD_DTS_OLD is empty-------0"
		find $BOARD_DTS_CUR -type f | sed "s%${BOARD_DTS_CUR}/%%" | sed -e '/Makefile/d;/-[PT][0-9]/d' > $LINK_LIST_DTS 
 		copy_file $LINK_LIST_DTS $BOARD_DTS_CUR
	else
		echo "zll -----BOARD_DTS_OLD=$BOARD_DTS_OLD-----------BOARD_DTS_CUR= $BOARD_DTS_CUR-------1"
		cat $LINK_LIST_DTS > $LINK_LIST_DTS_BAK 
		diff -ruaBpbs $BOARD_DTS_OLD $BOARD_DTS_CUR > $DIFF_RESULT
		grep identical $DIFF_RESULT | awk -F ' ' '{print $4}' | sed "s%${BOARD_DTS_CUR}/%%" | tee $LINK_LIST_DTS
		if [ `(cat $LINK_LIST_DTS_BAK | wc -l)` -gt `(cat $LINK_LIST_DTS | wc -l)` ];then
			echo "zll -----------new low than old"
			rm -rf $BOARD_DTS_OLD/*
			copy_file $LINK_LIST_DTS $BOARD_DTS_CUR
		else
			echo "zll ----------use old"
		fi
	fi
done < $BOARD_DIRS
#.>>111>>>>>>>>>>find the lower files between P0...Pn

#.<<<<<<0000<<<<<<find same files between qcom and P0...Pn
diff -ruaBpbs $PLT_DIR $BOARD_DTS_OLD > $DIFF_RESULT
grep identical $DIFF_RESULT | awk -F ' ' '{print $2}' | sed "s/qcom\///" | tee $LINK_LIST
echo "<><><><><>1111"	
del_file $LINK_LIST $LINK_LIST_DTS
#.<<<<<<1111<<<<<<find same files between qcom and P0...Pn

while read line
do	
	BOARD_DTS=$PRODUCT_NAME/$line
	while read line
	do
		ff=$line
		rm $BOARD_DTS/$ff
		ln -s ../../qcom/$ff $BOARD_DTS/$ff
	done < $LINK_LIST
done < $BOARD_DIRS

del_file $LINK_LIST $LINK_LIST_DTS

while read line
do
	BOARD_DTS=$PRODUCT_NAME/$line
	rm -rf  $BOARD_DTS_OLD
	rm -rf qcom
	while read line
	do
		ff=$line
		if echo "$ff" | grep -q "/"; then 
			ff1=`echo $ff | awk -F '/' '{print $2}'`
        		mv $BOARD_DTS/$ff $PRODUCT_NAME
			ln -s ../../$ff1 $BOARD_DTS/$ff
		else
        		mv $BOARD_DTS/$ff $PRODUCT_NAME
			ln -s ../$ff $BOARD_DTS/$ff
		fi
	done < $LINK_LIST_DTS

done < $BOARD_DIRS

