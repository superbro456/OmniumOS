/* galaxy.h — «галактическое» ядро Omnium: звёзды, клетки, каналы, ключи.
 *
 * Звезда (star)   — диспетчер на каждом ядре CPU. Сама раздаёт время своим
 *                   клеткам по их пайку (ration) и переключает их.
 * Клетка (cell)   — программа или сервис: свой стек, своя куча с квотой,
 *                   своя связка ключей. Падение клетки не роняет остальных.
 * Канал (channel) — единственный способ говорить с другой клеткой.
 *                   Большие данные не копируются, а передаются (move).
 * Ключ (key)      — право на объект. Проверяется один раз при выдаче,
 *                   дальше только сравнение номера версии (отзыв = версия+1).
 *
 * Три независимые ручки: орбита = доверие, ключи = права, паёк = скорость.
 */
#ifndef GALAXY_H
#define GALAXY_H

#include <stdint.h>
#include <stddef.h>
#include "smp.h"

#define GX_MAX_STARS   SMP_MAX_CPUS
#define GX_MAX_CELLS   64
#define GX_MAX_KEYS    8
#define GX_NAME_LEN    16
#define GX_RING        64        /* сообщений в канале */
#define GX_SLOT_SIZE   (256u * 1024u)   /* стек + куча одной клетки */
#define GX_STACK_SIZE  (16u * 1024u)

/* ---------- коды ответа ---------- */
#define GX_OK        0
#define GX_ENOKEY   -1   /* нет ключа на этот объект */
#define GX_EREVOKED -2   /* ключ отозван */
#define GX_ECLOSED  -3   /* вторая сторона канала умерла или закрыла его */
#define GX_ESCHEMA  -4   /* схема сообщений не совпала */
#define GX_ERIGHTS  -5   /* ключ есть, но не на это действие */
#define GX_EFULL    -6   /* нет места */

/* ---------- орбиты (доверие) ---------- */
typedef enum {
    ORBIT_DRIVER  = 1,   /* драйверы: можно danger, нужен IOMMU */
    ORBIT_SERVICE = 2,   /* сервисы системы */
    ORBIT_APP     = 3,   /* программы пользователя */
    ORBIT_KUIPER  = 4,   /* чужой Си-код: в будущем отдельная песочница MMU */
} gx_orbit_t;

typedef enum { CELL_FREE = 0, CELL_READY, CELL_RUNNING, CELL_WAITING, CELL_DEAD } gx_state_t;

/* права ключа на канал */
#define KEY_SEND  1u
#define KEY_RECV  2u

struct gx_chan;
struct gx_cell;

typedef struct {
    struct gx_chan *chan;   /* объект (пока ключи бывают только на каналы) */
    uint32_t rights;
    uint32_t version;       /* версия объекта в момент выдачи */
} gx_key_t;

/* блок «обменной кучи»: его можно передать через канал без копирования */
typedef struct gx_block {
    struct gx_block *next, *prev;   /* список блоков владельца */
    struct gx_cell *owner;          /* NULL — блок в пути (лежит в канале) */
    uint32_t size_class;
    uint32_t len;
} gx_block_t;

typedef struct gx_cell {
    int id;
    char name[GX_NAME_LEN];
    volatile gx_state_t state;
    gx_orbit_t orbit;
    int star;                    /* на какой звезде живёт */
    uint32_t ration;             /* паёк: мс подряд за один круг */
    uint32_t esp;                /* сохранённый стек при переключении */
    uint8_t *slot;               /* стек + куча */
    uint32_t heap_used, heap_quota;
    gx_key_t keys[GX_MAX_KEYS];
    uint32_t want_schema;        /* какую схему канала понимает (для замены) */
    gx_block_t *blocks;          /* чем владеет в обменной куче */
    void (*entry)(void *);
    void *arg;
    volatile uint64_t ticks;     /* сколько мс CPU съела */
    uint64_t wake_at;            /* для cell_sleep_ms */
    struct gx_cell *next_sleep;
    int pinned;                  /* не переходит к другим звёздам */
    uint64_t moved_at;           /* когда последний раз перешла */
    uint32_t moves;              /* сколько раз переходила */
    const char *death;           /* причина смерти */
    struct gx_cell *next;        /* очередь готовых */
} gx_cell_t;

typedef struct {
    uint32_t tag;
    uint32_t value;
    gx_block_t *block;   /* передаваемые данные (move), может быть NULL */
} gx_msg_t;

typedef struct gx_chan {
    uint32_t schema;             /* хеш схемы сообщений */
    volatile uint32_t version;   /* +1 при отзыве всех ключей */
    volatile uint32_t head, tail;
    gx_msg_t ring[GX_RING];
    gx_cell_t *volatile wait_recv;
    gx_cell_t *volatile wait_send;
    volatile int closed;         /* одна из сторон канала опустела */
    volatile int send_star;      /* звезда последнего отправителя */
    int senders, receivers;      /* сколько живых ключей на каждой стороне */
    spinlock_t lock;
} gx_chan_t;

/* ---------- звёзды ---------- */
void galaxy_init(void);                 /* на BSP, после памяти */
void galaxy_star_enter(int star);       /* ядро CPU становится звездой (не возвращается) */
void galaxy_tick(int star);             /* из прерывания таймера */
int  galaxy_handle_fault(uint32_t vector, const char *what); /* 1 — упала клетка, а не ядро */
int  galaxy_star_index(void);           /* номер звезды текущего ядра CPU */
void galaxy_set_star(uint32_t apic_id, int star);
extern volatile int galaxy_stars;       /* сколько звёзд зажглось */
int  galaxy_load(int star);
void galaxy_build_bundles(int nstars);  /* связки по общему кэшу (после старта всех ядер) */
int  galaxy_bundles(void);             /* сколько клеток хотят CPU на звезде */
void galaxy_moves(int star, uint32_t *in, uint32_t *out);

/* ---------- клетки ---------- */
gx_cell_t *cell_new(const char *name, void (*entry)(void *), void *arg,
                    int star, gx_orbit_t orbit, uint32_t ration, uint32_t heap_quota);
void       cell_start(gx_cell_t *c);   /* клетка создаётся на паузе */
void       cell_pin(gx_cell_t *c);     /* привязать к своей звезде */
gx_cell_t *cell_self(void);
void  cell_yield(void);
void  cell_sleep_ms(uint32_t ms);       /* уснуть, не тратя CPU */
void  cell_exit(void) __attribute__((noreturn));
void *cell_alloc(uint32_t size);       /* NULL, если квота кончилась */
uint64_t gx_now_ms(void);

/* ---------- каналы и ключи ---------- */
gx_chan_t *chan_new(uint32_t schema);
int  key_grant(gx_cell_t *c, gx_chan_t *ch, uint32_t rights, uint32_t schema); /* -> номер ключа */
void key_revoke_all(gx_chan_t *ch);
int  key_pass(int key, gx_cell_t *to);  /* отдать свой ключ другой клетке (горячая замена) */
int  ch_send(int key, uint32_t tag, uint32_t value, gx_block_t *move);
int  ch_recv(int key, gx_msg_t *out);

gx_block_t *block_new(uint32_t len);    /* блок в обменной куче, владелец — текущая клетка */
void  block_free(gx_block_t *b);
static inline void *block_data(gx_block_t *b) { return (void *)(b + 1); }

/* схема канала: хеш от описания типа (в Tachbit его посчитает компилятор) */
uint32_t gx_schema(const char *type_text);

/* ---------- вывод ---------- */
void gx_printf(const char *fmt, ...);

/* демонстрация и замеры */
void galaxy_demo(void *arg);

#endif
