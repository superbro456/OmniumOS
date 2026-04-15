#include "security_core.h"
#include "kernel_utils.h"                // для kprint и u32_to_dec
#include "parent_modules/parent_module4.h" // для plant4_command и макросов

typedef struct {
    uint32_t pid;
    security_level_t level;
    uint8_t whitelist[MAX_SYSCALLS]; // 1 = разрешён
} process_security_t;

static process_security_t process_table[MAX_PROCESSES];

// ------------------------- Инициализация -------------------------
void security_core_init() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid = 0;
        process_table[i].level = SEC_LEVEL_USER;
        for (int j = 0; j < MAX_SYSCALLS; j++)
            process_table[i].whitelist[j] = 0;
    }
    kprint("[Security] Security core initialized\n");
}

// ------------------------- Установка уровня процесса -------------------------
void security_set_process_level(uint32_t pid, security_level_t level) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == 0) {
            process_table[i].pid = pid;
            process_table[i].level = level;
            return;
        }
    }
}

// ------------------------- Установка whitelist для процесса -------------------------
void security_set_whitelist(uint32_t pid, uint8_t* syscalls, int count) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid) {
            for (int j = 0; j < MAX_SYSCALLS && j < count; j++)
                process_table[i].whitelist[j] = syscalls[j];
            return;
        }
    }
}

// ------------------------- Получение уровня процесса -------------------------
security_level_t security_get_process_level(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid) return process_table[i].level;
    }
    return SEC_LEVEL_USER;
}

// ------------------------- Карантин процесса -------------------------
void quarantine_process(uint32_t pid) {
    char buf[12];
    u32_to_dec(pid, buf);
    kprint("[Security] Process ");
    kprint(buf);
    kprint(" quarantined!\n");

    // ----------------- Интеграция с Plant4 -----------------
    plant4_command(PLANT4_CMD_QUARANTINE, (void*)(uintptr_t)pid);
}

// ------------------------- Проверка syscall -------------------------
int security_check_syscall(uint32_t syscall_id, uint32_t pid) {
    process_security_t *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (process_table[i].pid == pid) proc = &process_table[i];

    if (!proc) return 0; // неизвестный процесс → запрещено

    if (syscall_id < MAX_SYSCALLS && proc->whitelist[syscall_id]) return 1;

    char buf[12];
    u32_to_dec(syscall_id, buf);
    kprint("[Security] Forbidden syscall ");
    kprint(buf);
    kprint(" attempted by process ");
    u32_to_dec(pid, buf);
    kprint(buf);
    kprint("\n");

    quarantine_process(pid);
    return 0;
}