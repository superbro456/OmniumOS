/* kernel/galaxy/star.c — звёзды (диспетчер на каждом ядре CPU) и клетки.
 *
 * Каждое ядро CPU — своя звезда со своей очередью готовых клеток. Общей
 * очереди и общего замка на всю систему нет: звезда трогает только свои
 * клетки, а чужая звезда лишь ставит клетку в очередь, когда будит её.
 *
 * Паёк (ration) — сколько миллисекунд подряд клетка работает за круг.
 * Две занятые клетки с пайками 30 и 10 делят ядро как 3 к 1.
 * Права паёк не даёт: права дают только ключи.
 */
#include <stdarg.h>
#include "galaxy.h"
#include "arch.h"
#include "lapic.h"
#include "memory_core.h"
#include "kernel_utils.h"
#include "kernel_string.h"

#define HOT_SPIN        20000   /* сколько раз проверить очередь перед сном */

typedef struct {
    gx_cell_t idle;              /* «сама звезда»: стек загрузки ядра CPU */
    gx_cell_t *current;
    gx_cell_t *ready_head, *ready_tail;
    gx_cell_t *grave;            /* умершие клетки, ждут уборки */
    gx_cell_t *sleepers;         /* спящие по таймеру */
    int32_t credit;              /* сколько мс осталось текущей клетке */
    uint32_t apic_id;
    volatile int lit;            /* звезда зажглась */
    volatile uint64_t last_busy; /* когда последний раз работала клетка */
    uint32_t switches;
    spinlock_t lock;
} star_t;

static star_t stars[GX_MAX_STARS];
static int8_t apic_to_star[256];
static int star_no[GX_MAX_STARS];
volatile int galaxy_stars = 0;

static gx_cell_t cells[GX_MAX_CELLS];
static uint8_t *slot_area;
static int slot_used[GX_MAX_CELLS];
static spinlock_t cells_lock = 0;
static int next_cell_id = 1;

extern void gx_switch(uint32_t *old_esp, uint32_t new_esp);
void gx_chan_close_by(gx_cell_t *c);     /* chan.c */
void gx_blocks_free_all(gx_cell_t *c);   /* chan.c */

/* ---------------- прерывания ---------------- */
static inline uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) : : "memory");
    return f;
}
static inline void irq_restore(uint32_t f) {
    if (f & 0x200) __asm__ volatile("sti" : : : "memory");
}

/* ---------------- номер звезды ---------------- */
/* вызывается на самом ядре CPU: запоминает APIC ID и ставит свой GS */
void galaxy_set_star(uint32_t apic_id, int star) {
    apic_to_star[apic_id & 0xFF] = (int8_t)star;
    stars[star].apic_id = apic_id;
    star_no[star] = star;
    gdt_set_percpu(star, (uint32_t)(uintptr_t)&star_no[star], sizeof(int));
}

/* номер звезды лежит по адресу gs:0 — одна команда, без чтения LAPIC */

int galaxy_star_index(void) {
    int s;
    __asm__ volatile("movl %%gs:0, %0" : "=r"(s));
    return s;
}

uint64_t gx_now_ms(void) {
    uint64_t a, b;
    do { a = timer_ticks; b = timer_ticks; } while (a != b);   /* 64 бита читаются в два приёма */
    _Static_assert(TIMER_HZ == 1000, "один тик = 1 мс");
    return a;
}

/* ---------------- вывод ---------------- */
static char *put_u(char *p, char *end, uint32_t v, int base) {
    char tmp[12];
    int n = 0;
    do { uint32_t d = v % base; tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v);
    while (n && p < end) *p++ = tmp[--n];
    return p;
}

void gx_printf(const char *fmt, ...) {
    char buf[256];
    char *p = buf, *end = buf + sizeof(buf) - 1;
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt && p < end; fmt++) {
        if (*fmt != '%') { *p++ = *fmt; continue; }
        fmt++;
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            while (s && *s && p < end) *p++ = *s++;
        } else if (*fmt == 'd') {
            int v = va_arg(ap, int);
            if (v < 0) { *p++ = '-'; v = -v; }
            p = put_u(p, end, (uint32_t)v, 10);
        } else if (*fmt == 'u') {
            p = put_u(p, end, va_arg(ap, uint32_t), 10);
        } else if (*fmt == 'x') {
            p = put_u(p, end, va_arg(ap, uint32_t), 16);
        } else if (*fmt) {
            *p++ = *fmt;
        } else break;
    }
    va_end(ap);
    *p = 0;
    kprint(buf);
}

/* ---------------- очередь готовых ---------------- */
static void rq_push(star_t *s, gx_cell_t *c) {
    c->next = NULL;
    if (s->ready_tail) s->ready_tail->next = c; else s->ready_head = c;
    s->ready_tail = c;
}

static gx_cell_t *rq_pop(star_t *s) {
    gx_cell_t *c = s->ready_head;
    if (c) {
        s->ready_head = c->next;
        if (!s->ready_head) s->ready_tail = NULL;
        c->next = NULL;
    }
    return c;
}

/* ---------------- уборка умерших ---------------- */
static void reap(star_t *s) {
    gx_cell_t **pp = &s->grave;
    while (*pp) {
        gx_cell_t *c = *pp;
        if (c == s->current) { pp = &c->next; continue; }   /* со своего стека себя не убрать */
        *pp = c->next;
        spinlock_acquire(&cells_lock);
        slot_used[c - cells] = 0;
        c->state = CELL_FREE;
        spinlock_release(&cells_lock);
    }
}

/* ---------------- переключение (прерывания выключены) ---------------- */
static void schedule(star_t *s) {
    gx_cell_t *prev = s->current, *next;
    spinlock_acquire(&s->lock);
    if (prev != &s->idle && prev->state == CELL_RUNNING) {
        prev->state = CELL_READY;
        rq_push(s, prev);
    }
    next = rq_pop(s);
    if (!next) next = &s->idle;
    next->state = CELL_RUNNING;
    s->credit = (int32_t)next->ration;
    s->current = next;
    spinlock_release(&s->lock);
    if (next != prev) {
        s->switches++;
        gx_switch(&prev->esp, next->esp);
    }
    reap(&stars[galaxy_star_index()]);
}

/* разбудить клетку (из любой звезды; прерывания должны быть выключены) */
void gx_wake(gx_cell_t *c) {
    star_t *s = &stars[c->star];
    int woke = 0;
    spinlock_acquire(&s->lock);
    if (c->state == CELL_WAITING) {
        c->state = CELL_READY;
        rq_push(s, c);
        woke = 1;
    }
    spinlock_release(&s->lock);
    if (woke && c->star != galaxy_star_index() && s->current == &s->idle)
        lapic_send_ipi(s->apic_id, ICR_VECTOR(IPI_WAKE_VECTOR));
}

/* текущая клетка засыпает до gx_wake (прерывания выключены, state уже WAITING) */
void gx_block(void) {
    schedule(&stars[galaxy_star_index()]);
}

void galaxy_tick(int star) {
    star_t *s = &stars[star];
    if (!s->lit) return;
    if (s->sleepers) {
        uint64_t now = gx_now_ms();
        gx_cell_t **pp = &s->sleepers;
        while (*pp) {
            gx_cell_t *c = *pp;
            if (c->wake_at <= now) { *pp = c->next_sleep; gx_wake(c); }
            else pp = &c->next_sleep;
        }
    }
    gx_cell_t *cur = s->current;
    if (cur == &s->idle) return;
    cur->ticks++;
    s->last_busy = timer_ticks;
    if (--s->credit <= 0) schedule(s);
}

/* ---------------- клетки ---------------- */
gx_cell_t *cell_self(void) {
    star_t *s = &stars[galaxy_star_index()];
    return s->current == &s->idle ? NULL : s->current;
}

void cell_yield(void) {
    uint32_t f = irq_save();
    schedule(&stars[galaxy_star_index()]);
    irq_restore(f);
}

void cell_sleep_ms(uint32_t ms) {
    gx_cell_t *c = cell_self();
    if (!c || (int32_t)ms <= 0) return;
    uint32_t f = irq_save();
    star_t *s = &stars[c->star];
    c->wake_at = gx_now_ms() + ms;
    c->state = CELL_WAITING;
    c->next_sleep = s->sleepers;     /* список трогает только своя звезда */
    s->sleepers = c;
    schedule(s);
    irq_restore(f);
}

static void cell_die(gx_cell_t *c, const char *why) {
    /* прерывания уже выключены */
    c->death = why;
    gx_chan_close_by(c);       /* все каналы клетки закрываются, соседи получат ECLOSED */
    gx_blocks_free_all(c);     /* её данные в обменной куче возвращаются */
    star_t *s = &stars[c->star];
    spinlock_acquire(&s->lock);
    c->state = CELL_DEAD;
    c->next = s->grave;
    s->grave = c;
    spinlock_release(&s->lock);
    schedule(s);
    panic("dead cell resumed");
}

void cell_exit(void) {
    irq_save();
    cell_die(cell_self(), NULL);
    for (;;) {}
}

static void gx_cell_start(void) {
    star_t *s = &stars[galaxy_star_index()];
    reap(s);
    gx_cell_t *c = s->current;
    __asm__ volatile("sti");
    c->entry(c->arg);
    cell_exit();
}

gx_cell_t *cell_new(const char *name, void (*entry)(void *), void *arg,
                    int star, gx_orbit_t orbit, uint32_t ration, uint32_t heap_quota) {
    if (star < 0 || star >= galaxy_stars) star = 0;
    if (heap_quota > GX_SLOT_SIZE - GX_STACK_SIZE) return NULL;

    spinlock_acquire(&cells_lock);
    int i;
    for (i = 0; i < GX_MAX_CELLS && slot_used[i]; i++) {}
    if (i == GX_MAX_CELLS) { spinlock_release(&cells_lock); return NULL; }
    slot_used[i] = 1;
    int id = next_cell_id++;
    spinlock_release(&cells_lock);

    gx_cell_t *c = &cells[i];
    kmemset(c, 0, sizeof(*c));
    c->id = id;
    for (int k = 0; k < GX_NAME_LEN - 1 && name[k]; k++) c->name[k] = name[k];
    c->orbit = orbit;
    c->star = star;
    c->ration = ration ? ration : 10;
    c->heap_quota = heap_quota;
    c->slot = slot_area + (uint32_t)i * GX_SLOT_SIZE;
    c->entry = entry;
    c->arg = arg;

    /* начальный стек так, как его оставил бы gx_switch */
    uint32_t *sp = (uint32_t *)(c->slot + GX_STACK_SIZE);
    *--sp = 0;                                  /* «адрес возврата» из gx_cell_start */
    *--sp = (uint32_t)(uintptr_t)gx_cell_start; /* ret */
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; /* ebp ebx esi edi */
    *--sp = 0x2;                                /* eflags: прерывания выключены */
    c->esp = (uint32_t)(uintptr_t)sp;

    c->state = CELL_WAITING;      /* ждёт cell_start: сначала выдаём ключи */
    return c;
}

void cell_start(gx_cell_t *c) {
    uint32_t f = irq_save();
    gx_wake(c);
    irq_restore(f);
}

gx_cell_t *cell_by_id(int id) {
    for (int i = 0; i < GX_MAX_CELLS; i++)
        if (slot_used[i] && cells[i].id == id) return &cells[i];
    return NULL;
}

void *cell_alloc(uint32_t size) {
    gx_cell_t *c = cell_self();
    if (!c) return NULL;
    size = (size + 15) & ~15u;
    if (size > c->heap_quota - c->heap_used) return NULL;   /* квота: отказ, а не падение */
    void *p = c->slot + GX_STACK_SIZE + c->heap_used;
    c->heap_used += size;
    return p;
}

/* ---------------- падения ---------------- */
int galaxy_handle_fault(uint32_t vector, const char *what) {
    (void)vector;
    if (!galaxy_stars) return 0;
    star_t *s = &stars[galaxy_star_index()];
    if (!s->lit || s->current == &s->idle) return 0;   /* упала сама звезда — это паника */
    gx_cell_t *c = s->current;
    gx_printf("[Galaxy] cell '%s' crashed: %s. Cell removed, others keep running.\n", c->name, what);
    cell_die(c, what);
    return 1;
}

/* ---------------- звёзды ---------------- */
void galaxy_init(void) {
    for (int i = 0; i < 256; i++) apic_to_star[i] = -1;
    slot_area = memory_allocate(ZONE_FAST_APP, GX_MAX_CELLS * GX_SLOT_SIZE);
    if (!slot_area) panic("galaxy: no memory for cells");
    galaxy_set_star(lapic_get_id(), 0);
}

void galaxy_star_enter(int star) {
    star_t *s = &stars[star];
    s->idle.id = 0;
    s->idle.star = star;
    s->idle.state = CELL_RUNNING;
    s->idle.ration = 1;
    s->current = &s->idle;
    s->lit = 1;
    __atomic_add_fetch(&galaxy_stars, 1, __ATOMIC_SEQ_CST);

    for (;;) {
        __asm__ volatile("cli");
        if (s->ready_head) { schedule(s); __asm__ volatile("sti"); continue; }
        __asm__ volatile("sti");
        /* «горячая» звезда: если только что была работа, немного ждём без сна —
           проснуться из hlt дороже, чем покрутиться */
        if (timer_ticks - s->last_busy < 2) {
            for (int i = 0; i < HOT_SPIN && !s->ready_head; i++) __asm__ volatile("pause");
            if (s->ready_head) continue;
        }
        __asm__ volatile("cli");
        if (!s->ready_head) __asm__ volatile("sti; hlt");
        else __asm__ volatile("sti");
    }
}

uint32_t galaxy_switches(int star) { return stars[star].switches; }
