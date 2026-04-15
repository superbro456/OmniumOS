; kernel/ap_trampoline.asm
; 16-bit -> 32-bit trampoline. Exports ap_trampoline_blob_start/_end.

section .ap_trampoline_blob
    global ap_trampoline_blob_start
    global ap_trampoline_blob_end

BITS 16
ap_trampoline_blob_start:

start_ap16:
    cli
    ; Load GDT (descriptor below)
    lgdt [gdt_descriptor]

    ; Enable Protected Mode: set PE in CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; far jump to 32-bit selector:offset
    jmp 0x08:protected_entry

; ---------------------------------------
; GDT: null, code, data (3 descriptors x 8 bytes)
; ---------------------------------------
gdt_start:
    dq 0x0000000000000000

    ; code descriptor: base=0, limit=0xFFFFF, access=0x9A, gran=0xCF
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

    ; data descriptor: base=0, limit=0xFFFFF, access=0x92, gran=0xCF
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---------------------------------------
; Protected-mode entry
; ---------------------------------------
BITS 32
protected_entry:
    ; Set segment registers (selector 0x10 = data)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Setup stack for AP (adjust if needed)
    mov esp, 0x90000

    ; Call C function ap_main (must be provided by kernel)
    extern ap_main
    call ap_main

    cli
.hang:
    hlt
    jmp .hang

ap_trampoline_blob_end:
