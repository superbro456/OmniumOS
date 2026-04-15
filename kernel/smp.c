#include "lapic.h"
#include <stdint.h>
#include <stddef.h>

/* Простая memcpy для ядра */
void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

/* Простая задержка (используется для SIPI/INIT) */
static void delay_ms(unsigned ms) {
    for (volatile unsigned i = 0; i < ms * 100000; i++) {
        __asm__ volatile("pause");
    }
}

/* Экспорт символов из asm trampoline */
extern char ap_trampoline_blob_start[];
extern char ap_trampoline_blob_end[];

/* AP entry */
void ap_entry(uint32_t id) {
    (void)id;
}

/* Вернуть APIC ID текущего CPU */
uint32_t cpu_id(void) {
    return lapic_get_id();
}

/* Запустить AP с указанным APIC ID */
void start_ap(uint32_t apic_id) {
    uint32_t size = (uint32_t)(ap_trampoline_blob_end - ap_trampoline_blob_start);

    /* Копируем trampoline в физический адрес 0x7000 */
    memcpy((void*)0x7000, ap_trampoline_blob_start, size);

    /* INIT IPI */
    uint32_t icr_init = 0x00004500;   // delivery mode = INIT
    lapic_send_ipi(apic_id, icr_init);

    delay_ms(10);

    /* SIPI */
    uint32_t sipi = 0x00004607; // vector = 0x7
    lapic_send_ipi(apic_id, sipi);
    delay_ms(1);
    lapic_send_ipi(apic_id, sipi);
}
