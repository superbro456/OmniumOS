# ===== OmniumOS Makefile =====
#
#   make          собрать omnium.iso
#   make run      запустить в QEMU (окно + serial в терминал)
#   make test     загрузить в QEMU без окна и проверить лог загрузки
#   make SMP=4 run
#
# По умолчанию нужен кросс-компилятор i686-elf-gcc. На Linux можно
# собрать обычным gcc:  make CROSS=

TARGET = kernel.elf
ISO = omnium.iso

# --- Tools ---
CROSS ?= i686-elf-
CC = $(CROSS)gcc
LD = $(CROSS)ld
NASM = nasm
ifeq ($(CROSS),)
GRUB_MKRESCUE ?= grub-mkrescue
else
GRUB_MKRESCUE ?= $(CROSS)grub-mkrescue
endif
QEMU = qemu-system-i386

# --- Flags ---
CFLAGS = -Wall -Wextra -std=gnu11 -ffreestanding -m32 -O2 -g \
        -fno-pie -fno-stack-protector -fno-omit-frame-pointer \
        -mno-sse -mno-mmx -mno-80387 \
        -Iinclude \
        -Iinclude/core \
        -Iinclude/parent_modules \
        -Iipc
LDFLAGS = -T linker.ld -nostdlib -m elf_i386

# --- SMP / QEMU settings ---
SMP ?= 2
MEM ?= 256M
TEST_TIMEOUT ?= 15

# --- Sources ---
KERNEL_C_SRC = \
    kernel/central_core.c \
    kernel/arch/arch.c \
    kernel/smp.c \
    kernel/lapic.c \
    kernel/mem.c \
    kernel/process_core.c \
    kernel/memory_core.c \
    kernel/security_core.c \
    kernel/metrics_core.c \
    kernel/optimize_core.c \
    kernel/ai_core.c \
    kernel/spinlock.c \
    kernel/kernel_utils.c \
    kernel/kernel_string.c

IPC_SRC = ipc/flexipc.c

PARENTS_SRC = \
    include/parent_modules/common/parent_module1.c \
    include/parent_modules/common/parent_module2.c \
    include/parent_modules/common/parent_module3.c \
    include/parent_modules/common/parent_module4.c

USER_SRC = \
    user/init.c \
    user/shell.c

ASM_OBJS = boot/boot.o kernel/arch/isr.o kernel/ap_blob.o

# --- Objects ---
C_OBJS = $(KERNEL_C_SRC:.c=.o) \
         $(IPC_SRC:.c=.o) \
         $(PARENTS_SRC:.c=.o) \
         $(USER_SRC:.c=.o)

# boot.o первым: заголовок Multiboot должен быть в начале образа
OBJS = boot/boot.o $(filter-out boot/boot.o,$(ASM_OBJS)) $(C_OBJS)

# --- Rules ---
all: $(ISO)

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

boot/boot.o: boot/boot.asm
	$(NASM) -f elf32 $< -o $@

kernel/arch/isr.o: kernel/arch/isr.asm
	$(NASM) -f elf32 $< -o $@

# trampoline для AP: плоский бинарник для адреса 0x7000, встраивается в ядро
kernel/ap_trampoline.bin: kernel/ap_trampoline.asm
	$(NASM) -f bin $< -o $@

kernel/ap_blob.o: kernel/ap_blob.asm kernel/ap_trampoline.bin
	$(NASM) -f elf32 $< -o $@

# create ISO with GRUB
$(ISO): $(TARGET) boot/grub/grub.cfg
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/
	cp boot/grub/grub.cfg isodir/boot/grub/
	$(GRUB_MKRESCUE) -o $(ISO) isodir

run: $(ISO)
	$(QEMU) -cdrom $(ISO) -m $(MEM) -smp $(SMP) -serial stdio

# Загрузка без окна: лог COM1 пишется в serial.log и проверяется
test: $(ISO)
	rm -f serial.log
	-timeout $(TEST_TIMEOUT) $(QEMU) -cdrom $(ISO) -m $(MEM) -smp $(SMP) \
	    -display none -serial file:serial.log -no-reboot
	@cat serial.log
	@grep -q "All cores and plants initialized" serial.log
	@grep -q "\[Timer\] uptime 1 s" serial.log
	@! grep -q "PANIC\|ERROR\|EXCEPTION" serial.log
	@echo "=== OmniumOS boot test PASSED ==="

clean:
	rm -f $(C_OBJS) $(ASM_OBJS) kernel/ap_trampoline.bin $(TARGET) $(ISO) serial.log
	rm -rf isodir

.PHONY: all clean run test

# Проверка логики ядра на обычном компьютере (планировщик)
HOSTCC ?= gcc
unittest:
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Iinclude -Iinclude/core -o tests/host/test_scheduler \
	    tests/host/test_scheduler.c kernel/process_core.c kernel/kernel_string.c
	./tests/host/test_scheduler

.PHONY: unittest
