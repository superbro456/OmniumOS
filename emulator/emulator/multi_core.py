# multi_core.py — OmniumCPU: несколько ядер, исполняемых по очереди (round-robin)
import time
import threading
from collections import deque
from .core import Core


def _flatten(args):
    flat = []
    for a in args:
        if isinstance(a, (list, tuple)):
            flat.extend(a)
        else:
            flat.append(a)
    return flat


class MultiCoreCPU:
    def __init__(self, num_cores=1, debug=False, step_delay=0.0, timeout=5,
                 shared_memory=None, max_steps=None):
        self.num_cores = num_cores
        self.debug = debug
        self.step_delay = step_delay
        self.timeout = timeout
        self.max_steps = max_steps

        self.shared_memory = shared_memory if shared_memory is not None else {}
        self.memory_lock = threading.RLock()
        self.atomic_lock = threading.Lock()

        self.cores = []
        self._run_queue = deque()
        self.timed_out = False
        self.total_steps = 0

    def run(self, programs):
        """Запускает программы (по одной на ядро). Возвращает True, если все
        ядра завершились сами, и False при таймауте или лимите шагов."""
        self.cores.clear()
        self._run_queue.clear()
        self.timed_out = False
        self.total_steps = 0

        for core_id, program in enumerate(programs):
            core = Core(
                core_id=core_id,
                memory=self.shared_memory,
                memory_lock=self.memory_lock,
                atomic_lock=self.atomic_lock,
                debug=self.debug,
            )
            # аргументы раскладываем один раз, а не на каждом шаге
            core.program = [(op, _flatten(args)) for op, args in program]
            self.cores.append(core)
            self._run_queue.append(core)

        start_time = time.time()

        while self._run_queue:
            core = self._run_queue.popleft()
            if core.halted:
                continue

            self.total_steps += 1
            if self.total_steps % 1024 == 0 and time.time() - start_time > self.timeout:
                self._stop("Timeout reached after %.1fs" % self.timeout)
                break
            if self.max_steps is not None and self.total_steps > self.max_steps:
                self._stop("Step limit %d reached" % self.max_steps)
                break

            try:
                instr, args = core.program[core.PC]
                core.step(instr, args, core.program, program_length=len(core.program))
            except Exception as e:
                core.error = f"PC={core.PC}: {e}"
                print(f"[Core {core.core_id} ERROR] {core.error}")
                core.halted = True
                continue

            if not core.halted:
                self._run_queue.append(core)

            if self.step_delay:
                time.sleep(self.step_delay)

        return not self.timed_out

    def _stop(self, reason):
        # Таймаут сообщаем всегда: раньше программа обрывалась молча
        self.timed_out = True
        print(f"[MultiCoreCPU] {reason}, program stopped")
        for c in self.cores:
            c.halted = True

    @property
    def errors(self):
        return {c.core_id: c.error for c in self.cores if c.error}
