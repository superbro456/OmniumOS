#include "kernel_utils.h"
#include "spinlock.h"
#include "arch.h"

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

// ------------------ Serial (COM1) ------------------
#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03); outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) __asm__ volatile("pause");
    outb(COM1, (uint8_t)c);
}

// ------------------ VGA ------------------
#define VIDEO_MEMORY 0xb8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define ATTR (0x07 << 8)

static volatile uint16_t* const video_memory = (volatile uint16_t*)VIDEO_MEMORY;
static int cursor_x = 0, cursor_y = 0;

/* Один замок на весь вывод: строки с разных ядер не перемешиваются. */
static spinlock_t print_lock = 0;

static void vga_scroll(void) {
    for (int i = 0; i < (SCREEN_HEIGHT - 1) * SCREEN_WIDTH; i++)
        video_memory[i] = video_memory[i + SCREEN_WIDTH];
    for (int x = 0; x < SCREEN_WIDTH; x++)
        video_memory[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH + x] = ' ' | ATTR;
    cursor_y = SCREEN_HEIGHT - 1;
}

static void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        if (++cursor_y >= SCREEN_HEIGHT) vga_scroll();
        return;
    }
    video_memory[cursor_y * SCREEN_WIDTH + cursor_x] = (uint8_t)c | ATTR;
    if (++cursor_x >= SCREEN_WIDTH) {
        cursor_x = 0;
        if (++cursor_y >= SCREEN_HEIGHT) vga_scroll();
    }
}

void kclear_screen(void) {
    spinlock_acquire(&print_lock);
    for (int i = 0; i < SCREEN_HEIGHT * SCREEN_WIDTH; i++)
        video_memory[i] = ' ' | ATTR;
    cursor_x = cursor_y = 0;
    spinlock_release(&print_lock);
}

static void print_unlocked(const char* str, int to_screen) {
    for (; *str; str++) {
        if (to_screen) vga_putc(*str);
        if (*str == '\n') serial_putc('\r');
        serial_putc(*str);
    }
}

/* Печать на экран и копия в последовательный порт (видно в QEMU -serial). */
void kprint(const char* str) {
    spinlock_acquire(&print_lock);
    print_unlocked(str, 1);
    spinlock_release(&print_lock);
}

/* Только в последовательный порт. */
void serial_print(const char* str) {
    spinlock_acquire(&print_lock);
    print_unlocked(str, 0);
    spinlock_release(&print_lock);
}

// ------------------ Panic ------------------
void panic(const char* msg) {
    cpu_cli();
    /* Замок мог держать тот, кто упал; печатаем без него. */
    print_unlocked("\nPANIC: ", 1);
    print_unlocked(msg, 1);
    print_unlocked("\n", 1);
    for (;;) cpu_halt();
}
