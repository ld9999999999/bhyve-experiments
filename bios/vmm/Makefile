# $FreeBSD: head/sys/modules/vmm/Makefile 360648 2020-05-05 00:02:04Z jhb $

.include </usr/src/sys/conf/kmod.opts.mk>

KMOD=	vmm

SRCS=	opt_acpi.h opt_bhyve_snapshot.h opt_ddb.h
SRCS+=	device_if.h bus_if.h pci_if.h pcib_if.h acpi_if.h vnode_if.h
DPSRCS+=	vmx_assym.h svm_assym.h
DPSRCS+=	vmx_genassym.c svm_genassym.c offset.inc

CFLAGS+= -DVMM_KEEP_STATS
CFLAGS+= -I.
CFLAGS+= -I./io
CFLAGS+= -I./intel
CFLAGS+= -I./amd

# generic vmm support
.PATH: .
SRCS+=	vmm.c		\
	vmm_dev.c	\
	vmm_host.c	\
	vmm_instruction_emul.c	\
	vmm_ioport.c	\
	vmm_lapic.c	\
	vmm_mem.c	\
	vmm_stat.c	\
	vmm_util.c	\
	x86.c

.PATH: ./io
SRCS+=	iommu.c		\
	ppt.c           \
	vatpic.c	\
	vatpit.c	\
	vhpet.c		\
	vioapic.c	\
	vlapic.c	\
	vpmtmr.c	\
	vrtc.c

# intel-specific files
.PATH: ./intel
SRCS+=	ept.c		\
	vmcs.c		\
	vmx_msr.c	\
	vmx_support.S	\
	vmx.c		\
	vtd.c

# amd-specific files
.PATH: ./amd
SRCS+=	vmcb.c		\
	svm.c		\
	svm_support.S	\
	npt.c		\
	ivrs_drv.c	\
	amdvi_hw.c	\
	svm_msr.c

CLEANFILES=	vmx_assym.h vmx_genassym.o svm_assym.h svm_genassym.o

OBJS_DEPEND_GUESS.vmx_support.o+= vmx_assym.h
OBJS_DEPEND_GUESS.svm_support.o+= svm_assym.h

vmx_assym.h:    vmx_genassym.o
	sh ${SYSDIR}/kern/genassym.sh vmx_genassym.o > ${.TARGET}

svm_assym.h:    svm_genassym.o
	sh ${SYSDIR}/kern/genassym.sh svm_genassym.o > ${.TARGET}

vmx_support.o:
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS} \
	    ${.IMPSRC} -o ${.TARGET}

svm_support.o:
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS} \
	    ${.IMPSRC} -o ${.TARGET}

vmx_genassym.o: offset.inc
	${CC} -c ${CFLAGS:N-flto:N-fno-common} -fcommon ${.IMPSRC}

svm_genassym.o: offset.inc
	${CC} -c ${CFLAGS:N-flto:N-fno-common} -fcommon ${.IMPSRC}

.include </usr/src/share/mk/bsd.kmod.mk>
