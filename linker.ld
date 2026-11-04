ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

SECTIONS
{
    /* Ядро грузится с 1 МБ. Заголовок Multiboot должен лежать
       в первых 8 КБ файла, поэтому он идёт первым. */
    . = 0x00100000;
    _kernel_start = .;

    .text : ALIGN(4096) {
        KEEP(*(.multiboot))
        *(.text*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }

    . = ALIGN(4096);
    _kernel_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
