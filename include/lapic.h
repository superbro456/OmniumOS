#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

/* LAPIC base physical address (обычно 0xFEE00000) */
#define LAPIC_BASE 0xFEE00000U

/* LAPIC register offsets */
#define LAPIC_ID        0x020
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_ICR_LOW   0x300
#define LAPIC_ICR_HIGH  0x310

/* ICR helpers */
#define ICR_VECTOR(x)        ((uint32_t)((x) & 0xFFu))
#define ICR_DM(x)            ((uint32_t)((x) & 0x7u) << 8)

/* delivery mode values */
#define DM_FIXED      0u
#define DM_LOWPRIOR   1u
#define DM_SMI        2u
#define DM_NMI        4u
#define DM_INIT       5u
#define DM_STARTUP    6u

/* destination mode */
#define ICR_DEST_PHYSICAL   (0u << 11)
#define ICR_DEST_LOGICAL    (1u << 11)

/* level */
#define ICR_LEVEL_ASSERT    (1u << 14)
#define ICR_LEVEL_DEASSERT  (0u << 14)

/* trigger mode */
#define ICR_TRIGGER_EDGE    (0u << 15)
#define ICR_TRIGGER_LEVEL   (1u << 15)

/* shorthand */
#define ICR_SHORTHAND_NONE      (0u << 18)
#define ICR_SHORTHAND_SELF      (1u << 18)
#define ICR_SHORTHAND_ALL       (2u << 18)
#define ICR_SHORTHAND_OTHERS    (3u << 18)

/* API */
void lapic_write(uint32_t reg, uint32_t value);
uint32_t lapic_read(uint32_t reg);
void lapic_enable(void);
void lapic_send_ipi(uint32_t apic_id, uint32_t value);
uint32_t lapic_get_id(void);

#endif /* LAPIC_H */
