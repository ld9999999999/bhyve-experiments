#!/bin/sh

BHYVE=../bhyve/bhyve
VM=biosvm
ROM=$PWD/../microboot/microboot.bin

DISK=$PWD/freebsd-mini.img
#DISK=$PWD/MS-DOS-flat.vmdk

bhyvectl --destroy --vm=$VM

# to enable debugging single step:
# sudo bhyvectl --capname=mtrap_exit --setcap=1 --vm=biosvm
#GDBPORT="-G w1235"

MEM=256M

stty erase 

ISISO=$(echo $DISK | grep '.iso$')
if [ -z "$ISISO" ]; then
	SECTSZ=512
else
	SECTSZ=2048
fi

echo $BHYVE \
-C -A -Y -c 1 -m $MEM -s 1,ahci-hd,$DISK \
-o smbios_base=0xE1000 -o acpi_base=0xE2400 \
-s 31,lpc -l com1,stdio -l bootrom,$ROM -H -P -w \
-e $GDBPORT $VM

#WAIT=,wait
#HALTPAUSEEXIT="-H -P"

$BHYVE \
-C -A -Y -c 1 -m $MEM -s 1,ahci-hd,$DISK \
-s 2,fbuf,tcp=0.0.0.0:5901,w=640,h=400${WAIT} \
-o smbios_base=0xE1000 -o acpi_base=0xE2400 \
-V $PWD/cp437-8x16.psf \
-s 31,lpc -l com1,stdio -l bootrom,$ROM $HALTPAUSEEXIT -w \
$GDBPORT $VM

#bhyvectl --destroy --vm=$VM

echo
echo
echo
echo
echo
echo
echo
echo
echo
echo
echo
echo
reset; stty erase 
