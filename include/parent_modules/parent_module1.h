#pragma once
#include <stdint.h>

// Инициализация Plant 1
void plant1_init(void);

// Обновление Plant 1
void plant1_update(void);

// Выполнение команды Plant 1
void plant1_command(uint32_t cmd, void* payload);
