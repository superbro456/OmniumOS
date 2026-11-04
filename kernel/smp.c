#include "lapic.h"
#include "smp.h"
#include "mem.h"
#include <stdint.h>
#include <stddef.h>

/* Простая задержка (используется для INIT/SIPI). */
static void delay_loops(unsigned n) {
    for (volatile unsigned i = 0; i < n; i++) {
        __asm__ volatile("pause");
    }
}

/* Trampoline собран отдельно (nasm -f bin) и встроен в ядро как данные. */
extern const char ap_trampoline_blob_start[];
extern const char ap_trampoline_blob_end[];

#define TRAMP_BASE   0x7000u
#define TRAMP_STACK  ((volatile uint32_t*)0x7FF0)
#define TRAMP_ENTRY  ((volatile uint32_t*)0x7FF4)
#define TRAMP_INDEX  ((volatile uint32_t*)0x7FF8)

#define AP_STACK_SIZE 16384
static uint8_t ap_stacks[SMP_MAX_CPUS][AP_STACK_SIZE] __attribute__((aligned(16)));

volatile uint32_t smp_cpus_online = 1;   /* BSP уже работает */

/* C-вход для AP, вызывается из trampoline (определён в central_core.c). */
void ap_entry(uint32_t index);

uint32_t cpu_id(void) {
    return lapic_get_id();
}

int start_ap(uint32_t apic_id, uint32_t index) {
    if (index >= SMP_MAX_CPUS) return 0;

    uint32_t size = (uint32_t)(ap_trampoline_blob_end - ap_trampoline_blob_start);
    memcpy((void*)TRAMP_BASE, ap_trampoline_blob_start, size);

    /* «Почтовый ящик»: у каждого AP свой стек, иначе ядра портят друг другу данные */
    *TRAMP_STACK = (uint32_t)(uintptr_t)(ap_stacks[index] + AP_STACK_SIZE);
    *TRAMP_ENTRY = (uint32_t)(uintptr_t)ap_entry;
    *TRAMP_INDEX = index;

    uint32_t before = smp_cpus_online;

    lapic_send_ipi(apic_id, ICR_DM(DM_INIT) | ICR_LEVEL_ASSERT);
    delay_loops(1000000);
    for (int i = 0; i < 2; i++) {
        lapic_send_ipi(apic_id, ICR_DM(DM_STARTUP) | ICR_VECTOR(TRAMP_BASE >> 12));
        delay_loops(100000);
    }

    /* Ждём, пока AP отметится. Следующий AP можно запускать только после
       этого: trampoline и почтовый ящик у всех общие. */
    for (unsigned spin = 0; spin < 50000000u; spin++) {
        if (smp_cpus_online != before) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}
