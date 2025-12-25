1.在Linux下用libusb进行usb设备打开时遇到“LIBUSB_ERROR_ACCESS  libusb_open函数返回值为-3”此问题原因为该用户没有权限！
sudo chmod -R 777 /dev/bus/usb/
https://blog.csdn.net/weixin_39979245/article/details/116911510

2.How to install libusb in Ubuntu 
https://stackoverflow.com/questions/4853389/how-to-install-libusb-in-ubuntu
    sudo apt-get install libusb-1.0-0-dev 
    locate libusb.h
    #include <libusb-1.0/libusb.h> (Include the header to your C code)
    gcc -o example example.c -lusb-1.0