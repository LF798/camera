## 代码结构
```shell
./
├── bin    #app
├── build  #toolchain
├── cfg    #cfg
├── common #fw
├── inc    #top include
├── ins.sh #tool shell
├── lib    #soc lib
├── Makefile
├── mod
│   ├── apx003_mpi_sample    #get data from mpi
│   ├── apx003_v4l2_sample   #get data from v4l2 
│   ├── mpp                  #rk libmpp
│   ├── usb_app              #put data to usb endpoint
│   ├── usb_device           #put data to usb endpoint sample
│   └── usb_host             #host get data from endpoint sample
├── README.md
└── updateNetwork.sh

# 网线传输相关代码在mod/apx003_v4l2_sample/src目录下
```