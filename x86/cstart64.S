
#include "apic-defs.h"

.globl boot_idt

.globl idt_descr
.globl tss_descr
.globl gdt64_desc
.globl online_cpus
.globl cpu_online_count

ipi_vector = 0x20

max_cpus = MAX_TEST_CPUS

.bss

	. = . + 4096 * max_cpus
	.align 16
stacktop:

	. = . + 4096 * max_cpus
	.align 16
ring0stacktop:

.data

.align 4096
.globl ptl2
ptl2:
i = 0
	.rept 512 * 8
	.quad 0x1e7 | (i << 21)
	i = i + 1
	.endr

.align 4096
ptl3:
i = 0
	.rept 8
	.quad ptl2 + 7 + i * 4096
	i = i + 1
	.endr

.align 4096
ptl4:
	.quad ptl3 + 7

.align 4096
ptl5:
	.quad ptl4 + 7

.align 4096

boot_idt:
	.rept 256
	.quad 0
	.quad 0
	.endr
end_boot_idt:

gdt64_desc:
	.word gdt64_end - gdt64 - 1
	.quad gdt64

gdt64:
	.quad 0
	.quad 0x00af9b000000ffff // 64-bit code segment
	.quad 0x00cf93000000ffff // 32/64-bit data segment
	.quad 0x00af1b000000ffff // 64-bit code segment, not present
	.quad 0x00cf9b000000ffff // 32-bit code segment
	.quad 0x008f9b000000FFFF // 16-bit code segment
	.quad 0x008f93000000FFFF // 16-bit data segment
	.quad 0x00cffb000000ffff // 32-bit code segment (user)
	.quad 0x00cff3000000ffff // 32/64-bit data segment (user)
	.quad 0x00affb000000ffff // 64-bit code segment (user)

	.quad 0			 // 6 spare selectors
	.quad 0
	.quad 0
	.quad 0
	.quad 0
	.quad 0

tss_descr:
	.rept max_cpus
	.quad 0x000089000000ffff // 64-bit avail tss
	.quad 0                  // tss high addr
	.endr
gdt64_end:

i = 0
.globl tss
tss:
	.rept max_cpus
	.long 0
	.quad ring0stacktop - i * 4096
	.quad 0, 0
	.quad 0, 0, 0, 0, 0, 0, 0, 0
	.long 0, 0, 0
i = i + 1
	.endr
tss_end:

mb_boot_info:	.quad 0

pt_root:	.quad ptl4

.section .init

.code32

mb_magic = 0x1BADB002
mb_flags = 0x0

	# multiboot header
	.long mb_magic, mb_flags, 0 - (mb_magic + mb_flags)
mb_cmdline = 16

MSR_GS_BASE = 0xc0000101

.macro setup_percpu_area
	lea -4096(%esp), %eax
	mov $0, %edx
	mov $MSR_GS_BASE, %ecx
	wrmsr
.endm

.macro setup_segments
	mov $MSR_GS_BASE, %ecx
	rdmsr

	mov $0x10, %bx
	mov %bx, %ds
	mov %bx, %es
	mov %bx, %fs
	mov %bx, %gs
	mov %bx, %ss

	/* restore MSR_GS_BASE */
	wrmsr
.endm

.globl start
start:
	mov %ebx, mb_boot_info
	mov $stacktop, %esp
	setup_percpu_area
	call prepare_64
	jmpl $8, $start64

switch_to_5level:
	/* Disable CR4.PCIDE */
	mov %cr4, %eax
	btr $17, %eax
	mov %eax, %cr4

	mov %cr0, %eax
	btr $31, %eax
	mov %eax, %cr0

	mov $ptl5, %eax
	mov %eax, pt_root

	/* Enable CR4.LA57 */
	mov %cr4, %eax
	bts $12, %eax
	mov %eax, %cr4

	mov $0x10, %ax
	mov %ax, %ss

	call enter_long_mode
	jmpl $8, $lvl5

prepare_64:
	lgdt gdt64_desc
	setup_segments

	xor %eax, %eax
	mov %eax, %cr4

enter_long_mode:
	mov %cr4, %eax
	bts $5, %eax  // pae
	mov %eax, %cr4

	mov pt_root, %eax
	mov %eax, %cr3

efer = 0xc0000080
	mov $efer, %ecx
	rdmsr
	bts $8, %eax
	wrmsr

	mov %cr0, %eax
	bts $0, %eax
	bts $31, %eax
	mov %eax, %cr0
	ret

smp_stacktop:	.long stacktop - 4096

.align 16

gdt32:
	.quad 0
	.quad 0x00cf9b000000ffff // flat 32-bit code segment
	.quad 0x00cf93000000ffff // flat 32-bit data segment
gdt32_end:

.code16
sipi_entry:
	mov %cr0, %eax
	or $1, %eax
	mov %eax, %cr0
	lgdtl gdt32_descr - sipi_entry
	ljmpl $8, $ap_start32

gdt32_descr:
	.word gdt32_end - gdt32 - 1
	.long gdt32

sipi_end:

.code32
ap_start32:
	setup_segments
	mov $-4096, %esp
	lock xaddl %esp, smp_stacktop
	setup_percpu_area
	call prepare_64
	ljmpl $8, $ap_start64

.code64
save_id:
	movl $(APIC_DEFAULT_PHYS_BASE + APIC_ID), %eax
	movl (%rax), %eax
	shrl $24, %eax
	lock btsl %eax, online_cpus
	retq

ap_start64:
	call reset_apic
	call load_tss
	call enable_apic
	call save_id
	call enable_x2apic
	sti
	nop
	lock incw cpu_online_count

1:	hlt
	jmp 1b

start64:
	call reset_apic
	call load_tss
	call mask_pic_interrupts
	call enable_apic
	call save_id
	mov mb_boot_info(%rip), %rbx
	mov %rbx, %rdi
	call setup_multiboot
	call setup_libcflat
	mov mb_cmdline(%rbx), %eax
	mov %rax, __args(%rip)
	call __setup_args

	call ap_init
	call enable_x2apic
	call smp_init

	mov __argc(%rip), %edi
	lea __argv(%rip), %rsi
	lea __environ(%rip), %rdx
	call main
	mov %eax, %edi
	call exit

.globl setup_5level_page_table
setup_5level_page_table:
	/* Check if 5-level paging has already enabled */
	mov %cr4, %rax
	test $0x1000, %eax
	jnz lvl5

	pushq $32
	pushq $switch_to_5level
	lretq
lvl5:
	retq

idt_descr:
	.word end_boot_idt - boot_idt - 1
	.quad boot_idt

online_cpus:
	.fill (max_cpus + 7) / 8, 1, 0

load_tss:
	lidtq idt_descr
	mov $(APIC_DEFAULT_PHYS_BASE + APIC_ID), %eax
	mov (%rax), %eax
	shr $24, %eax
	mov %eax, %ebx
	shl $4, %ebx
	mov $((tss_end - tss) / max_cpus), %edx
	imul %edx
	add $tss, %rax
	mov %ax, tss_descr+2(%rbx)
	shr $16, %rax
	mov %al, tss_descr+4(%rbx)
	shr $8, %rax
	mov %al, tss_descr+7(%rbx)
	shr $8, %rax
	mov %eax, tss_descr+8(%rbx)
	lea tss_descr-gdt64(%rbx), %rax
	ltr %ax
	ret

ap_init:
	cld
	lea sipi_entry, %rsi
	xor %rdi, %rdi
	mov $(sipi_end - sipi_entry), %rcx
	rep movsb
	mov $APIC_DEFAULT_PHYS_BASE, %eax
	movl $(APIC_DEST_ALLBUT | APIC_DEST_PHYSICAL | APIC_DM_INIT | APIC_INT_ASSERT), APIC_ICR(%rax)
	movl $(APIC_DEST_ALLBUT | APIC_DEST_PHYSICAL | APIC_DM_STARTUP), APIC_ICR(%rax)
	call fwcfg_get_nb_cpus
1:	pause
	cmpw %ax, cpu_online_count
	jne 1b
	ret

cpu_online_count:	.word 1
