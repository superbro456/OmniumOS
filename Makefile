# ===== OmniumOS Makefile =====

TARGET = kernel.elf
ISO = omnium.iso

# --- Tools ---
CC = i686-elf-gcc
LD = i686-elf-ld
NASM = nasm
GRUB_MKRESCUE = i686-elf-grub-mkrescue
QEMU = qemu-system-i386

# --- Flags ---
CFLAGS = -Wall -Wextra -std=c11 -ffreestanding -m32 \
        -Iinclude \
        -Iinclude/core \
        -Iinclude/parent_modules \
        -Iipc
LDFLAGS = -T linker.ld -nostdlib -m elf_i386

# --- SMP / QEMU settings ---
SMP ?= 2   # можно задать через "make SMP=4 all"

# --- Sources ---
KERNEL_C_SRC = \
    kernel/central_core.c \
    kernel/smp.c \
    kernel/lapic.c \
    kernel/process_core.c \
    kernel/memory_core.c \
    kernel/security_core.c \
    kernel/metrics_core.c \
    kernel/optimize_core.c \
    kernel/ai_core.c \
    kernel/spinlock.c \
    kernel/kernel_utils.c \
    kernel/kernel_string.c   # <<< добавили kernel_string.c

IPC_SRC = ipc/flexipc.c

PARENTS_SRC = \
    include/parent_modules/common/parent_module1.c \
    include/parent_modules/common/parent_module2.c \
    include/parent_modules/common/parent_module3.c \
    include/parent_modules/common/parent_module4.c

USER_SRC = \
    user/init.c \
    user/shell.c

# trampoline assembly (NASM)
AP_TRAMP_ASM = kernel/ap_trampoline.asm
AP_TRAMP_OBJ = kernel/ap_trampoline.o

# --- Objects ---
C_OBJS = $(KERNEL_C_SRC:.c=.o) \
         $(IPC_SRC:.c=.o) \
         $(PARENTS_SRC:.c=.o) \
         $(USER_SRC:.c=.o)

OBJS = $(C_OBJS) $(AP_TRAMP_OBJ)

# --- Rules ---
all: $(ISO) run

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJS)

# compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# assemble trampoline with NASM (elf32)
$(AP_TRAMP_OBJ): $(AP_TRAMP_ASM)
	$(NASM) -f elf32 $(AP_TRAMP_ASM) -o $(AP_TRAMP_OBJ)

# create ISO with GRUB
$(ISO): $(TARGET) boot/grub/grub.cfg
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/
	cp boot/grub/grub.cfg isodir/boot/grub/
	$(GRUB_MKRESCUE) -o $(ISO) isodir

# run QEMU
run: $(ISO)
	$(QEMU) -cdrom $(ISO) -m 1536M -smp $(SMP) -serial stdio

clean:
	rm -f $(C_OBJS) $(AP_TRAMP_OBJ) $(TARGET) $(ISO)
	rm -rf isodir

.PHONY: all clean run
