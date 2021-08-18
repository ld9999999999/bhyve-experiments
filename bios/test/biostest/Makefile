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

all:    biostest

INT_TESTS=-DINT_TESTS


biostest:	biostest.S
	gcc $(CFLAGS) $(INT_TESTS)  -I . -m16 -c biostest.S -o biostest.o
	cpp -x assembler-with-cpp -std=c99 -P biostest.lds -o biostest.cpp.lds
	if [ -f /usr/local/bin/ld ]; then \
		/usr/local/bin/ld -T biostest.cpp.lds biostest.o -o biostest; \
	else \
		ld -q -T biostest.cpp.lds biostest.o -o biostest; \
	fi
	objcopy --gap-fill=0xff -O binary biostest biostest.bin

clean:
	rm -f *.o *.cpp.lds biostest biostest.bin

objdumptest:
	objdump -mi386 -Maddr16,data16 -D biostest | less

dumpbin:
	objdump -b binary -D -Maddr16,data16 -m i386 biostest.bin | less


