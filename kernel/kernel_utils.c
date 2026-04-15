#include "kernel_utils.h"

// ------------------ Преобразование числа в строку ------------------
void u32_to_dec(uint32_t v, char *buf) {
    char tmp[12];
    int i = 0, j;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

void u32_to_hex(uint32_t v, char *buf) {
    const char hex[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; ++i)
        buf[2 + i] = hex[(v >> (28 - 4*i)) & 0xF];
    buf[10] = '\0';
}

// ------------------ VGA ------------------
#define VIDEO_MEMORY 0xb8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

static uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
static uint8_t cursor_x = 0, cursor_y = 0;

void kclear_screen(void) {
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
            video_memory[y*SCREEN_WIDTH + x] = (uint16_t)' ' | (0x07 << 8);
    cursor_x = cursor_y = 0;
}

void kprint(const char* str) {
    while (*str) {
        if (*str == '\n') { cursor_x = 0; cursor_y++; }
        else {
            if (cursor_y >= SCREEN_HEIGHT) cursor_y = 0;
            video_memory[cursor_y*SCREEN_WIDTH + cursor_x] = (uint16_t)(*str) | (0x07 << 8);
            cursor_x++;
            if (cursor_x >= SCREEN_WIDTH) { cursor_x = 0; cursor_y++; }
        }
        str++;
    }
}

// ------------------ Panic ------------------
void panic(const char* msg) {
    kclear_screen();
    kprint("PANIC: ");
    kprint(msg);
    while(1) __asm__ volatile("hlt");
}
