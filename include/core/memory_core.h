#ifndef MEMORY_CORE_H
#define MEMORY_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ZONE_KERNEL,
    ZONE_FAST_APP,
    ZONE_SERVICE,
    ZONE_COUNT
} MemoryZoneType;

/* bytes_above_1mb — объём RAM выше 1 МБ (из multiboot mem_upper). */
void memory_init(uint64_t bytes_above_1mb);
/* Разметить зоны на диапазоне [start, end) — для тестов и будущих карт памяти. */
void memory_init_range(uintptr_t start, uintptr_t end);
void* memory_allocate(MemoryZoneType zone, size_t size);
void memory_free(void* ptr, MemoryZoneType zone);
size_t memory_zone_size(MemoryZoneType zone);
size_t memory_zone_free(MemoryZoneType zone);

// Тест памяти
void memory_test(void);

#endif // MEMORY_CORE_H
