#include "memory_core.h"
#include "kernel_utils.h"
#include "spinlock.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===== Memory configuration =====
// Размеры зон считаются от реального объёма RAM (раньше было жёстко 1 ГБ,
// и на машине с меньшей памятью ядро раздавало несуществующие адреса).
#define MB                  (1024u * 1024u)
#define ZONE_KERNEL_MAX     (128u * MB)
#define ZONE_SERVICE_MIN    (4u * MB)
#define PAGE_ALIGN          4096u
#define ALLOC_ALIGN         16u

typedef struct Allocator {
    uintptr_t start;
    uintptr_t end;
    uintptr_t current;     // kernel/app: растёт вверх; service: растёт вниз
    spinlock_t lock;
} Allocator;

typedef struct Zone {
    uintptr_t min_size;
    uintptr_t max_size;
    Allocator allocator;
} Zone;

static Zone memory_zones[ZONE_COUNT];
static spinlock_t zones_lock = 0;   // защищает границу FastApp/Service

static inline uintptr_t align_up(uintptr_t v, uintptr_t a) { return (v + a - 1) & ~(a - 1); }
static inline uintptr_t align_down(uintptr_t v, uintptr_t a) { return v & ~(a - 1); }

// ----------------- Bump Allocator с блокировкой -----------------
static void* bump_alloc_up(Allocator* a, size_t size) {
    void* ptr = NULL;
    spinlock_acquire(&a->lock);
    uintptr_t p = align_up(a->current, ALLOC_ALIGN);
    if (p >= a->current && size <= a->end - p) {   // без переполнения
        a->current = p + size;
        ptr = (void*)p;
    }
    spinlock_release(&a->lock);
    return ptr;
}

// Service-зона выдаёт память сверху вниз. Тогда свободное место всегда
// лежит у её нижней границы, и FastApp может безопасно забрать его себе.
static void* bump_alloc_down(Allocator* a, size_t size) {
    void* ptr = NULL;
    spinlock_acquire(&a->lock);
    if (size <= a->current - a->start) {
        uintptr_t p = align_down(a->current - size, ALLOC_ALIGN);
        if (p >= a->start) {
            a->current = p;
            ptr = (void*)p;
        }
    }
    spinlock_release(&a->lock);
    return ptr;
}

static void zone_set(MemoryZoneType t, uintptr_t start, uintptr_t end,
                     uintptr_t min_size, uintptr_t max_size) {
    Zone* z = &memory_zones[t];
    z->min_size = min_size;
    z->max_size = max_size;
    z->allocator.start = start;
    z->allocator.end = end;
    z->allocator.current = (t == ZONE_SERVICE) ? end : start;
    z->allocator.lock = 0;
}

// ----------------- Инициализация памяти -----------------
void memory_init_range(uintptr_t start, uintptr_t end) {
    start = align_up(start, PAGE_ALIGN);
    end = align_down(end, PAGE_ALIGN);
    uintptr_t total = (end > start) ? end - start : 0;

    uintptr_t kernel_sz = align_down(total / 8, PAGE_ALIGN);
    if (kernel_sz > ZONE_KERNEL_MAX) kernel_sz = ZONE_KERNEL_MAX;
    uintptr_t rest = total - kernel_sz;
    uintptr_t app_sz = align_down(rest / 2, PAGE_ALIGN);
    uintptr_t service_sz = rest - app_sz;
    // FastApp может вырасти, но Service всегда оставляет себе минимум
    uintptr_t app_max = app_sz + (service_sz > ZONE_SERVICE_MIN ? service_sz - ZONE_SERVICE_MIN : 0);

    uintptr_t k0 = start, a0 = k0 + kernel_sz, s0 = a0 + app_sz;
    zone_set(ZONE_KERNEL,   k0, a0,  kernel_sz, kernel_sz);
    zone_set(ZONE_FAST_APP, a0, s0,  app_sz, app_max);
    zone_set(ZONE_SERVICE,  s0, end, ZONE_SERVICE_MIN, service_sz);
}

extern char _kernel_end[];

void memory_init(uint64_t bytes_above_1mb) {
    uint64_t end = 0x100000ULL + bytes_above_1mb;
    if (end > 0xFFFFF000ULL) end = 0xFFFFF000ULL;   // 32-битное ядро
    // Память начинается ПОСЛЕ образа ядра, а не с 1 МБ, где лежит само ядро
    memory_init_range((uintptr_t)_kernel_end, (uintptr_t)end);

    char buf[12];
    u32_to_dec((uint32_t)((end - 0x100000ULL) / MB), buf);
    kprint("[MemoryCore] RAM above 1 MB: ");
    kprint(buf);
    kprint(" MB, zones ready.\n");
}

// ----------------- Расширение FastApp -----------------
static bool grow_fast_app_zone(size_t extra_size) {
    Zone* apps = &memory_zones[ZONE_FAST_APP];
    Zone* service = &memory_zones[ZONE_SERVICE];
    bool ok = false;

    extra_size = align_up(extra_size, PAGE_ALIGN);

    spinlock_acquire(&zones_lock);
    spinlock_acquire(&apps->allocator.lock);
    spinlock_acquire(&service->allocator.lock);

    uintptr_t service_free = service->allocator.current - service->allocator.start;
    uintptr_t service_left = service->allocator.end - service->allocator.start - extra_size;
    uintptr_t apps_size = apps->allocator.end - apps->allocator.start;

    if (service_free >= extra_size && service_left >= service->min_size &&
        apps_size + extra_size <= apps->max_size) {
        // Границу двигаем только по свободной части Service
        service->allocator.start += extra_size;
        apps->allocator.end += extra_size;
        ok = true;
    }

    spinlock_release(&service->allocator.lock);
    spinlock_release(&apps->allocator.lock);
    spinlock_release(&zones_lock);
    if (!ok) kprint("[MemoryCore] Cannot grow FastApp zone.\n");
    return ok;
}

// ----------------- Allocate/Free -----------------
void* memory_allocate(MemoryZoneType zone_type, size_t size) {
    if ((unsigned)zone_type >= ZONE_COUNT || size == 0) return NULL;
    Zone* zone = &memory_zones[zone_type];

    if (zone_type == ZONE_SERVICE)
        return bump_alloc_down(&zone->allocator, size);

    void* ptr = bump_alloc_up(&zone->allocator, size);
    if (!ptr && zone_type == ZONE_FAST_APP) {
        Allocator* a = &zone->allocator;
        uintptr_t have = a->end - align_up(a->current, ALLOC_ALIGN);
        if (size > have && grow_fast_app_zone(size - have))
            ptr = bump_alloc_up(a, size);
    }
    return ptr;
}

void memory_free(void* ptr, MemoryZoneType zone_type) {
    (void)ptr;
    (void)zone_type;
    // Bump-аллокатор не освобождает отдельные блоки (TODO: свободные списки)
}

size_t memory_zone_size(MemoryZoneType zone_type) {
    const Allocator* a = &memory_zones[zone_type].allocator;
    return (size_t)(a->end - a->start);
}

size_t memory_zone_free(MemoryZoneType zone_type) {
    const Allocator* a = &memory_zones[zone_type].allocator;
    if (zone_type == ZONE_SERVICE) return (size_t)(a->current - a->start);
    return (size_t)(a->end - a->current);
}

// ----------------- Memory test -----------------
static void report(bool ok, const char* what) {
    kprint(ok ? "OK: " : "ERROR: ");
    kprint(what);
    kprint("\n");
}

void memory_test(void) {
    kprint("[MemoryCore Test] Starting...\n");

    size_t ksz = memory_zone_size(ZONE_KERNEL);
    void* k1 = memory_allocate(ZONE_KERNEL, 64 * 1024);
    void* k2 = memory_allocate(ZONE_KERNEL, ksz / 4);
    report(k1 && k2 && k1 != k2, "kernel allocations");
    report(memory_allocate(ZONE_KERNEL, ksz) == NULL, "kernel zone refuses oversize request");

    int small = 0;
    for (int i = 0; i < 8; i++)
        if (memory_allocate(ZONE_FAST_APP, 1024)) small++;
    report(small == 8, "FastApp small allocations");

    // Больше, чем зона FastApp: должна подрасти за счёт свободной части Service
    size_t app_before = memory_zone_size(ZONE_FAST_APP);
    size_t big_size = memory_zone_free(ZONE_FAST_APP) + MB;
    uint8_t* big = memory_allocate(ZONE_FAST_APP, big_size);
    report(big && memory_zone_size(ZONE_FAST_APP) > app_before, "FastApp grows into free Service space");

    uint8_t* s1 = memory_allocate(ZONE_SERVICE, 4096);
    report(s1 && big && s1 >= big + big_size, "Service allocation does not overlap FastApp");

    kprint("MemoryCore Test Done.\n");
}
