/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2020 Leon Dang.
 *
 * 16-bit BIOS for bhyve
 */
#ifndef __MICROBOOT_H__
#define __MICROBOOT_H__

#define COM1              0x3F8
#define BIOS_DATA_AREA    0x400

#define BHYVE_IO_PORT     0x100

#define SEG_BIOS          0xF000
#define SEG_BIOS_ADDR     (SEG_BIOS << 4)

/*
 * All BIOS ROM memory is from 0xF5000 to avoid collision with default
 * bhyve smbios, acpi, mptable etc locations.
 *
 * Layout:
 *     0xF0000 - rom image
 *     0xF5000 - variables shared with bhyve
 *     0xF5500 - e820 table
 *     0xF6000 - bhyve BIOS commands
 *     0xF7000 - ROM code
 */
#define BIOS_VARS_SEG     0xF500
#define BIOS_VARS_ADDR    0xF5000
#define E820_INFO_BLOCK   0xF5500

/* Start of ROM code offset in SEG_BIOS */
#define BIOS_CODE_START   0x7000

#define BHYVE_CMD_BUF_SEG      0xF600
#define BHYVE_CMD_BUF          (BHYVE_CMD_BUF_SEG << 4)

/*
 * struct bhyve_cmd offsets and commands
 */
#define BHYVE_CMD_COMMAND_OFF  2
#define BHYVE_CMD_BUF_ARGS     (BHYVE_CMD_BUF+BHYVE_CMD_BUF_ARGS_OFF)
#define BHYVE_CMD_BUF_ARGS_OFF 8
#define BHYVE_CMD_BUF_ARGS     (BHYVE_CMD_BUF+BHYVE_CMD_BUF_ARGS_OFF)
#define BHYVE_CMD_BUF_RES_OFF  4
#define BHYVE_CMD_BUF_RES      (BHYVE_CMD_BUF+BHYVE_CMD_BUF_RES_OFF)
#define BHYVE_CMD_BUF_DATA_OFF 8
#define BHYVE_CMD_BUF_DATA     (BHYVE_CMD_BUF+BHYVE_CMD_BUF_DATA_OFF)

#define BCMD_SETUP            0x01
#define BCMD_DISK_PARAMS      0x02
#define BCMD_DISK_IO          0x03
#define BCMD_CHANGE_ISO_EJECT 0x04
#define BCMD_PRINTS           0x05
#define BCMD_VIDEO            0x06
#define BCMD_DBG_PRINT        0xfe
#define BMCD_POWER_OFF        0xff


// Offsets into BIOS_VARS
#define BHYVE_VARS_CFG_TBL    0
#define BHYVE_VARS_FLAGS      2
#define BHYVE_VARS_EAX        4
#define BHYVE_VARS_EDX        8
#define BHYVE_VARS_ESP        12
#define BHYVE_VARS_SS         16
#define BHYVE_VARS_DS         18
#define BHYVE_VARS_ES         20
#define BHYVE_VARS_GDTR_LIM   22
#define BHYVE_VARS_GDTR_BASE  24
#define BHYVE_VARS_GDT_COPY   28


#ifndef __ASM__

typedef unsigned int   uint32;
typedef unsigned short uint16;
typedef unsigned char  uint8;


#include "bios.h"

#pragma pack(1)
typedef struct bios_vars {
	uint16  bios_config_tbl_offset;       // 0
	uint16  flags;                        // 2
	uint32  eax;                          // 4
	uint32  edx;                          // 8

	// Saved GDTR and segment registers
	uint32  saved_esp;                    // 12
	uint16  saved_ss;                     // 16
	uint16  saved_ds;                     // 18
	uint16  saved_es;                     // 20
	uint16  gdtr_limit;                   // 22
	uint32  gdtr_base;                    // 24
} bios_vars;

struct bhyve_cmd {
	uint16 seq;
	uint16 command;           // command to issue
	uint32 results;           // results/error from processing command
	union {
		uint8  args[0];   // data for the issuing the command
		uint8  data[0];   // data from bhyve
	};
};
typedef struct bhyve_cmd bhyve_cmd;

/* Command structure for disk read and writes */
typedef struct {
	uint32 direction;   // 0 = read, 1 = write
	uint32 disk;	    // which BIOS disk to read from (>= 0x80: HDD)
	uint32 head;
	uint32 cylinder;
	uint32 sector;
	uint32 sectors;     // blocks to read
	uint32 lba_low;     // used if H,C,S == 0
	uint32 lba_high;
	uint32 addr_low;    // physical address of data source/dest (low)
	uint32 addr_high;   // physical address to data source/dest (high)
	uint32 io_delay_us; // nunber of useconds to delay io completion
} bhyve_disk_io_cmd;

/* Command structure for disk parameters */
typedef struct {
	uint32 disk;
	uint32 heads;
	uint32 cylinders;
	uint32 sectors;           // sectors per track
	uint32 disk_sectors_low;
	uint32 disk_sectors_high;
	uint32 sector_size;
} bhyve_disk_params;

/* Command structure for system information */
struct bhyve_sysinfo {
	uint32 total_mem;       // in kB
	uint32 total_mem_high;  // in kB
	uint32 num_disks;       // same value stored in bda->number_of_drives
	uint32 disks_addr;      // 32bit pointer to disks array, sorted by boot-order
	uint32 gop_fb;          // address of video framebuffer
};

#define BHYVE_DISKTYPE_FLOPPY 0
#define BHYVE_DISKTYPE_ISO    1
#define BHYVE_DISKTYPE_FIXED  2
struct bhyve_sysinfo_disks {
	uint32 disk_type;
	uint32 heads;
	uint32 cylinders;
	uint32 sectors_per_track;
	uint32 sectors_low;
	uint32 sectors_high;
	uint32 block_size;
	uint32 total_size;
	uint32 total_size_high;
};

/* Command structure for display */
#define BVIDCMD_VIDMODE      0x01
#define BVIDCMD_DISPLAY_PAGE 0x02
#define BVIDCMD_WRITE_CHAR   0x03
#define BVIDCMD_SET_PALETTE  0x04
#define BVIDCMD_SET_PALETTE  0x04
#define BVIDCMD_VESA         0x05

typedef struct {
	uint32 vidcmd; // sub-command code (same codes as int10h)
	union {
		uint32 display_page; // BVIDCMD_DISPLAY_PAGE

		struct { // BVIDCMD_VIDMODE
			uint8 mode;
			uint8 plane; // graphics plane, or text page
			uint8 rows;
			uint8 columns;
		} vidmode;

		struct { // BVIDCMD_WRITE_CHAR
			uint8  row;
			uint8  col;
			uint8  ch;
			uint8  attrib;
			uint16 repeat;
		} write_char;

		struct { // BVIDCMD_SET_PALETTE
			uint32 paddr;
			uint32 len;
			uint16 vgareg;
		} set_palette;

		struct { // BVIDCMD_VESA
			
		} vesa;
	};
} bhyve_display_cmd;


/*
 * CPU Registers
 */

/* 32-bit EFLAGS */
typedef struct flags_reg {
	uint16     CF:1;
	uint16     rsvd1:1;
	uint16     PF:1;
	uint16     rsvd2:1;
	uint16     AF:1;
	uint16     rsvd3:1;
	uint16     ZF:1;
	uint16     SF:1;
	uint16     TF:1;
	uint16     IF:1;
	uint16     DF:1;
	uint16     OF:1;
	uint16     IOPL:2;
	uint16     NT:1;
	uint16     rsvd4:1;
} flags_reg;

typedef union flags {
	flags_reg fb;  // bit fields
	uint32    fu;  // int version
} flags_uint32;
#pragma pack()


/*
 * Macros to define 32-bit registers and their subtypes
 * e.g. REG32_DEF(_EAX,a) -> union _EAX { ax, ah, al, eax }
 */
#define REGNAME(a,b)  a##b
#define EREGNAME(b,c) e##b##c
#define REG32_DEF(t,n) union t {   \
	struct {                       \
		uint16 REGNAME(n,x); \
		uint16 _rsvd1;       \
	};                       \
	struct {                 \
		uint8  REGNAME(n,l), REGNAME(n,h); \
		uint16 _rsvd2;       \
	};                       \
	uint32 EREGNAME(n,x);    \
}

/*
 * Macro to define registers with only 32 and 16 bit versions
 */
#define EREG32_DEF(t,n,sfx) union t { \
	struct {                   \
		uint16 REGNAME(n,sfx); \
		uint16 _rsvd1;         \
	};                         \
	uint32 EREGNAME(n,sfx);    \
}

/* Define CPU registers */
REG32_DEF(_EAX,a);
REG32_DEF(_EBX,b);
REG32_DEF(_ECX,c);
REG32_DEF(_EDX,d);

EREG32_DEF(_ESI,s,i);
EREG32_DEF(_EDI,d,i);
EREG32_DEF(_EBP,b,p);
EREG32_DEF(_ESP,s,p);

/*
 * Interrupt handler call stack register-set.
 */
#pragma pack(1)
typedef struct callregs {
	uint16 ss;
	uint32 esp;

	uint16 ds;
	uint16 es;

	union _EDI _edi;
	union _ESI _esi;
	union _EBP _ebp;
	union _ESP _esp;
	union _EBX _ebx;
	union _EDX _edx;
	union _ECX _ecx;
	union _EAX _eax;

	uint16 intcode;
	uint16 ip;
	uint16 cs;
	flags_reg flags;
} callregs;
#pragma pack()


/*
 * Various helpers
 */
static inline void
outb(uint16 port, uint8 val) {
	__asm__ __volatile__("outb %b0, %w1" :: "a"(val), "Nd"(port));
}

static inline uint8
inb(uint16 port) {
	uint8 v;
	__asm__ __volatile__("inb %w1, %b0" : "=a"(v) : "Nd"(port));
	return v;
}

static inline uint32
get_reg_ss() {
	uint16 v;
	asm ("mov %%ss, %%ax; mov %%ax, %0" : "=r"(v) :: "%eax");
	return v;
}

static inline uint32
get_reg_cs() {
	uint16 v;
	asm ("mov %%cs, %%ax; mov %%ax, %0" : "=r"(v) :: "%eax");
	return v;
}

static inline uint32
get_reg_ds() {
	uint16 v;
	asm ("mov %%ds, %%ax; mov %%ax, %0" : "=r"(v) :: "%eax");
	return v;
}


/*
 * gcc references constants with %ds (which is 0x00) so need
 * to let it know to use the offset to ROM base to get those consts.
 */
#define GLOBAL_PTR(x) (void *)((char *)(x) + SEG_BIOS_ADDR)

#define DEBUG(x) printf(GLOBAL_PTR(x))

/* Read a value at address */
static inline uint8
read_u8(uint32 address) {
	uint8 v;
	asm volatile ("pushw %%ds\n"
	              "pushw $0\n"
	              "popw %%ds\n"
	              "movb %%ds:(%1), %0\n"
	              "popw %%ds"
	              : "=r"(v) : "r"(address));
	return v;
}

static inline uint16
read_u16(uint32 address) {
	uint16 v;
	asm volatile ("pushw %%ds\n"
	              "pushw $0\n"
	              "popw %%ds\n"
	              "movw %%ds:(%1), %0\n"
	              "popw %%ds"
	              : "=r"(v) : "r"(address));
	return v;
}

static inline uint32
read_u32(uint32 address) {
	uint32 v;
	asm volatile ("pushw %%ds\n"
	              "pushw $0\n"
	              "popw %%ds\n"
	              "movl %%es:(%1), %0\n"
	              "popw %%ds"
	              : "=r"(v) : "r"(address));
	return v;
}



void printf(char *fmt, ...);

void handle_int10(callregs *regs);
void handle_int13(callregs *regs);
void handle_int15(callregs *regs);
void handle_int1a(callregs *regs);

void *addrptr(uint32 addr);
void *bdaptr(uint32 addr);
void memxfer(void *from, void *to, uint32 len);
uint32 memget(uint32 addr);
void memset(uint32 addr, uint32 val, uint32 bits);

#endif /* ! __ASM__ */

#endif
