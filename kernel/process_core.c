#include "process_core.h"
#include <stddef.h>
#include "kernel_string.h"  // Тут наш мини-string.h, freestanding style

// ----- ГЛОБАЛЫ -----
//  CPU и очереди процессов
runqueue_t runqueues[MAX_CPUS];
proc_t *current_on_cpu[MAX_CPUS] = {0};
static uint64_t spare_pool[MAX_CPUS] = {0};
#define BG_TURN_EVERY 8
static unsigned picks_since_bg[MAX_CPUS];

// ----- КВАНТЫ -----
// Сколько "времени CPU процессу дать "
static const uint64_t Q_FOREGROUND = 4 * 1000000ULL;
static const uint64_t Q_RECENT     = 8 * 1000000ULL;
static const uint64_t Q_BG_BATCH   = 12 * 1000000ULL;
static const uint64_t Q_BG_MIN     = 1 * 1000000ULL;

// ----- ОЧЕРЕДИ -----
// Забираем процесс из начало очереди
static proc_t* pop(proc_t **head) {
    proc_t *p = *head;
    if (!p) return NULL;
    *head = p->next;
    p->next = NULL;
    return p;
}

// Ставка процесса в конец очереди 
static void push(proc_t **head, proc_t *p) {
    p->next = NULL;   // иначе старый хвост списка может замкнуть очередь в кольцо
    if (!*head) { *head = p; return; }
    proc_t *it = *head;
    while (it->next) it = it->next;
    it->next = p;
}

// ----- ИНИЦИАЛИЗАЦИЯ -----
// Очистка всех процессов 
void process_core_init(void) {
    kmemset(runqueues, 0, sizeof(runqueues));
    kmemset(current_on_cpu, 0, sizeof(current_on_cpu));
    kmemset(spare_pool, 0, sizeof(spare_pool));
    kmemset(picks_since_bg, 0, sizeof(picks_since_bg));
}

// ----- ПРИОРИТЕТЫ -----
// Меняем приоритет процесса 
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
// Процесс снимается с очереди (pop): раньше он оставался в ней и при
// возврате попадал в очередь второй раз, создавая цикл.
// Каждый BG_TURN_EVERY-й выбор отдаётся фону, если он ждёт: так фон
// не голодает, даже когда активная программа занимает CPU постоянно.

static proc_t* pick_next(int cpu) {
    runqueue_t *rq = &runqueues[cpu];
    if (++picks_since_bg[cpu] >= BG_TURN_EVERY && (rq->semi_bg || rq->deep_bg)) {
        picks_since_bg[cpu] = 0;
        return rq->semi_bg ? pop(&rq->semi_bg) : pop(&rq->deep_bg);
    }
    if (rq->foreground) return pop(&rq->foreground);
    if (rq->recent)     return pop(&rq->recent);
    picks_since_bg[cpu] = 0;
    if (rq->semi_bg)    return pop(&rq->semi_bg);
    if (rq->deep_bg)    return pop(&rq->deep_bg);
    return NULL;  // Если нету процесса ЦПУ не используется 
}

// Фоновому процессу даём остаток чужого времени, но не меньше
// минимального кванта, иначе при пустом spare_pool фон не работает никогда.
static uint64_t bg_budget(int cpu) {
    uint64_t give = (spare_pool[cpu] > Q_BG_BATCH) ? Q_BG_BATCH : spare_pool[cpu];
    spare_pool[cpu] -= give;
    if (give < Q_BG_MIN) give = Q_BG_MIN;
    return give;
}

// ----- ПЕРЕКЛЮЧЕНИЕ КОНТЕКСТА -----
// Тут ASM-код должен сделать прыжок с процессора на процесс
void context_switch(proc_t *oldp, proc_t *newp) {
    // Пока просто разводим руками
    (void)oldp;
    (void)newp;
}

// ----- TICK Sheduller -----
// Каждый тик задача для ЦПУ
void scheduler_tick(int cpu, uint64_t now) {
    if (cpu < 0 || cpu >= MAX_CPUS) return;
    proc_t *cur = current_on_cpu[cpu];

    if (cur) {
        uint64_t elapsed = now - cur->last_active_ns;

        if (elapsed >= cur->budget_ns || !cur->wants_cpu) {
            // Отдаем остатки процессорного времени
            if (!cur->wants_cpu && cur->budget_ns > 0)
                spare_pool[cpu] += cur->budget_ns;

            cur->budget_ns = 0;

            // Возвращаем процесс в очередь 
            runqueue_t *rq = &runqueues[cpu];
            if (cur->prio == PRIO_FOREGROUND) push(&rq->foreground, cur);
            else if (cur->prio == PRIO_RECENT) push(&rq->recent, cur);
            else {
                if (cur->bg_type == BG_SEMI) push(&rq->semi_bg, cur);
                else                           push(&rq->deep_bg, cur);
            }

            // Выбираем следующего "готовый процесс" CPU
            proc_t *next = pick_next(cpu);

            if (next) {
                if (next->prio == PRIO_FOREGROUND)
                    next->budget_ns = Q_FOREGROUND;
                else if (next->prio == PRIO_RECENT)
                    next->budget_ns = Q_RECENT;
                else
                    next->budget_ns = bg_budget(cpu);
            }

            context_switch(cur, next);
            current_on_cpu[cpu] = next;
            if (next) next->last_active_ns = now;
        } else {
            // Процесс продолжает использовать CPU
            cur->budget_ns -= elapsed;
            cur->last_active_ns = now;
        }
        return;
    }

    // Если CPU в застое
    proc_t *next = pick_next(cpu);
    if (next) {
        if (next->prio == PRIO_FOREGROUND)
            next->budget_ns = Q_FOREGROUND;
        else if (next->prio == PRIO_RECENT)
            next->budget_ns = Q_RECENT;
        else
            next->budget_ns = bg_budget(cpu);

        current_on_cpu[cpu] = next;
        next->last_active_ns = now;
        context_switch(NULL, next);
    }
}
