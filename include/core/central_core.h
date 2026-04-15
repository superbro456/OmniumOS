#ifndef CENTRAL_CORE_H
#define CENTRAL_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// === Основная точка входа ядра ===
void kernel_main(void);

// === AP точка входа ===
void ap_main(void);

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
