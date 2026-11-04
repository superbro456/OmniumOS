// flexipc.c — общая очередь сообщений между ядрами
#include "flexipc.h"
#include "spinlock.h"

#define FLEXIPC_QUEUE_SIZE 16

static flexipc_msg_t queue[FLEXIPC_QUEUE_SIZE];
static int head = 0;
static int tail = 0;
// Очередь общая для всех ядер: без замка два ядра могли взять одно
// сообщение или затереть друг друга при записи.
static spinlock_t queue_lock = 0;

int flexipc_send(const flexipc_msg_t* msg) {
    int ok = 0;
    spinlock_acquire(&queue_lock);
    int next = (head + 1) % FLEXIPC_QUEUE_SIZE;
    if (next != tail) {           // иначе очередь полна
        queue[head] = *msg;
        head = next;
        ok = 1;
    }
    spinlock_release(&queue_lock);
    return ok;
}

int flexipc_receive(flexipc_msg_t* msg) {
    int ok = 0;
    spinlock_acquire(&queue_lock);
    if (tail != head) {
        *msg = queue[tail];
        tail = (tail + 1) % FLEXIPC_QUEUE_SIZE;
        ok = 1;
    }
    spinlock_release(&queue_lock);
    return ok;
}
