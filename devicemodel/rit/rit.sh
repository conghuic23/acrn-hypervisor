#!/bin/bash

# offline SOS CPUs except BSP before launch UOS
for i in `ls -d /sys/devices/system/cpu/cpu[1-99]`; do
        online=`cat $i/online`
        idx=`echo $i | tr -cd "[1-99]"`
        echo cpu$idx online=$online
        if [ "$online" = "1" ]; then
                echo 0 > $i/online
                echo $idx > /sys/class/vhm/acrn_vhm/offline_cpu
        fi
done


if [ "$1" == "" ]; then
	DIR=/usr/share/acrn/rit
else
	DIR=${1}
	if [ -d /usr/share/acrn/rit ]; then
		cp /usr/share/acrn/rit/* ${DIR}
	fi
fi

cd ${DIR}

RESULT=$?

if [ $RESULT != 0 ]; then
	echo "dir $DIR not exist"
	exit
fi

python3 rit_parse_config.py

rm rit.*.out

acrn-dm -A -m 4096M -c 1 -s 0:0,hostbridge -s 1:0,lpc -l com1,stdio \
  --rit ./rit.bin rit
