#!/bin/sh

writefile=$1 # full path to a file (including filename) on the filesystem
writestr=$2 # text string which will be written within the file

# check parameters
if [ $# -lt 2 ];
then
	echo "Fail --> need to put 2 parameters"
	exit 1
fi 

# remove existing file
rm -rf $writefisle

# check and create directory
dirname=$(dirname $writefile)
if ! [ -d $dirname ];
then
	# echo "Directory not exist --> create new one"
	mkdir -p $dirname
fi

# create file and write string
touch $writefile
echo $writestr > $writefile

# check if file is created
if ! [ -e $writefile ];
then
	echo "Failed --> file is not created"
	exit 1
fi
