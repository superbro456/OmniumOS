; boot/boot.asm — точка входа ядра OmniumOS (Multiboot 1)
; GRUB прыгает сюда в 32-битном режиме, но без стека. Даём ядру свой стек
; и передаём в kernel_main magic и адрес структуры multiboot_info.

MB_MAGIC  equ 0x1BADB002
MB_FLAGS  equ 0x00000003          ; бит 0: модули по 4K, бит 1: карта памяти
MB_CHECK  equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECK

section .bss
align 16
boot_stack_bottom:
    resb 16384                    ; 16 КБ стека для BSP
boot_stack_top:

section .text
global _start
extern kernel_main
_start:
    cli
    mov esp, boot_stack_top
    xor ebp, ebp
    push ebx                      ; multiboot_info*
    push eax                      ; magic (0x2BADB002)
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
