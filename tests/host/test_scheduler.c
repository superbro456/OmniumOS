// Проверка планировщика на обычном компьютере (без QEMU): make unittest
#include <stdio.h>
#include <string.h>
#include "process_core.h"

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok: %s\n", msg); } while (0)

static int queue_len(proc_t* h) {
    int n = 0;
    for (proc_t* p = h; p && n < 100; p = p->next) n++;
    return n;
}

int main(void) {
    static proc_t fg, bg;
    process_core_init();
    memset(&fg, 0, sizeof fg); memset(&bg, 0, sizeof bg);
    fg.pid = 1; fg.wants_cpu = true; set_process_priority(&fg, PRIO_FOREGROUND);
    bg.pid = 2; bg.wants_cpu = true; set_process_priority(&bg, PRIO_BACKGROUND); bg.bg_type = BG_DEEP;
    runqueues[0].foreground = &fg;
    runqueues[0].deep_bg = &bg;

    uint64_t now = 0;
    int bg_runs = 0, fg_runs = 0;
    for (int t = 0; t < 200; t++) {
        now += 1000000;   // 1 мс
        scheduler_tick(0, now);
        proc_t* cur = current_on_cpu[0];
        if (cur == &bg) bg_runs++;
        if (cur == &fg) fg_runs++;
        if (queue_len(runqueues[0].foreground) >= 100 || queue_len(runqueues[0].deep_bg) >= 100) {
            CHECK(0, "queues never form a cycle");
            return 1;
        }
    }
    CHECK(1, "queues never form a cycle");
    CHECK(current_on_cpu[0] != NULL && current_on_cpu[0]->next == NULL, "running process is not left in a queue");
    CHECK(fg_runs > bg_runs, "foreground gets more CPU than background");
    CHECK(bg_runs > 0, "background is not starved");
    return fails ? 1 : 0;
}
