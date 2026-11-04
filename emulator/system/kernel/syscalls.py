# syscalls.py — OmniumOS (Расширенный универсальный набор)
import time

# =========================
# Таблица "файлов"
# =========================
FILE_TABLE = {}
FD_TABLE = {}
NEXT_FD = 3  # 0,1,2 зарезервированы (stdin/stdout/stderr)

# =========================
# Вспомогательные
# =========================
def _ret(core, v=0):
    core.registers['L1'] = v

# =========================
# 1–9: Отладка и консоль
# =========================
def sys_nop(core): _ret(core, 0)
def print_text(core, text): print(f"[core {core.core_id}] {text}")
def read_input(core, prompt="> "):
    v = input(f"[core {core.core_id}] {prompt}")
    try: core.registers['L1'] = int(v)
    except: core.registers['L1'] = 0
def halt_system(core): core.HALT()
def debug_log(core, level, text): print(f"[DBG {level}] {text}")

# =========================
# 10–19: Файловая система
# =========================
def file_open(core, addr):
    global NEXT_FD
    addr = int(addr, 0)
    name = FILE_TABLE.get(addr, {}).get('name', None)
    if name is None: _ret(core, -1); return
    fd = NEXT_FD; NEXT_FD += 1
    FD_TABLE[fd] = addr
    _ret(core, fd)

def file_close(core, fd):
    FD_TABLE.pop(fd, None)
    _ret(core, 0)

def file_create(core, name, addr, size):
    addr = int(addr, 0)
    FILE_TABLE[addr] = {'name': name, 'address': addr, 'size': size, 'created': time.time()}
    _ret(core, addr)

def file_read(core, fd, size):
    addr = FD_TABLE.get(fd, None)
    if addr not in FILE_TABLE: _ret(core, -1); return
    info = FILE_TABLE[addr]
    _ret(core, info['size'])

def file_write(core, fd, size):
    addr = FD_TABLE.get(fd, None)
    if addr in FILE_TABLE:
        FILE_TABLE[addr]['size'] = size
    _ret(core, size)

def file_seek(core, fd, offset): _ret(core, offset)
def file_stat(core, fd): _ret(core, 0)
def file_delete(core, addr):
    FILE_TABLE.pop(int(addr, 0), None); _ret(core, 0)

# =========================
# 20–29: Процессы и потоки
# =========================
PROCESS_TABLE = {}
THREAD_TABLE = {}

def proc_spawn(core, entry):
    pid = len(PROCESS_TABLE) + 1
    PROCESS_TABLE[pid] = entry
    _ret(core, pid)

def proc_exit(core, code=0): core.HALT()
def proc_wait(core, pid): _ret(core, 0)
def proc_kill(core, pid, sig): _ret(core, 0)

def thread_create(core, entry):
    tid = len(THREAD_TABLE)+1
    THREAD_TABLE[tid] = entry
    _ret(core, tid)

def thread_exit(core): core.HALT()
def thread_join(core, tid): _ret(core, 0)
def sched_yield(core): _ret(core, 0)

# =========================
# 30–39: Память
# =========================
def mem_map(core, addr, size): _ret(core, int(addr, 0))
def mem_unmap(core, addr): _ret(core, 0)
def mem_protect(core, addr, flags): _ret(core, 0)
def mem_brk(core, addr): _ret(core, int(addr, 0))
def mem_info(core): _ret(core, 0)

# =========================
# 40–49: IPC / синхронизация
# =========================
MESSAGE_QUEUES = {}
def ipc_create(core, q): MESSAGE_QUEUES[q] = []; _ret(core, 0)
def ipc_destroy(core, q): MESSAGE_QUEUES.pop(q, None); _ret(core,0)
def ipc_send(core, q, msg):
    if q in MESSAGE_QUEUES: MESSAGE_QUEUES[q].append(msg)
    _ret(core, 0)
def ipc_recv(core, q):
    msgs = MESSAGE_QUEUES.get(q, [])
    core.registers['L1'] = msgs.pop(0) if msgs else 0

def mutex_lock(core, m): _ret(core, 0)
def mutex_unlock(core, m): _ret(core, 0)
def semaphore_down(core, s): _ret(core,0)
def semaphore_up(core, s): _ret(core,0)

# =========================
# 50–59: Таймеры / сигналы
# =========================
TIMERS = {}
def timer_create(core, interval): tid = len(TIMERS)+1; TIMERS[tid]=interval; _ret(core, tid)
def timer_sleep(core, ms): time.sleep(ms/1000); _ret(core,0)
def signal_send(core, pid, sig): _ret(core,0)
def signal_set_handler(core, sig, h): _ret(core,0)
def pause(core): _ret(core,0)
def nanosleep(core, ns): time.sleep(ns/1e9); _ret(core,0)

# =========================
# 60–69: Сеть (Socket)
# =========================
SOCKETS = {}
def socket_create(core, proto): sid=len(SOCKETS)+1; SOCKETS[sid]={}; _ret(core, sid)
def socket_bind(core, sid, port): _ret(core,0)
def socket_listen(core, sid, backlog): _ret(core,0)
def socket_accept(core, sid): _ret(core, sid)
def socket_connect(core, sid, addr, port): _ret(core,0)
def socket_send(core, sid, data): _ret(core, len(data))
def socket_recv(core, sid, size): _ret(core, 0)

# =========================
# 70–79: Устройства / драйверы
# =========================
DEVICES = {}
def dev_open(core, name): DEVICES[name]=True; _ret(core,0)
def dev_close(core, name): DEVICES.pop(name,None); _ret(core,0)
def dev_ioctl(core, name, cmd, arg): _ret(core,0)
def dev_mmap(core, name, addr): _ret(core, int(addr,0))
def dev_list(core): _ret(core, len(DEVICES))

# =========================
# 80–89: Системные настройки
# =========================
def set_priority(core, pid, prio): _ret(core,prio)
def get_priority(core, pid): _ret(core,0)
def reboot_system(core): _ret(core,0)
def shutdown_system(core): _ret(core,0)
def clock_get(core): _ret(core, int(time.time()))
def thermal_read(core): _ret(core,0)

# =========================
# Таблица системных вызовов
# =========================
SYSCALL_TABLE = {
    1: print_text, 2: read_input, 3: halt_system, 4: debug_log, 5: sys_nop,
    10: file_open, 11: file_close, 12: file_create, 13: file_read,
    14: file_write, 15: file_seek, 16: file_stat, 17: file_delete,
    20: proc_spawn, 21: proc_exit, 22: proc_wait, 23: proc_kill,
    24: thread_create, 25: thread_exit, 26: thread_join, 27: sched_yield,
    30: mem_map, 31: mem_unmap, 32: mem_protect, 33: mem_brk, 39: mem_info,
    40: ipc_create, 41: ipc_destroy, 42: ipc_send, 43: ipc_recv,
    44: mutex_lock, 45: mutex_unlock, 46: semaphore_down, 47: semaphore_up,
    50: timer_create, 51: timer_sleep, 52: signal_send,
    53: signal_set_handler, 54: pause, 55: nanosleep,
    60: socket_create, 61: socket_bind, 62: socket_listen,
    63: socket_accept, 64: socket_connect, 65: socket_send,
    66: socket_recv,
    70: dev_open, 71: dev_close, 72: dev_ioctl, 73: dev_mmap, 74: dev_list,
    80: set_priority, 81: get_priority, 82: reboot_system,
    83: shutdown_system, 84: clock_get, 85: thermal_read
}
