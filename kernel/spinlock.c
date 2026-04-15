#include "spinlock.h"

void spinlock_acquire(spinlock_t* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        __asm__ volatile("pause");
    }
}

void spinlock_release(spinlock_t* lock) {
    __sync_lock_release(lock);
}
