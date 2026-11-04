// kernel/arch/arch.c — GDT, IDT, PIC и таймер для i386.
#include "arch.h"
#include "lapic.h"
#include "smp.h"
#include "kernel_utils.h"
#include "process_core.h"

/* ---------------- GDT ---------------- */
static uint64_t gdt[3] = {
    0,
    0x00CF9A000000FFFFULL,   /* 0x08: код ядра, 0..4 ГБ */
    0x00CF92000000FFFFULL,   /* 0x10: данные ядра, 0..4 ГБ */
};

static struct __attribute__((packed)) { uint16_t limit; uint32_t base; } gdtr;

extern void gdt_flush(const void* gdtr);

void gdt_init(void) {
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint32_t)(uintptr_t)gdt;
    gdt_flush(&gdtr);
}

/* ---------------- IDT ---------------- */
typedef struct __attribute__((packed)) {
    uint16_t off_lo, sel;
    uint8_t zero, flags;
    uint16_t off_hi;
} idt_entry_t;

static idt_entry_t idt[256];
static struct __attribute__((packed)) { uint16_t limit; uint32_t base; } idtr;

#define X(n) extern void isr##n(void);
X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13)
X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25)
X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(39) X(64) X(255)
#undef X

static void idt_set(int n, void (*handler)(void)) {
    uint32_t a = (uint32_t)(uintptr_t)handler;
    idt[n].off_lo = a & 0xFFFF;
    idt[n].off_hi = a >> 16;
    idt[n].sel = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;     /* присутствует, кольцо 0, 32-битный interrupt gate */
}

void idt_init(void) {
    void (*exc[32])(void) = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7, isr8, isr9, isr10,
        isr11, isr12, isr13, isr14, isr15, isr16, isr17, isr18, isr19, isr20,
        isr21, isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
    };
    for (int i = 0; i < 32; i++) idt_set(i, exc[i]);
    idt_set(IRQ_TIMER_VECTOR, isr32);
    idt_set(39, isr39);
    idt_set(IPI_TICK_VECTOR, isr64);
    idt_set(255, isr255);
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)(uintptr_t)idt;
}

void idt_load(void) {
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

/* ---------------- PIC 8259 ---------------- */
void pic_init(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);     /* ICW1: init */
    outb(0x21, 32);   outb(0xA1, 40);       /* ICW2: векторы 32..47 */
    outb(0x21, 0x04); outb(0xA1, 0x02);     /* ICW3: каскад */
    outb(0x21, 0x01); outb(0xA1, 0x01);     /* ICW4: 8086 */
    outb(0x21, 0xFE); outb(0xA1, 0xFF);     /* разрешён только IRQ0 */
}

/* ---------------- PIT ---------------- */
volatile uint64_t timer_ticks = 0;

void pit_init(uint32_t hz) {
    uint32_t div = 1193182 / hz;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, (div >> 8) & 0xFF);
}

/* ---------------- Диспетчер ---------------- */
static const char* const exc_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor",
    "Invalid TSS", "Segment not present", "Stack fault", "General protection",
    "Page fault", "Reserved", "x87 FPU error", "Alignment check", "Machine check",
    "SIMD error", "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor injection",
    "VMM communication", "Security", "Reserved",
};

void isr_dispatch(isr_frame_t* f) {
    if (f->vector < 32) {
        char buf[12];
        kprint("\n[CPU EXCEPTION] ");
        kprint(exc_names[f->vector]);
        kprint(" at EIP=");
        u32_to_hex(f->eip, buf);
        kprint(buf);
        kprint(" err=");
        u32_to_hex(f->error, buf);
        kprint(buf);
        kprint("\n");
        panic("unhandled CPU exception");
    }

    if (f->vector == IRQ_TIMER_VECTOR) {
        timer_ticks++;
        outb(0x20, 0x20);                    /* EOI для PIC */
        /* будим остальные ядра, чтобы они тоже проверили свою работу */
        lapic_send_ipi_all_others(IPI_TICK_VECTOR);
        scheduler_tick(0, timer_ticks * (1000000000ULL / TIMER_HZ));
        return;
    }

    if (f->vector == IPI_TICK_VECTOR) {
        lapic_eoi();
        return;
    }
    /* 39 и 255 — ложные прерывания, EOI для них не нужен */
}
