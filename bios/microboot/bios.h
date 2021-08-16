/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2020 Leon Dang.
 *
 * 16-bit BIOS for bhyve
 *
 * BIOS spec datastructures
 */

#pragma pack(1)
/* BIOS Data Area */
typedef struct {
	uint16  com1;
#define BDA_COM1 0x00
	uint16  com2;
#define BDA_COM2 0x02
	uint16  com3;
#define BDA_COM3 0x04
	uint16  com4;
#define BDA_COM4 0x06
	uint16  lpt1;
#define BDA_LPT1 0x08
	uint16  lpt2;
#define BDA_LPT2 0x0a
	uint16  lpt3;
#define BDA_LPT3 0x0c
	uint16  ebda;
#define BDA_EBDA 0x0e
	uint16  machine_config;
#define BDA_MACHCONF 0x10
	uint8   bda12;
	uint16  mem_size;
#define BDA_MEMSIZE 0x13
	uint8   bda15_16[0x02];
	uint8   keyboard_status1;
#define BDA_KBD_STS1 0x17
	uint8   keyboard_status2;
#define BDA_KBD_STS2 0x18
	uint8   bda19;
	uint16  key_buffer_head;
#define BDA_KBD_BUF_HEAD 0x1a
	uint16  key_buffer_tail;
#define BDA_KBD_BUF_TAIL 0x1c
	uint16  key_buffer[0x10];
	uint16  floppy_data;
	uint8   floppy_timeout;
	uint8   bda41_48[0x08];
	uint8   vid_mode;
	uint16  text_columns;
	uint16  vid_page_size;
	uint16  vid_page_offset;
	uint16  cursor_position[8]; // cursor positions for text pages; list of uint16(x, y)
	uint8   cursor_end;
	uint8   cursor_start;
	uint8   disp_page;          // current page displayed
	uint8   bda63_6B[0x9];
	uint32  timer_counter;      // 0x6c
	uint8   timer_rollover;     // 0x70 - set when 0x6c = 24 hours
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
#define BDA_KBD_STS3 0x96
	uint8   keyboard_status4;
#define BDA_KBD_STS4 0x97
} BDA;

typedef struct {
	uint16  bios_config_tbl_offset;
	uint16  flags;
	uint32  eax;
	uint32  edx;
} BIOS_VARS;

/* Enhanced Disk Structures */
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

typedef struct edd_drive_packet_sm {
	uint8  struct_size;
	uint8  rsvd1;
	uint16 blocks;
	uint16 addr;
	uint16 segment;
	uint32 sector_low;
	uint32 sector_high;
} edd_drive_packet_sm;

typedef struct edd_drive_packet_lg {
	uint8  struct_size;      // >= 16
	uint8  rsvd1;
	uint16 blocks;
	uint32 seg_offset;       // format: seg:offset
	uint32 lba_low;
	uint32 lba_high;
	uint32 transfer_buf_low; // struct-size > 16 && if seg_offset = 0xffffffff
	uint32 transfer_buf_high;
} edd_drive_packet_lg;

typedef union edd_drive_packet {
	edd_drive_packet_sm sm;
	edd_drive_packet_lg lg;
} edd_drive_packet;


/* VESA BIOS Extension */
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

#pragma pack()
