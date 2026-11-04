; Встраивает собранный trampoline (ap_trampoline.bin) в ядро как данные.
section .rodata
global ap_trampoline_blob_start
global ap_trampoline_blob_end
ap_trampoline_blob_start:
    incbin "kernel/ap_trampoline.bin"
ap_trampoline_blob_end:

section .note.GNU-stack noalloc noexec nowrite progbits
