/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2020 Leon Dang.
 *
 * Parts taken from BhyeCsm (c) 2015 Pluribus Networks, Tycho Nightingale
 *
 * 16-bit BIOS for bhyve
 * objdump with: objdump -mi386 -Maddr16,data16 -D microboot
 */


#include "microboot.h"

void do_nothing_with_arg(void *arg);

int
bhyve_cmd_set(uint16 command, void *data, uint16 len)
{
	bhyve_cmd *cmd = (bhyve_cmd *)addrptr(BHYVE_CMD_BUF);
	cmd->seq++;
	cmd->command = command;
	cmd->results = 0x01BADC0D; // Magic to see if bhyve responded
	if (data) {
		memxfer(&cmd->args, data, len);
	}
	outb(BHYVE_IO_PORT, 0x02);
	return cmd->results;
}

void
dump_regs(callregs *regs)
{
	BDA *bda = (BDA *)bdaptr(0);

	printf("REGS[%x]: int%x:cs:%x,ip:%x, ds:%x, es:%x, "
	       "ss:esp:(%x:%x, bp %x) flags: %x, eax:%x, ebx:%x, ecx:%x, "
	       "edx:%x si:%x di:%x cf %d timer %u\r\n",
	   regs,
	   regs->intcode, regs->cs, regs->ip, regs->ds, regs->es,
	   regs->ss, regs->_esp.esp, regs->_ebp.bp, regs->flags, regs->_eax.eax,
	   regs->_ebx.ebx, regs->_ecx.ecx, regs->_edx.edx, regs->_esi.esi,
	   regs->_edi.edi, regs->flags.CF, bda->timer_counter);

	uint16 *d = (uint16 *)regs;
	for (int i = 0; i < 40; i++) {
		printf("%04x ", d[i]);
	}
	printf("\r\n");
}

void
dump_stack(uint16 stack)
{
	uint16 *ptr = &stack;
	printf("[STACK %x] ", ptr);
	for (int i = 0; i < 32; i++)
		printf("%04x ", ptr[i]);
	printf("\r\n");
}

static uint32
csum_blocks(uint32 addr, uint32 len)
{
	uint32 csum = 0x32838211;
	len /= 4;
	for (; len > 0; len--) {
		csum ^= *(uint32 *)addr;
		addr += 4;
	}
	return csum;
}

void
bhyve_test_call(callregs *regs)
{
	bhyve_cmd *cmd = (bhyve_cmd *)addrptr(BHYVE_CMD_BUF);
	BDA *bda = (BDA *)bdaptr(0);
	uint16 *wtest;
	int i;
	uint32 com1val;

	// bhyve cmd test
	cmd->seq++;
	cmd->command = BCMD_DBG_PRINT;
	memxfer(&cmd->args, GLOBAL_PTR("TEST-DBG_PRINT"), 16);
	outb(BHYVE_IO_PORT, 0x02);

	printf("BHYVE MICROBIOS");

	// Verify access to BIOS segment
	wtest = (uint16 *)addrptr(SEG_BIOS_ADDR + BIOS_CODE_START);
	printf("Memory/reg test [bios:0]0x%x.. ds %x, cs %x\r\n",
	       wtest[0], get_reg_ds(), get_reg_cs());

	// BDA access test
	com1val = memget(0x400);
	printf("(BDA): com1: %x|%x, columns: %x, rows: %x\r\n",
	       bda->com1, com1val, bda->text_columns, bda->text_rows_minus_one);
	printf("(BDA): head 0x%x tail 0x%x end 0x%x start 0x%x\r\n",
	       bda->key_buffer_head, bda->key_buffer_tail, bda->key_buffer_end, bda->key_buffer_start);

	// E820 table test
	printf("E820: %u %u\r\n", read_u16(E820_INFO_BLOCK), read_u16(E820_INFO_BLOCK+2));
}

extern uint32 read_timer();
extern uint32 read_com1();

void
bhyve_load_bootsect()
{
	// Load the boot sector and run with it
	bhyve_disk_io_cmd *iocmd = (bhyve_disk_io_cmd *)addrptr(BHYVE_CMD_BUF_ARGS);
	int res;
	uint16 sig;

	printf("LOADING BOOT SECTOR into [%x]\r\n", 0x7c00);

	iocmd->direction = 0;
	iocmd->disk = 0x80;
	iocmd->head = 0;
	iocmd->cylinder = 0;
	iocmd->sector = 0;
	iocmd->sectors = 1;
	iocmd->lba_low = 0;
	iocmd->lba_high = 0;
	iocmd->addr_low = 0x7c00;
	iocmd->addr_high = 0;

	res = bhyve_cmd_set(BCMD_DISK_IO, 0, 0);
	if (res) {
		printf("BHYVE BOOT SECTOR LOAD FAILED, RES 0x%x\r\n", res);
		for (;;) ;
	}

	sig = read_u16(0xF0000);
	printf("@0xF0000 0x%x.. ds %x, cs %x\r\n", sig, get_reg_ds(), get_reg_cs());

	sig = read_u16(0x7c00);
	printf("@7c00 BOOT SECTOR BYTES: [00]: 0x%x\r\n", sig);

	// check if 0x7c00-2 == 0xaa55
	sig = read_u16(0x7c00 + 510);
	if (sig != 0xAA55) {
		printf("BOOT SIGNATURE NOT VALID: 0x%x\r\n", sig);
		for (;;)
			asm ("cli; hlt");
	}

	printf("BOOT SECTOR BYTES: TAIL: 0x%x\r\n", sig);
	sig = read_u16(0x7c00);
	printf("BOOT SECTOR BYTES: [00]: 0x%x\r\n", sig);
}



#define DEBUG(x) printf(GLOBAL_PTR(x))

void handle_int09(callregs *regs);
void handle_int10(callregs *regs);
void handle_int15(callregs *regs);
void handle_int16(callregs *regs);
void handle_int1a(callregs *regs);

int
int_handler(callregs *regs)
{
	//printf("&ARGS: %x... regs %x cs %x, ip %x\r\n", &regs, regs, regs->cs, regs->ip);

	switch (regs->intcode) {
	case 0x09:
		// keyboard command interrupt
		handle_int09(regs);
		break;
	case 0x10:
		// VGA
		handle_int10(regs);
		break;
	case 0x19:
		printf("REBOOT CALLED\r\n");
		asm ("cli; hlt");
		//asm ("jmp $0xf000,$0x0000");
		break;
	case 0x14:
		// serial port
		printf("INT14\r\n");
		break;
	case 0x15:
		handle_int15(regs);
		break;
#ifdef INT16_C
	case 0x16:
		handle_int16(regs);
		break;
#endif
#ifdef INT1A_C
	case 0x1a:
		handle_int1a(regs);
		break;
#endif
	case 0x1c:
		printf("INT1C\r\n");
		break;
	default:
		printf("UNDEFINED INT %x\r\n", regs->intcode);
		dump_regs(regs);
		break;
	}

	if (regs->flags.IF)
		asm("sti");

	return 0;
}


// Keyboard handling
#define KBD_DATA_REG          0x60
#define	KBD_STATUS_REG        0x64
#define	KBD_CMD_REG           0x64

// Status register bits
#define KBD_STSREG_OUTPUT_BUF 0x1
#define	KBD_STSREG_INPUT_BUF  0x2

#define KBD_CMD_WRITE                   0x60
#define KBD_CMD_DISABLE_MOUSE_INTERFACE 0xA7
#define KBD_CMD_CONTROLLER_SELF_TEST    0xAA
#define KBD_CMD_DISABLE_KBD_INTERFACE   0xAD
#define	KBD_CMD_ENABLE_KBD_INTERFACE    0xAE

#define KBD_CMD_SELECT_SCAN_CODE_SET    0xF0
#define KBD_CMD_CLEAR_OUTPUT_DATA       0xF4
#define KBD_RETURN_8042_ACK             0xFA

//  KEYBOARD COMMAND BYTE
//  7: Reserved
//  6: PC/XT translation mode convert
//  5: Disable Auxiliary device interface
//  4: Disable keyboard interface
//  3: Reserved
//  2: System Flag: selftest successful
//  1: Enable Auxiliary device interrupt
//  0: Enable Keyboard interrupt
#define KBD_CMDBYTE_TRANSLATE      (0x1 << 6)
#define KBD_CMDBYTE_DISABLE_AUX    (0x1 << 5)
#define KBD_CMDBYTE_DISABLE_KB     (0x1 << 4)
#define KBD_CMDBYTE_SELFTEST_OK	   (0x1 << 2)
#define KBD_CMDBYTE_ENABLE_AUXINT  (0x1 << 1)
#define KBD_CMDBYTE_ENABLE_KBINT   (0x1 << 0)

// 0040:0017 - KEYBOARD - STATUS FLAGS 1
//   7 Insert active
//   6 Caps Lock active
//   5 Num Lock active
//   4 Scroll Lock active
//   3 either Alt pressed
//   2 either Ctrl pressed
//   1 Left Shift pressed
//   0 Right Shift pressed
#define KBSF1_INSERT_BIT         (0x1 << 7)
#define KBSF1_CAPS_LOCK_BIT      (0x1 << 6)
#define KBSF1_NUM_LOCK_BIT       (0x1 << 5)
#define KBSF1_SCROLL_LOCK_BIT    (0x1 << 4)
#define KBSF1_ALT_PRESSED        (0x1 << 3)
#define KBSF1_CTRL_PRESSED       (0x1 << 2)
#define KBSF1_LSHIFT_PRESSED     (0x1 << 1)
#define KBSF1_RSHIFT_PRESSED     (0x1 << 0)

// 0040:0018 - KEYBOARD - STATUS FLAGS 2
//   7: insert key is depressed
//   6: caps-lock key is depressed
//   5: num-lock key is depressed
//   4: scroll lock key is depressed
//   3: suspend key has been toggled
//   2: system key is pressed and held
//   1: left ALT key is pressed
//   0: left CTRL key is pressed
#define KBSF2_SUSPEND_PRESSED        (0x1 << 3)
#define KBSF2_SYSREQ_PRESSED         (0x1 << 2)
#define KBSF2_LEFT_ALT_PRESSED       (0x1 << 1)
#define KBSF2_LEFT_CTRL_PRESSED      (0x1 << 0)

// 0040:0096 - KEYBOARD - STATUS FLAGS 3
//   4: 101/102 enhanced keyboard installed
//   1: last scancode was E0
//   0: last scancode was E1
#define KBSF3_LAST_CODE_E1       (0x1 << 0)
#define KBSF3_LAST_CODE_E0       (0x1 << 1)
#define KBSF3_ENHANCED_KBD           (0x1 << 4)

// 0040:0097 - KEYBOARD - STATUS FLAGS 4

#define SCANCODE_CONTROL                                0x1d
#define SCANCODE_L_SHIFT                                0x2a
#define SCANCODE_R_SHIFT                                0x36
#define SCANCODE_ALT                                    0x38
#define SCANCODE_CAPS_LOCK                              0x3a
#define SCANCODE_NUM_LOCK                               0x45
#define SCANCODE_SCROLL_LOCK                            0x46
#define SCANCODE_INSERT                                 0x52

struct key_entry {
	uint16  normal;
	uint16  shift;
	uint16  control;
	uint16  alt;
};

static const struct key_entry key_1byte_entries[] = {
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key  0 */
  { 0x011b, 0x011b, 0x011b, 0xffff }, /* key  1 - Escape key */
  { 0x0231, 0x0221, 0xffff, 0x7800 }, /* key  2 - '1' */
  { 0x0332, 0x0340, 0x0300, 0x7900 }, /* key  3 - '2' */
  { 0x0433, 0x0423, 0xffff, 0x7a00 }, /* key  4 - '3' */
  { 0x0534, 0x0524, 0xffff, 0x7b00 }, /* key  5 - '4' */
  { 0x0635, 0x0625, 0xffff, 0x7c00 }, /* key  6 - '5' */
  { 0x0736, 0x075e, 0x071e, 0x7d00 }, /* key  7 - '6' */
  { 0x0837, 0x0826, 0xffff, 0x7e00 }, /* key  8 - '7' */
  { 0x0938, 0x092a, 0xffff, 0x7f00 }, /* key  9 - '8' */
  { 0x0a39, 0x0a28, 0xffff, 0x8000 }, /* key 10 - '9' */
  { 0x0b30, 0x0b29, 0xffff, 0x8100 }, /* key 11 - '0' */
  { 0x0c2d, 0x0c5f, 0x0c1f, 0x8200 }, /* key 12 - '-' */
  { 0x0d3d, 0x0d2b, 0xffff, 0x8300 }, /* key 13 - '=' */
  { 0x0e08, 0x0e08, 0x0e7f, 0xffff }, /* key 14 - backspace */
  { 0x0f09, 0x0f00, 0xffff, 0xffff }, /* key 15 - tab */
  { 0x1071, 0x1051, 0x1011, 0x1000 }, /* key 16 - 'Q' */
  { 0x1177, 0x1157, 0x1117, 0x1100 }, /* key 17 - 'W' */
  { 0x1265, 0x1245, 0x1205, 0x1200 }, /* key 18 - 'E' */
  { 0x1372, 0x1352, 0x1312, 0x1300 }, /* key 19 - 'R' */
  { 0x1474, 0x1454, 0x1414, 0x1400 }, /* key 20 - 'T' */
  { 0x1579, 0x1559, 0x1519, 0x1500 }, /* key 21 - 'Y' */
  { 0x1675, 0x1655, 0x1615, 0x1600 }, /* key 22 - 'U' */
  { 0x1769, 0x1749, 0x1709, 0x1700 }, /* key 23 - 'I' */
  { 0x186f, 0x184f, 0x180f, 0x1800 }, /* key 24 - 'O' */
  { 0x1970, 0x1950, 0x1910, 0x1900 }, /* key 25 - 'P' */
  { 0x1a5b, 0x1a7b, 0x1a1b, 0xffff }, /* key 26 - '[' */
  { 0x1b5d, 0x1b7d, 0x1b1d, 0xffff }, /* key 27 - ']' */
  { 0x1c0d, 0x1c0d, 0x1c0a, 0xffff }, /* key 28 - CR */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 29 - left control */
  { 0x1e61, 0x1e41, 0x1e01, 0x1e00 }, /* key 30 - 'A' */
  { 0x1f73, 0x1f53, 0x1f13, 0x1f00 }, /* key 31 - 'S' */
  { 0x2064, 0x2044, 0x2004, 0x2000 }, /* key 32 - 'D' */
  { 0x2166, 0x2146, 0x2106, 0x2100 }, /* key 33 - 'F' */
  { 0x2267, 0x2247, 0x2207, 0x2200 }, /* key 34 - 'G' */
  { 0x2368, 0x2348, 0x2308, 0x2300 }, /* key 35 - 'H' */
  { 0x246a, 0x244a, 0x240a, 0x2400 }, /* key 36 - 'J' */
  { 0x256b, 0x254b, 0x250b, 0x2500 }, /* key 37 - 'K' */
  { 0x266c, 0x264c, 0x260c, 0x2600 }, /* key 38 - 'L' */
  { 0x273b, 0x273a, 0xffff, 0xffff }, /* key 39 - ';' */
  { 0x2827, 0x2822, 0xffff, 0xffff }, /* key 40 - ''' */
  { 0x2960, 0x297e, 0xffff, 0xffff }, /* key 41 - '`' */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 42 - left shift */
  { 0x2b5c, 0x2b7c, 0x2b1c, 0xffff }, /* key 43 - '' */
  { 0x2c7a, 0x2c5a, 0x2c1a, 0x2c00 }, /* key 44 - 'Z' */
  { 0x2d78, 0x2d58, 0x2d18, 0x2d00 }, /* key 45 - 'X' */
  { 0x2e63, 0x2e43, 0x2e03, 0x2e00 }, /* key 46 - 'C' */
  { 0x2f76, 0x2f56, 0x2f16, 0x2f00 }, /* key 47 - 'V' */
  { 0x3062, 0x3042, 0x3002, 0x3000 }, /* key 48 - 'B' */
  { 0x316e, 0x314e, 0x310e, 0x3100 }, /* key 49 - 'N' */
  { 0x326d, 0x324d, 0x320d, 0x3200 }, /* key 50 - 'M' */
  { 0x332c, 0x333c, 0xffff, 0xffff }, /* key 51 - ',' */
  { 0x342e, 0x343e, 0xffff, 0xffff }, /* key 52 - '.' */
  { 0x352f, 0x353f, 0xffff, 0xffff }, /* key 53 - '/' */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 54 - right shift - */
  { 0x372a, 0xffff, 0x3772, 0xffff }, /* key 55 - prt-scr - */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 56 - left alt - */
  { 0x3920, 0x3920, 0x3920, 0x3920 }, /* key 57 - space bar */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 58 - caps-lock -  */
  { 0x3b00, 0x5400, 0x5e00, 0x6800 }, /* key 59 - F1 */
  { 0x3c00, 0x5500, 0x5f00, 0x6900 }, /* key 60 - F2 */
  { 0x3d00, 0x5600, 0x6000, 0x6a00 }, /* key 61 - F3 */
  { 0x3e00, 0x5700, 0x6100, 0x6b00 }, /* key 62 - F4 */
  { 0x3f00, 0x5800, 0x6200, 0x6c00 }, /* key 63 - F5 */
  { 0x4000, 0x5900, 0x6300, 0x6d00 }, /* key 64 - F6 */
  { 0x4100, 0x5a00, 0x6400, 0x6e00 }, /* key 65 - F7 */
  { 0x4200, 0x5b00, 0x6500, 0x6f00 }, /* key 66 - F8 */
  { 0x4300, 0x5c00, 0x6600, 0x7000 }, /* key 67 - F9 */
  { 0x4400, 0x5d00, 0x6700, 0x7100 }, /* key 68 - F10 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 69 - num-lock - */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 70 - scroll-lock -  */
  { 0x4700, 0x4737, 0x7700, 0xffff }, /* key 71 - home */
  { 0x4800, 0x4838, 0xffff, 0xffff }, /* key 72 - cursor up */
  { 0x4900, 0x4939, 0x8400, 0xffff }, /* key 73 - page up */
  { 0x4a2d, 0x4a2d, 0xffff, 0xffff }, /* key 74 - minus sign */
  { 0x4b00, 0x4b34, 0x7300, 0xffff }, /* key 75 - cursor left */
  { 0xffff, 0x4c35, 0xffff, 0xffff }, /* key 76 - center key */
  { 0x4d00, 0x4d36, 0x7400, 0xffff }, /* key 77 - cursor right */
  { 0x4e2b, 0x4e2b, 0xffff, 0xffff }, /* key 78 - plus sign */
  { 0x4f00, 0x4f31, 0x7500, 0xffff }, /* key 79 - end */
  { 0x5000, 0x5032, 0xffff, 0xffff }, /* key 80 - cursor down */
  { 0x5100, 0x5133, 0x7600, 0xffff }, /* key 81 - page down */
  { 0x5200, 0x5230, 0xffff, 0xffff }, /* key 82 - insert */
  { 0x5300, 0x532e, 0xffff, 0xffff }, /* key 83 - delete */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 84 - sys key */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 85 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key 86 */
  { 0x8500, 0x5787, 0x8900, 0x8b00 }, /* key 87 - F11 */
  { 0x8600, 0x5888, 0x8a00, 0x8c00 }, /* key 88 - F12 */
};

static const struct key_entry key_2byte_entries[] = {
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 0 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 1 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 2 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 3 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 4 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 5 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 6 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 7 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 8 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 9 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 a */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 b */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 c */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 d */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 e */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 f */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 10 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 11 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 12 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 13 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 14 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 15 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 16 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 17 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 18 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 19 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 1a */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 1b */
  { 0x1c0d, 0x1c0d, 0x1c0a, 0xffff }, /* key e0 1c - numeric keypad enter */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 1d - right control */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 1e */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 1f */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 20 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 21 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 22 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 23 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 24 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 25 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 26 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 27 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 28 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 29 */
  { 0xffff, 0xffff, 0x7200, 0xffff }, /* key e0 2a - numlock */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 2b */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 2c */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 2d */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 2e */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 2f */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 30 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 31 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 32 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 33 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 34 */
  { 0x352f, 0x352f, 0xffff, 0xffff }, /* key e0 35 - '/' */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 36 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 37 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 38 - right alt */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 39 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 3a */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 3b */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 3c */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 3d */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 3e */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 3f */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 40 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 41 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 42 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 43 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 44 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 45 */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 46 */
  { 0x4700, 0x4700, 0x7700, 0xffff }, /* key e0 47 - home */
  { 0x4800, 0x4800, 0xffff, 0xffff }, /* key e0 48 - cursor up */
  { 0x4900, 0x4900, 0x8400, 0xffff }, /* key e0 49 - page up */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 4a */
  { 0x4b00, 0x4b00, 0x7300, 0xffff }, /* key e0 4b - cursor left */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 4c */
  { 0x4d00, 0x4d00, 0x7400, 0xffff }, /* key e0 4d - cursor right */
  { 0xffff, 0xffff, 0xffff, 0xffff }, /* key e0 4e */
  { 0x4f00, 0x4f00, 0x7500, 0xffff }, /* key e0 4f - end */
  { 0x5000, 0x5000, 0xffff, 0xffff }, /* key e0 50 - cursor down */
  { 0x5100, 0x5100, 0x7600, 0xffff }, /* key e0 51 - page down */
  { 0x5200, 0x5200, 0xffff, 0xffff }, /* key e0 52 - insert */
  { 0x5300, 0x5300, 0xffff, 0xffff }, /* key e0 53 - delete */
};



static inline void
kbd_write_cmd(uint8 data)
{
	// TODO: wait for input empty

	outb(KBD_CMD_REG, data);

	// TODO: wait for input empty again
}

static inline void
kbd_write_data(uint8 data)
{
	outb(KBD_DATA_REG, data);
}


static inline uint8
kbd_read_sts_reg()
{
	return inb(KBD_STATUS_REG);
}

static inline uint8
kbd_read_data_reg()
{
	return inb(KBD_DATA_REG);
}

void
kbd_init()
{
	kbd_write_cmd(KBD_CMD_WRITE);
	kbd_write_data(KBD_CMDBYTE_TRANSLATE|KBD_CMDBYTE_DISABLE_AUX|KBD_CMDBYTE_SELFTEST_OK|KBD_CMDBYTE_ENABLE_KBINT);
}

void
process_scancode(uint8 scancode)
{
	BDA *bda = (BDA *)bdaptr(0);
	struct key_entry *key_entries;
	uint16 key;
	uint16 new_tail;
	uint8 scanbit;
	uint8 *status1, *status3;

	status3 = (uint8 *)bdaptr(BDA_KBD_STS3);
	if (scancode == 0xe0) {
		*status3 |= KBSF3_LAST_CODE_E0;
		return;
	}
	if (scancode == 0xe1) {
		*status3 |= KBSF3_LAST_CODE_E1;
		return;
	}

	switch (scancode & 0x7f) {
	case SCANCODE_INSERT:
		scanbit = KBSF1_INSERT_BIT;
		break;
	case SCANCODE_CAPS_LOCK:
		scanbit = KBSF1_CAPS_LOCK_BIT;
		break;
	case SCANCODE_NUM_LOCK:
		scanbit = KBSF1_NUM_LOCK_BIT;
		break;
	case SCANCODE_SCROLL_LOCK:
		scanbit = KBSF1_SCROLL_LOCK_BIT;
		break;
	case SCANCODE_ALT:
		scanbit = KBSF1_ALT_PRESSED;
		break;
	case SCANCODE_CONTROL:
		scanbit = KBSF1_CTRL_PRESSED;
		break;
	case SCANCODE_L_SHIFT:
		scanbit = KBSF1_LSHIFT_PRESSED;
		break;
	case SCANCODE_R_SHIFT:
		scanbit = KBSF1_RSHIFT_PRESSED;
		break;
	default:
		scanbit = 0;
	}

	status1 = (uint8 *)bdaptr(BDA_KBD_STS1);
	if (scanbit) {
		if (scancode & 0x80) {
			// released
			*status1 &= ~scanbit;
			goto done;
		} else {
			// pressed
			*status1 |= scanbit;
		}
	} else if (scancode & 0x80) {
		goto done;
	}

	if (*status3 & KBSF3_LAST_CODE_E0) {
		key_entries = GLOBAL_PTR(key_2byte_entries);
	} else {
		key_entries = GLOBAL_PTR(key_1byte_entries);
	}

	if (*status1 & KBSF1_ALT_PRESSED) {
		key = key_entries[scancode].alt;
	} else if (*status1 & KBSF1_CTRL_PRESSED) {
		key = key_entries[scancode].control;
	} else if (*status1 & (KBSF1_LSHIFT_PRESSED | KBSF1_RSHIFT_PRESSED)) {
		key = key_entries[scancode].shift;
	} else {
		key = key_entries[scancode].normal;
	}

	//printf("XXX INT9: scancode %x -> key %x\r\n", scancode, key);

	if (key == 0xffff)
		goto done;

	new_tail = bda->key_buffer_tail + 2;
	if (new_tail == bda->key_buffer_end)
		new_tail = bda->key_buffer_start;

	if (new_tail == bda->key_buffer_head)
		goto done;  // buffer full

	*(uint16 *)((uint8 *)bda + bda->key_buffer_tail) = key;
	bda->key_buffer_tail = new_tail;

done:
	*status3 &= ~(KBSF3_LAST_CODE_E0 | KBSF3_LAST_CODE_E1);
}

void
handle_int09(callregs *regs)
{
	uint8 data;

#if 0
	/* for some reason no output buf status set when dos edit.com runs... */
	kbd_write_cmd(KBD_CMD_DISABLE_KBD_INTERFACE);

	if (kbd_read_sts_reg() & KBD_STSREG_OUTPUT_BUF) {
		data = kbd_read_data_reg();
		process_scancode(data);
	}

	kbd_write_cmd(KBD_CMD_ENABLE_KBD_INTERFACE);
#else
	data = kbd_read_data_reg();
	process_scancode(data);
#endif

	// EOI master PIC
	asm ("push %ax; mov $0x20, %al; outb %al, $0x20; pop %ax");
}


// Display
// XXX: VESA linear framebuffer support? https://wiki.osdev.org/VESA_Video_Modes
//      -- provide a physical addr for the LFB

#define VGA_BUF_ADDR            0xA0000
#define VGA_BUF_LENGTH          0x10000

#define VGA_GFX_INDEX           0x3CE
#define VGA_GFX_DATA            0x3CF

#define VGA_CRTC_INDEX          0x3D4
#define VGA_CRTC_DATA           0x3D5

#define VGA_WRITE_ATTR_INDEX    0x3C0
#define VGA_WRITE_ATTR_DATA     0x3C0

#define VGA_SEQ_INDEX           0x3C4
#define VGA_SEQ_DATA            0x3C5

#define VGA_PALETTE_WRITE       0x3C8
#define VGA_PALETTE_READ        0x3C7
#define VGA_PALETTE_DATA        0x3C9

#define VGA_INPUT_STATUS_1      0x3DA
#define VGA_WRITE_MISC_PORT     0x3C2

#define VGA_READ_FEATURE_PORT   0x3CA
#define VGA_WRITE_FEATURE_PORT  0x3DA

volatile void
setuint8(uint32 addr, uint8 val)
{
	volatile uint8 *d = (uint8 *)addr;
	*d = val;
}

/*
 * In text mode, just write to the text buffer; let the hypervisor render the glyphs
 * in VGA emulation, since it can use a different CPU thread to do that. It also means
 * in graphics mode, the same rendering engine can convert the pixel format to RGBA.
 */
void
handle_int10(callregs *regs)
{
	BDA *bda = (BDA *)bdaptr(0);
	uint16 attrib;
	uint16 page;
	uint8 ah;

	ah = regs->_eax.ah;

	switch (regs->_eax.ah) {
	case 0x00: // set video mode
	{
		/*
		 * AL = 03  80x25 16 color text (CGA,EGA,MCGA,VGA) (640x480x4bpp)
		 *    = 12  640x480 16 color graphics (VGA)
		 *    = 13  320x200 256 color graphics (MCGA,VGA)
		 */
		switch (regs->_eax.al) {
		case 0x03:
			bda->text_columns = 80;
			bda->text_rows_minus_one = 24;
			bda->scan_lines_per_char = 16;
			break;
		case 0x12:
			bda->text_columns = 80;
			bda->text_rows_minus_one = 29;
			bda->scan_lines_per_char = 16;
			break;
		case 0x13:
			bda->text_columns = 40;
			bda->text_rows_minus_one = 24;
			bda->scan_lines_per_char = 16;
			break;
		default:
			regs->flags.CF = 1;
			goto out;
		}

		bda->vid_mode = regs->_eax.al;

		bhyve_display_cmd *cmd = (bhyve_display_cmd *)addrptr(BHYVE_CMD_BUF_ARGS);
		cmd->vidcmd = BVIDCMD_VIDMODE;
		cmd->vidmode.mode = regs->_eax.al;
		cmd->vidmode.plane = 0;
		cmd->vidmode.rows = bda->text_rows_minus_one + 1;
		cmd->vidmode.columns = bda->text_columns;
		bhyve_cmd_set(BCMD_VIDEO, 0, 0);
		break;
	}
	case 0x01: // set cursor shape
		goto ok;
	case 0x02: // set cursor position
		page = regs->_ebx.bh;
		if (bda->vid_mode == 0x03) {
			if (page > 7)
				break;
		} else if (page > 0) {
			break;
		}

		uint8 x = regs->_edx.dl;
		uint8 y = regs->_edx.dh;
		if (x >= bda->text_columns)
			x = bda->text_columns - 1;
		if (y > bda->text_rows_minus_one)
			y = bda->text_rows_minus_one;
		bda->cursor_position[page] = x | (y << 8);
		goto ok;
	case 0x03: // get cursor position
		page = regs->_ebx.bh;
		if (bda->vid_mode == 0x03) {
			if (page > 7)
				break;
		} else if (page > 0) {
			break;
		}
		regs->_edx.dl = bda->cursor_position[page] & 0xff;
		regs->_edx.dh = bda->cursor_position[page] >> 8;
		regs->_ecx.ch = 10; // cursor start line
		regs->_ecx.cl = 16; // cursor end line
		goto ok;
	case 0x05: // set active display page
		page = regs->_eax.al;
		if (bda->vid_mode == 0x03) {
			if (page > 7)
				break;
		} else if (page > 0) {
			break;
		}

		printf("INT10 - Set display page: %u\r\n", page);

		bda->disp_page = page;

		// Tell bhyve of active page change
		bhyve_display_cmd *cmd = (bhyve_display_cmd *)addrptr(BHYVE_CMD_BUF_ARGS);
		cmd->vidcmd = BVIDCMD_DISPLAY_PAGE;
		cmd->display_page = regs->_eax.al;
		bhyve_cmd_set(BCMD_VIDEO, 0, 0);
		break;
	case 0x06: // scroll up window
	case 0x07: // scroll down window
	{
		uint16  x, y, x1, y1, x2, y2;
		uint16  lines;
		uint8   ch, attr;

		// number of cells in the page
		uint16 cells = (bda->text_rows_minus_one + 1)*bda->text_columns;
		uint16 *page_addr = (uint16 *)addrptr(0xB8000 + bda->disp_page*cells);

		lines = regs->_eax.al;
		if (lines == 0) {
			// clear window
			for (int i = 0; i < cells; i++) {
				page_addr[i] = 0x0f20;
			} 
			bda->cursor_position[page] = 0;
			break;
		}

		x1 = regs->_ecx.cl; // top left
		y1 = regs->_ecx.ch;
		x2 = regs->_edx.dl; // bottom right
		y2 = regs->_edx.dh;

		if (x1 >= bda->text_columns)
			x1 = bda->text_columns - 1;
		if (x2 >= bda->text_columns)
			x2 = bda->text_columns - 1;

		uint16 *dst = page_addr, *src = page_addr;
		if (regs->_eax.ah == 0x06) {
			if (y1 == 0) // no lines to move
				break;
			if (lines > y1) // limit number of lines
				lines = y1;

			dst += (y1 - lines) * bda->text_columns;
			src += y1 * bda->text_columns;
			for (y = y1; y <= y2; y++) {
				for (x = x1; x <= x2; x++) {
					dst[x] = src[x];
				}
				dst += bda->text_columns;
				src += bda->text_columns;
			}

			// fill area at bottom
			dst = page_addr + (y2+1-lines)*bda->text_columns;
		} else {
			if (y2 >= bda->text_rows_minus_one) // no lines to move
				break;
			if (lines > (bda->text_rows_minus_one-y2))
				lines = bda->text_rows_minus_one-y2;

			// scroll down
			if (y1 == 0)
				y1 = 1;
			if (x2 >= bda->text_columns)
				x2 = bda->text_columns - 1;


			dst += (y2 + lines) * bda->text_columns;
			src += y2 * bda->text_columns;
			for (y = y2; y >= y1; y--) {
				for (x = x1; x <= x2; x++) {
					dst[x] = src[x];
				}
				dst += bda->text_columns;
				src += bda->text_columns;
			}

			// fill area top row
			dst = page_addr + y1*bda->text_columns;
		}

		// fill
		uint16 fill_data = regs->_ebx.bh << 8 | ' ';
		for (y = 0; y < lines; y++) {
			for (x = x1; x <= x2; x++) {
				dst[x] = fill_data;
			}
			dst += bda->text_columns;
		}

		break;
	}
	case 0x08: // get character at cursor
	{
		page = regs->_ebx.bh;
		if (bda->vid_mode == 0x03) {
			if (page > 7)
				break;
		} else if (page > 0) {
			break;
		}
		uint16 cells = (bda->text_rows_minus_one + 1)*bda->text_columns;
		uint16 *page_addr = (uint16 *)addrptr(0xB8000 + bda->disp_page*cells);
		uint16 x = bda->cursor_position[page] & 0xff;
		uint16 y = bda->cursor_position[page] >> 8;
		uint16 v = page_addr[y * bda->text_columns + x];
		regs->_eax.ah = v >> 8;
		regs->_eax.al = v & 0xff;
		break;
	}
	case 0x09: // write character and attribute
		attrib = regs->_ebx.bl | 0x8000;
		// Fall-through
	case 0x0a: // write character, no attribute
	{
		page = regs->_ebx.bh;
		if (bda->vid_mode == 0x03) {
			if (page > 7)
				break;
		} else if (page > 0) {
			break;
		}

		uint16 cells = (bda->text_rows_minus_one + 1)*bda->text_columns;
		uint16 *page_addr = (uint16 *)addrptr(0xB8000 + page*cells);
		uint8 ch = regs->_eax.al;
		uint16 x, y;

		x = bda->cursor_position[page] & 0xff;
		y = bda->cursor_position[page] >> 8;

		uint16 *dst = page_addr + (y*bda->text_columns + x);

		uint16 count = regs->_ecx.cx;
		if (ah == 0x0a) {
			attrib = *dst >> 8;
			count = 1;
		} else {
			attrib &= ~0x8000;
		}

		uint16 output = ch | attrib << 8;

		// For graphics mode, ask bhyve to render the text glyph at the
		// corresponding position. Always put the plain text into B8000 however.
		//int xorattrib = regs->_ebx.bl & 0x80;
		for (uint16 i = 0; i < count; i++) {
#if 0
			if (xorattrib) {
				*dst = (*dst & 0xff00) | ' '; // clear character
			} else {
				*dst = output;
			}
#endif

			*dst = output;
			dst++;
		}

		if (bda->vid_mode != 0x03) {
			bhyve_display_cmd *cmd = (bhyve_display_cmd *)addrptr(BHYVE_CMD_BUF_ARGS);
			cmd->vidcmd = BVIDCMD_WRITE_CHAR;
			cmd->write_char.ch = ch;
			cmd->write_char.attrib = attrib;
			cmd->write_char.row = y;
			cmd->write_char.col = x;
			cmd->write_char.repeat = count;

			bhyve_cmd_set(BCMD_VIDEO, 0, 0);
		}

		break;
	}
	case 0x0b: // set colour palette
		// XXX skip for now
		break;
	case 0x0e: // write character at cursor position; tty mode
	{
		/* XXX switch active page?
		page = regs->_ebx.bh;
		if (bda->vid_mode == 0x03) {
			if (page > 7) {
				break;
			}
		} else if (page > 0) {
			break;
		}
		*/
		page = bda->disp_page;

		uint8 ch = regs->_eax.al;
		uint16 cells = (bda->text_rows_minus_one + 1)*bda->text_columns;
		uint8 x, y;

		x = bda->cursor_position[page] & 0xff;
		y = bda->cursor_position[page] >> 8;

		printf("%c", ch);

		switch (ch) {
		case 0x07: // bell
			break;
		case 0x08: // backspace
			if (x > 0) {
				x--;
			}
			break;
		case 0x09: // tab
			x = x + ((x + 8) & ~7);
			break;
		case 0x0a: // newline
			y++;
			break;
		case 0x0b: // vertical tab
			break;
		case 0x0c: // form feed
			break;
		case 0x0d: // carriage return;
			x = 0;
			break;
		default:
			setuint8(0xB8000 + page * cells * 2 + (y * bda->text_columns + x) * 2, ch);
			x++;
			break;
		}

		if (x >= bda->text_columns) {
			x = 0;
			y++;
		}

		if (y > bda->text_rows_minus_one) {
			// Move screen up
			volatile uint8 *dst = (uint8 *)addrptr(0xB8000 + page*cells*2);
			volatile uint8 *src = dst + (bda->text_columns * 2);
			int i, size;
			size = bda->text_columns * (bda->text_rows_minus_one);
			for (i = 0; i < size; i++) {
				dst[0] = src[0];
				dst[1] = src[1];
				dst += 2;
				src += 2;

			}
			for (i = 0; i < bda->text_columns; i++) {
				*dst = ' ';
				dst += 2;
			}
			y = bda->text_rows_minus_one;
		}

		bda->cursor_position[page] = x | (y << 8);
		break;
	}
	case 0x0f: // get video mode
		regs->_eax.al = bda->vid_mode;
		regs->_eax.ah = bda->text_columns;
		regs->_ebx.bh = bda->disp_page;
		break;
	case 0x10: // get/set palette registers
	{
		bhyve_display_cmd *cmd = (bhyve_display_cmd *)addrptr(BHYVE_CMD_BUF_ARGS);

		switch (regs->_eax.al) {
		case 0x00: // set individual palette register
			inb(VGA_INPUT_STATUS_1);
			outb(VGA_WRITE_ATTR_INDEX, regs->_ebx.bl);
			outb(VGA_WRITE_ATTR_DATA, regs->_ebx.bh);
			break;
		case 0x02: // set all palette registers and overscan register
		{
#if 0
			uint8 *val = (uint8 *)((uint32)(regs->es << 4) + regs->_edx.dx);
			for (uint8 i = 0; i < 16; i++) {
				inb(VGA_INPUT_STATUS_1);
				outb(VGA_WRITE_ATTR_INDEX, i);
				outb(VGA_WRITE_ATTR_DATA, *val);
			}
#endif
			cmd->vidcmd = BVIDCMD_SET_PALETTE;
			cmd->set_palette.paddr = (uint32)(regs->es << 4) + regs->_edx.dx;
			cmd->set_palette.len = 16;
			cmd->set_palette.vgareg = VGA_INPUT_STATUS_1;
			bhyve_cmd_set(BCMD_VIDEO, 0, 0);

			break;
		}
		case 0x03: // toggle intensity
			break;
		case 0x10: // set individual colour register
			outb(VGA_PALETTE_WRITE, regs->_ebx.bx);
			outb(VGA_PALETTE_DATA, regs->_edx.dh);
			outb(VGA_PALETTE_DATA, regs->_ecx.ch);
			outb(VGA_PALETTE_DATA, regs->_ecx.cl);
			break;
		case 0x12: // set block of colour registers
		{
#if 0
			uint32 total = regs->_ecx.cx;
			uint8 *val = (uint8 *)((uint32)(regs->es << 4) + regs->_edx.dx);
			outb(VGA_PALETTE_WRITE, regs->_ebx.bx);
			for (uint32 i = 0; i < total; i++) {
                                outb(VGA_PALETTE_DATA, *val);
				val++;
                                outb(VGA_PALETTE_DATA, *val);
				val++;
                                outb(VGA_PALETTE_DATA, *val);
				val++;
			}
#endif
			cmd->vidcmd = BVIDCMD_SET_PALETTE;
			cmd->set_palette.paddr = (uint32)(regs->es << 4) + regs->_edx.dx;
			cmd->set_palette.len = regs->_ecx.cx;
			cmd->set_palette.vgareg = VGA_PALETTE_WRITE;
			bhyve_cmd_set(BCMD_VIDEO, 0, 0);

			break;
		}
		default:
			printf("Unhandled int10-0x10 palette AL:%x\r\n", regs->_eax.al);
			break;
		}
		break;
	}
	case 0x11: // character generator
		switch (regs->_eax.al) {
		case 0x23: // set int43 to 8x8 double dot font
			break;
		case 0x30: // get font information
			break;
		}
		break;
	case 0x12: // special functions/alternate select
		switch (regs->_ebx.bl) {
		case 0x10: // get current display configuration
			break;
		case 0x20: // select alt print screen handler
			break;
		case 0x34: // cursor emulation
			break;
		default:
			regs->_ebx.bh = 0;
			regs->_ebx.bl = 0x03;
			regs->_ecx.cx = 0;
			break;
		}
	case 0x1a: // video display combination code (DCC)
		switch (regs->_eax.al) {
		case 0x00: // get DCC
			regs->_eax.al = 0x1a;
			regs->_ebx.bh = 0x00;
			break;
		case 0x01: // set DCC
			break;
		}
		break;
	case 0x1b: // get state information
		break;
	case 0x4f: // VBE, use pci_fbuf in bhyve to get FB address
	{
		switch (regs->_eax.al) {
		case 0x00: // get VBE info
			break;
		case 0x01: // get VBE mode info
			break;
		case 0x02: // set VBE mode
			break;
		case 0x03: // get VBE mode
			break;
		}
		break;
	}
	case 0xef: /* get video mode */
		regs->_eax.al = 0x3;
		regs->flags.CF = 0;
		break;
	default:
		printf("INT10 Unknown command 0x%x\r\n", regs->_eax.ax);
		regs->flags.CF = 1;
		return;
	}

ok:
	regs->flags.CF = 0;
out:
	return;
}


// Memory ops
void
handle_int15(callregs *regs)
{
	BDA *bda = (BDA *)bdaptr(0);

	switch (regs->_eax.ah) {
	case 0x41:
		regs->flags.CF = 1; // not supported
		break;
	default:
		printf("Unhandled int15 AX 0x%x\r\n", regs->_eax.ax);
		regs->flags.CF = 1;
		break;
	}
}

// KBD

#ifdef INT16_C
void
handle_int16(callregs *regs)
{
	BDA *bda = (BDA *)bdaptr(0);
	int advance_head = 0;

	switch (regs->_eax.ah) {
	case 0x00: // wait for keypress and read
		asm("sti");
		while (bda->key_buffer_head == bda->key_buffer_tail) {
			asm("nop; nop; nop; nop; nop; hlt;");
		}
		asm("cli");
		regs->_eax.ax = *(uint16 *)bdaptr(bda->key_buffer_head);
		bda->key_buffer_head += 2;
		if (bda->key_buffer_head > bda->key_buffer_end)
			bda->key_buffer_head = bda->key_buffer_start;
		break;
	case 0x01: // check keystroke status
		if (bda->key_buffer_head == bda->key_buffer_tail) {
			regs->flags.ZF = 1;
			break;
		}
		regs->_eax.ax = *(uint16 *)bdaptr(bda->key_buffer_head);
		regs->flags.ZF = 0;
		break;
	case 0x10:
		asm("sti");
		while (bda->key_buffer_head == bda->key_buffer_tail) {
			asm("nop; nop; nop; nop; nop; hlt;");
		}
		regs->_eax.ax = *(uint16 *)bdaptr(bda->key_buffer_head);
		bda->key_buffer_head += 2;
		if (bda->key_buffer_head > bda->key_buffer_end)
			bda->key_buffer_head = bda->key_buffer_start;
		asm("cli");
		break;
	case 0x11:
		if (bda->key_buffer_head == bda->key_buffer_tail) {
			regs->flags.ZF = 1;
		} else {
			regs->_eax.ax = *(uint16 *)bdaptr(bda->key_buffer_head);
			regs->flags.ZF = 0;
		}
		break;
	case 0x02: // check shift status
	case 0x12:
		regs->_eax.al = memget(BIOS_DATA_AREA + BDA_KBD_STS1) & 0xff;
		regs->_eax.ah = memget(BIOS_DATA_AREA + BDA_KBD_STS2) & 0xff;
		break;
	case 0x03: // set typematic rate
		break;
	case 0x04: // keyboard click adjustment
		break;
	case 0x05: // keyboard buffer write
		regs->_eax.al = 1;
		break;
	}
}

#endif


#ifdef INT1A_C
// RTC

#define RTC_ADDRESS_REG 0x70
#define RTC_DATA_REG    0x71

#define RTC_ADDRESS_SECONDS           0   // R/W  Range 0..59
#define RTC_ADDRESS_SECONDS_ALARM     1   // R/W  Range 0..59
#define RTC_ADDRESS_MINUTES           2   // R/W  Range 0..59
#define RTC_ADDRESS_MINUTES_ALARM     3   // R/W  Range 0..59
#define RTC_ADDRESS_HOURS             4   // R/W  Range 1..12 or 0..23 Bit 7 is AM/PM
#define RTC_ADDRESS_HOURS_ALARM       5   // R/W  Range 1..12 or 0..23 Bit 7 is AM/PM
#define RTC_ADDRESS_DAY_OF_THE_WEEK   6   // R/W  Range 1..7
#define RTC_ADDRESS_DAY_OF_THE_MONTH  7   // R/W  Range 1..31
#define RTC_ADDRESS_MONTH             8   // R/W  Range 1..12
#define RTC_ADDRESS_YEAR              9   // R/W  Range 0..99
#define RTC_ADDRESS_REG_A             10  // R/W[0..6]  R0[7]
#define RTC_ADDRESS_REG_B             11  // R/W
#define RTC_ADDRESS_REG_C             12  // RO
#define RTC_ADDRESS_REG_D             13  // RO
#define RTC_ADDRESS_CENTURY           50  // R/W  Range 19..20 Bit 8 is R/W

static inline uint8
rtc_read(uint8 addr) {
	outb(RTC_ADDRESS_REG, addr);
	return inb(RTC_DATA_REG);
}

static inline void
rtc_write(uint8 addr, uint8 val) {
	outb(RTC_ADDRESS_REG, addr);
	outb(RTC_DATA_REG, val);
}

#define U8_BCD(x) ((x / 10) << 4 | (x % 10))

void
handle_int1a(callregs *regs)
{
	BDA *bda = (BDA *)bdaptr(0);
	uint8 v;

	switch (regs->_eax.ah) {
	case 0x00:	/* get RTC counter */
		regs->_eax.al = 0;
		regs->_ecx.cx = bda->timer_counter >> 16;
		regs->_edx.dx = bda->timer_counter & 0xffff;
		regs->flags.CF = 0;
		//printf("RTC COUNTER %u %u\r\n", read_timer(), bda->timer_counter);
		break;
	case 0x01:
		bda->timer_counter = ((uint32)regs->_ecx.cx << 16) | regs->_edx.dx;
		//printf("RTC SET COUNTER %u\r\n", bda->timer_counter);
		break;
	case 0x02:	/* get RTC time */
		v = rtc_read(RTC_ADDRESS_HOURS);
		regs->_ecx.ch = U8_BCD(v);
		regs->_ecx.cl = rtc_read(RTC_ADDRESS_MINUTES);
		regs->_edx.dh = rtc_read(RTC_ADDRESS_SECONDS);
		regs->_edx.dl = 0; // no DST for now
		//printf("RTC-TIME: %u:%u:%u\r\n", regs->_ecx.ch, regs->_ecx.cl, regs->_edx.dl);
		regs->flags.CF = 0;
		break;
	case 0x03:	/* set RTC time */
		rtc_write(RTC_ADDRESS_HOURS, regs->_ecx.ch);
		rtc_write(RTC_ADDRESS_MINUTES, regs->_ecx.cl);
		rtc_write(RTC_ADDRESS_SECONDS, regs->_edx.dh);
		break;
	case 0x04:  /* get RTC date */
		v = rtc_read(RTC_ADDRESS_CENTURY);
		regs->_ecx.ch = U8_BCD(v);
		v = rtc_read(RTC_ADDRESS_YEAR);
		regs->_ecx.cl = U8_BCD(v);
		v = rtc_read(RTC_ADDRESS_MONTH);
		regs->_edx.dh = U8_BCD(v);
		v = rtc_read(RTC_ADDRESS_DAY_OF_THE_MONTH);
		regs->_edx.dl = U8_BCD(v);
		regs->flags.CF = 0;
		break;
	case 0x05:  /* set RTC date */
		rtc_write(RTC_ADDRESS_CENTURY, regs->_ecx.ch);
		rtc_write(RTC_ADDRESS_YEAR, regs->_ecx.cl);
		rtc_write(RTC_ADDRESS_MONTH, regs->_edx.dh);
		rtc_write(RTC_ADDRESS_DAY_OF_THE_MONTH, regs->_edx.dl);
		break;
	case 0x0f:  /* initialize RTC */
		regs->flags.CF = 0;
		break;
	case 0xB1:	/* PCI function ID */
		regs->_edx.edx = 'P' | 'C' << 8 | 'I' << 16 | ' ' << 24;
		regs->_eax.ax = 0;
		regs->_ebx.bl = 0x02;
		regs->_ebx.bh = 0x10;
		regs->flags.CF = 0;
		break;
	case 0xBB:  /* TPM */
		regs->_eax.eax = 0;
		regs->flags.CF = 0;
		break;
	default:
		printf("INT1a UNHANDLED %x\r\n", regs->_eax.ah);
		break;
	}
}

#endif // INT1A_C
