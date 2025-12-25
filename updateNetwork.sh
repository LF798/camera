#!/bin/sh

if [ $# -ne 2 ]; then
	echo "ERROR - usage: ./`basename $0` dir IPaddr"
	exit 1
fi

NODE_TYPE=$1
IP=$2
WORK_DIR=/data/bin

if [ $? -eq 0 ]; then
	SSH_OPT="sshpass -p 0"
else
	echo "sshpass NOT installed !!!" 
	exit -1
fi
$SSH_OPT ssh root@$IP "killall usb-ffs_sendData.exe apsGetdata.exe evsGetdata.exe ; cd /data/bin ;rm *.exe;" 
$SSH_OPT scp -r ${NODE_TYPE}/* root@$IP:$WORK_DIR/
$SSH_OPT ssh root@$IP "cd /data/bin ;chmod +x ./*;sync" 
# $SSH_OPT rsync -avP --delete --inplace ${NODE_TYPE}/ root@$IP:$WORK_DIR/

$SSH_OPT ssh root@$IP "chmod -R 777 $WORK_DIR; /sbin/reboot"
echo "done, rebooting..."