find_include()
{
#	FILE=`echo $1 | sed 's/\"//g'`
	FILE=$1
	if [ ! -e $FILE ] ; then
	   return 1
    fi	   
	echo $FILE
	ALL=`grep "include" $FILE | awk '{ print $2}'`
	
	for ff in $ALL
	{
		ff1=`echo $ff | sed 's/\"//g'`
		find_include $ff1
	}
    return 1
}

grep "/include/" -l * > ~/temp/a.txt
find_include $1
