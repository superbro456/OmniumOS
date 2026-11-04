#ifndef CENTRAL_CORE_H
#define CENTRAL_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// === Основная точка входа ядра ===
#include "multiboot.h"
void kernel_main(uint32_t magic, multiboot_info_t* mbi);

// === AP точка входа (вызывается из trampoline) ===
void ap_entry(uint32_t index);

// === BSP helpers ===
void start_all_aps(void);

// === Базовые функции вывода ===
void kprint(const char* str);
void kclear_screen(void);

// === Serial ===
void serial_init(void);
void serial_print(const char* str);

// === Глобальные переменные ===
extern volatile int system_initialized;

#endif // CENTRAL_CORE_H
