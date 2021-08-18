/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2020 Leon Dang.
 *
 * Portions of this code copied from 8086tiny: www.megalith.co.uk/8086tiny
 *   Copyright 2013-14, Adrian Cable (adrian.cable@gmail.com)
 *   This work is licensed under the MIT License. See included LICENSE.TXT.
 */

#define __ASM__
#include "microboot.h"

/*
 * Stack segment as part of pre-guest OS boot only. Not used once the OS loads.
 */
#define STACK_SEG 0x1000
#define STACK_TOP 0xFFFF

	.code16

	/* Registers in interrupt handler */
	.macro PUSHREGS
	pushal
	push  %es
	push  %ds
	push  %ss
	pushl %esp
	.endm

	.macro POPREGS
	popl %eax // esp
	pop  %ax  // ss
	pop  %ds
	pop  %es
	popal
	.endm

	/*
	 * Set stack pointer before calling c-function.
	 * Make segments = 0 because gcc compiles 16-bit code to use %ds/%es
	 * as segment register, and stack variables need to also be in
	 * absolute %ss:%esp locations as well to match the %ds:offsets.
	 */
	.macro SET_SP_CFUNC
	PUSHREGS
	xor      %eax, %eax
	mov      %ss, %ax
	shl      $4, %eax
	add      %eax, %esp
	xor      %bx, %bx
	mov      %bx, %ss
	mov      %bx, %ds
	mov      %bx, %es
	.endm

	/*
	 * Restore stack pointer after calling c-function. Must be called before
	 * popping registers. SS and ESP were saved on the stack.
	 */
	.macro RESTORE_SP_CFUNC
	movw	 %ss:4(%esp), %bx  // ss
	movl	 %ss:(%esp), %eax  // esp
	sub      $4, %eax          // value of esp pushed was higher in the stack
	mov      %eax, %esp
	mov      %bx, %ss
	POPREGS
	.endm

	// Long call to C function (pushes 16bit CS, 32bit EIP)
	.macro call_c fn
	calll	\fn
	.endm

        /*
         * Prepare to call a cfunction without arguments pushed.
         * Requires setting general seg registers to 0
         */      
        .macro SET_SP_CFUNC_NOARGS
	mov      %ds, %bx
	mov      $(BIOS_VARS_SEG), %ax
	mov      %ax, %ds
	mov      %bx, %ds:BHYVE_VARS_DS
	mov      %es, %ds:BHYVE_VARS_ES
	mov      %ss, %ds:BHYVE_VARS_SS
	mov      %esp, %ds:BHYVE_VARS_ESP

	xor      %eax, %eax
	mov      %ss, %ax
	shl      $4, %eax
	add      %eax, %esp
	xor      %bx, %bx
	mov      %bx, %ss
	mov      %bx, %ds
	mov      %bx, %es
        .endm

        .macro  RESTORE_SP_CFUNC_NOARGS
	mov     $(BIOS_VARS_SEG), %ax
	mov     %ax, %ds
	mov     %ds:BHYVE_VARS_ES, %ax
	mov     %ax, %es
	mov     %ds:BHYVE_VARS_SS, %ax
	mov     %ax, %ss
	mov     %ds:BHYVE_VARS_ESP, %eax
	mov     %eax, %esp
	mov     %ds:BHYVE_VARS_DS, %ax
	mov     %ax, %ds
        .endm    

	.section .start, "ax"
	.globl _start
_bios_ram:
	// For variables and shared data with bhyve
	.fill BIOS_CODE_START, 1, 0

_start:
	// Initialize stack
	mov     $ STACK_TOP, %sp
	mov     $ STACK_SEG, %ax
	mov     %ax, %ss

	// DEBUG PRINT CS:IP
	call get_ip
get_ip:
	pop	%ax

	// Test long jump cs:offset to make sure code block is ok
	ljmp	$SEG_BIOS, $_jmptest
_jmptest:

	// Initialize register
	mov	$0x0000, %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %fs
	mov	%ax, %gs

	cli

	// Enable A20
	mov     $0xd1, %al
	out     %al, $0x64
	mov     $0x03, %al
	out     %al, $0x60

	// Setup unreal mode for %ds, %ss, %es
	// otherwise 32-bit offsets can't be used
	call set_unrealmode_seg

#if 0
	// test unreal mode (00:0x10f000)
	mov     $0x10f000, %eax
	movw	$0x1234, %ds:(%eax)
	mov     %ds:(%eax), %bx

	// test 1MB+ write with segment 0xffff
	push	%ds
	mov	$0xffff, %ax
	mov	%ax, %ds
	mov	$0x500, %si
	movw    $0x1234, %ds:(%si)
	mov	%ds:(%si), %bx
	pop	%ds
#endif

	// Setup BIOS data area
	push    %ds
	mov     $SEG_BIOS, %bx
	mov     %bx, %ds
	mov     $0x40, %ax
	mov     %ax, %es
	mov     $0x0, %di
	mov     $bios_data, %si
	mov     $0x100, %cx
	rep     movsb

	// Setup BIOS variables location to be shared with bhyve
	mov     $BIOS_VARS_SEG, %bx
	mov     %bx, %ds
	movw	$bios_config_tbl, %ds:0
	pop     %ds

	// Setup shared page for bhyve ioport-hypercall
	mov     $ BHYVE_IO_PORT, %dx

	// Set seq and request bhyve to initialize
	push    %es
	mov     $BHYVE_CMD_BUF_SEG, %ax
	mov     %ax, %es
	xor     %eax, %eax
	movw    $0x01, %es:0x00(%eax)     // sequence number
	movw    $0x01, %es:0x02(%eax)     // setup command
	mov     $0x02, %al
	out     %al, %dx

	// XXX test bhyve print command
	movw    $0x02, %es:0x00(%eax)     // sequence number
	movw    $0x05, %es:0x02           // command
	movb    $'m',  %es:0x08           // args = string(mub00t)
	movb    $'u',  %es:0x09
	movb    $'b',  %es:0x0a
	movb    $'0',  %es:0x0b
	movb    $'0',  %es:0x0c
	movb    $'t',  %es:0x0d
	movb    $'\r', %es:0x0e
	movb    $'\n', %es:0x0f
	movb    $0x00, %es:0x10
	mov     $0x02, %al
	mov     $BHYVE_IO_PORT, %dx
	out     %al, %dx
	pop     %es

        // Test bhyve print calls in C
        push    $0xbeeb   // artificial intr-vector in structure
        SET_SP_CFUNC
        pushl   %esp      // pointer to "struct callregs"
        call_c  bhyve_test_call
        popl    %eax      // pointer to "struct callregs"
        RESTORE_SP_CFUNC
        pop     %ax       // 0xbeeb intr-vector

	// Erase VGA text area, and set fg white, bg black (0x0f20)
	push    %es
	pushw   $0xb800
	pop     %es
	mov     $(0xbffff-0xb8000/2), %ecx
	mov	$0x0f20, %bx
	call    memset16
	pop     %es

	cld

	/*
	 * Setup interrupt vector table.
	 *
	 * zero out table
	 */
	mov	$0, %ax
	mov	%ax, %es
	mov	$0, %di
	mov	$512, %ecx
	rep	stosw

	// copy table DS:SI -> ES:DI
	push	%ds
	mov	$ SEG_BIOS, %ax
	mov	%ax, %ds
	mov	$int_table, %si
	mov	$0, %di
	xor	%ecx, %ecx
	mov	$(itbl_size-int_table), %cx
	rep	movsb
	pop	%ds

        // Configure RTC (see cmos_ram.html for bits in registers)
#if 0
	mov	$0x0a, %al     // disable NMI
	outb	%al, $0x70
	mov	$0x26, %al
	outb	%al, $0x71
#endif

	mov	$0x0b, %al     // enable 24 hour, square wave
	outb	%al, $0x70
	mov	$0x0a, %al
	outb	%al, $0x71

	mov     $0x0c, %al     // read reg c and d
	out	%al, $0x70
	inb	$0x71, %al
	mov     $0x0d, %al
	out	%al, $0x70
	inb	$0x71, %al

	// Set periodic RTC timer frequency
	mov	$0x70, %dx
	mov	$0x0A, %al    // select status register A
	outb    %al, %dx
	inb	$0x71         // previous value
	and     $0xf0, %al    // previous value & 0xf0
	or	$0x08, %al    // set rate; frequency = (32768 >> (rate - 1))
	mov     $0x71, %dx    // write new value with rate
	outb    %al, %dx

	// Enable interrupts for timer and kbd
	call	init_pic
	call	init_pit
	call	enable_kbd

	// Enable int8 IRQ0 and kbd handlers
	mov	0xfc, %al
	outb	%al, $0x21
	mov	0xfe, %al
	outb	%al, $0xa1

	SET_SP_CFUNC_NOARGS
	call_c	bhyve_load_bootsect
	RESTORE_SP_CFUNC_NOARGS

	// Initialize registers that boot loaders expect
	mov	$0x80, %edx  // boot disk number
	xor     %eax, %eax
	xor     %ebx, %ebx
	xor     %ecx, %ecx
	xor     %esi, %esi
	xor     %edi, %edi

	// Jump into guest boot sector
	ljmpw   $0, $0x7c00

 	////////////////////// END OF BIOS INIT/////////////////////


	/*
 	 * Initialize 8259 master and slaves to send interrupts to the
	 * right int handler.
	 */
init_pic:
	in	$0x21, %al   // save master IMR
	push    %ax
	in	$0xa1, %al   // save slave IMR
	push	%ax
	mov	$0x11, %al   // ICW1 (cascade)
	outb	%al, $0x20   //   to master
	outb	%al, $0xa0   //   to slave
	mov	$8, %al      // ICW2 (int vector offset 8)
	outb	%al, $0x21   //   to master
	mov	$0x70, %al   // ICW2 (int vector offest 0x70)
	outb	%al, $0xa1   //   to slave
	mov	$4, %al      // ICW3 (slave at IRQ2 [0b0100])
	outb	%al, $0x21   //   to master
	mov	$2, %al      // ICW3 (slave's cascade identity)
	outb	%al, $0xa1   //   to slave
	mov	$1, %al      // ICW4
	outb	%al, $0x21   //   to master,
	outb	%al, $0xa1   //   to slave
	pop	%ax          // Restore slave
	outb	%al, $0xa1   //   IMR
	pop	%ax          // Restore master
	outb	%al, $0x21   //   IMR
	ret

init_pit:
	movb	$0b00110110, %al // mode 3 square wave, access mode lobyte/hibyte
	outb	%al, $0x43
	mov	$(1193181 / 50), %ax  // freq = (1193181 clock rate / N)
	out	%al, $0x40
	mov	%ah, %al
	out	%al, $0x40
	ret

enable_kbd:
	mov	$0xf4, %al  // PS2 enable
	out	%al, $0x60

	mov	$0x20, %al // atkbd get command
	out	%al, $0x64
	inb	$0x60, %al

	or	$0x61, %al // toggle kbd enable intr, translate, aux disable
	mov	%al, %ah

	mov	$0x60, %al // atkbd set command
	out	%al, $0x64
	mov	%ah, %al
	out	%al, $0x60
	mov	$0xae, %al // atkbd enable
	out	%al, $0x64
	int	$0x1b

	ret

printint:
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	pushl	%ebx
	mov     $ 0x3f8, %dx
	mov	$8, %cx
	mov	%eax, %ebx
printint_1:
	roll	$4, %ebx
	mov	%bl, %al
	and	$0x0f, %al
	add	$0x30, %al
	cmp	$0x39, %al
	jna     printint_2
	add	$0x7, %al
printint_2:
	out     %al, %dx

	dec	%cx
	jnz	printint_1

	mov	$'|', %al
	out     %al, %dx

	pop	%ebx
	pop	%edx
	pop	%ecx
	pop	%eax
	ret
	
// Flat 32-bit address space for unreal mode
biggdt:
	.word (gdt_end-gdt-1)
	.long (SEG_BIOS << 4) + gdt
gdt:
	.long 0, 0
	.long 0x0000FFFF,0x00CF9200     // data
gdt_end:

smallgdt:
	.word (smgdt_end-smgdt- 1)
	.long (SEG_BIOS << 4) + smgdt
smgdt:
	.long 0, 0
	.long 0x0000FFFF,0x00009200     // data
smgdt_end:

/* Switch to unreal mode */
set_unrealmode_seg:
	push    %es
	push    %ds
	push    %eax
	push    %ebx
	push    %bp
	mov     %ss, %bp

	mov     $SEG_BIOS, %ax
	mov     %ax, %ds
	lgdt    %ds:biggdt
	mov     %cr0, %eax
	or      $1, %eax
	mov     %eax, %cr0
	jmp     _su_pmode
_su_pmode:
	mov     $0x08, %bx // 32-bit data segment selection
	mov     %bx, %ds
	mov     %bx, %es
	mov     %bx, %ss

	// Back to real mode
	and     $0xfe, %al
	mov     %eax, %cr0

	mov     %bp, %ss
	pop     %bp
	pop     %ebx
	pop     %eax
	pop     %ds
	pop     %es
	ret

/* Switch back to regular real mode */
set_small_realmode_seg:
	push    %es
	push    %ds
	push    %eax
	push    %ebx
	push    %bp

	mov     %ss, %bp

	mov     $SEG_BIOS, %ax
	mov     %ax, %ds
	lgdt    %ds:smallgdt
	mov     %cr0, %eax
	or      $1, %eax
	mov     %eax, %cr0
	jmp     _ss_pmode
_ss_pmode:
	mov     $0x08, %bx // 16-bit data segment selection
	mov     %bx, %ds
	mov     %bx, %es
	mov     %bx, %ss

	// Back to real mode
	and     $0xfe, %al
	mov     %eax, %cr0

	mov     %bp, %ss

	pop     %bp
	pop     %ebx
	pop     %eax
	pop     %ds
	pop     %es
	ret


eoi_master:
	push    %ax
	mov	$0x20, %al
	out	%al, $0x20
	pop     %ax
	ret
eoi_all:
	push    %ax
	mov     $0x20, %al
	out     %al, $0xa0
	out     %al, $0x20
	pop     %ax
	ret


#include "mem.S"

# Interrupt handlers are located at CS SEG_BIOS


// Default interrupt handlers
int0:
int1:
int2:
int3:
int4:
int5:
int6:
int7:
intb:
intd:
inte:
intf:
int17:
int18:
int1b:
int1d:
	iret

int14:
	push %dx
	push %ax
	mov     $0x3f8, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	mov     $'I', %al
	out     %al, %dx
	mov     $'N', %al
	out     %al, %dx
	mov     $'T', %al
	out     %al, %dx
	mov     $'1', %al
	out     %al, %dx
	mov     $'4', %al
	out     %al, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	pop %ax
	pop %dx
	mov $0x00, %ah  // line status
	mov $0x30, %al  // modem status

	iret

int_hw_dummy:
	//call    eoi_all
	call    eoi_master
	iret

intc:
	call    eoi_all
	iret

int9:
	pushw   $0x9
        jmp     int_hwcommon
inta:
	pushw	$0xa
	jmp     int_swcommon
int10:
	pushw   $0x10
	jmp     int_swcommon
int11: // get equipment list
	mov     %cs:equip, %ax
	iret
int12:  // return memory size
	mov     $0x27f, %ax  // 640k conventional memory
	iret
int13:
	pushw   $0x13
	//jmp     int_swcommon
	jmp     int_bhyvebios
int15:
	cmp     $0x41, %ah
 	jne     int15_bhyve
	pushw   $0x15
	jmp     int_swcommon
int15_bhyve:
	pushw   $0x15
	//jmp     int_swcommon
	jmp     int_bhyvebios

int19:
	// reboot...
	pushw   $0x19
	jmp     int_swcommon

int1a_c:
	pushw   $0x1a
	jmp     int_swcommon

int1c:
	iret
	//push    $0x1c
	//jmp     int_swcommon

int_bhyvebios: // bhyve bios hypercall
	// Save EAX, EDX to bios_vars (eax and edx used for I/O out)
	push    %bp
	mov	%sp, %bp
	pushl   %eax
	pushl   %ebx
	push    %ds
	mov	$BIOS_VARS_SEG, %bx
	mov	%bx, %ds
	movl    %eax, %ds:4
	movl    %edx, %ds:8
	movw    %ss:8(%bp), %ax
	movw    %ax, %ds:2           // flags
	pop     %ds
	popl	%ebx
	popl	%eax
	pop     %bp

	pushw	%ax
	popl	%eax  // clear stack. Set EAX = ax << 16 | int-vec;

	mov	$BHYVE_IO_PORT, %dx
	outl	%eax, %dx

	// Copy BIOS_VARS.flags to flags reg
	push	%bp
	mov	%sp, %bp
	push	%ax
	push    %ds
	mov	$BIOS_VARS_SEG, %ax
	mov	%ax, %ds
	movw    %ds:2, %ax
	mov	%ax, %ss:6(%bp)     // move flags to caller's frame
	pop     %ds
	pop	%ax
	pop	%bp
	iret

int_swcommon:
	cli
	cld
#if 0
	push %dx
	push %ax
	
	mov	%ss:4(%esp), %ax
	cmp     $0x1a, %ax
	je      skip_1avec

	mov     $0x3f8, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	mov     $'I', %al
	out     %al, %dx
	mov     $'N', %al
	out     %al, %dx
	mov     $'T', %al
	out     %al, %dx
	mov	%ss:4(%esp), %ax
	call    printint
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
skip_1avec:
	pop %ax
	pop %dx
#endif

	SET_SP_CFUNC
	jmp	int_common
int_hwcommon:
	cli
	cld
	SET_SP_CFUNC
	jmp	int_common

int_common:
	mov     %ds, %dx
	mov     $(BIOS_VARS_SEG), %ax
	mov	%ax, %ds
	mov	%dx, %ds:BHYVE_VARS_DS
	mov	%es, %ds:BHYVE_VARS_ES
	mov	%ss, %ds:BHYVE_VARS_SS
	mov	%esp, %ds:BHYVE_VARS_ESP
	sgdt    %ds:BHYVE_VARS_GDTR_LIM

	mov	%ds:BHYVE_VARS_GDTR_BASE, %eax
	shr     $16, %eax
	cmp	$0xf, %eax
	jge     skip_gdtr_copy

	// Copy gdtr to bios_vars
	xor     %ecx, %ecx
	mov	%ds:BHYVE_VARS_GDTR_LIM, %cx
	mov     %ds:BHYVE_VARS_GDTR_BASE, %eax

	add     $(2+4), %eax

	// from ds:si
	xor     %esi, %esi
	mov	%ax, %si
	shr	$16,%eax
	shl	$12,%eax
	mov	%ax, %ds

	// to es:di
	mov	$BIOS_VARS_SEG, %ax
	mov	%ax, %es
	xor     %edi, %edi
	mov	$BHYVE_VARS_GDT_COPY, %di
	rep     movsb

	// override guest GDT
	call    set_unrealmode_seg

skip_gdtr_copy:
	xor %eax, %eax
	mov %ax, %ds
	mov %ax, %es

	// push regs
	mov     %esp, %eax
	pushl	%eax
	call_c  int_handler
	popl    %ebx

	cmp	$0x0, %ax
	je	_ih_noeoi

	call    eoi_master
_ih_noeoi:
	RESTORE_SP_CFUNC

	// drop interrupt vector code
	add     $2, %sp

	iret


// gcc-gas -Os bug: if this is not called, then the argument in the function call
// references the wrong stack address!
        .globl do_nothing_with_arg
do_nothing_with_arg:
	retl

// memset: es: segment, bx: pattern, ecx: len
memset16:
	xor  %eax, %eax
memset16.1:
	mov  %bx, %es:0(%eax)
	add  $2, %eax
	loop memset16.1
	ret

/*
 * Interrupt handlers in assembly
 */

	.globl read_timer
read_timer:
	push   %es
	mov    $0x40, %ax
	mov    %ax, %es
	movl   %es:(clk_dtimer_lo - bios_data), %eax
	pop    %es
	retl

	.globl read_com1
read_com1:
	push   %es
	mov    $0x40, %ax
	mov    %ax, %es
	movl   %es:0x0, %eax
	pop    %es
	retl

// 8254 Timer interrupt, every 1/18.2 seconds
int8:
	push	%eax
	push	%ds

	push    %edx
	mov     $0x70, %dx
	mov     $0x0C, %al
	outb    %al, %dx
	inb     $0x71
	pop     %edx

	// bda->timer_counter++
	mov     $0x40, %ax
	mov     %ax, %ds
	addl    $1, %ds:(clk_dtimer_lo - bios_data)

	movl	%ds:(clk_dtimer_lo - bios_data), %eax
	// call printint

	// Test 24-hour count; clear timer values if reached
	cmp     $0x001800b0, %eax
	jl	inc_done

	xor	%eax, %eax
	movl	%eax, %ds:(clk_dtimer_lo - bios_data)
	movb	$1, %ds:(clk_rollover - bios_data)
inc_done:
	sti
	int	$0x1c           // raise sw interrupt handler to do its thing

	pop	%ds
	pop	%eax

	call    eoi_master
        iret


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


int1a:
#if 0
	push %dx
	push %ax
	mov     $0x3f8, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	mov     $'I', %al
	out     %al, %dx
	mov     $'N', %al
	out     %al, %dx
	mov     $'T', %al
	out     %al, %dx
	mov	$'1', %ax
	out     %al, %dx
	mov	$'A', %ax
	out     %al, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	pop %ax
	pop %dx
#endif

	push    %bp
	mov     %sp, %bp
	push    %ds
	push    %bx
	push    %si

	mov     $0x40, %bx
	mov     %bx, %ds

	cmp     $0x00, %ah
	je      int1a_00
	cmp     $0x01, %ah
	je      int1a_01
	cmp     $0x02, %ah
	je      int1a_02
	cmp     $0x03, %ah
	je      int1a_03
	cmp     $0x04, %ah
	je      int1a_04
	cmp     $0x05, %ah
	je      int1a_05
	cmp     $0x06, %ah
	je      int1a_06
	cmp     $0x07, %ah
	je      int1a_07
	cmp     $0x0f, %ah
	je      int1a_0f
	cmp     $0xb1, %ah
	je      int1a_b1
	cmp     $0xbb, %ah
	je      int1a_bb

	// call stack: 2 cs, 4 ip, 6 flags
	mov     %ss:6(%bp), %bx
	or      $0x01, %bx                    // set CF flag
	mov     %bx, %ss:6(%bp)

	jmp     int1a_ret

int1a_00: // get rtc counter
	xor     %al, %al
	mov     %ds:(clk_dtimer_hi - bios_data), %cx
	mov     %ds:(clk_dtimer_lo - bios_data), %dx
	jmp     int1a_ret

int1a_01: // set rtc counter
	mov     %cx, %ds:(clk_dtimer_hi - bios_data)
	mov     %dx, %ds:(clk_dtimer_hi - bios_data)
	jmp     int1a_ret

int1a_02: // get RTC time
	push    %ax
	mov	$RTC_ADDRESS_HOURS, %al
	call    rtc_read
	call    u8_bcd
	mov     %al, %ch
	mov	$RTC_ADDRESS_MINUTES, %al
	call    rtc_read
	call    u8_bcd
	mov     %al, %cl
	mov	$RTC_ADDRESS_SECONDS, %al
	call    rtc_read
	call    u8_bcd
	mov     %al, %dh
	mov     $0, %dl   // no DST
	pop     %ax
	jmp     int1a_ret

int1a_03: // set RTC time
	push    %ax
	mov     $RTC_ADDRESS_HOURS, %al
	mov     %ch, %ah
	call    u8_dcb
	call    rtc_write
	mov     $RTC_ADDRESS_MINUTES, %al
	mov     %cl, %ah
	call    u8_dcb
	call    rtc_write
	mov     $RTC_ADDRESS_SECONDS, %al
	mov     %dh, %ah
	call    u8_dcb
	call    rtc_write
	pop     %ax
	jmp     int1a_ret

int1a_04: // get RTC date
	push    %ax
	mov     $RTC_ADDRESS_CENTURY, %al
	call    rtc_read
	call    u8_bcd
	mov     %al, %ch
	mov     $RTC_ADDRESS_YEAR, %al
	call    rtc_read
	call    u8_bcd
	mov     %al, %cl
	mov     $RTC_ADDRESS_MONTH, %al
	call    rtc_read
	call    u8_bcd
	mov     %al, %dh
	mov     $RTC_ADDRESS_DAY_OF_THE_MONTH, %al
	call    rtc_read
	call    u8_bcd
	mov     %al, %dl
	pop     %ax
	jmp     int1a_ret

int1a_05: // set RTC date
	push    %ax
	mov     $RTC_ADDRESS_CENTURY, %al
	mov     %ch, %ah
	call    u8_dcb
	call    rtc_write
	mov     $RTC_ADDRESS_YEAR, %al
	mov     %cl, %ah
	call    u8_dcb
	call    rtc_write
	mov     $RTC_ADDRESS_MONTH, %al
	mov     %dh, %ah
	call    u8_dcb
	call    rtc_write
	mov     $RTC_ADDRESS_DAY_OF_THE_MONTH, %al
	mov     %dl, %ah
	call    u8_dcb
	call    rtc_write
	pop     %ax
	jmp     int1a_ret

int1a_06: // set RTC alarm
#if 1
	push %dx
	push %ax
	mov     $0x3f8, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	mov     $'A', %al
	out     %al, %dx
	mov     $'L', %al
	out     %al, %dx
	mov     $'A', %al
	out     %al, %dx
	mov	$'R', %ax
	out     %al, %dx
	mov	$'M', %ax
	out     %al, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	pop %ax
	pop %dx
#endif

	jmp     int1a_ret

int1a_07: // cancel RTC alarm
	jmp     int1a_ret

int1a_0f: // initialize RTC
	jmp     int1a_ret

int1a_b1: // PCI function ID
	mov     $('P' | 'C' << 8 | 'I' << 16 | ' ' << 24), %edx
	mov     $0x0, %ax
	mov     $0x2, %bl
	mov     $0x10, %bh
	jmp     int1a_ret

int1a_bb: // TPM
	mov     $0x00, %eax
	jmp     int1a_ret

int1a_ret:
	pop     %si
	pop     %bx
	pop     %ds
	pop     %bp
	call    eoi_all
	iret


// convert byte in %al to BCD format, value is returned in %al
u8_bcd:
	push   %edx
	push   %ecx
	push   %eax
	and    $0xff, %eax
	mov    $10, %dl
	div    %dl
	mov    %al, %cl
	shl    $0x4, %cl    // cl = (al / 10) << 4
	mov    %ah, %al     // ah = al % 10
	or     %ecx, %eax   // eax |= cl
	mov    %eax, %ecx
	pop    %eax
	mov    %cl, %al
	pop    %ecx
	pop    %edx
	ret

// Convert %ah decimal back to binary: v = ((v >> 4) * 10) + (v & 0xf)
u8_dcb:
	push   %edx
	push   %ecx

	mov    %al, %ch    // preserve %al
	mov    %ah, %cl    // this contains a BCD
	and    $0x0f, %cl

	mov    %ah, %al
	mov    $0, %ah
	shr    $0x4, %al
	mov    $10, %dl
	mul    %dl
	add    %al, %cl

	mov    %cl, %ah
	mov    %ch, %al

	pop    %ecx
	pop    %edx
	ret

// %al contains address to read from; result in %al
rtc_read:
	outb   %al, $RTC_ADDRESS_REG
	inb    $RTC_DATA_REG, %al
	ret

// %al registers to write to, %ah value to write
rtc_write:
	outb   %al, $RTC_ADDRESS_REG
	mov    %ah, %al
	outb   %al, $RTC_DATA_REG
	ret

int16:
#if 0
	push %dx
	push %ax
	mov     $0x3f8, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	mov     $'I', %al
	out     %al, %dx
	mov     $'N', %al
	out     %al, %dx
	mov     $'T', %al
	out     %al, %dx
	mov	$'1', %ax
	out     %al, %dx
	mov	$'6', %ax
	out     %al, %dx
	mov     $'\r', %al
	out     %al, %dx
	mov     $'\n', %al
	out     %al, %dx
	pop %ax
	pop %dx
#endif
	push    %bp
	mov     %sp, %bp
	push    %ds
	push    %bx
	push    %si

	mov     $0x40, %bx
	mov	%bx, %ds

	cmp     $0x00, %ah
	je	int16_00
	cmp     $0x01, %ah
	je	int16_01
	cmp     $0x02, %ah
	je	int16_02
	cmp     $0x03, %ah
	je	int16_03
	cmp     $0x04, %ah
	je	int16_04
	cmp     $0x05, %ah
	je	int16_05
	cmp     $0x10, %ah
	je	int16_10
	cmp     $0x11, %ah
	je	int16_11
	cmp     $0x12, %ah
	je	int16_12

	// call stack: 2 cs, 4 ip, 6 flags
	mov     %ss:6(%bp), %bx
	or      $0x01, %bx                    // set CF flag
	mov     %bx, %ss:6(%bp)

	jmp     int16_ret

int16_00:  // Wait for keypress and read into %ax
int16_10:
	mov	%ds:(kbbuf_head-bios_data), %bx   // head unchanged until we consume it
	mov     %bx, %si                          // %si = kbdbuf head pointer
	sti

int16_wait_for_key:                               // loop while kbbuf_head == kbbuf_tail
	mov	%ds:(kbbuf_tail-bios_data), %ax
	cmp	%bx, %ax
	jne     int16_end_wait_for_key            // key avail: kbbuf_head != kbbuf_tail
	nop
	hlt
	jmp     int16_wait_for_key

int16_end_wait_for_key:
	cli
	mov     %ds:(%si), %ax                // ax = keystroke bda[keybuffer_head]
	add     $2, %bx                       // kbbuf_head += 2
	cmp	$(drivecal-bios_data-2), %bx  // if (kbbuf_head > kbbuf_end)
	jle     int16_kb_head_ok
	movw    $(kbbuf-bios_data), %ds:(kbbuf_head-bios_data) // kbbuf_head = kbbuf_start_ptr
	jmp     int16_nosetz
int16_kb_head_ok:
	movw	%bx, %ds:(kbbuf_head-bios_data)
	jmp     int16_nosetz

int16_01: // check keystroke status
int16_11:
	movw	%ds:(kbbuf_tail-bios_data), %ax
	movw	%ds:(kbbuf_head-bios_data), %bx
	cmp	%bx, %ax
	je      int16_setz
	mov     %bx, %si
	movw    %ds:(%si), %ax
	jmp     int16_nosetz

int16_03: // typematic rate
	jmp     int16_ret
int16_04: // keyboard click adjustment
	jmp     int16_ret
int16_05: // keyboard buffer write
	mov     $1, %al
	jmp     int16_ret

int16_02: // check shift status %al = sts1, %ah = sts2
int16_12:
	movw    %ds:(keyflags1-bios_data), %ax
	jmp     int16_ret

int16_setz:
	mov	%ss:6(%bp), %bx
	or      $0x40, %bx
	mov     %bx, %ss:6(%bp)
	jmp     int16_ret
int16_nosetz:
	mov	%ss:6(%bp), %bx
	and     $(~0x40), %bx
	mov     %bx, %ss:6(%bp)

int16_ret:
	pop     %si
	pop     %bx
	pop     %ds
	pop     %bp
	iret

int1e:
	.byte 0xdf # Step rate 2ms, head unload time 240ms
	.byte 0x02 # Head load time 4 ms, non-DMA mode 0
	.byte 0x25 # Byte delay until motor turned off
	.byte 0x02 # 512 bytes per sector
	.byte 18	# 18 sectors per track (1.44MB)
	.byte 0x1B # Gap between sectors for 3.5" floppy
	.byte 0xFF # Data length (ignored)
	.byte 0x54 # Gap length when formatting
	.byte 0xF6 # Format filler byte
	.byte 0x0F # Head settle time (1 ms)
	.byte 0x08 # Motor start time in 1/8 seconds

// Print string in %si to com1 port
out_com1:
	pusha
	cld
	mov $ 0x3f8, %dx
_oc_repeat:
	lods %ds:(%si),%al
	cmp $0, %al
	je _oc_done
        out     %al, %dx
	jmp _oc_repeat
_oc_done:
	popa
	ret

	.globl get_bios_config_tbl
get_bios_config_tbl:
	mov $bios_config_tbl, %eax
	retl

#
# Data
#
# Standard PC-compatible BIOS data area - to copy to 40:0

bios_data:

com1addr:       .word	0x03F8
com2addr:       .word	0x02F8
com3addr:       .word	0x03E8
com4addr:       .word	0x02E8
lpt1addr:       .word	0
lpt2addr:       .word	0
lpt3addr:       .word	0
lpt4addr:       .word	0
equip:          .word	0b0000010000000001   // number of serial ports at bits 11-9 (000 = none, 001 = 1, 010 = 2, 011  3, 100 = 4)
                .byte	0
memsize:        .word	0x280
                .byte	0
                .byte	0
keyflags1:      .byte	0
keyflags2:      .byte	0
                .byte	0
kbbuf_head:     .word	(kbbuf-bios_data)
kbbuf_tail:     .word	(kbbuf-bios_data)
kbbuf:          .fill   32,1,0
drivecal:       .byte	0
diskmotor:      .byte	0
motorshutoff:   .byte	0x07
disk_laststatus: .fill 8,1,0

bios_data_vid_start:            //  0x49
vid_mode:       .byte	0x03
vid_cols:       .word	80
vid_page_size:  .word	0x1000
vid_page_off:   .word	0
curpos_x:       .byte	0
curpos_y:       .byte	0
                .fill   7, 2, 0 # cursor pos for pages 
cur_v_end:      .byte	0x07
cur_v_start:    .byte	0x06
disp_page:      .byte	0
crtport:        .word	0x3d4
vid_3x8:        .byte	0x09
vid_3x9:        .byte	0
vid_rom_off:    .word	0
vid_rom_seg:    .word	0
biod_data_vid_end:

last_intr:      .byte	0
clk_dtimer_lo:  .word	0
clk_dtimer_hi:  .word	0
clk_rollover:   .byte	0
ctrl_break:     .byte	0
soft_rst_flg:   .word	0x1234
                .byte	0
num_hd:         .byte	0
                .byte	0
                .byte	0
                .long	0
com1_timeout:   .byte	27	// 500 ms
com2_timeout:   .byte	27	// 500 ms
com3_timeout:   .byte	27	// 500 ms
com4_timeout:   .byte	27	// 500 ms
kbbuf_start_ptr: .word  (kbbuf-bios_data)
kbbuf_end_ptr:  .word	(drivecal-bios_data-2)
vid_rows:       .byte	24      // at 40:84
                .byte	8	// scan lines per character
                .byte	0
vidmode_opt:    .byte	0x60    //0x96 // at 40:87 0x70
                .byte	0x09    // high-res 80x25,MDA
                .byte	0x81    // VGA active
video_card:     .byte	0x0c    // VGA, color monitor
                .byte	0
                .byte	0
                .byte	0
                .byte	0
                .byte	0
                .byte	0
                .byte	0
                .byte	0
                .byte	0
                .byte	0
                .byte	0
kb_mode:        .byte	0
kb_led:         .byte	0
                .byte	0
                .byte	0
                .byte	0
                .byte	0
boot_device:    .byte	0
crt_curpos_x:   .byte	0
crt_curpos_y:   .byte	0
key_now_down:   .byte	0
next_key_fn:    .byte	0
cursor_visible: .byte	1
esc_flag_last:  .byte	0
next_key_alt:   .byte	0
escape_flag:    .byte	0
noxlate_flg:    .byte	0
this_keystroke: .byte	0
                .byte	0
bios_ending:    .fill   (0xff-(bios_ending-com1addr)), 1, 0


int_table:
	.word int0
	.word SEG_BIOS
	.word int1
	.word SEG_BIOS
	.word int2
	.word SEG_BIOS
	.word int3
	.word SEG_BIOS
	.word int4
	.word SEG_BIOS
	.word int5
	.word SEG_BIOS
	.word int6
	.word SEG_BIOS
	.word int7
	.word SEG_BIOS
	.word int8
	.word SEG_BIOS
	.word int9
	.word SEG_BIOS
	.word int_hw_dummy
	.word SEG_BIOS
	.word int_hw_dummy
	.word SEG_BIOS
	.word intc
	.word SEG_BIOS
	.word int_hw_dummy
	.word SEG_BIOS
	.word inte
	.word SEG_BIOS
	.word int_hw_dummy
	.word SEG_BIOS
	.word int10
	.word SEG_BIOS
	.word int11
	.word SEG_BIOS
	.word int12
	.word SEG_BIOS
	.word int13
	.word SEG_BIOS
	.word int14
	.word SEG_BIOS
	.word int15
	.word SEG_BIOS
	.word int16
	.word SEG_BIOS
	.word int17
	.word SEG_BIOS
	.word int18
	.word SEG_BIOS
	.word int19
	.word SEG_BIOS
	//.word int1a_c
	.word int1a
	.word SEG_BIOS
	.word int_hw_dummy
	.word SEG_BIOS
	.word int_hw_dummy
	.word SEG_BIOS
	.word 0 //video_init_table
	.word 0 //SEG_BIOS
	.word int1e
	.word SEG_BIOS
itbl_end:
	//.fill (0x100 - itbl_end - int_table), 1, 0

itbl_size:
	.word (itbl_size-int_table)

video_init_table:
	# 6845 register values for 40x25 modes
	.byte	0x39, 0x28, 0x2d, 0x10, 0x1f, 0x06, 0x19, 0x1c, 0x02, 0x07, 0x66, 0x07, 0x00, 0x00, 0x00, 0x00
	# 6845 register values for 80x25 modes
	.byte	0x72, 0x50, 0x5a, 0x10, 0x1f, 0x06, 0x19, 0x1c, 0x02, 0x07, 0x66, 0x07, 0x00, 0x00, 0x00, 0x00
	# 6845 register values for graphics modes
	.byte	0x39, 0x28, 0x2d, 0x10, 0x7f, 0x06, 0x64, 0x70, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00
	# 6845 register values for 80x25 monochrome modes
	.byte	0x72, 0x50, 0x5a, 0x10, 0x1f, 0x06, 0x19, 0x1c, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00
	# wSize40x25
	.word	0x3E8
	# wSize80x25
	.word	0x7d0
	# wSizeLoRes
	.word	0x3e80
	# wSizeHiRes
	.word	0x3e80
	# abClmCnts (Text columns in each mode)
	.byte	0x28, 0x28, 0x50, 0x50, 0x28, 0x28, 0x50, 0x00
	# abModeCodes (port 3d8 values for each mode)
	.byte	0x0c, 0x08, 0x02, 0x09, 0x0a, 0x0e, 0x1a, 0x00 

	// BIOS Configuration Table - requested by INT 15h - C0h
	.globl bios_config_tbl
bios_config_tbl:
	.byte 0x08            // 16-bit size
	.byte 0x00
	.byte 0xFC            // model (IBM AT)
	.byte 0x00            // submodel
	.byte 0x01            // bios revision
	.byte (0 << 7) | /* DMA channel 3 */                           \
	      (1 << 6) | /* 2nd 8259 PIC installed */                  \
	      (1 << 5) | /* RTC installed */                           \
	      (0 << 4) | /* INT15h 4Fh called @INT 09h */              \
	      (0 << 3) | /* Wait for ext event (INT15h/41h support) */ \
	      (0 << 2) | /* Ext BIOS area allocated */                 \
	      (0 << 1) | /* Bus is Micro Channel */                    \
	      (0 << 0)   /* Dual bus system */
	.byte (0 << 7) | \
	      (1 << 6) | \
	      (0 << 5) | \
	      (0 << 4) | \
	      (0 << 3) | \
	      (0 << 2) | \
	      (0 << 1) | \
	      (0 << 0)
	.byte 0x00
	.byte 0x00
	.byte 0x00



/*
 * Reset vector is to be put in :0xFFF0 (end of INT-MAX-16)
 */
	.section .resetvec, "ax"
	.globl reset_vector
reset_vector:
	ljmp $SEG_BIOS, $(_start - _bios_ram) // if this doesn't work then the copy to 0xf0000 broken or corrupt
	//jmp _start            // relative jump only useful if doing pure EFI
reset_vector_end:
	.fill 16 - reset_vector_end - reset_vector, 1, 0
