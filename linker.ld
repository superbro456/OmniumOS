ENTRY(kernel_main)
OUTPUT_ARCH(i386)

SECTIONS
{
    /* 1) Multiboot header — первый сегмент ELF */
    .multiboot : {
        KEEP(*(.multiboot))
    }

    /* 2) AP trampoline blob — разместим его точно по 0x7000 */
    . = 0x00007000;              /* <- установить адрес перед blob */
    .ap_trampoline_blob : ALIGN(0x1000) {
        PROVIDE(ap_trampoline_blob_start = .);
        KEEP(*(.ap_trampoline_blob))
        PROVIDE(ap_trampoline_blob_end = .);
    }

    /* 3) Базовый адрес ядра (1MB) */
    . = 0x00100000;

    /* Kernel code */
    .text : ALIGN(16) {
        *(.text*)
        *(.gnu.linkonce.t.*)
    }

    /* Read-only data */
    .rodata : ALIGN(16) {
        *(.rodata*)
    }

    /* Data */
    .data : ALIGN(16) {
        *(.data*)
    }

    /* BSS */
    .bss : ALIGN(16) {
        *(.bss*)
        *(COMMON)
    }
}
