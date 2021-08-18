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

#ifndef __MICROBIOS_H_
#define __MICROBIOS_H_

#include "block_if.h"

#define BIOS_IO_PORT     0x100

#define BIOS_DATA_AREA   0x400

#define BIOS_VARS_ADDR   0xF5000
#define E820_INFO_BLOCK  0xF5500
#define BIOS_CMDS_ADDR   0xF6000

#define uint8  uint8_t
#define uint16 uint16_t
#define uint32 uint32_t
#define uint64 uint64_t

#pragma pack(1)
typedef struct {
        uint16  com1;             // 0x00
        uint16  com2;             // 0x02
        uint16  com3;             // 0x04
        uint16  com4;             // 0x06
        uint16  lpt1;             // 0x08
        uint16  lpt2;             // 0x0A
        uint16  lpt3;             // 0x0C
        uint16  ebda;             // 0x0E
        uint16  machine_config;   // 0x10
        uint8   bda12;            // 0x12
        uint16  mem_size;         // 0x13
        uint8   bda15_16[0x02];   // 0x15
        uint8   keyboard_status1; // 0x17
        uint8   keyboard_status2; // 0x18
        uint8   bda19;            // 0x19
        uint16  key_buffer_head;  // 0x1A
        uint16  key_buffer_tail;  // 0x1C
        uint16  key_buffer[0x10]; // 0x1E
        uint16  floppy_data;      // 0x3E
        uint8   floppy_timeout;   // 0x40
        uint8   bda41_48[0x08];   // 0x41
        uint8   vid_mode;         // 0x49
        uint16  text_columns;     // 0x4A
        uint16  vid_page_size;    // 0x4C
        uint16  vid_page_offset;  // 0x4E
        uint16  cursor_position[8]; // 0x50 cursor positions for text pages; list of uint16(x, y)
	uint8   cursor_end;       // 0x60
	uint8   cursor_start;     // 0x61
        uint8   disp_page;        // 0x62 current page displayed
        uint8   bda63_6B[0x9];    // 0x63
        uint32  timer_counter;
	uint8   timer_rollover;
        uint8   bda71_74[0x4];
        uint8   number_of_drives;
        uint8   bda76_77[0x02];
        uint16  lpt1_2timeout;
        uint16  lpt3_4timeout;
        uint16  com1_2timeout;
        uint16  com3_4timeout;
        uint16  key_buffer_start;
        uint16  key_buffer_end;
        uint8   text_rows_minus_one;
        uint16  scan_lines_per_char;
        uint8   video_mode_options;
        uint8   video_feature_bits_switches;
        uint8   video_display_data_area;
        uint8   video_dcc;
        uint8   bda8B_8F[0x05];
        uint8   floppy_xrate;
        uint8   bda91_95[0x05];
        uint8   keyboard_status3;
        uint8   keyboard_status4;
} BDA;

typedef struct {
	uint16  bios_config_tbl_offset;
	uint16  flags;
	uint32  eax;
	uint32  edx;
	uint32  esp;
	uint16  ss;
	uint16  ds;
	uint16  es;
	uint16  gdtr_limit;
	uint32  gdtr_base;
	uint64  gdtr_copy;
} BIOS_VARS;


enum bhyve_cmds {
        BCMD_SETUP = 0x01,
        BCMD_DISK_PARAMS,
        BCMD_DISK_IO,
        BCMD_CHANGE_ISO_EJECT,
        BCMD_PRINTS,
        BCMD_VIDEO,
        BCMD_DBG_PRINT = 0xfe,
        BMCD_POWER_OFF = 0xff
};

struct bhyve_cmd {
        uint16 seq;
        uint16 command;       // command to issue
        uint32 results;       // results/error from processing command
        union {
                uint8  args[0];   // data for the issuing the command
                uint8  data[0];   // data from bhyve
        };
};
typedef struct bhyve_cmd bhyve_cmd;

// Command structure for disk read and writes
typedef struct {
        uint32 direction; // 0 = read, 1 = write
        uint32 disk;      // which BIOS disk to read from [0, ...] (mapped in BIOS as 0x80, etc)
        uint32 head;
        uint32 cylinder;
        uint32 sector;
        uint32 sectors;   // blocks to read
        uint64 lba;       // used if H,C,S == 0
        uint64 addr;      // physical address of data source/dest (low)
        uint32 iodelay;   // delay io usec (0 = no delay, max 100000)
} bhyve_disk_io_cmd;

typedef struct {
        uint32 disk;
        uint32 heads;
        uint32 cylinders;
        uint32 sectors;           // sectors per track
        uint64 disk_sectors;
        uint32 sector_size;
} bhyve_disk_params;

typedef struct {
        uint8  sig[4];
        uint16 version;
        uint32 oem_name_address;
        uint32 capabilites;
        uint32 mode_list_address;
        uint16 video_mem_64k;
        uint16 oem_sw_version;
        uint32 vendor_name_address;
        uint32 product_name_address;
        uint32 product_rev_address;
} vbe_info;

typedef struct {
        uint16 mode_attr;
        uint8  window_a_attr;
        uint8  window_b_attr;
        uint16 window_granularity_kb;
        uint16 window_size_kb;
        uint16 window_a_start_segment;
        uint16 window_b_start_segment;
        uint32 window_positioning_address;
        uint16 bytes_per_scan_line;

        uint16 width;
        uint16 height;
        uint8  char_cell_width;
        uint8  char_cell_height;
        uint8  num_planes;
        uint8  bits_per_pixel;
        uint8  num_banks;
        uint8  memory_model;
        uint8  bank_size_kb;
        uint8  num_image_pages_less_one;
        uint8  vbe3;

        uint8  red_mask_size;
        uint8  red_mask_pos;
        uint8  green_mask_size;
        uint8  green_mask_pos;
        uint8  blue_mask_size;
        uint8  blue_mask_pos;
        uint8  reserved_mask_size;
        uint8  reserved_mask_pos;
        uint8  direct_color_mode_info;

        uint32 lfb_address;
        uint32 off_screen_address;
        uint16 off_screen_size_kb;
} vbe2_mode_info;


struct bhyve_sysinfo {
        uint32 total_mem;       // in kB
        uint32 total_mem_high;  // in kB
        uint32 num_disks;       // same value stored in bda->number_of_drives
        uint32 disks_addr;      // 32bit pointer to disks array, sorted by boot-order
        uint32 gop_fb;          // address of video framebuffer
};

typedef struct {
	uint16 seg;
	uint32 paddr; // mask with 0xffffff
	uint16 rsvd;
} int15_gdt;

#define BHYVE_DISKTYPE_FLOPPY 0
#define BHYVE_DISKTYPE_ISO    1
#define BHYVE_DISKTYPE_FIXED  2
struct bhyve_sysinfo_disks {
        uint32 disk_type;            // 0 = floppy, 1 = ISO, 2 = fixed
        uint32 heads;
        uint32 cylinders;
        uint32 sectors_per_track;
        uint32 sectors_low;
        uint32 sectors_high;
        uint32 block_size;
        uint32 total_size;
        uint32 total_size_high;
};

#define EDD_GEOMETRY_VALID          0x02
#define EDD_DEVICE_REMOVABLE        0x04
#define EDD_WRITE_VERIFY_SUPPORTED  0x08
#define EDD_DEVICE_CHANGE           0x10
#define EDD_DEVICE_LOCKABLE         0x20

typedef struct edd_drive_params {
        uint16 struct_size;
        uint16 flags;
        uint32 cylinders;
        uint32 heads;
        uint32 sectors_per_track;
        uint32 sectors_per_drive_low;
        uint32 sectors_per_drive_high;
        uint16 sector_size;
        uint16 rsvd1;
        uint16 rsvd2;
} edd_drive_params;

typedef struct edd_drive_packet {
        uint8  struct_size;
        uint8  rsvd1;
        uint16 blocks;
        uint32 buf_addr;
        uint32 lba_low;
        uint32 lba_high;
        uint32 buf_laddr_low;  // used if buf_addr == 0xffffffff
        uint32 buf_laddr_high;
} edd_drive_packet;

// Command for display settings
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
                        uint8 plane;
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
#pragma pack()


#define MD_DISK_HDD 0
#define MD_DISK_CD  1
typedef struct microbios_disk {
	int disk_type;
	void *sc;
	int (*md_write)(void *sc, uint64_t lba, void *buf, uint64_t sectors);
	int (*md_read)(void *sc, uint64_t lba, void *buf, uint64_t sectors);
	struct blockif_ctxt *(*md_getblkif)(void *sc);
} microbios_disk;

void microbios_init(struct vmctx *ctx);
void microbios_register_disk(microbios_disk *md);
uint8_t microbios_get_textpage();

#endif
