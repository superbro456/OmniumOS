#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>

/* Кадр прерывания в том порядке, в каком его строит isr_common. */
typedef struct {
    uint32_t es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t vector, error;
    uint32_t eip, cs, eflags;
} isr_frame_t;

#define IRQ_TIMER_VECTOR  32
#define IPI_TICK_VECTOR   64
#define TIMER_HZ          100

void gdt_init(void);        /* общая для всех ядер GDT */
void idt_init(void);        /* строит IDT (один раз, на BSP) */
void idt_load(void);        /* загружает IDT на текущем ядре */
void pic_init(void);        /* переносит IRQ 0..15 на векторы 32..47 */
void pit_init(uint32_t hz); /* системный таймер */

extern volatile uint64_t timer_ticks;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void cpu_sti(void) { __asm__ volatile ("sti"); }
static inline void cpu_cli(void) { __asm__ volatile ("cli"); }
static inline void cpu_halt(void) { __asm__ volatile ("hlt"); }

#endif
