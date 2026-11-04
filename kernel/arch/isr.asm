; kernel/arch/isr.asm — входы прерываний. Каждая заглушка кладёт номер
; вектора (и 0 вместо кода ошибки, если CPU его не даёт) и идёт в общий
; обработчик, который вызывает C-функцию isr_dispatch(frame*).

section .text
extern isr_dispatch

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

; исключения CPU 0..31 (с кодом ошибки: 8, 10-14, 17, 21, 29, 30)
%assign i 0
%rep 32
  %if i = 8 || (i >= 10 && i <= 14) || i = 17 || i = 21 || i = 29 || i = 30
    ISR_ERR i
  %else
    ISR_NOERR i
  %endif
  %assign i i+1
%endrep

ISR_NOERR 32      ; IRQ0 таймер PIT
ISR_NOERR 39      ; IRQ7 ложное от PIC
ISR_NOERR 64      ; IPI «тик» для AP
ISR_NOERR 255     ; ложное от LAPIC

isr_common:
    pusha
    push ds
    push es
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    push esp                      ; аргумент: указатель на кадр
    call isr_dispatch
    add esp, 4
    pop es
    pop ds
    popa
    add esp, 8                    ; номер вектора + код ошибки
    iret

global gdt_flush
; void gdt_flush(const void* gdtr)
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload
.reload:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
