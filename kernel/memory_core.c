#include "memory_core.h"
#include "kernel_utils.h"
#include "spinlock.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===== Memory configuration =====
#define MEM_SIZE            (1024 * 1024 * 1024)  // 1 GB RAM
#define ZONE_KERNEL_SIZE    (128 * 1024 * 1024)   // 128 MB
#define ZONE_APP_MIN        (256 * 1024 * 1024)   // 256 MB минимально для приложений
#define ZONE_APP_MAX        (512 * 1024 * 1024)   // 512 MB максимально для приложений
#define ZONE_SERVICE_MIN    (32  * 1024 * 1024)   // 32 MB минимально для сервисов
#define ZONE_SERVICE_MAX    (128 * 1024 * 1024)   // 128 MB максимально для сервисов

typedef struct Allocator {
    uint8_t* start;
    uint8_t* end;
    uint8_t* current;
    spinlock_t lock;
} Allocator;

typedef struct Zone {
    uintptr_t start;
    uintptr_t end;
    uintptr_t min_size;
    uintptr_t max_size;
    Allocator allocator;
} Zone;

static Zone memory_zones[ZONE_COUNT];

// ----------------- Bump Allocator с блокировкой -----------------
static void* bump_alloc(Allocator* a, size_t size) {
    spinlock_acquire(&a->lock);
    if ((uintptr_t)(a->current + size) > (uintptr_t)a->end) {
        spinlock_release(&a->lock);
        return NULL;
    }
    void* ptr = a->current;
    a->current += size;
    spinlock_release(&a->lock);
    return ptr;
}

// ----------------- Инициализация памяти -----------------
void memory_init(void) {
    kprint("[MemoryCore] Memory core initialized.\n");

    uint8_t* mem_start = (uint8_t*)0x100000;
    uint8_t* mem_end   = mem_start + MEM_SIZE;

    // Kernel Zone
    memory_zones[ZONE_KERNEL].start = (uintptr_t)mem_start;
    memory_zones[ZONE_KERNEL].end   = (uintptr_t)mem_start + ZONE_KERNEL_SIZE;
    memory_zones[ZONE_KERNEL].min_size = ZONE_KERNEL_SIZE;
    memory_zones[ZONE_KERNEL].max_size = ZONE_KERNEL_SIZE;
    memory_zones[ZONE_KERNEL].allocator.start = mem_start;
    memory_zones[ZONE_KERNEL].allocator.end   = mem_start + ZONE_KERNEL_SIZE;
    memory_zones[ZONE_KERNEL].allocator.current = mem_start;
    memory_zones[ZONE_KERNEL].allocator.lock = 0;

    // FastApp Zone
    memory_zones[ZONE_FAST_APP].start = (uintptr_t)(mem_start + ZONE_KERNEL_SIZE);
    memory_zones[ZONE_FAST_APP].end   = (uintptr_t)(mem_start + ZONE_KERNEL_SIZE + ZONE_APP_MIN);
    memory_zones[ZONE_FAST_APP].min_size = ZONE_APP_MIN;
    memory_zones[ZONE_FAST_APP].max_size = ZONE_APP_MAX;
    memory_zones[ZONE_FAST_APP].allocator.start = (uint8_t*)memory_zones[ZONE_FAST_APP].start;
    memory_zones[ZONE_FAST_APP].allocator.end = (uint8_t*)memory_zones[ZONE_FAST_APP].end;
    memory_zones[ZONE_FAST_APP].allocator.current = memory_zones[ZONE_FAST_APP].allocator.start;
    memory_zones[ZONE_FAST_APP].allocator.lock = 0;

    // Service Zone
    memory_zones[ZONE_SERVICE].start = (uintptr_t)(mem_start + ZONE_KERNEL_SIZE + ZONE_APP_MIN);
    memory_zones[ZONE_SERVICE].end   = (uintptr_t)mem_end;
    memory_zones[ZONE_SERVICE].min_size = ZONE_SERVICE_MIN;
    memory_zones[ZONE_SERVICE].max_size = ZONE_SERVICE_MAX;
    memory_zones[ZONE_SERVICE].allocator.start = (uint8_t*)memory_zones[ZONE_SERVICE].start;
    memory_zones[ZONE_SERVICE].allocator.end = (uint8_t*)memory_zones[ZONE_SERVICE].end;
    memory_zones[ZONE_SERVICE].allocator.current = memory_zones[ZONE_SERVICE].allocator.start;
    memory_zones[ZONE_SERVICE].allocator.lock = 0;
}

// ----------------- Расширение FastApp -----------------
static bool grow_fast_app_zone(size_t extra_size) {
    Zone* apps = &memory_zones[ZONE_FAST_APP];
    Zone* service = &memory_zones[ZONE_SERVICE];

    spinlock_acquire(&apps->allocator.lock);
    spinlock_acquire(&service->allocator.lock);

    uintptr_t service_free = (uintptr_t)service->allocator.end - (uintptr_t)service->allocator.current;
    if (service_free < extra_size) goto fail;

    if ((apps->end - apps->start + extra_size) > apps->max_size) goto fail;

    service->end -= extra_size;
    service->allocator.end -= extra_size;

    apps->end += extra_size;
    apps->allocator.end += extra_size;

    spinlock_release(&service->allocator.lock);
    spinlock_release(&apps->allocator.lock);
    return true;

fail:
    kprint("[MemoryCore] Cannot grow FastApp zone.\n");
    spinlock_release(&service->allocator.lock);
    spinlock_release(&apps->allocator.lock);
    return false;
}

// ----------------- Allocate/Free -----------------
void* memory_allocate(MemoryZoneType zone_type, size_t size) {
    Zone* zone = &memory_zones[zone_type];
    void* ptr = bump_alloc(&zone->allocator, size);

    if (!ptr && zone_type == ZONE_FAST_APP) {
        if (grow_fast_app_zone(size))
            ptr = bump_alloc(&zone->allocator, size);
    }

    return ptr;
}

void memory_free(void* ptr, MemoryZoneType zone_type) {
    (void)ptr;
    (void)zone_type;
    // Пока noop
}

// ----------------- Memory test -----------------
void memory_test(void) {
    kprint("[MemoryCore Test] Starting...\n");

    void* k1 = memory_allocate(ZONE_KERNEL, 1024*1024);
    void* k2 = memory_allocate(ZONE_KERNEL, 64*1024*1024);
    if (k1 && k2) kprint("OK: Kernel allocations succeeded.\n");
    else kprint("ERROR: Kernel allocations failed.\n");

    void* fa_small[8];
    int success_small = 0;
    for (int i=0;i<8;i++) {
        fa_small[i] = memory_allocate(ZONE_FAST_APP, 1024);
        if (fa_small[i]) success_small++;
    }
    if (success_small==8) kprint("OK: FastApp small allocations succeeded.\n");
    else kprint("WARN: FastApp small allocations failed.\n");

    void* fa_big = memory_allocate(ZONE_FAST_APP, 200*1024*1024UL);
    if (fa_big) kprint("OK: FastApp big allocation succeeded.\n");
    else kprint("WARN: FastApp big allocation failed.\n");

    for (int i=0;i<8;i++)
        if (memory_allocate(ZONE_FAST_APP, 4096)) success_small++;

    int service_count = 0;
    for (int i=0;i<1024;i++)
        if (memory_allocate(ZONE_SERVICE, 4*1024)) service_count++;

    kprint("MemoryCore Test Done.\n");
}