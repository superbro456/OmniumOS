#include "delay.h"

// Простейший busy-wait
void delay(uint32_t ms) {
    volatile uint32_t i, j;
    for (i = 0; i < ms; i++) {
        for (j = 0; j < 10000; j++) {
            asm volatile("nop");
        }
    }
}
