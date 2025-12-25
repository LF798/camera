ins3588()
{
INST=/home/szm/samba/app
BIN=/home/szm/samba/RK3588_Linux_app/bin/rk3588
LIB=/home/szm/samba/RK3588_Linux_app/lib/rk3588
LIB_SDK=/home/szm/samba/RK3588_Linux_app/mod/apx003_mpi_sample/lib
echo "ins3588 to [$INST]"

if [ ! -d $INST ]; then
        echo "No app dir, create it"
        mkdir -p $INST
fi


if [ -d $INST/bin ]; then
        echo "rm exist bin lib"
        rm -rf $INST/bin $INST/lib
fi

mkdir -p $INST/lib $INST/bin ;

# sdk
cp $LIB/libaio.so        $INST/lib -arfv 
cp $LIB/libmpi.so       $INST/lib -arfv 
cp $LIB_SDK/*so*         $INST/lib -arfv 
cp $BIN/*.exe        $INST/bin -arfv 

# cfg
cp /home/szm/samba/RK3588_Linux_app/cfg/usb_config.sh  $INST/ -arfv;
######################
echo "ok."
######################
}


PROJ=`pwd`
echo "PROJ:[$PROJ]"

case $1 in
    3588)
        ins3588
        ;;
    *)
        echo "usage: $0 {3588}"
        exit 1
        ;;
esac