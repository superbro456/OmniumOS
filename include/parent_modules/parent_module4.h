#pragma once
#include <stdint.h>

#define PLANT4_CMD_QUARANTINE 1

void plant4_init(void);
void plant4_update(void);
void plant4_command(uint32_t cmd, void* payload);
