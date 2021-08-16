/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2020 Leon Dang.
 *
 * Memory access functions. These must be called with "calll".
 */

/*
 * memxfer(u32 to, u32 from, u32 len)
 */
	.globl memxfer
memxfer:
	push  %ebp
	mov   %esp, %ebp
	push  %ds
	push  %es
	push  %esi
	push  %edi
	pushf

	xor   %ax, %ax
	mov   %ax, %ds
	mov   %ax, %es

	cld

	// ds:si -> es:di
	mov   0x8(%ebp), %edi   // to
	mov   0xc(%ebp), %esi   // from
	mov   0x10(%ebp), %ecx  // len
	rep   movsb %ds:(%esi),%es:(%edi)

	popf
	pop   %edi
	pop   %esi
	pop   %es
	pop   %ds
	pop   %ebp
	retl

/*
 * read 32-bit value at address
 * memread(u32 addr)
 */
	.globl memget
memget:
	mov    0x4(%esp), %eax
	push   %ds
	push   %esi

	mov    %eax, %esi
	xor    %ax, %ax
	mov    %ax, %ds
	mov    %ds:(%esi), %eax

	pop    %esi
	pop    %ds
	retl

	.globl addrptr
addrptr:
	movl	0x4(%esp), %eax
	retl

	.globl bdaptr
bdaptr:
	movl	0x4(%esp), %eax
	addl	$0x400, %eax
	retl



/*
 * Set value at address.
 * memset(u32 addr, u32 val, u32 bits)
 */
	.globl memset
memset:
	push   %ebp
	mov    %esp, %ebp
	push   %ds
	mov    0x8(%ebp),  %eax  // address
	mov    0xc(%ebp),  %ebx  // value
	mov    0x10(%ebp), %ecx  // bits

	pushw  $0x0000
	popw   %ds
	cmp    8, %ecx
	jne    memset.16
	movb   %bl, %ds:(%eax)
	jmp    memset.done
memset.16:
	cmp    16, %ecx
	jne    memset.32
	movw   %bx, %ds:(%eax)
	jmp    memset.done
memset.32:
	movl   %ebx, %ds:(%eax)
memset.done:
	pop    %ds
	pop    %ebp
	retl


