#include "lapic.h"
#include <stdint.h>

/* If you later enable paging, ensure LAPIC_BASE is mapped to virtual memory.
   Currently we assume no paging (linear == physical) or identity mapping. */

static volatile uint32_t *lapic = (volatile uint32_t *)LAPIC_BASE;

void lapic_write(uint32_t reg, uint32_t value) {
    lapic[reg / 4] = value;
    /* read-back to ensure write posted */
    (void)lapic[reg / 4];
}

uint32_t lapic_read(uint32_t reg) {
    return lapic[reg / 4];
}

void lapic_enable(void) {
    /* Set bit 8 in Spurious Interrupt Vector Register to enable LAPIC */
    /* + вектор ложного прерывания 0xFF */
    lapic_write(LAPIC_SVR, (lapic_read(LAPIC_SVR) & ~0xFFu) | 0x100u | 0xFFu);
}

/* wait until Delivery Status (bit 12) cleared */
static void icr_wait_done(void) {
    while (lapic_read(LAPIC_ICR_LOW) & (1u << 12)) {
        __asm__ volatile("pause");
    }
}

/* apic_id: physical APIC ID (8-bit), value: ICR low dword */
void lapic_send_ipi(uint32_t apic_id, uint32_t value) {
    /* destination high: APIC ID in bits 31:24 */
    lapic_write(LAPIC_ICR_HIGH, (apic_id & 0xFFu) << 24);
    lapic_write(LAPIC_ICR_LOW, value);
    icr_wait_done();
}

uint32_t lapic_get_id(void) {
    return (lapic_read(LAPIC_ID) >> 24) & 0xFFu;
}

void lapic_mark_ready(void);

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

static volatile int lapic_ready = 0;

void lapic_send_ipi_all_others(uint32_t vector) {
    if (!lapic_ready) return;
    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, ICR_SHORTHAND_OTHERS | ICR_LEVEL_ASSERT | ICR_VECTOR(vector));
    icr_wait_done();
}

void lapic_mark_ready(void) {
    lapic_ready = 1;
}
