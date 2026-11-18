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

section .note.GNU-stack noalloc noexec nowrite progbits
