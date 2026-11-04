#ifndef SMP_H
#define SMP_H

#include <stdint.h>

typedef volatile int spinlock_t;

#define SMP_MAX_CPUS 16

uint32_t cpu_id(void);                 /* APIC ID текущего ядра */
int start_ap(uint32_t apic_id, uint32_t index); /* 1 = ядро ответило */
extern volatile uint32_t smp_cpus_online;

void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);

#endif /* SMP_H */
