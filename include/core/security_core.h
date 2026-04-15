#pragma once
#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES 256
#define MAX_SYSCALLS  256

typedef enum {
    SEC_LEVEL_USER,
    SEC_LEVEL_ADMIN
} security_level_t;

typedef enum {
    EVT_AETHIS,
    EVT_PHANTOM,
    EVT_VOLTBREAKER,
    EVT_IRONVEIL,
    EVT_SHADOW,
    EVT_QUARANTINE
} security_event_t;

// Функции Security Core
void security_core_init(void);
void security_set_process_level(uint32_t pid, security_level_t level);
void security_set_whitelist(uint32_t pid, uint8_t* syscalls, int count);
security_level_t security_get_process_level(uint32_t pid);
int security_check_syscall(uint32_t syscall_id, uint32_t pid);
void quarantine_process(uint32_t pid);

// Новые функции для интеграции с слоями
void security_report_event(security_event_t type, uint32_t pid);
