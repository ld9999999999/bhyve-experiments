#
# Build microboot
# Requires gcc tools
#

OPT=-Os

CFLAGS = -march=i386 -fno-stack-protector -fstack-check=no \
 -minline-all-stringops \
 -fomit-frame-pointer -freg-struct-return -ffreestanding \
 -fno-delete-null-pointer-checks -ffunction-sections -fdata-sections \
 -fno-merge-constants -Wno-address-of-packed-member -fno-defer-pop \
 -nostdinc \
 -fno-jump-tables -fno-tree-switch-conversion --param large-stack-frame=48 $(OPT)

LD = /usr/local/bin/ld
LDPPFLAGS = -include . 

all:	micro

INT_TESTS=-DINT_TESTS


micro:
	gcc $(CFLAGS) $(INT_TESTS)  -I . -m16 -c start16.S -o start16.o
	gcc $(CFLAGS) -m16 -I . -c microboot.c
	gcc $(CFLAGS) -m16 -c printf.c
	cpp -x assembler-with-cpp -std=c99 -P microboot.lds -o microboot.cpp.lds
	if [ -f /usr/local/bin/ld ]; then \
		/usr/local/bin/ld -T microboot.cpp.lds start16.o microboot.o printf.o -o microboot; \
	else \
		ld -q -T microboot.cpp.lds start16.o microboot.o printf.o -o microboot; \
	fi
	objcopy --gap-fill=0xff -O binary microboot microboot.bin

objdumptest:
	objdump -mi386 -Maddr16,data16 -D microboot | less

dumpbin:
	objdump -b binary -D -Maddr16,data16 -m i386 microboot.bin | less

clean:
	rm -f *.o *.cpp.lds microboot.S microboot.bin microboot
