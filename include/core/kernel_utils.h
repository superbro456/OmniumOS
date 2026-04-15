#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include <stdint.h>

// ------------------ VGA ------------------
void kprint(const char* str);
void kclear_screen(void);

// ------------------ Паника ------------------
void panic(const char* msg);

// ------------------ Преобразование чисел ------------------
void u32_to_dec(uint32_t value, char* buffer);
void u32_to_hex(uint32_t value, char* buffer);

#endif // KERNEL_UTILS_H
