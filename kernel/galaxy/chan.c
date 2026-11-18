/* kernel/galaxy/chan.c — каналы, ключи и обменная куча.
 *
 * Канал — кольцо сообщений между клетками. Сообщение маленькое (тег + число),
 * большие данные едут в блоке обменной кучи: отправитель отдаёт блок, получатель
 * забирает тот же блок. Ничего не копируется (move).
 *
 * Ключ проверяется полностью один раз — в key_grant (схема, права). При каждой
 * отправке остаётся одно сравнение: версия ключа == версия канала. Отзыв всех
 * ключей — просто версия+1.
 */
#include "galaxy.h"
#include "memory_core.h"
#include "kernel_utils.h"
#include "kernel_string.h"

void gx_wake(gx_cell_t *c);
void gx_block(void);

static inline uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) : : "memory");
    return f;
}
static inline void irq_restore(uint32_t f) {
    if (f & 0x200) __asm__ volatile("sti" : : : "memory");
}

/* ---------------- обменная куча ---------------- */
#define XH_SIZE      (8u * 1024u * 1024u)
#define XH_CLASSES   12            /* 64 Б .. 128 КБ */
static uint8_t *xh_base;
static uint32_t xh_top;
static gx_block_t *xh_free[XH_CLASSES];
static spinlock_t xh_lock = 0;

static void own_link(gx_cell_t *c, gx_block_t *b) {
    b->owner = c;
    b->prev = NULL;
    b->next = c ? c->blocks : NULL;
    if (c) {
        if (c->blocks) c->blocks->prev = b;
        c->blocks = b;
    }
}

static void own_unlink(gx_block_t *b) {
    gx_cell_t *c = b->owner;
    if (!c) return;
    if (b->prev) b->prev->next = b->next; else c->blocks = b->next;
    if (b->next) b->next->prev = b->prev;
    b->next = b->prev = NULL;
    b->owner = NULL;
}

gx_block_t *block_new(uint32_t len) {
    uint32_t need = len + sizeof(gx_block_t);
    uint32_t cls = 0;
    while (cls < XH_CLASSES && (64u << cls) < need) cls++;
    if (cls == XH_CLASSES) return NULL;
    uint32_t f = irq_save();
    spinlock_acquire(&xh_lock);
    gx_block_t *b = xh_free[cls];
    if (b) xh_free[cls] = b->next;
    else if (xh_top + (64u << cls) <= XH_SIZE) {
        b = (gx_block_t *)(xh_base + xh_top);
        xh_top += 64u << cls;
    }
    spinlock_release(&xh_lock);
    if (b) {
        b->size_class = cls;
        b->len = len;
        own_link(cell_self(), b);
    }
    irq_restore(f);
    return b;
}

static void block_release(gx_block_t *b) {   /* прерывания выключены */
    own_unlink(b);
    spinlock_acquire(&xh_lock);
    b->next = xh_free[b->size_class];
    xh_free[b->size_class] = b;
    spinlock_release(&xh_lock);
}

void block_free(gx_block_t *b) {
    if (!b || b->owner != cell_self()) return;   /* чужой блок освободить нельзя */
    uint32_t f = irq_save();
    block_release(b);
    irq_restore(f);
}

void gx_blocks_free_all(gx_cell_t *c) {
    while (c->blocks) block_release(c->blocks);
}

/* ---------------- схемы ---------------- */
uint32_t gx_schema(const char *t) {     /* FNV-1a */
    uint32_t h = 2166136261u;
    while (*t) { h ^= (uint8_t)*t++; h *= 16777619u; }
    return h;
}

/* ---------------- каналы ---------------- */
#define MAX_CHANS 64
static gx_chan_t chans[MAX_CHANS];
static int chans_used;
static spinlock_t chans_lock = 0;

gx_chan_t *chan_new(uint32_t schema) {
    spinlock_acquire(&chans_lock);
    if (!xh_base) {
        xh_base = memory_allocate(ZONE_SERVICE, XH_SIZE);
        if (!xh_base) panic("galaxy: no memory for exchange heap");
    }
    gx_chan_t *ch = chans_used < MAX_CHANS ? &chans[chans_used++] : NULL;
    spinlock_release(&chans_lock);
    if (!ch) return NULL;
    kmemset(ch, 0, sizeof(*ch));
    ch->schema = schema;
    ch->version = 1;
    ch->send_star = -1;
    return ch;
}

int key_grant(gx_cell_t *c, gx_chan_t *ch, uint32_t rights, uint32_t schema) {
    if (!c || !ch) return GX_ENOKEY;
    if (schema != ch->schema) return GX_ESCHEMA;     /* полная проверка — здесь, один раз */
    for (int i = 0; i < GX_MAX_KEYS; i++) {
        if (!c->keys[i].chan) {
            c->keys[i].chan = ch;
            c->keys[i].rights = rights;
            c->keys[i].version = ch->version;
            spinlock_acquire(&ch->lock);
            if (rights & KEY_SEND) ch->senders++;
            if (rights & KEY_RECV) ch->receivers++;
            spinlock_release(&ch->lock);
            return i;
        }
    }
    return GX_EFULL;
}

void key_revoke_all(gx_chan_t *ch) {
    __atomic_add_fetch(&ch->version, 1, __ATOMIC_SEQ_CST);
}

static gx_key_t *key_check(int key, uint32_t need, int *err) {
    gx_cell_t *me = cell_self();
    if (!me || key < 0 || key >= GX_MAX_KEYS || !me->keys[key].chan) { *err = GX_ENOKEY; return NULL; }
    gx_key_t *k = &me->keys[key];
    if (k->version != k->chan->version) { *err = GX_EREVOKED; return NULL; }   /* одно сравнение */
    if (!(k->rights & need)) { *err = GX_ERIGHTS; return NULL; }
    return k;
}

int key_pass(int key, gx_cell_t *to) {
    int err;
    gx_key_t *k = key_check(key, KEY_SEND | KEY_RECV, &err);
    if (!k) return err;
    if (to->want_schema != k->chan->schema) return GX_ESCHEMA;   /* новый сервис не понимает старые сообщения */
    int n = key_grant(to, k->chan, k->rights, k->chan->schema);
    if (n < 0) return n;
    to->keys[n].version = k->version;   /* отозванный ключ и после передачи отозван */
    spinlock_acquire(&k->chan->lock);  /* ключ переехал, а не размножился */
    if (k->rights & KEY_SEND) k->chan->senders--;
    if (k->rights & KEY_RECV) k->chan->receivers--;
    spinlock_release(&k->chan->lock);
    k->chan = NULL;    /* у старой клетки ключа больше нет */
    return n;
}

void gx_chan_close_by(gx_cell_t *c) {
    for (int i = 0; i < GX_MAX_KEYS; i++) {
        gx_chan_t *ch = c->keys[i].chan;
        if (!ch) continue;
        uint32_t rights = c->keys[i].rights;
        c->keys[i].chan = NULL;
        spinlock_acquire(&ch->lock);
        if (rights & KEY_SEND) ch->senders--;
        if (rights & KEY_RECV) ch->receivers--;
        if (ch->senders > 0 && ch->receivers > 0) { spinlock_release(&ch->lock); continue; }
        ch->closed = 1;   /* с одной стороны никого не осталось */
        gx_cell_t *r = ch->wait_recv, *s = ch->wait_send;
        ch->wait_recv = ch->wait_send = NULL;
        spinlock_release(&ch->lock);
        if (r && r != c) gx_wake(r);
        if (s && s != c) gx_wake(s);
    }
}

int ch_send(int key, uint32_t tag, uint32_t value, gx_block_t *move) {
    int err;
    gx_key_t *k = key_check(key, KEY_SEND, &err);
    if (!k) return err;
    gx_chan_t *ch = k->chan;
    gx_cell_t *me = cell_self();
    if (move && move->owner != me) return GX_ERIGHTS;   /* отдать можно только своё */

    uint32_t f = irq_save();
    for (;;) {
        spinlock_acquire(&ch->lock);
        if (ch->closed) { spinlock_release(&ch->lock); irq_restore(f); return GX_ECLOSED; }
        if (ch->head - ch->tail < GX_RING) {
            gx_msg_t *m = &ch->ring[ch->head % GX_RING];
            m->tag = tag;
            m->value = value;
            m->block = move;
            ch->send_star = me->star;
            if (move) own_unlink(move);          /* блок в пути: ничей */
            __atomic_store_n(&ch->head, ch->head + 1, __ATOMIC_RELEASE);
            gx_cell_t *r = ch->wait_recv;
            ch->wait_recv = NULL;
            spinlock_release(&ch->lock);
            if (r) gx_wake(r);
            irq_restore(f);
            return GX_OK;
        }
        ch->wait_send = me;                       /* кольцо полно — ждём */
        me->state = CELL_WAITING;
        spinlock_release(&ch->lock);
        gx_block();
    }
}

int ch_recv(int key, gx_msg_t *out) {
    int err;
    gx_key_t *k = key_check(key, KEY_RECV, &err);
    if (!k) return err;
    gx_chan_t *ch = k->chan;
    gx_cell_t *me = cell_self();

    /* отправитель на другой звезде ответит быстрее, чем мы уснём и проснёмся.
       Если он на нашей звезде, крутиться бесполезно: он не пойдёт, пока мы не уступим. */
    if (ch->send_star >= 0 && ch->send_star != me->star)
        for (int i = 0; i < 4000 && ch->head == ch->tail && !ch->closed; i++)
            __asm__ volatile("pause");

    uint32_t f = irq_save();
    for (;;) {
        spinlock_acquire(&ch->lock);
        if (ch->head != ch->tail) {
            *out = ch->ring[ch->tail % GX_RING];
            ch->tail++;
            if (out->block) own_link(me, out->block);   /* теперь блок наш */
            gx_cell_t *s = ch->wait_send;
            ch->wait_send = NULL;
            spinlock_release(&ch->lock);
            if (s) gx_wake(s);
            irq_restore(f);
            return GX_OK;
        }
        if (ch->closed) { spinlock_release(&ch->lock); irq_restore(f); return GX_ECLOSED; }
        ch->wait_recv = me;
        me->state = CELL_WAITING;
        spinlock_release(&ch->lock);
        gx_block();
    }
}
