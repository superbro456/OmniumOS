; kernel/galaxy/switch.asm — переключение между клетками.
; void gx_switch(uint32_t *old_esp, uint32_t new_esp)
; Сохраняем регистры, которые по соглашению Си должна беречь вызываемая
; функция, и флаги (в них бит прерываний). Остальное уже сохранил вызвавший.

section .text
global gx_switch
gx_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    push ebp
    push ebx
    push esi
    push edi
    pushfd
    mov [eax], esp
    mov esp, edx
    popfd
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; int gx_try(uint32_t jb[6]) — запомнить место; вернёт 0, а после gx_throw — код
global gx_try
gx_try:
    mov eax, [esp + 4]
    mov [eax], ebx
    mov [eax + 4], esi
    mov [eax + 8], edi
    mov [eax + 12], ebp
    lea ecx, [esp + 4]
    mov [eax + 16], ecx
    mov ecx, [esp]
    mov [eax + 20], ecx
    xor eax, eax
    ret

; void gx_throw(uint32_t jb[6], int code) — вернуться в gx_try (из обработчика падения)
global gx_throw
gx_throw:
    mov edx, [esp + 4]
    mov eax, [esp + 8]
    mov ebx, [edx]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    sti                           ; клетки работают с включёнными прерываниями
    jmp [edx + 20]

section .note.GNU-stack noalloc noexec nowrite progbits
