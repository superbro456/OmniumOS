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

void memory_init(void);
void* memory_allocate(MemoryZoneType zone, size_t size);
void memory_free(void* ptr, MemoryZoneType zone);

// Тест памяти
void memory_test(void);

#endif // MEMORY_CORE_H
