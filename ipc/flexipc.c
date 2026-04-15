// flexipc.c
#include "flexipc.h"
#include "kernel_utils.h"

#define FLEXIPC_QUEUE_SIZE 16

static flexipc_msg_t queue[FLEXIPC_QUEUE_SIZE];
static int head = 0;
static int tail = 0;

void flexipc_send(flexipc_msg_t* msg) {
    int next = (head + 1) % FLEXIPC_QUEUE_SIZE;
    if (next == tail) return; // очередь полна
    queue[head] = *msg;
    head = next;
}

int flexipc_receive(flexipc_msg_t* msg) {
    if (tail == head) return 0; // пусто
    *msg = queue[tail];
    tail = (tail + 1) % FLEXIPC_QUEUE_SIZE;
    return 1;
}
