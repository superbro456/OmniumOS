// kernel/acpi.c — список ядер CPU из таблицы ACPI MADT.
// Раньше число ядер бралось из CPUID и запускались APIC ID 0..n-1 — это
// видит только один корпус (сокет). MADT перечисляет все ядра машины.
#include <stdint.h>
#include "acpi.h"

static int sig_eq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int sum_ok(const uint8_t *p, uint32_t len) {
    uint8_t s = 0;
    for (uint32_t i = 0; i < len; i++) s += p[i];
    return s == 0;
}

static const uint8_t *find_rsdp(void) {
    uintptr_t bda = 0x40E;                  /* адрес в BIOS Data Area, где лежит сегмент EBDA */
    __asm__("" : "+r"(bda));                /* чтобы компилятор не ругался на «нулевой» массив */
    uint32_t ebda = (uint32_t)(*(volatile uint16_t *)bda) << 4;
    uint32_t ranges[2][2] = { { ebda, ebda + 1024 }, { 0xE0000, 0x100000 } };
    for (int r = 0; r < 2; r++) {
        if (!ranges[r][0]) continue;
        for (uint32_t a = ranges[r][0]; a < ranges[r][1]; a += 16) {
            const uint8_t *p = (const uint8_t *)(uintptr_t)a;
            if (sig_eq((const char *)p, "RSD PTR ", 8) && sum_ok(p, 20)) return p;
        }
    }
    return 0;
}

int acpi_cpu_list(uint32_t *apic_ids, int max) {
    const uint8_t *rsdp = find_rsdp();
    if (!rsdp) return 0;
    const uint8_t *rsdt = (const uint8_t *)(uintptr_t)*(const uint32_t *)(rsdp + 16);
    if (!sig_eq((const char *)rsdt, "RSDT", 4)) return 0;
    uint32_t len = *(const uint32_t *)(rsdt + 4);
    for (uint32_t off = 36; off + 4 <= len; off += 4) {
        const uint8_t *t = (const uint8_t *)(uintptr_t)*(const uint32_t *)(rsdt + off);
        if (!sig_eq((const char *)t, "APIC", 4)) continue;
        uint32_t tlen = *(const uint32_t *)(t + 4);
        int n = 0;
        for (uint32_t e = 44; e + 2 <= tlen && n < max; e += t[e + 1]) {
            if (t[e + 1] < 2) break;
            if (t[e] == 0 && (*(const uint32_t *)(t + e + 4) & 3))   /* ядро включено или может включиться */
                apic_ids[n++] = t[e + 3];
        }
        return n;
    }
    return 0;
}
