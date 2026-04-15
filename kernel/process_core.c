#include "process_core.h"
#include <stddef.h>
#include "kernel_string.h"  // Тут наш мини-string.h, freestanding style 😎

// ----- ГЛОБАЛЫ -----
// Тут наши CPU и очереди процессов — как детская песочница,
// но с настоящими процессорами
runqueue_t runqueues[MAX_CPUS];
proc_t *current_on_cpu[MAX_CPUS] = {0};
static uint64_t spare_pool[MAX_CPUS] = {0};

// ----- КВАНТЫ -----
// Сколько "времени процессу дать пожевать пирог CPU"
static const uint64_t Q_FOREGROUND = 4 * 1000000ULL;
static const uint64_t Q_RECENT     = 8 * 1000000ULL;
static const uint64_t Q_BG_BATCH   = 12 * 1000000ULL;

// ----- ОЧЕРЕДИ -----
// Забираем процесс из головы очереди, как пирожок с полки
static proc_t* pop(proc_t **head) {
    proc_t *p = *head;
    if (!p) return NULL;
    *head = p->next;
    p->next = NULL;
    return p;
}

// Засовываем процесс в конец очереди, как конфетку в банку
static void push(proc_t **head, proc_t *p) {
    if (!*head) { *head = p; return; }
    proc_t *it = *head;
    while (it->next) it = it->next;
    it->next = p;
}

// ----- ИНИЦИАЛИЗАЦИЯ -----
// Обнуляем всё и вся — пусть CPU думает, что всё чисто
void process_core_init(void) {
    kmemset(runqueues, 0, sizeof(runqueues));
    kmemset(current_on_cpu, 0, sizeof(current_on_cpu));
    kmemset(spare_pool, 0, sizeof(spare_pool));
}

// ----- ПРИОРИТЕТЫ -----
// Меняем настроение процессу: "Сейчас ты важный!" или "Ты в уголочке"
void set_process_priority(proc_t *p, process_prio_t newp) {
    p->prio = newp;

    if (newp == PRIO_FOREGROUND)
        p->budget_cap_ns = Q_FOREGROUND * 2;
    else if (newp == PRIO_RECENT)
        p->budget_cap_ns = Q_RECENT * 2;
    else
        p->budget_cap_ns = Q_BG_BATCH * 4;
}

// ----- ВЫБОР СЛЕДУЮЩЕГО ПРОЦЕССА -----
// CPU спрашивает: "А кто следующий на ужин?"
static proc_t* pick_next(int cpu) {
    runqueue_t *rq = &runqueues[cpu];
    if (rq->foreground) return rq->foreground;
    if (rq->recent)     return rq->recent;
    if (rq->semi_bg)    return rq->semi_bg;
    if (rq->deep_bg)    return rq->deep_bg;
    return NULL;  // Никто не хочет есть — CPU отдыхает
}

// ----- ПЕРЕКЛЮЧЕНИЕ КОНТЕКСТА -----
// Тут ASM-штука должна прыгать с процесса на процесс
void context_switch(proc_t *oldp, proc_t *newp) {
    // Пока просто разводим руками
    (void)oldp;
    (void)newp;
}

// ----- TICK ШЕДУЛЕРА -----
// Каждый тиканье часов ядра — маленькая вечеринка для CPU
void scheduler_tick(int cpu, uint64_t now) {
    proc_t *cur = current_on_cpu[cpu];

    if (cur) {
        uint64_t elapsed = now - cur->last_active_ns;

        if (elapsed >= cur->budget_ns || !cur->wants_cpu) {
            // Отдаем остатки бюджета в общий котёл — щедрый CPU
            if (!cur->wants_cpu && cur->budget_ns > 0)
                spare_pool[cpu] += cur->budget_ns;

            cur->budget_ns = 0;

            // Возвращаем процесс в очередь — "иди постой в очереди, малыш"
            runqueue_t *rq = &runqueues[cpu];
            if (cur->prio == PRIO_FOREGROUND) push(&rq->foreground, cur);
            else if (cur->prio == PRIO_RECENT) push(&rq->recent, cur);
            else {
                if (cur->bg_type == BG_SEMI) push(&rq->semi_bg, cur);
                else                           push(&rq->deep_bg, cur);
            }

            // Выбираем следующего "жаждущего" CPU
            proc_t *next = pick_next(cpu);

            if (next) {
                if (next->prio == PRIO_FOREGROUND)
                    next->budget_ns = Q_FOREGROUND;
                else if (next->prio == PRIO_RECENT)
                    next->budget_ns = Q_RECENT;
                else {
                    uint64_t give = (spare_pool[cpu] > Q_BG_BATCH)
                        ? Q_BG_BATCH
                        : spare_pool[cpu];
                    next->budget_ns = give;
                    spare_pool[cpu] -= give;
                }
            }

            context_switch(cur, next);
            current_on_cpu[cpu] = next;
            if (next) next->last_active_ns = now;
        } else {
            // Процесс продолжает жевать CPU
            cur->budget_ns -= elapsed;
            cur->last_active_ns = now;
        }
        return;
    }

    // Если CPU просто скучает и никого нет
    proc_t *next = pick_next(cpu);
    if (next) {
        if (next->prio == PRIO_FOREGROUND)
            next->budget_ns = Q_FOREGROUND;
        else if (next->prio == PRIO_RECENT)
            next->budget_ns = Q_RECENT;
        else {
            uint64_t give = (spare_pool[cpu] > Q_BG_BATCH)
                ? Q_BG_BATCH
                : spare_pool[cpu];
            next->budget_ns = give;
            spare_pool[cpu] -= give;
        }

        current_on_cpu[cpu] = next;
        next->last_active_ns = now;
        context_switch(NULL, next);
    }
}
