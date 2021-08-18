/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2020 Leon Dang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <assert.h>
#include <errno.h>

#include <machine/vmm.h>
#include <vmmapi.h>

#include "bhyverun.h"
#include "inout.h"
#include "pci_lpc.h"
#include "microbios.h"
#include "vga.h"

static BDA *bda;
static BIOS_VARS *bios_vars;
static bhyve_cmd *guest_cmd;


static microbios_disk *mddisks[32];
static int num_mddisks;
static BDA *bda;

int textcons_init(char *hostname, int port, void *guest_vga_buf);

struct inth_regs {
	uint16_t	eflags;
	uint32_t	eax;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	ebx;
	uint32_t	esp;
	uint32_t	ebp;
	uint32_t	esi;
	uint32_t	edi;
	uint32_t	eip;

	uint16_t	cs;
	uint16_t	ss;
	uint16_t	ds;
	uint16_t	es;
};

static int handle_int13(struct vmctx *ctx, struct inth_regs *regs, int vcpu);
static int handle_int15(struct vmctx *ctx, struct inth_regs *regs, int vcpu);


void
microbios_init(struct vmctx *ctx)
{
	void *vgatxt = paddr_guest2host(ctx, 0xB8000, 128*1024);
	textcons_init("127.0.0.1", 50001, vgatxt);
}

#pragma pack(1)
struct e820_entry {
	uint64_t addr;
	uint64_t size;
	uint32_t type;
};
#pragma pack()

static int e820_entries = 0;
static uint8_t *e820_tbl = NULL;

void
microbios_setup_shared(struct vmctx *ctx)
{
	printf("SETUP SHARED CALLED\r\n");
	bda->number_of_drives = num_mddisks;
	bda->com1 = 0x3f8;
	bda->mem_size = 640;
	bda->text_rows_minus_one = 24;
	bda->text_columns = 80;
	bda->vid_mode = 3;

	// Setup E820 map entries

	uint8_t *e820 = paddr_guest2host(ctx, E820_INFO_BLOCK, 4096);
	/*
 	 * Create 4 entries: XXX add high memory
	 *    0-500        reserved BDA
         *    500-A0000    free
	 *    A0000-100000 reserved - BIOS, ROM
	 *    100000 -     free
	 */
	uint16_t *header = (uint16_t *)e820;
	header[0] = 0; // entries
	header[1] = 8 + 8 + 4;
	uint32_t *entries = (uint32_t *)(e820+4);

	// BIOS area
	header[0]++;
	entries[0] = 0x0;
	entries[1] = 0x0;
	entries[2] = 0x500;
	entries[3] = 0x0;
	entries[4] = 0x2;
	entries += 5;

	header[0]++;
       	entries[0] = 0x500;
	entries[1] = 0x0;
	entries[2] = 0xa0000 - 0x500;
	entries[3] = 0x0;
	entries[4] = 0x1;
	entries += 5;

	// VGA memory
	header[0]++;
       	entries[0] = 0xa0000;
	entries[1] = 0x0;
	entries[2] = 0x100000 - 0xa0000;
	entries[3] = 0x0;
	entries[4] = 0x2;
	entries += 5;

	uint32_t memsize = vm_get_lowmem_size(ctx);
	header[0]++;
       	entries[0] = 0x100000;
	entries[1] = 0x0;
	entries[2] = memsize - 0x100000;
	entries[3] = 0x0;
	entries[4] = 0x1;

	e820_entries = header[0];

	if (e820_tbl == NULL) {
		e820_tbl = (uint8_t *)malloc(e820_entries*20);
		memcpy(e820_tbl, e820+4, e820_entries*20);
	}
}

uint8_t
microbios_get_textpage()
{
	return bda ? bda->disp_page : 0;
}

void
microbios_register_disk(microbios_disk *md)
{
	for (int i = 0; i < 32; i++) {
		if (mddisks[i] == NULL) {
			mddisks[i] = md;
			num_mddisks++;
			break;
		}
	}
}

void
microbios_disk_params(struct vmctx *ctx)
{
	bhyve_disk_params *pcmd = (bhyve_disk_params *)(guest_cmd->args);
	uint32_t diskunit = pcmd->disk & 0x7f;
	if (pcmd->disk < 0x80 || diskunit >= num_mddisks) {
		guest_cmd->results = EINVAL;
		return;
	}

	microbios_disk *disk = mddisks[diskunit];
	struct blockif_ctxt *blkctx = disk->md_getblkif(disk->sc);
	uint64_t sectsz = blockif_sectsz(blkctx);
	uint64_t size = blockif_size(blkctx);
	uint16_t c;
	uint8_t h, s;

	blockif_chs(blkctx, &c, &h, &s);

        pcmd->disk = num_mddisks;
        pcmd->heads = h;
        pcmd->cylinders = c;
        pcmd->sectors = s;
	pcmd->disk_sectors = size / sectsz;
        pcmd->sector_size = sectsz;

	guest_cmd->results = 0;
}

static uint32_t
csum_blocks(uint32_t *addr, uint32_t len)
{
        uint32_t csum = 0x32838211;
	len /= 4;
        for (; len > 0; len--) {
                csum ^= *addr;
                addr++;
        }
	return csum;
}

void
microbios_disk_io_cmd(struct vmctx *ctx, bhyve_disk_io_cmd *iocmd)
{
	if (iocmd->disk < 0x80) {
		guest_cmd->results = 1;
		return;
	}

	microbios_disk *disk = mddisks[iocmd->disk & ~0x80];
	struct blockif_ctxt *blkctx = disk->md_getblkif(disk->sc);
	uint64_t sectsz = blockif_sectsz(blkctx);
	uint64_t size = iocmd->sectors * sectsz;

	if (iocmd->lba == ~0ULL) {
		uint16_t c;
		uint8_t h, s;

		blockif_chs(blkctx, &c, &h, &s);

		iocmd->lba = ((iocmd->cylinder * h + iocmd->head) * s) + iocmd->sector - 1;
		printf("MICROBIOS DISK IO LBA: %lu\r\n", iocmd->lba);
	}

	printf("DISK IO REQUESTED: %s disk %u, CHS %u|%u|%u sectors %u, lba %lx, addr 0x%lx io-size %lu\r\n",
               iocmd->direction ? "WRITE" : "READ", iocmd->disk, iocmd->cylinder, iocmd->head,
               iocmd->sector, iocmd->sectors, iocmd->lba, iocmd->addr, size);

	uint32_t sectors = iocmd->sectors;
	uint64_t lba = iocmd->lba;

	void *gpa = paddr_guest2host(ctx, iocmd->addr, size);

	if (iocmd->direction) {
		if (disk->md_write(disk->sc, lba, gpa, sectors) < 0) {
			printf("DISK WRITE ERROR %d\r\n", errno);
			guest_cmd->results = errno;
			return;
		}
	} else {
		if (disk->md_read(disk->sc, lba, gpa, sectors) <= 0) {
			printf("DISK READ ERROR %d\r\n", errno);
			guest_cmd->results = errno;
			return;
		}
		//printf("DISK CSUM: 0x%x\r\n", csum_blocks(gpa, sectsz));
	}

	if (iocmd->iodelay > 0 && iocmd->iodelay <= 100000)
		usleep(iocmd->iodelay);
	guest_cmd->results = 0;
}

int
microbios_cmd_handler(struct vmctx *ctx)
{
	switch (guest_cmd->command) {
        case BCMD_SETUP:
		printf("(BHYVE) %u BCMD_SETUP\r\n", guest_cmd->seq);
		microbios_setup_shared(ctx);
		guest_cmd->results = 0;
		break;
	case BCMD_DISK_PARAMS:
		microbios_disk_params(ctx);
		break;
	case BCMD_DISK_IO: {
		printf("(BHYVE) DISK_IO requested\r\n");
		assert(num_mddisks > 0);
		bhyve_disk_io_cmd *iocmd = (bhyve_disk_io_cmd *)(guest_cmd->args);
		microbios_disk_io_cmd(ctx, iocmd);
		guest_cmd->results = 0;
		break;
	}
	case BCMD_CHANGE_ISO_EJECT:
		printf("(bhyve) BCMD_CHANGE_ISO_EJECT\r\n");
		guest_cmd->results = 0;
		break;
	case BCMD_PRINTS:
		printf("BCMD-PRINTS: %s\r\n", (char *)guest_cmd->args);
		break;
	case BCMD_VIDEO: {
		bhyve_display_cmd *displaycmd = (bhyve_display_cmd *)(guest_cmd->args);
		if (displaycmd->vidcmd == BVIDCMD_DISPLAY_PAGE) {
			printf("(bhyve) BVMD_VIDEO set page %d\r\n", displaycmd->display_page);
			bda->disp_page = displaycmd->display_page;
		} else if(displaycmd->vidcmd == BVIDCMD_VIDMODE) {
			printf("(bhyve) BCMD_VIDEO set mode %x\r\n", displaycmd->vidmode.mode);
			guest_cmd->results = vga_switchmode(displaycmd->vidmode.mode);
		}
		break;
	}
	case BCMD_DBG_PRINT:
		printf("BCMD-PRINT: %s\r\n", (char *)guest_cmd->args);
		break;
	case BMCD_POWER_OFF:
		printf("(bhyve) BMCD_POWER_OFF\r\n");
		exit(0);
	default:
		printf("(BHYVE) Unknown ROM command: %x\r\n", guest_cmd->command);
		return 1;
	}
	return 0;
}

static uint64_t
GETREG(struct vmctx *ctx, int vcpu, int reg)
{
	uint64_t val;
	int error;

	error = vm_get_register(ctx, vcpu, reg, &val);
	assert(error == 0);
	return (val);
}

static void
SETREG(struct vmctx *ctx, int vcpu, int reg, uint64_t val)
{
	int error;

	error = vm_set_register(ctx, vcpu, reg, val);
	assert(error == 0);
}



#define REG_WORD(x)   ((x) & 0xffff)
#define REG_LOBYTE(x) ((x) & 0xff)
#define REG_HIBYTE(x) (((x) >> 8) & 0xff)

#define EFLAGS_CF 0x0001
#define EFLAGS_ZF 0x0040

#define CLEAR_CF(reg) do { reg &= ~EFLAGS_CF; } while (0)
#define SET_CF(reg)   do { reg |= EFLAGS_CF; } while (0)
#define CLEAR_ZF(reg) do { reg &= ~EFLAGS_ZF; } while (0)
#define SET_ZF(reg)   do { reg |= EFLAGS_ZF; } while (0)

static int
microbios_bios_inth_handler(struct vmctx *ctx, uint32_t *eaxp, int vcpu)
{
	struct inth_regs regs;
	int32_t vec;

	/* EAX has interrupt vector on low word, and AX on high word */

	/* General purpose registers */
	regs.ecx = GETREG(ctx, vcpu, VM_REG_GUEST_RCX);
//	regs.edx = GETREG(ctx, vcpu, VM_REG_GUEST_RDX);
	regs.ebx = GETREG(ctx, vcpu, VM_REG_GUEST_RBX);
	regs.esp = GETREG(ctx, vcpu, VM_REG_GUEST_RSP);
	regs.ebp = GETREG(ctx, vcpu, VM_REG_GUEST_RBP);
	regs.esi = GETREG(ctx, vcpu, VM_REG_GUEST_RSI);
	regs.edi = GETREG(ctx, vcpu, VM_REG_GUEST_RDI);
	regs.eip = GETREG(ctx, vcpu, VM_REG_GUEST_RIP);

	/* EDX was saved to BIOS vars because of being used for outb */
	regs.edx = bios_vars->edx;
	SETREG(ctx, vcpu, VM_REG_GUEST_RDX, regs.edx);

	/* Segment selectors */
	regs.cs = GETREG(ctx, vcpu, VM_REG_GUEST_CS);
	regs.ss = GETREG(ctx, vcpu, VM_REG_GUEST_SS);
	regs.ds = GETREG(ctx, vcpu, VM_REG_GUEST_DS);
	regs.es = GETREG(ctx, vcpu, VM_REG_GUEST_ES);

	/* eflags and eip */
	regs.eflags = bios_vars->flags;

	//printf("Interrupt 0x%x, AX 0x%x\r\n", (eax >> 16) & 0xffff, eax & 0xffff);

	bda->timer_counter++;

	regs.eax = bios_vars->eax;
	vec = (*eaxp >> 16) & 0xffff;
	switch (vec) {
	case 0x13:
		return handle_int13(ctx, &regs, vcpu);
	case 0x15:
		return handle_int15(ctx, &regs, vcpu);
	default:
		printf("UNKNOWN INT%xH\r\n", vec);
		SET_CF(regs.eflags);
		goto eflags_err;
	}

	return 0;

eflags_err:
	//printf("INT%d DONE eax %x, eflags %x\r\n", vec, eax, eflags);
	bios_vars->flags = regs.eflags;
	return -1;
}

static int
handle_int13(struct vmctx *ctx, struct inth_regs *regs, int vcpu) {
	uint32_t is_read = 0;

	//printf("(BHYVE) INT13 HANDLER\r\n");

	switch (REG_HIBYTE(regs->eax)) {
	case 0x00: // RESET
	case 0x01: // LAST STATUS
	case 0x04: // VERIFY DISK
	case 0x05: // FORMAT TRACK
	case 0x10: // DRIVE READY XXX
	case 0x0C: // SEEK
	case 0x16: // DETECT DRIVE CHANGE
	case 0x4b: // TERMINATE CD EMULATION
		regs->eax &= 0xffffff00;
		CLEAR_CF(regs->eflags);
		break;

	case 0x02: // READ
		is_read = 1;
	case 0x03: // WRITE
	{
		uint32_t disknum = REG_LOBYTE(regs->edx);
		uint32_t head = REG_HIBYTE(regs->edx) & 0x3F;
		uint32_t cylinder = REG_HIBYTE(regs->ecx) |
		                    ((REG_LOBYTE(regs->ecx) & 0xC0) << 2);
		uint32_t sector = REG_LOBYTE(regs->ecx) & 0x3F;
		uint32_t sectors = REG_LOBYTE(regs->eax);
		uint64_t addr = ((uint32_t)regs->es << 4) + REG_WORD(regs->ebx);

		void *gpa;
		uint64_t lba;
		uint16_t c;
		uint8_t h, s;

		// floppy support not yet
		if (disknum < 0x80) {
			SET_CF(regs->eflags);
			goto eflags_err;
		}

		microbios_disk *disk = mddisks[disknum & 0x7f];
		struct blockif_ctxt *blkctx = disk->md_getblkif(disk->sc);
		uint64_t sectsz = blockif_sectsz(blkctx);
		uint64_t size;

		blockif_chs(blkctx, &c, &h, &s);
		lba = ((cylinder * h + head) * s) + sector - 1;

#if 0
                printf("(BHYVE) int13 DISKIO disk %u [%s] head %u, cyl %u, sect %u, sectors %u, lba %lx, gpa(%lx):es(%x):bx(%x)\r\n",
			disknum, is_read ? "RD" : "WR",
			head, cylinder, sector, sectors, lba, addr, es, ebx);
#endif

		size = sectors * sectsz;
		gpa = paddr_guest2host(ctx, addr, size);
		if (is_read) {
			if (disk->md_read(disk->sc, lba, gpa, sectors) < 0) {
				printf("DISK READ ERROR %d\r\n", errno);
				SET_CF(regs->eflags);
				goto eflags_err;
			}
		} else {
			if (disk->md_write(disk->sc, lba, gpa, sectors) < 0) {
				printf("DISK WRITE ERROR %d\r\n", errno);
				SET_CF(regs->eflags);
				goto eflags_err;
			}
		}

		regs->eax &= 0xFFFF00FF;
		CLEAR_CF(regs->eflags);
		break;
	}

	case 0x42:
		is_read = 1;
	case 0x43:
	{
		uint64_t addr = (regs->ds << 4) + REG_WORD(regs->esi);
		edd_drive_packet *dp = (edd_drive_packet *)
		                       paddr_guest2host(ctx, addr,
		                                      sizeof(edd_drive_packet));
		if (dp == NULL) {
			regs->eax = (regs->eax & 0xffff00ff) | 0x0700;
			SET_CF(regs->eflags);
			goto eflags_err;
		}

		// floppy support not yet
		if (REG_LOBYTE(regs->edx) < 0x80 || (REG_LOBYTE(regs->edx) & 0x7f) >= num_mddisks) {
			SET_CF(regs->eflags);
			goto eflags_err;
		}

		microbios_disk *disk = mddisks[REG_LOBYTE(regs->edx & 0x7F)];

		uint32_t sectors = dp->blocks;
		uint64_t lba = dp->lba_low | ((uint64_t)dp->lba_high << 32);
		struct blockif_ctxt *blkctx = disk->md_getblkif(disk->sc);
		uint64_t sectsz = blockif_sectsz(blkctx);
		uint64_t size = sectsz * sectors;

		if (dp->struct_size == 16 || (dp->buf_addr != 0xffffffff)) {
			addr = ((dp->buf_addr & 0xFFFF0000) >> 12) + (dp->buf_addr & 0xFFFF);
		} else {
			addr = dp->buf_laddr_low | ((uint64_t)dp->buf_laddr_high << 32);
		}
#if 0
		printf("(BHYVE) DISK-EXTIO [%s] sectors %u, lba %lx, [addr:%lx] (es %x, bx %x, dx %x)\r\n",
			is_read ? "RD" : "WR",
			sectors, lba, addr, regs->es, regs->ebx, regs->edx);
#endif

		void *gpa = paddr_guest2host(ctx, addr, size);

		if (is_read) {
			if (disk->md_read(disk->sc, lba, gpa, sectors) < 0) {
				//printf("DISK READ ERROR %d\r\n", errno);
				SET_CF(regs->eflags);
				goto eflags_err;
			}
		} else {
			if (disk->md_write(disk->sc, lba, gpa, sectors) < 0) {
				//printf("DISK WRITE ERROR %d\r\n", errno);
				SET_CF(regs->eflags);
				goto eflags_err;
			}
		}

		regs->eax &= 0xFFFF00FF;
		CLEAR_CF(regs->eflags);

		break;
	}

	case 0x48:
	case 0x08: // DRIVE PARAMETERS
	{
		// floppy support not yet
		if (REG_LOBYTE(regs->edx) < 0x80) {
			SET_CF(regs->eflags);
			goto eflags_err;
		}

		microbios_disk *disk = mddisks[REG_LOBYTE(regs->edx & 0x7F)];
		struct blockif_ctxt *blkctx = disk->md_getblkif(disk->sc);
		uint64_t sectsz = blockif_sectsz(blkctx);
		uint64_t sectors = blockif_size(blkctx) / sectsz;
		uint16_t c;
		uint8_t h, s;

		blockif_chs(blkctx, &c, &h, &s);

		if (REG_HIBYTE(regs->eax) == 0x08) {
			//printf("DRIVE PARAMS 0x08: CHS: %x|%x|%x\r\n", c, h, s);

			regs->eax &= 0xFFFF0000;
			CLEAR_CF(regs->eflags);

			regs->edx = ((uint32_t)(h-1) << 8) | 0x01;
			SETREG(ctx, vcpu, VM_REG_GUEST_RDX, regs->edx);

			regs->ecx = (((uint32_t)(c-1) << 6) & 0xFFC0) | (s & 0x3F);
			SETREG(ctx, vcpu, VM_REG_GUEST_RCX, regs->ecx);

			regs->ebx &= ~0xFF;
			SETREG(ctx, vcpu, VM_REG_GUEST_RBX, regs->ebx);
		} else {
			edd_drive_params *params = (edd_drive_params *)
			                           paddr_guest2host(ctx,
			                              (regs->ds << 4) + REG_WORD(regs->esi),
			                              sizeof(edd_drive_params));
			//printf("EXT_DRIVE PARAMS, size: %u\r\n", params->struct_size);
			if (params == NULL || params->struct_size < 26) {
				regs->eax &= 0xFFFF00FF;
				SET_CF(regs->eflags);
				goto eflags_err;
			}

			//printf("APPLYING DRIVE EXT PARAMS CHS: %x|%x|%x sectors %lu\r\n", c, h ,s, sectors);

			params->struct_size = 0x1a;
			params->flags = EDD_GEOMETRY_VALID;
			params->heads = h;
			params->cylinders = c;
			params->sectors_per_track = s;
			params->sector_size = sectsz;
			params->sectors_per_drive_low = sectors;
			params->sectors_per_drive_high = sectors >> 32;

			regs->eax &= 0xFFFF00FF;
			CLEAR_CF(regs->eflags);
		}
		break;
	}

	case 0x15: // GET DISK (DASD) TYPE
	{
		uint32_t diskunit = REG_LOBYTE(regs->edx);
		if (diskunit >= 0x80 && ((diskunit & 0x7f) < num_mddisks)) {
			diskunit &= 0x7f;
			microbios_disk *disk = mddisks[diskunit];
			struct blockif_ctxt *blkctx = disk->md_getblkif(disk->sc);
			uint64_t sectsz = blockif_sectsz(blkctx);
			uint64_t sectors = blockif_size(blkctx) / sectsz;

			regs->ecx = (sectors >> 16) & 0xFFFF;
			SETREG(ctx, vcpu, VM_REG_GUEST_RCX, regs->ecx);
			regs->edx = sectors & 0xFFFF;
			SETREG(ctx, vcpu, VM_REG_GUEST_RDX, regs->edx);
			regs->eax &= 0xFFFFFF00;
		} else {
			regs->eax = (regs->eax & 0xffffff00) | 0x03;
		}
		CLEAR_CF(regs->eflags);
		break;
	}

	case 0x41: // CHECK DISK EXTENSIONS PRESENT
		CLEAR_CF(regs->eflags);
		regs->eax = (regs->eax & 0xffff0000) | 0x2100; // EDD 1.1
		regs->ebx = 0xAA55;
		SETREG(ctx, vcpu, VM_REG_GUEST_RBX, regs->ebx);
		regs->ecx = 0x05; // Fixed disk & enhanced drive support
		SETREG(ctx, vcpu, VM_REG_GUEST_RCX, regs->ecx);
		break;

	default:
		printf("UNHANDLED INT 13\r\n");
		SET_CF(regs->eflags);
		goto eflags_err;
	}

	SETREG(ctx, vcpu, VM_REG_GUEST_RAX, regs->eax);

	//printf("INT13h DONE eax %x, eflags %x\r\n", regs->eax, eflags);
	bios_vars->flags = regs->eflags;
	return 0;

eflags_err:
	bios_vars->flags = regs->eflags;
	return -1;
}

static int
handle_int15(struct vmctx *ctx, struct inth_regs *regs, int vcpu) {
	switch (REG_HIBYTE(regs->eax)) {
	case 0x00: // Byte-swapped Return System Configuration Parameters
		regs->eax = 0x8600 | (regs->eax & 0xFFFF00FF);
		SETREG(ctx, vcpu, VM_REG_GUEST_RAX, regs->eax);
		SET_CF(regs->eflags);
		goto eflags_err;
	case 0x24: {
		static int a20_mode = 1;

		switch (REG_LOBYTE(regs->eax)) {
		case 0x00: // disable A20
			printf("*** bhyve: INT15-24h A20=%sable\r\n", (regs->eax & 0xff) ? "dis" : "en");
			a20_mode = 0;
			regs->eax &= 0xffff00ff;
			break;
		case 0x01: // enable A20
			printf("*** bhyve: INT15-24h A20=%sable\r\n", (regs->eax & 0xff) ? "dis" : "en");
			a20_mode = 1;
			regs->eax &= 0xffff00ff;
			break;
		case 0x02: // get a20 gate status
			printf("*** bhyve: INT15-24h 0x2 status = %x\r\n", a20_mode);
			regs->eax = (regs->eax & 0xffff0000) | a20_mode;
			break;
		case 0x03: // query a20 gate support
			printf("*** bhyve: INT15-24h 0x3\r\n");
			regs->eax = (regs->eax & 0xffff0000);
			regs->ebx = (regs->ebx & 0xffff0000) | 0x03;
			SETREG(ctx, vcpu, VM_REG_GUEST_RBX, regs->ebx);
			break;
		}
		CLEAR_CF(regs->eflags);
		SETREG(ctx, vcpu, VM_REG_GUEST_RAX, regs->eax);
		break;
	}
	case 0x41:
		SET_CF(regs->eflags);
		break;
	case 0x53: // Advanced Power Management
		switch (REG_LOBYTE(regs->eax)) {
                case 0x00: // Advanced Power Management Installation Check
			// APM not present
			SET_CF(regs->eflags);
			regs->eax = 0x8600 | (regs->eax & 0xFFFF00FF);
			break;
		case 0x04: // Advanced Power Management Interface Disconnect
			// Interface not connected
			SET_CF(regs->eflags);
			regs->eax = (regs->eax & 0xFFFF0000) | 0x03;
			break;
		}
		break;
        case 0x87: { // Move block of memory to high memory (MSDOS needed)
		uint64_t gdtaddr = (regs->es << 4) + (regs->esi & 0xFFFF);
		int15_gdt *gdt, *srcgdt, *dstgdt;
		uint8_t *srcp, *dstp;

                /* cx: size, es:si: GDT */
		gdt = (int15_gdt *)paddr_guest2host(ctx, gdtaddr, sizeof(*gdt) * 6);
                srcgdt = gdt + 2;
                dstgdt = gdt + 3;
		srcp = (uint8_t *)paddr_guest2host(ctx, srcgdt->paddr & 0xffffff, regs->ecx & 0xffff);
		dstp = (uint8_t *)paddr_guest2host(ctx, dstgdt->paddr & 0xffffff, regs->ecx & 0xffff);

                printf("(BHYVE) memcpy src_gdt %lx [seg %x paddr %x] dst_gdt %lx [seg %x paddr %x]\r\n",
                       gdtaddr + sizeof(*gdt)*2, srcgdt->seg, srcgdt->paddr & 0xffffff,
		       gdtaddr + sizeof(*gdt)*3, dstgdt->seg, dstgdt->paddr & 0xffffff);
		memcpy(dstp, srcp, regs->ecx & 0xffff);

		regs->eax &= 0xffff00ff;
		CLEAR_CF(regs->eflags);
                break;
	}
        case 0x88: // Get Extended Memory Size
                /*
                 * Report 8mb contiguous starting at 1024k as available which
                 * isn't quite true.
                 */
		CLEAR_CF(regs->eflags);
		
		regs->eax = 8 * 1024;
                break;
        case 0x8a: // Get Extended memory Size in DX:AX in KB above 1024k
	{
		uint32_t extmem = vm_get_lowmem_size(ctx) - (1024*1024);

                regs->eax = extmem & 0xffff;
                regs->edx = (extmem >> 16) & 0xffff;
                SET_CF(regs->eflags);
                break;
	}
        case 0xc0: { // Return System Configuration Parameters
		CLEAR_CF(regs->eflags);
		regs->eax &= 0xff;

		regs->es = 0xF000;
		regs->ebx = bios_vars->bios_config_tbl_offset;

		SETREG(ctx, vcpu, VM_REG_GUEST_ES, regs->es);
		SETREG(ctx, vcpu, VM_REG_GUEST_RBX, regs->ebx);

		printf("INT15-C0: bios_config_tbl: 0x%x\r\n", regs->ebx);
                break;
        }
	case 0xe8: {
		uint32_t continuation;
		uint64_t gbufaddr;
		void *bp;
		struct e820_entry *ee;

		if (REG_LOBYTE(regs->eax) == 0x01) {
			uint32_t memsize = vm_get_lowmem_size(ctx);
			regs->eax = 0x3c00;
			regs->ecx = (regs->ecx & 0xffff0000) | 0x3c00;
			regs->ebx = (regs->ebx & 0xffff0000) | (((memsize - 16) * 1024 / 64) & 0xffff);
			regs->edx = (regs->edx & 0xffff0000) | (regs->ebx & 0xffff);
			CLEAR_CF(regs->eflags);

			SETREG(ctx, vcpu, VM_REG_GUEST_RBX, regs->ebx);
			SETREG(ctx, vcpu, VM_REG_GUEST_RCX, regs->ecx);
			SETREG(ctx, vcpu, VM_REG_GUEST_RDX, regs->edx);
			break;
		} else if (REG_LOBYTE(regs->eax) != 0x20 ||        // only support e820 here
		           regs->edx != 0x534D4150 ||              // missing signature
		           (regs->ecx & 0xffff) < sizeof(*ee) ||   // len of buffer too small
		           (regs->ebx & 0xffff) >= e820_entries) { // continuation invalid

			printf("(BHYVE) int15-e820 (invalid eax 0x%x) sig 0x%x, buflen %x, cont %u\r\n",
			       regs->eax, regs->edx, regs->ecx, regs->ebx);
			regs->eax = (regs->eax & 0xffff0000);
			SET_CF(regs->eflags);
			break;
		}

		gbufaddr = ((regs->es & 0xffff) << 4) + (regs->edi & 0xffff);

		printf("(BHYVE) INT15-e820 (buf 0x%lx) sig 0x%x, buflen %x, cont %u\r\n",
		       gbufaddr, regs->edx, regs->ecx, regs->ebx);

		continuation = regs->ebx & 0xffff;
		bp = paddr_guest2host(ctx, gbufaddr, sizeof(*ee));
		ee = (struct e820_entry *)(e820_tbl + (sizeof(*ee) * continuation));
		memcpy(bp, ee, 20);
		printf("     >>> address 0x%lx, size 0x%lx, type 0x%x\r\n", ee->addr, ee->size, ee->type);

		CLEAR_CF(regs->eflags);
		regs->ebx = (continuation + 1) % e820_entries;
		regs->eax = 0x534D4150;
		regs->ecx = sizeof(*ee);
		regs->edx = 0;
		SETREG(ctx, vcpu, VM_REG_GUEST_RBX, regs->ebx);
		SETREG(ctx, vcpu, VM_REG_GUEST_RCX, regs->ecx);
		SETREG(ctx, vcpu, VM_REG_GUEST_RDX, regs->edx);

		break;
	}
	case 0xec:
		if (REG_LOBYTE(regs->eax) == 0 && REG_LOBYTE(regs->ebx) <= 3) {
			// Detect target operating mode
			CLEAR_CF(regs->eflags);
		} else {
			SET_CF(regs->eflags);
		}
		regs->eax &= 0xffff00ff;
		break;
	case 0x86: {
		// sleep for CX:DX microseconds
		uint32_t usecs = (regs->ecx << 16) + regs->edx;
		if (usecs > 2000000)
			usecs = 2000000;
		if (usecs > 50000)
			usleep(usecs);
		regs->eax &= 0xffffff00;
		CLEAR_CF(regs->eflags);
		break;
	}
	default:
		printf("Unhandled INT15 %x\r\n", regs->eax);
		SET_CF(regs->eflags);
	}
	SETREG(ctx, vcpu, VM_REG_GUEST_RAX, regs->eax);
	bios_vars->flags = regs->eflags;
	return 0;

eflags_err:
	bios_vars->flags = regs->eflags;
	return -1;
}

static int
microbios_io_handler(struct vmctx *ctx, int vcpu, int in, int port, int bytes,
		  uint32_t *eax, void *arg)
{
	uint8 c;
	assert(in == 0);
	c = *eax;

	if (!bda) {
		bda = paddr_guest2host(ctx, BIOS_DATA_AREA, sizeof(BDA));
		bios_vars = (BIOS_VARS *)paddr_guest2host(ctx, BIOS_VARS_ADDR, sizeof(BIOS_VARS));
		guest_cmd = (bhyve_cmd *)((uint8 *)bios_vars + (BIOS_CMDS_ADDR-BIOS_VARS_ADDR));
	}

	if (bytes == 4) {
		// BIOS INTH handler hypercall
		microbios_bios_inth_handler(ctx, eax, vcpu);
		return (0);
	} else {
		// Handle bhyve command
		microbios_cmd_handler(ctx);
	}

	*eax = 0xff;
	return (0);
}


INOUT_PORT(microbios, BIOS_IO_PORT, IOPORT_F_OUT, microbios_io_handler);
SYSRES_IO(BIOS_IO_PORT, 1);

