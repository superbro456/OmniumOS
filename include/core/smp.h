#ifndef SMP_H
#define SMP_H

#include <stdint.h>

typedef volatile int spinlock_t;

extern volatile uint32_t current_cpu;

uint32_t cpu_id(void);
void start_ap(uint32_t apic_id);
void ap_entry(void);
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);

#endif /* SMP_H */
