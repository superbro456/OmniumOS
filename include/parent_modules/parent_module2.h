#pragma once
#include <stdint.h>

void plant2_init(void);
void plant2_update(void);
void plant2_command(uint32_t cmd, void* payload);
