#include "parent_module4.h"
#include "kernel_utils.h"

void plant4_init(void) {}
void plant4_update(void) {}

void plant4_command(uint32_t cmd, void* payload) {
    if (cmd == PLANT4_CMD_QUARANTINE) {
        uint32_t pid = (uint32_t)(uintptr_t)payload;
        char buf[12];
        u32_to_dec(pid, buf);
        kprint("[Plant4] Quarantine process ");
        kprint(buf);
        kprint("\n");
    }
}
