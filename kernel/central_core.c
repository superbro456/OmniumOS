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
#include "arch.h"
#include "lapic.h"
#include "multiboot.h"
#include "galaxy.h"

// Заголовок Multiboot теперь в boot/boot.asm, точка входа _start.

// ---------------- Глобальные переменные ----------------
volatile int system_initialized = 0;

// ---------------- CPU detection ----------------
// CPUID.1 EBX[23:16] — сколько логических ядер в корпусе. Это приближение:
// точный список ядер даёт таблица ACPI MADT (TODO). Лишние попытки запуска
// просто не получат ответа и будут пропущены.
static uint32_t get_cpu_count(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    uint32_t n = (edx & (1u << 28)) ? (ebx >> 16) & 0xFF : 1;
    if (n == 0) n = 1;
    if (n > SMP_MAX_CPUS) n = SMP_MAX_CPUS;
    return n;
}

static void print_num(const char* before, uint32_t v, const char* after) {
    char buf[12];
    u32_to_dec(v, buf);
    kprint(before);
    kprint(buf);
    kprint(after);
}

// Часы: клетка, которая спит и раз в секунду (потом раз в минуту) пишет время.
static void uptime_cell(void* arg) {
    (void)arg;
    uint32_t next_report = 1;
    for (;;) {
        cell_sleep_ms(next_report * 1000u - (uint32_t)gx_now_ms());
        print_num("[Timer] uptime ", next_report, " s\n");
        next_report = next_report < 3 ? next_report + 1 : next_report + 60;
    }
}

// ---------------- AP main ----------------
// Сюда приходит каждое второстепенное ядро из trampoline, уже со своим стеком.
void ap_entry(uint32_t index) {
    gdt_init();
    idt_load();
    lapic_enable();
    __atomic_add_fetch(&smp_cpus_online, 1, __ATOMIC_SEQ_CST);

    print_num("AP ", index, " started (APIC ID ");
    print_num("", cpu_id(), ").\n");

    galaxy_set_star(cpu_id(), (int)index);
    while (!system_initialized) __asm__ volatile("pause");
    galaxy_star_enter((int)index);   // ядро CPU становится звездой и больше не возвращается
}

// ---------------- BSP helpers ----------------
void start_all_aps(void) {
    uint32_t bsp_id = cpu_id();
    uint32_t cpu_count = get_cpu_count();
    uint32_t index = 1;
    for (uint32_t id = 0; id < cpu_count; id++) {
        if (id == bsp_id) continue;
        if (start_ap(id, index)) index++;
        else print_num("[SMP] CPU ", id, " did not respond\n");
    }
    print_num("[SMP] CPUs online: ", smp_cpus_online, "\n");
}

// ---------------- Kernel main ----------------
void kernel_main(uint32_t magic, multiboot_info_t* mbi) {
    serial_init();
    kclear_screen();
    kprint("Welcome to Omnium OS!\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not loaded by a Multiboot bootloader");

    gdt_init();
    idt_init();
    idt_load();
    pic_init();
    pit_init(TIMER_HZ);
    lapic_enable();
    kprint("[Arch] GDT, IDT, PIC, PIT ready.\n");

    // ---------------- Initialize cores ----------------
    uint32_t mem_kb = (mbi->flags & MULTIBOOT_INFO_MEMORY) ? mbi->mem_upper : 0;
    if (mem_kb == 0) panic("bootloader did not report memory size");
    memory_init((uint64_t)mem_kb * 1024);
    memory_test();

    process_core_init();
    kprint("[ProcessCore] Initialized.\n");

    security_core_init();
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

    kprint("Testing allowed syscall 0... ");
    kprint(security_check_syscall(0, test_pid) ? "allowed\n" : "DENIED (bug)\n");
    kprint("Testing forbidden syscall 1...\n");
    security_check_syscall(1, test_pid);  // запрещён → карантин

    // ---------------- Start APs ----------------
    galaxy_init();
    start_all_aps();
    lapic_mark_ready();
    system_initialized = 1;

    // ---------------- Galaxy ----------------
    // Каждое ядро CPU — звезда. Первая клетка — смотритель с проверками.
    while (galaxy_stars < (int)smp_cpus_online - 1) __asm__ volatile("pause");
    cpu_sti();
    cell_start(cell_new("uptime", uptime_cell, NULL, 0, ORBIT_SERVICE, 1, 0));
    cell_start(cell_new("supervisor", galaxy_demo, NULL, 0, ORBIT_SERVICE, 10, 0));
    galaxy_star_enter(0);
}
