#!/bin/sh

filesdir=$1 # path to a directory on the filesystem
searchstr=$2 # text string which will be searched

# check parameters
if [ $# -lt 2 ]
then
	echo "Fail --> need to put 2 parameters"
	exit 1
else
	if ! [ -d $filesdir ]
	then
		echo "Invalid directory path"
		exit 1
	else
		if [ -z $searchstr ]
		then
			echo "Inalid search string"
			exit 1
		fi
	fi
fi 

# count number of files in director
number_of_files=$(find $filesdir -type f | wc -l)

# count matching lines found in respective files
number_of_matching=$(grep -Rw $filesdir -e $searchstr | wc -l)

echo "The number of files are $number_of_files and the number of matching lines are $number_of_matching"