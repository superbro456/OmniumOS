; kernel/ap_trampoline.asm — старт второстепенных ядер (AP).
; Собирается как плоский бинарник (nasm -f bin) для адреса 0x7000 и
; копируется туда ядром перед SIPI (вектор 0x07 -> CS:IP = 0700:0000).
;
; Ядро перед запуском каждого AP пишет в «почтовый ящик» в конце страницы:
;   0x7FF0  вершина стека этого AP (у каждого ядра свой стек)
;   0x7FF4  адрес C-функции входа: void ap_entry(uint32_t index)
;   0x7FF8  номер AP (index)

TRAMP_STACK equ 0x7FF0
TRAMP_ENTRY equ 0x7FF4
TRAMP_INDEX equ 0x7FF8

BITS 16
org 0x7000

start16:
    cli
    cld
    xor ax, ax
    mov ds, ax                    ; адреса ниже абсолютные (org 0x7000)
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:start32

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF         ; код 0..4G
    dq 0x00CF92000000FFFF         ; данные 0..4G
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BITS 32
start32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [TRAMP_STACK]
    xor ebp, ebp
    push dword [TRAMP_INDEX]
    call [TRAMP_ENTRY]
.hang:
    cli
    hlt
    jmp .hang
