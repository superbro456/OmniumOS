#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_MEMORY      0x00000001

/* Начало структуры multiboot_info (нужные нам поля). */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;   /* КБ ниже 1 МБ */
    uint32_t mem_upper;   /* КБ выше 1 МБ (непрерывно) */
} multiboot_info_t;

#endif
