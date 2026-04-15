#pragma once
#include <stdint.h>

typedef struct {
    uint32_t src;
    uint32_t dst;
    uint32_t cmd;
    void* payload;
} flexipc_msg_t;

void flexipc_send(flexipc_msg_t* msg);
int flexipc_receive(flexipc_msg_t* msg);
