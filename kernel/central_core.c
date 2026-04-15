#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "central_core.h"
#include "smp.h"
#include "flexipc.h"
#include "process_core.h"
#include "memory_core.h"
#include "security_core.h"   // Security Core
#include "metrics_core.h"
#include "optimize_core.h"
#include "ai_core.h"
#include "parent_modules/parent_module1.h"
#include "parent_modules/parent_module2.h"
#include "parent_modules/parent_module3.h"
#include "parent_modules/parent_module4.h"
#include "kernel_utils.h"
#include "spinlock.h"

// ---------------- Multiboot Header ----------------
__attribute__((section(".multiboot"), used, aligned(4)))
const uint32_t multiboot_header[] = {
    0x1BADB002,
    0x00,
    -(0x1BADB002 + 0x00)
};

// ---------------- Глобальные переменные ----------------
volatile int system_initialized = 0;

// ---------------- Serial output ----------------
static inline void outb(uint16_t port, uint8_t val) { __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t ret; __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port)); return ret; }

void serial_init(void) {
    outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03); outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03); outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}
int serial_is_transmit_empty(void) { return (inb(0x3F8 + 5) & 0x20) != 0; }
void serial_write(char c) { while (!serial_is_transmit_empty()) __asm__ volatile("pause"); outb(0x3F8, (uint8_t)c); }

// ---------------- Spinlock for serial ----------------
static spinlock_t serial_lock = {0};
void serial_print(const char* str) {
    spinlock_acquire(&serial_lock);
    while (*str) {
        if (*str == '\n') serial_write('\r');
        serial_write(*str++);
    }
    spinlock_release(&serial_lock);
}

// ---------------- CPU detection ----------------
static uint32_t get_cpu_count(void) {
    uint32_t eax=0, ebx=0, ecx=0, edx=0;
    __asm__ volatile(
        "mov $1, %%eax\n\t"
        "cpuid\n\t"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        :
        : "memory"
    );
    return (ebx >> 16) & 0xFF;
}

// ---------------- AP main ----------------
void ap_main(void) {
    while (!system_initialized) __asm__ volatile("pause");

    char buf[12];
    u32_to_dec((uint32_t)cpu_id(), buf);
    serial_print("AP ");
    serial_print(buf);
    serial_print(" started.\n");

    for (;;) {
        flexipc_msg_t msg;
        while(flexipc_receive(&msg)) {
            switch(msg.dst) {
                case 1: plant1_command(msg.cmd,msg.payload); break;
                case 2: plant2_command(msg.cmd,msg.payload); break;
                case 3: plant3_command(msg.cmd,msg.payload); break;
                case 4: plant4_command(msg.cmd,msg.payload); break;
            }
        }
        plant1_update();
        plant2_update();
        plant3_update();
        plant4_update();
        __asm__ volatile("hlt");
    }
}

// ---------------- BSP helpers ----------------
void start_all_aps(void) {
    uint32_t bsp_id = cpu_id();
    uint32_t cpu_count = get_cpu_count();
    for (uint32_t i=0;i<cpu_count;i++)
        if (i!=bsp_id) start_ap(i);
}

// ---------------- Kernel main ----------------
void kernel_main(void) {
    if (cpu_id() != 0) { ap_main(); return; }

    serial_init();
    serial_print("BSP started.\n");
    kclear_screen();  
    kprint("Welcome to Omnium OS!\n");

    // ---------------- Initialize cores ----------------
    memory_init();
    memory_test();

    serial_print("[ProcessCore] Initialization started...\n");
    process_core_init();
    serial_print("[ProcessCore] Initialization completed.\n");

    security_core_init();   // Security Core
    metrics_init();
    optimize_init();
    ai_init();

    plant1_init();
    plant2_init();
    plant3_init();
    plant4_init();

    kprint("All cores and plants initialized.\n");

    // ---------------- Test quarantine ----------------
    uint32_t test_pid = 42;
    security_set_process_level(test_pid, SEC_LEVEL_USER);
    uint8_t whitelist[MAX_SYSCALLS] = {0};
    whitelist[0] = 1;  // Разрешаем syscall 0
    security_set_whitelist(test_pid, whitelist, MAX_SYSCALLS);

    kprint("Testing forbidden syscall...\n");
    security_check_syscall(1, test_pid);  // ← syscall 1 запрещён → должен вызвать карантин

    // ---------------- Start APs ----------------
    system_initialized = 1;
    start_all_aps();

    // ---------------- Main loop BSP ----------------
    static spinlock_t ipc_lock = {0};
    for (;;) {
        flexipc_msg_t msg;
        while(flexipc_receive(&msg)) {
            spinlock_acquire(&ipc_lock);
            switch(msg.dst) {
                case 1: plant1_command(msg.cmd,msg.payload); break;
                case 2: plant2_command(msg.cmd,msg.payload); break;
                case 3: plant3_command(msg.cmd,msg.payload); break;
                case 4: plant4_command(msg.cmd,msg.payload); break;
            }
            spinlock_release(&ipc_lock);
        }
        plant1_update();
        plant2_update();
        plant3_update();
        plant4_update();
        __asm__ volatile("hlt");
    }
}
