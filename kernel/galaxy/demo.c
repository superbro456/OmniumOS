/* kernel/galaxy/demo.c — проверка галактического ядра и замеры.
 *
 * Клетка-смотритель (supervisor) по очереди запускает проверки. Каждая
 * проверка — несколько клеток, которые сообщают результат по каналу.
 * Строки "[Check] ... OK" ищет автотест (make test).
 */
#include "galaxy.h"
#include "kernel_utils.h"
#include "kernel_string.h"
#include "arch.h"

gx_cell_t *cell_by_id(int id);
uint32_t galaxy_switches(int star);

/* ---------------- время ---------------- */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t div64(uint64_t a, uint32_t b) {   /* без libgcc */
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { r -= b; q |= (uint64_t)1 << i; }
    }
    return q;
}

static uint32_t tsc_per_us;

static void calibrate(void) {
    uint64_t t0 = gx_now_ms();
    while (gx_now_ms() == t0) cell_yield();
    uint64_t a = rdtsc(), m0 = gx_now_ms();
    while (gx_now_ms() < m0 + 100) cell_yield();
    tsc_per_us = (uint32_t)div64(rdtsc() - a, 100000);
    if (!tsc_per_us) tsc_per_us = 1;
}

/* нс на одну операцию с одним знаком после запятой: "113.4" */
static void print_ns(const char *what, uint64_t cycles, uint32_t ops) {
    uint64_t tenth_ns = div64(div64(cycles * 10000u, tsc_per_us), ops);
    gx_printf("[Measure] %s: %u.%u ns\n", what, (uint32_t)(tenth_ns / 10), (uint32_t)(tenth_ns % 10));
}

/* ---------------- база для сравнения в том же эмуляторе ---------------- */
__attribute__((noinline)) static void empty_func(volatile int *x) { (*x)++; }

static void baselines(void) {
    volatile int x = 0;
    enum { N = 20000 };
    uint64_t t = rdtsc();
    for (int i = 0; i < N; i++) empty_func(&x);
    print_ns("baseline: function call", rdtsc() - t, N);
    t = rdtsc();
    for (int i = 0; i < N; i++) __asm__ volatile("int $255");   /* вход в ядро и выход, как пустой syscall */
    print_ns("baseline: interrupt enter+leave (like empty syscall)", rdtsc() - t, N);
}

/* ---------------- общий канал отчётов ---------------- */
static gx_chan_t *report;
static uint32_t S_REPORT, S_PING, S_REQ, S_STATE;

typedef struct { int key_report, k1, k2, k3; uint32_t n; volatile uint64_t until; } args_t;
static args_t A[24];

static gx_cell_t *spawn(const char *name, void (*f)(void *), args_t *a, int star, gx_orbit_t orbit,
                        uint32_t ration, uint32_t quota) {
    if (star >= galaxy_stars) star = galaxy_stars - 1;
    gx_cell_t *c = cell_new(name, f, a, star, orbit, ration, quota);
    cell_pin(c);    /* проверкам 1–8 важно, на какой звезде живёт клетка */
    a->key_report = key_grant(c, report, KEY_SEND, S_REPORT);
    return c;
}

/* ---------------- 1. паёк ---------------- */
static void busy(void *p) {
    args_t *a = p;
    volatile uint32_t work = 0;
    while (gx_now_ms() < a->until) work++;
    ch_send(a->key_report, 1, (uint32_t)cell_self()->ticks, NULL);
}

/* ---------------- 2–3. пинг-понг ---------------- */
static void pinger(void *p) {
    args_t *a = p;
    gx_msg_t m;
    for (int i = 0; i < 100; i++) { ch_send(a->k1, 1, i, NULL); ch_recv(a->k2, &m); }  /* прогрев */
    uint64_t t = rdtsc();
    for (uint32_t i = 0; i < a->n; i++) { ch_send(a->k1, 1, i, NULL); ch_recv(a->k2, &m); }
    t = rdtsc() - t;
    ch_send(a->k1, 2, 0, NULL);
    ch_send(a->key_report, 2, (uint32_t)(t >> 32), NULL);
    ch_send(a->key_report, 2, (uint32_t)t, NULL);
}

static void ponger(void *p) {
    args_t *a = p;
    gx_msg_t m;
    while (ch_recv(a->k1, &m) == GX_OK && m.tag == 1) ch_send(a->k2, 1, m.value, NULL);
}

static void ping_pong(int star_a, int star_b, uint32_t n, int base) {
    gx_chan_t *there = chan_new(S_PING), *back = chan_new(S_PING);
    args_t *pa = &A[base], *pb = &A[base + 1];
    kmemset(pa, 0, sizeof(*pa)); kmemset(pb, 0, sizeof(*pb));
    pa->n = n;
    gx_cell_t *b = spawn("ponger", ponger, pb, star_b, ORBIT_APP, 10, 0);
    pb->k1 = key_grant(b, there, KEY_RECV, S_PING);
    pb->k2 = key_grant(b, back, KEY_SEND, S_PING);
    gx_cell_t *a = spawn("pinger", pinger, pa, star_a, ORBIT_APP, 10, 0);
    pa->k1 = key_grant(a, there, KEY_SEND, S_PING);
    pa->k2 = key_grant(a, back, KEY_RECV, S_PING);
    cell_start(b);
    cell_start(a);
}

/* ---------------- 4. move против копии ----------------
 * Блок 64 КБ ездит туда-обратно между двумя клетками. Если move работает,
 * круг с блоком стоит столько же, сколько круг с пустым сообщением. */
static void mover(void *p) {
    args_t *a = p;
    enum { N = 2000, SZ = 64 * 1024 };
    gx_msg_t m;
    gx_block_t *src = block_new(SZ), *dst = block_new(SZ);
    kmemset(block_data(src), 7, SZ);
    uint64_t t = rdtsc();
    for (int i = 0; i < 200; i++) kmemcpy(block_data(dst), block_data(src), SZ);
    uint64_t copy = div64(rdtsc() - t, 200);

    gx_block_t *b = src;
    uint32_t same = 0;
    for (int i = 0; i < 100; i++) { ch_send(a->k1, 1, 0, b); ch_recv(a->k2, &m); b = m.block; }  /* прогрев */
    t = rdtsc();
    for (int i = 0; i < N; i++) {
        ch_send(a->k1, 1, 0, b);
        ch_recv(a->k2, &m);
        same += (m.block == src && m.block->owner == cell_self());   /* тот же блок, теперь снова наш */
        b = m.block;
    }
    uint64_t trip = div64(rdtsc() - t, N);
    ch_send(a->k1, 2, 0, NULL);
    ch_send(a->key_report, 4, (uint32_t)copy, NULL);
    ch_send(a->key_report, 4, (uint32_t)trip, NULL);
    ch_send(a->key_report, 4, same, NULL);
}

static void taker(void *p) {
    args_t *a = p;
    gx_msg_t m;
    while (ch_recv(a->k1, &m) == GX_OK && m.tag == 1) ch_send(a->k2, 1, 0, m.block);  /* отдаём обратно */
}

/* ---------------- 5. падение ---------------- */
static void bad_div(void *p) {
    (void)p;
    volatile int zero = 0;
    volatile int x = 10 / zero;     /* деление на ноль */
    (void)x;
}

static void bad_opcode(void *p) {
    (void)p;
    __asm__ volatile("ud2");        /* неизвестная команда */
}

static void friend_of_bad(void *p) {
    args_t *a = p;
    gx_msg_t m;
    int r = ch_recv(a->k1, &m);     /* ждём сообщения от упавшей клетки */
    ch_send(a->key_report, 5, (uint32_t)(-r), NULL);
}

static void neighbour(void *p) {
    args_t *a = p;
    uint32_t n = 0;
    while (gx_now_ms() < a->until) { n++; if ((n & 0xFFF) == 0) cell_yield(); }
    ch_send(a->key_report, 5, 100 + (n > 0), NULL);
}

/* ---------------- 6. квота памяти ---------------- */
static void greedy(void *p) {
    args_t *a = p;
    uint32_t got = 0;
    while (cell_alloc(16 * 1024)) got++;
    ch_send(a->key_report, 6, got, NULL);
}

/* ---------------- 7. ключи ---------------- */
static void keyholder(void *p) {
    args_t *a = p;
    gx_msg_t m;
    int r1 = ch_send(5, 1, 0, NULL);          /* ключа №5 нет */
    int r2 = ch_send(a->k2, 1, 0, NULL);      /* ключ только на чтение */
    int r3 = ch_send(a->k1, 1, 0, NULL);      /* нормальный ключ */
    ch_send(a->key_report, 7, (uint32_t)(-r1) * 100 + (uint32_t)(-r2) * 10 + (uint32_t)(-r3), NULL);
    ch_recv(a->k3, &m);                       /* смотритель отзывает ключи */
    int r4 = ch_send(a->k1, 1, 0, NULL);
    ch_send(a->key_report, 7, (uint32_t)(-r4), NULL);
}

/* ---------------- 8. горячая замена ---------------- */
#define T_REQ     1
#define T_SWAP    9
#define T_ABORT   8
typedef struct { uint32_t served, sum, key_req, key_rep; } svc_state_t;

static void service(void *p);

static void client(void *p) {
    args_t *a = p;
    gx_msg_t m;
    uint32_t ok = 0, by[3] = {0};
    for (uint32_t i = 1; i <= a->n; i++) {
        if (ch_send(a->k1, T_REQ, i, NULL) != GX_OK) break;
        if (ch_recv(a->k2, &m) != GX_OK) break;
        if (m.value == i * 2) ok++;
        if (m.tag < 3) by[m.tag]++;
        if (i == 1000) {               /* «нагрузка идёт»: ждём, пока смотритель затеет замену */
            ch_send(a->key_report, 86, i, NULL);
            gx_msg_t go;
            ch_recv(a->k3, &go);
        }
    }
    ch_send(a->key_report, 82, ok, NULL);
    ch_send(a->key_report, 83, by[1], NULL);
    ch_send(a->key_report, 84, by[2], NULL);
}

/* версия сервиса: 1 или 2. Состояние — сколько обслужено и сумма. */
static void service(void *p) {
    args_t *a = p;
    gx_msg_t m;
    uint32_t version = a->n;
    svc_state_t st = { 0, 0, (uint32_t)a->k1, (uint32_t)a->k2 };
    if (version == 2) {
        /* новая версия сначала забирает состояние старой */
        if (ch_recv(a->k3, &m) != GX_OK || m.tag == T_ABORT) return;
        st = *(svc_state_t *)block_data(m.block);
        block_free(m.block);
        ch_send(a->key_report, 81, st.served, NULL);
    }
    for (;;) {
        if (ch_recv((int)st.key_req, &m) != GX_OK) break;
        if (m.tag == T_REQ) {
            st.served++;
            st.sum += m.value;
            ch_send((int)st.key_rep, version, m.value * 2, NULL);
        } else if (m.tag == T_SWAP) {
            gx_cell_t *to = cell_by_id((int)(m.value & 0xFFFF));
            int handoff = (int)(m.value >> 16);       /* мой ключ на канал передачи */
            int kq = key_pass((int)st.key_req, to);
            if (kq < 0) {                               /* схема не та — работаю дальше */
                ch_send(a->key_report, 85, (uint32_t)(-kq), NULL);
                ch_send(handoff, T_ABORT, 0, NULL);
                continue;
            }
            int kr = key_pass((int)st.key_rep, to);
            svc_state_t *ns = block_data(block_new(sizeof(svc_state_t)));
            ns->served = st.served; ns->sum = st.sum;
            ns->key_req = (uint32_t)kq; ns->key_rep = (uint32_t)kr;
            ch_send(handoff, 1, 0, (gx_block_t *)ns - 1);
            return;                                     /* старая версия уходит */
        }
    }
}

/* ---------------- 9. танец звёзд ---------------- */
static void planet(void *p) {
    args_t *a = p;
    volatile uint32_t work = 0;
    while (gx_now_ms() < a->until) work++;
    gx_cell_t *me = cell_self();
    ch_send(a->key_report, 9, ((uint32_t)me->star << 16) | me->moves, NULL);
}

/* ---------------- смотритель ---------------- */
static int rk;   /* ключ смотрителя на чтение отчётов */

static uint32_t get(void) {
    gx_msg_t m;
    if (ch_recv(rk, &m) != GX_OK) return 0xFFFFFFFF;
    return m.value;
}

static gx_msg_t getm(void) {
    gx_msg_t m = { 0, 0xFFFFFFFF, NULL };
    ch_recv(rk, &m);
    return m;
}

static void check(const char *name, int ok, const char *detail) {
    gx_printf("[Check] %s: %s%s%s\n", name, ok ? "OK" : "FAILED", detail ? " - " : "", detail ? detail : "");
}

void galaxy_demo(void *arg) {
    (void)arg;
    gx_cell_t *me = cell_self();
    S_REPORT = gx_schema("report{tag:int,value:int}");
    S_PING   = gx_schema("ping{n:int}");
    S_REQ    = gx_schema("request{n:int}");
    S_STATE  = gx_schema("state{served:int,sum:int}");
    report = chan_new(S_REPORT);
    rk = key_grant(me, report, KEY_RECV, S_REPORT);
    key_grant(me, report, KEY_SEND, S_REPORT);   /* смотритель держит канал открытым между проверками */
    int far = galaxy_stars > 1 ? 1 : 0;
    int far2 = galaxy_stars > 2 ? 2 : far;

    calibrate();
    gx_printf("[Galaxy] %d stars lit, TSC %u MHz. Running checks...\n", galaxy_stars, tsc_per_us);
    baselines();

    /* 1. паёк: 30 и 10 мс за круг -> доля 3:1 */
    kmemset(&A[2], 0, sizeof(args_t) * 2);
    A[2].until = A[3].until = gx_now_ms() + 1000;
    gx_cell_t *b30 = spawn("busy30", busy, &A[2], far, ORBIT_APP, 30, 0);
    gx_cell_t *b10 = spawn("busy10", busy, &A[3], far, ORBIT_APP, 10, 0);
    cell_start(b30); cell_start(b10);
    uint32_t t1 = get(), t2 = get();
    uint32_t hi = t1 > t2 ? t1 : t2, lo = t1 > t2 ? t2 : t1;
    uint32_t ratio10 = lo ? hi * 10 / lo : 0;
    gx_printf("[Measure] ration 30:10 -> CPU ms %u and %u, share %u.%u : 1\n", hi, lo, ratio10 / 10, ratio10 % 10);
    check("ration splits CPU 3:1", ratio10 >= 25 && ratio10 <= 35, NULL);

    /* 2. пинг-понг на одной звезде (каждый круг = 2 переключения клеток) */
    uint32_t n = 20000;
    ping_pong(far, far, n, 16);
    uint64_t c = ((uint64_t)get() << 32); c |= get();
    print_ns("channel round trip, same star (send+recv+2 switches)", c, n);
    check("channels on one star", c > 0, NULL);

    /* 3. пинг-понг между звёздами */
    if (far2 != far) {
        ping_pong(far, far2, n, 18);
        c = ((uint64_t)get() << 32); c |= get();
        print_ns("channel round trip, two stars (cores)", c, n);
        check("channels between stars", c > 0, NULL);
    }

    /* 4. move против копии 64 КБ */
    {
        gx_chan_t *ch = chan_new(S_PING), *back = chan_new(S_PING);
        kmemset(&A[4], 0, sizeof(args_t) * 2);
        gx_cell_t *t = spawn("taker", taker, &A[5], far, ORBIT_APP, 10, 0);
        A[5].k1 = key_grant(t, ch, KEY_RECV, S_PING);
        A[5].k2 = key_grant(t, back, KEY_SEND, S_PING);
        gx_cell_t *mv = spawn("mover", mover, &A[4], far, ORBIT_APP, 10, 0);
        A[4].k1 = key_grant(mv, ch, KEY_SEND, S_PING);
        A[4].k2 = key_grant(mv, back, KEY_RECV, S_PING);
        cell_start(t); cell_start(mv);
        uint32_t copy = get(), trip = get(), same = get();
        print_ns("copy 64 KB (memcpy)", copy, 1);
        print_ns("round trip carrying 64 KB block by move", trip, 1);
        check("move: same block goes there and back, no copy", same == 2000, NULL);
    }

    /* 5. падение клетки */
    {
        gx_chan_t *ch = chan_new(S_PING), *ch2 = chan_new(S_PING);
        kmemset(&A[6], 0, sizeof(args_t) * 5);
        A[8].until = gx_now_ms() + 300;
        cell_start(spawn("neighbour", neighbour, &A[8], far, ORBIT_APP, 10, 0));
        gx_cell_t *f1 = spawn("friend1", friend_of_bad, &A[6], far2, ORBIT_APP, 10, 0);
        A[6].k1 = key_grant(f1, ch, KEY_RECV, S_PING);
        gx_cell_t *f2 = spawn("friend2", friend_of_bad, &A[7], far2, ORBIT_APP, 10, 0);
        A[7].k1 = key_grant(f2, ch2, KEY_RECV, S_PING);
        gx_cell_t *b1 = cell_new("bad-div", bad_div, NULL, far, ORBIT_KUIPER, 10, 0);
        key_grant(b1, ch, KEY_SEND, S_PING);
        cell_pin(b1);
        gx_cell_t *b2 = cell_new("bad-opcode", bad_opcode, NULL, far, ORBIT_KUIPER, 10, 0);
        key_grant(b2, ch2, KEY_SEND, S_PING);
        cell_pin(b2);
        cell_start(f1); cell_start(f2); cell_start(b1); cell_start(b2);
        uint32_t r1 = get(), r2 = get(), r3 = get();
        int closed = 0, alive = 0;
        uint32_t rs[3] = { r1, r2, r3 };
        for (int i = 0; i < 3; i++) { if (rs[i] == (uint32_t)-GX_ECLOSED) closed++; if (rs[i] == 101) alive++; }
        check("crashed cells removed, their channels closed", closed == 2, NULL);
        check("neighbour cell on the same core kept running", alive == 1, NULL);
    }

    /* 6. квота памяти: 64 КБ -> ровно 4 куска по 16 КБ, потом отказ */
    kmemset(&A[9], 0, sizeof(args_t));
    cell_start(spawn("greedy", greedy, &A[9], far, ORBIT_APP, 10, 64 * 1024));
    uint32_t pieces = get();
    gx_printf("[Measure] heap quota 64 KB: got %u pieces of 16 KB, then refused\n", pieces);
    check("heap quota refuses instead of crashing", pieces == 4, NULL);

    /* 7. ключи */
    {
        gx_chan_t *target = chan_new(S_PING), *ro = chan_new(S_PING), *go = chan_new(S_PING);
        kmemset(&A[10], 0, sizeof(args_t));
        gx_cell_t *k = spawn("keyholder", keyholder, &A[10], far, ORBIT_APP, 10, 0);
        A[10].k1 = key_grant(k, target, KEY_SEND, S_PING);
        A[10].k2 = key_grant(k, ro, KEY_RECV, S_PING);
        A[10].k3 = key_grant(k, go, KEY_RECV, S_PING);
        cell_start(k);
        int gk = key_grant(me, go, KEY_SEND, S_PING);
        int bad = key_grant(me, target, KEY_SEND, gx_schema("ping{n:text}"));
        uint32_t codes = get();
        key_revoke_all(target);
        ch_send(gk, 1, 0, NULL);
        uint32_t after = get();
        /* 1 = нет ключа, 5 = не те права, 0 = можно, 2 = отозван */
        check("no key -> refused", codes / 100 == (uint32_t)-GX_ENOKEY, NULL);
        check("read-only key cannot send", codes / 10 % 10 == (uint32_t)-GX_ERIGHTS, NULL);
        check("valid key works", codes % 10 == 0, NULL);
        check("revoked key refused (one version compare)", after == (uint32_t)-GX_EREVOKED, NULL);
        check("key with wrong schema not granted", bad == GX_ESCHEMA, NULL);
    }

    /* 8. горячая замена сервиса под нагрузкой */
    {
        gx_chan_t *req = chan_new(S_REQ), *rep = chan_new(S_REQ);
        gx_chan_t *h_bad = chan_new(S_STATE), *h_good = chan_new(S_STATE);
        kmemset(&A[11], 0, sizeof(args_t) * 4);
        A[11].n = 1;
        gx_cell_t *v1 = spawn("service-v1", service, &A[11], far, ORBIT_SERVICE, 10, 0);
        A[11].k1 = key_grant(v1, req, KEY_RECV, S_REQ);
        A[11].k2 = key_grant(v1, rep, KEY_SEND, S_REQ);
        int v1_bad = key_grant(v1, h_bad, KEY_SEND, S_STATE);
        int v1_good = key_grant(v1, h_good, KEY_SEND, S_STATE);
        int my_req = key_grant(me, req, KEY_SEND, S_REQ);
        cell_start(v1);

        A[12].n = 20000;
        gx_cell_t *cl = spawn("client", client, &A[12], far2, ORBIT_APP, 10, 0);
        A[12].k1 = key_grant(cl, req, KEY_SEND, S_REQ);
        A[12].k2 = key_grant(cl, rep, KEY_RECV, S_REQ);
        gx_chan_t *go = chan_new(S_PING);
        A[12].k3 = key_grant(cl, go, KEY_RECV, S_PING);
        int my_go = key_grant(me, go, KEY_SEND, S_PING);
        cell_start(cl);

        while (getm().tag != 86) {}     /* ждём, пока клиент разгонится */

        /* сначала версия с другой схемой: замена должна быть отклонена */
        A[13].n = 2;
        gx_cell_t *v3 = spawn("service-v3", service, &A[13], far, ORBIT_SERVICE, 10, 0);
        v3->want_schema = gx_schema("request{n:text}");
        A[13].k3 = key_grant(v3, h_bad, KEY_RECV, S_STATE);
        cell_start(v3);
        ch_send(my_req, T_SWAP, ((uint32_t)v1_bad << 16) | (uint32_t)v3->id, NULL);
        gx_msg_t rf = getm();
        check("swap to wrong schema refused, old version keeps serving",
              rf.tag == 85 && rf.value == (uint32_t)-GX_ESCHEMA, NULL);

        /* теперь правильная версия 2 */
        A[14].n = 2;
        gx_cell_t *v2 = spawn("service-v2", service, &A[14], far, ORBIT_SERVICE, 10, 0);
        v2->want_schema = S_REQ;
        A[14].k3 = key_grant(v2, h_good, KEY_RECV, S_STATE);
        cell_start(v2);
        ch_send(my_req, T_SWAP, ((uint32_t)v1_good << 16) | (uint32_t)v2->id, NULL);
        ch_send(my_go, 1, 0, NULL);     /* клиент продолжает; замена уже стоит в очереди перед его запросами */

        uint32_t handed = 0, ok = 0, by1 = 0, by2 = 0;
        for (int i = 0; i < 4; i++) {
            gx_msg_t m = getm();
            if (m.tag == 81) handed = m.value;
            else if (m.tag == 82) ok = m.value;
            else if (m.tag == 83) by1 = m.value;
            else if (m.tag == 84) by2 = m.value;
        }
        gx_printf("[Measure] hot swap: %u requests, answered %u (v1 %u, v2 %u), state handed over: %u served\n",
                  A[12].n, ok, by1, by2, handed);
        check("hot swap loses no requests", ok == A[12].n && by1 + by2 == A[12].n && by2 > 0, NULL);
        check("new version got old state", handed == by1, NULL);
    }

    /* 9. танец: 2N занятых планет рождаются на одной звезде и расходятся сами */
    if (galaxy_stars > 1) {
        static args_t pa[32];
        int P = 2 * galaxy_stars;          /* по 2 планеты на звезду */
        uint64_t start = gx_now_ms();
        for (int i = 0; i < P; i++) {
            kmemset(&pa[i], 0, sizeof(args_t));
            pa[i].until = start + 600;
            gx_cell_t *c = cell_new("planet", planet, &pa[i], far, ORBIT_APP, 10, 0);
            pa[i].key_report = key_grant(c, report, KEY_SEND, S_REPORT);
            cell_start(c);
        }
        int spread_ok = 0;
        for (int t = 0; t <= 300; t += 20) {
            /* строка целиком в один вывод, время — настоящее, от старта */
            char line[160], *q = line;
            int mx = 0, mn = 99;
            for (int i = 0; i < galaxy_stars && q < line + 150; i++) {
                int l = galaxy_load(i);
                *q++ = ' ';
                if (l >= 10) *q++ = (char)('0' + l / 10);
                *q++ = (char)('0' + l % 10);
                if (l > mx) mx = l;
                if (l < mn) mn = l;
            }
            *q = 0;
            uint32_t real = (uint32_t)(gx_now_ms() - start);
            gx_printf("[Dance] t=%ums loads:%s\n", real, line);
            if (real >= 100 && real < 600 && mx - mn <= 1) spread_ok = 1;
            cell_sleep_ms(start + (uint32_t)t + 20 - (uint32_t)gx_now_ms());
        }
        uint32_t per[GX_MAX_STARS] = {0}, total = 0, maxm = 0;
        for (int i = 0; i < P; i++) {
            uint32_t v = get();
            per[(v >> 16) % GX_MAX_STARS]++;
            total += v & 0xFFFF;
            if ((v & 0xFFFF) > maxm) maxm = v & 0xFFFF;
        }
        gx_printf("[Dance] planets per star at the end:");
        for (int i = 0; i < galaxy_stars; i++) gx_printf(" %u", per[i]);
        gx_printf(", moves total %u, max per planet %u\n", total, maxm);
        check("stars share planets evenly (difference <= 1)", spread_ok, NULL);
        /* через каждый мостик клетка проходит один раз, плюс пара шагов внутри связок */
        check("planets move gradually, no ping-pong", maxm <= (uint32_t)galaxy_bundles() + 3, NULL);
    }

    gx_printf("[Galaxy] switches per star:");
    for (int s = 0; s < galaxy_stars; s++) gx_printf(" %u", galaxy_switches(s));
    gx_printf("\n[Galaxy] All checks done.\n");
    /* в make galaxy у QEMU есть устройство isa-debug-exit: запись в порт 0xF4
       сразу завершает эмулятор. Без этого устройства запись ничего не делает. */
    outb(0xF4, 0);
}
