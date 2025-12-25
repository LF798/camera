#!/bin/bash

if [ ! -d /config ]; then
        echo "no config dir, create it"
        mkdir /config
fi

mount -t configfs none /config

mkdir /config/usb_gadget/g1
echo 0x1d6b > /config/usb_gadget/g1/idVendor
echo 0x0105 > /config/usb_gadget/g1/idProduct
echo 0x0310 > /config/usb_gadget/g1/bcdDevice
echo 0x0300 > /config/usb_gadget/g1/bcdUSB

mkdir /config/usb_gadget/g1/strings/0x409
echo 0123459876 > /config/usb_gadget/g1/strings/0x409/serialnumber
echo "rockchip" > /config/usb_gadget/g1/strings/0x409/manufacturer
echo "rkusbtest" >  /config/usb_gadget/g1/strings/0x409/product

mkdir /config/usb_gadget/g1/configs/b.1
mkdir /config/usb_gadget/g1/configs/b.1/strings/0x409
echo "test" > /config/usb_gadget/g1/configs/b.1/strings/0x409/configuration
echo 500 > /config/usb_gadget/g1/configs/b.1/MaxPower
echo 0x1 > /config/usb_gadget/g1/os_desc/b_vendor_code
echo "MSFT100" > /config/usb_gadget/g1/os_desc/qw_sign

ln -s /config/usb_gadget/g1/configs/b.1 /config/usb_gadget/g1/os_desc/b.1

# mkdir -p /config/usb_gadget/g1/functions/ffs.test
# ln -s /config/usb_gadget/g1/functions/ffs.test  /config/usb_gadget/g1/configs/b.1/

mkdir -p /dev/usb-ffs/test
mount -o rmode=0770,fmode=0660,uid=1024,gid=1024 -t functionfs test /dev/usb-ffs/test
echo device > /sys/kernel/debug/usb/fc000000.usb/mode

cd /data/
sleep 1
./bin/apsGetdata.exe &
sleep 1
./bin/evsGetdata.exe &
sleep 1
./bin/usb-ffs_sendData.exe /dev/usb-ffs/test/ &

