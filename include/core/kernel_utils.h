#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include <stdint.h>

// ------------------ Вывод ------------------
void kprint(const char* str);        // экран + COM1
void kclear_screen(void);
void serial_init(void);
void serial_print(const char* str);  // только COM1

// ------------------ Паника ------------------
void panic(const char* msg) __attribute__((noreturn));

// ------------------ Преобразование чисел ------------------
void u32_to_dec(uint32_t value, char* buffer);
void u32_to_hex(uint32_t value, char* buffer);

#endif // KERNEL_UTILS_H
