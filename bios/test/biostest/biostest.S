/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2021, Leon Dang
 *
 * Test suite for bhyve bios
 *
 * Memory map used by this test:
 * 0x0500  - 0x7BFF       - RAM for stack
 * 0x7C00  - 0x7DFF  - boot sector gets loaded here by BIOS
 * 0x7E00  - 0x7FFFF - RAM
 *    0x8000  - 0x7FFFF   - global variables
 *    0x10000 - 0x7FFFF   - test suite code to be loaded into here
 * 0x80000 - 0xFFFFF - BIOS ROM
 */

#define CODE_START_SEG 0x7C0
#define REAL_SEG       0x1000
#define REAL_START     (REAL_SEG << 4)

	.code16

	.section .start, "ax"
	.globl _start
_start:
	ljmp    $CODE_START_SEG, $(_startjmp-_start)
_startjmp:
	mov	%cs, %ax
	mov	%ax, %ds
	mov	$(post_msg), %ax
	call	str_to_com1

	// TODO Get test image size - int13h, for now assume 128KB image size

	// Set stack segment
	mov	$0x700, %ax
	mov 	%ax, %ss
	mov	$0xBFF, %eax
	mov	%eax, %esp

        // Extended read test XXX - for now image must be built as 128kb
	mov     $(boot_dap - _start), %si
	mov     $0x80, %dx // disk 0
        mov     $0x42, %ah // extended read
        int     $0x13
	jc      print_error

	mov	$(load_ok), %ax
	call	str_to_com1

	ljmp    $REAL_SEG, $(real_start-_start)
print_error:
	mov	$(load_failed), %ax
	call	str_to_com1

// prints a string (in ax) to the serial port
str_to_com1:
	mov     %ax, %bx
	mov     $0x3f8, %dx
_1:
	mov	%ds:(%bx), %al
	test    %al, %al
	je      _2
	out     %al, %dx
	inc     %bx
	jmp     _1
_2:
	ret
	
// Boot read sectors
boot_dap:
	.byte   0x10  // size of DAP
	.byte   0x00
	.word   256   // sectors (256 = 128kB)
	.word   0x0000, 0x1000   // segment:offset destination
	.word   0, 0, 0, 0       // LBA 0

post_msg:
	.string "bhyve bios test\r\n"
load_failed:
	.string "suite load failed\r\n"
load_ok:
	.string "suite loaded successfully\r\n"
real_msg:
	.string "TEST SUITE STARTING\r\n"

	// This must be at byte 510
_boot_sig:
	.fill   (510 - (_boot_sig - _start)), 1, 0
	.byte   0x55, 0xaa

	//
	// Test suite code starts here.
	//
real_start:
	mov	%cs, %ax
	mov	%ax, %ds
	mov	$(real_msg), %ax
	call	str_to_com1

	hlt


	// Signature end to let .lds know where to place this in the payload.
	.section .suite_end, "ax"
	.globl _suite_end
_suite_end:
	.fill	16, 1, 0
	
