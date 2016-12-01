#!/bin/bash

if [ "$#" != "3" ] ; then
  echo -e "\n  $0  <list-file>  <out-dir>  <diff-type>"
  echo  "  e.g: $0  change_all.lst  ../out "
  echo  " <list-file>: the file-name that list all file-path need to copy."
  echo  " <out-dir>:   the directory all \"list-file\" will be copied in."
  echo  " <diff-type>: \"dir\" or \"file\"."
  echo  "       \"dir\" mean to copy \"list-file\" directory recursive."
  echo  "       \"file\" mean to copy \"list-file\" only."
   exit 1
fi

ALL_LST_FILES=$1
OUT_DIR=$2
DIF_TYPE=$3

ALL_FILES=`cat $ALL_LST_FILES`
mkdir -p $OUT_DIR

echo "  copy all files in $ALL_LST_FILES to $OUT_DIR/ ..."
if [ "$DIF_TYPE" = "file" ] ; then
   for ff in $ALL_FILES 
   {
     echo "==$ff..."

     dir=`dirname $ff`
     file=`basename $ff`
     mkdir -p $OUT_DIR/$dir

     if [ -e $ff ] ; then
	echo ""
     else
	mkdir -p $dir
     fi
     cp -f $ff $OUT_DIR/$dir
   }
 elif [ "$DIF_TYPE" = "dir" ] ; then

   for ff in $ALL_FILES 
   {
     echo "==$ff..."
     dir=`dirname $ff`
     file=`basename $ff`
     mkdir -p $OUT_DIR/$dir
     cp -rf $dir/*  $OUT_DIR/$dir/ 
   }
 else
   {
     echo "   unknow diffrence type, do nothing!"
   }
fi

