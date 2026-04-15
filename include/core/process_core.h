#ifndef PROCESS_CORE_H
#define PROCESS_CORE_H

#include <stdint.h>
#include <stdbool.h>

// ----- PRIORITIES -----
typedef enum {
    PRIO_FOREGROUND = 1,
    PRIO_RECENT     = 2,
    PRIO_BACKGROUND = 3
} process_prio_t;

typedef enum {
    BG_SEMI,   // полуфоновые
    BG_DEEP    // глубокий фон
} bg_type_t;

// ----- PROCESS STRUCT -----
typedef struct registers {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
} registers_t;

typedef struct proc {
    int pid;
    process_prio_t prio;
    bg_type_t bg_type;

    registers_t ctx;

    uint64_t runtime_ns;
    uint64_t last_active_ns;

    uint64_t budget_ns;      // оставшийся бюджет
    uint64_t budget_cap_ns;  // максимальный бюджет

    bool running;
    bool wants_cpu;
    int affinity_cpu;

    struct proc *next;
} proc_t;

// ----- RUNQUEUES -----
#define MAX_CPUS 16

typedef struct runqueue {
    proc_t *foreground;
    proc_t *recent;
    proc_t *semi_bg;
    proc_t *deep_bg;
} runqueue_t;

extern runqueue_t runqueues[MAX_CPUS];
extern proc_t *current_on_cpu[MAX_CPUS];

// ----- API -----
void process_core_init(void);
proc_t* create_process(void (*entry)(void), int pid, process_prio_t prio);
void scheduler_tick(int cpu_id, uint64_t now_ns);
void yield_current(int cpu_id);
void set_process_priority(proc_t *p, process_prio_t prio);
void context_switch(proc_t *oldp, proc_t *newp);

#endif // PROCESS_CORE_H
