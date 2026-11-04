# core.py — OmniumCPU Core (SMP-safe, atomic-correct)
import re
from collections import deque
import threading
from isa import OPCODES

PRINT_LOCK = threading.RLock()


class Core:
    def __init__(self, core_id, memory, memory_lock, atomic_lock, debug=False):
        self.core_id = core_id
        self.debug = debug
        self.error = None
        self.steps = 0
        self.last_cmp_pc = None
        self.reg = {f"H{i}": 0 for i in range(31)}
        self.freg = {f"F{i}": 0.0 for i in range(16)}  # регистры для float
        self.RETVAL = None
        self.PC = 0
        self.halted = False
        self.Z = False
        self.N = False
        self.stack = []
        self._stack_lock = threading.RLock()
        self.call_stack = []
        self.memory = memory
        self._memory_lock = memory_lock
        self._atomic_lock = atomic_lock
        self.interrupts = deque()

    # ----------------------
    # Helpers
    # ----------------------
    def val(self, x):
        if isinstance(x, (int, float)):
            return x
        if isinstance(x, str):
            if x in self.reg:
                return self.reg[x]
            if x in self.freg:
                return self.freg[x]
            with self._memory_lock:
                val = self.memory.get(x, 0)
                # если value из memory float, вернуть как float
                if isinstance(val, float):
                    return val
                # если int, вернуть как есть
                return val
        raise TypeError(x)




    def write(self, dst, value):
        if isinstance(dst, str):
            if dst in self.reg:
                self.reg[dst] = value
                return
            if dst in self.freg:
                self.freg[dst] = float(value)
                return
            with self._memory_lock:
                # если value float, сохраняем как float
                if isinstance(value, float):
                    self.memory[dst] = float(value)
                else:
                    self.memory[dst] = value
                return
        raise TypeError(dst)





    # ----------------------
    # Step execution
    # ----------------------
    def step(self, opcode, args, program, program_length):
        if self.halted:
            return
        self.steps += 1

        # Преобразуем opcode и args
        if isinstance(opcode, (list, tuple)):
            opcode = int(opcode[0])
        

        jumped = False

        try:
            # -------------------------
            # Arithmetic
            # -------------------------
            if opcode == OPCODES["SUM"]:
                if args[2] not in self.reg:
                    raise RuntimeError("SUM result must be int register")
                self.reg[args[2]] = int(self.val(args[0]) + self.val(args[1]))

            elif opcode == OPCODES["SUB"]:
                self.write(args[2], self.val(args[0]) - self.val(args[1]))
            elif opcode == OPCODES["MUL"]:
                self.write(args[2], self.val(args[0]) * self.val(args[1]))
            elif opcode == OPCODES["DIV"]:
                a = self.val(args[0])
                b = self.val(args[1])
                if b == 0:
                    raise RuntimeError("Division by zero")
                if isinstance(a, float) or isinstance(b, float):
                    raise RuntimeError("DIV does not support float, use FDIV")
                self.write(args[2], self.val(args[0]) // b)

            elif opcode == OPCODES["MOD"]:
                b = self.val(args[1])
                if b == 0:
                    raise RuntimeError("Modulo by zero")
                self.write(args[2], self.val(args[0]) % b)
            elif opcode == OPCODES["POW"]:
                self.write(args[2], self.val(args[0]) ** self.val(args[1]))
            elif opcode == OPCODES["FLOORDIV"]:
                b = self.val(args[1])
                if b == 0:
                    raise RuntimeError("Division by zero")
                self.write(args[2], self.val(args[0]) // b)
            # -------------------------
            # Float Arithmetic
            # -------------------------
            elif opcode == OPCODES["FSUM"]:
                self.write(args[2], float(self.val(args[0])) + float(self.val(args[1])))
            elif opcode == OPCODES["FSUB"]:
                self.write(args[2], float(self.val(args[0])) - float(self.val(args[1])))
            elif opcode == OPCODES["FMUL"]:
                self.write(args[2], float(self.val(args[0])) * float(self.val(args[1])))
            elif opcode == OPCODES["FDIV"]:
                b = float(self.val(args[1]))
                if b == 0.0:
                    raise RuntimeError("Division by zero")
                self.write(args[2], float(self.val(args[0])) / b)
            elif opcode == OPCODES["FPOW"]:
                self.write(args[2], float(self.val(args[0])) ** float(self.val(args[1])))

            # -------------------------
            # LOADF / STOREF
            # -------------------------
            elif opcode == OPCODES["LOADF"]:
                src, dst = args
                if dst not in self.freg:
                    raise RuntimeError("LOADF target must be float register")
                value = self.val(src)
                self.freg[dst] = value if isinstance(value, float) else float(value)
            elif opcode == OPCODES["STOREF"]:
                dst, src = args
                if src not in self.freg:
                    raise RuntimeError("STOREF source must be float register")

                with self._memory_lock:
                    # Сохраняем float напрямую, не кастим
                    self.memory[dst] = self.freg[src]



            elif opcode == OPCODES["FTOI"]:
                src, dst = args
                if src not in self.freg or dst not in self.reg:
                    raise RuntimeError("FTOI: source must be F-reg, dest must be H-reg")
                self.reg[dst] = int(self.freg[src])  # обрезает к нулю (как в C)

            elif opcode == OPCODES["ITOF"]:
                src, dst = args
                if src not in self.reg or dst not in self.freg:
                    raise RuntimeError("ITOF: source must be H-reg, dest must be F-reg")
                self.freg[dst] = float(self.reg[src])



            # -------------------------
            # Logic
            # -------------------------
            elif opcode == OPCODES["AND"]:
                self.write(args[2], self.val(args[0]) & self.val(args[1]))
            elif opcode == OPCODES["OR"]:
                self.write(args[2], self.val(args[0]) | self.val(args[1]))
            elif opcode == OPCODES["XOR"]:
                self.write(args[2], self.val(args[0]) ^ self.val(args[1]))
            elif opcode == OPCODES["NOT"]:
                self.write(args[1], ~self.val(args[0]))



            # -------------------------
            # Memory
            # -------------------------
            elif opcode == OPCODES["LOAD"]:
                src, dst = args
                if dst not in self.reg:
                    raise RuntimeError("LOAD target must be int register")
                if isinstance(src, int):
                    self.reg[dst] = src
                else:
                    with self._memory_lock:
                        self.reg[dst] = self.memory.get(src, 0)
                if self.debug:
                    print(f"[LOAD] {src} -> {dst} = {self.reg[dst]}")
            elif opcode == OPCODES["STORE"]:
                if args[1] not in self.reg:
                    raise RuntimeError("STORE source must be int register")

                with self._memory_lock:
                    self.memory[args[0]] = self.reg[args[1]]
                if self.debug:
                    print(f"[STORE] {args[1]} -> {args[0]} = {self.reg[args[1]]}")



            # -------------------------
            # Stack
            # -------------------------
            elif opcode == OPCODES["PUSH"]:
                with self._stack_lock:
                    self.stack.append(self.val(args[0]))
            elif opcode == OPCODES["POP"]:
                with self._stack_lock:
                    value = self.stack.pop() if self.stack else 0
                self.write(args[0], value)



            # -------------------------
            # Compare
            # -------------------------
            elif opcode == OPCODES["CMP"]:
                a = self.val(args[0])
                b = self.val(args[1])
                if isinstance(a, float) or isinstance(b, float):
                    raise RuntimeError("CMP does not support float")

                self.Z = (a == b)
                self.N = (a < b)
                self.last_cmp_pc = self.PC

                if self.debug:
                    print(f"[CMP] {a} vs {b} | Z={self.Z} N={self.N}")



            # -------------------------
            # Jumps
            # -------------------------
            elif opcode in (
                OPCODES["JMP"], OPCODES["JE"], OPCODES["JNE"],
                OPCODES["JL"], OPCODES["JLE"], OPCODES["JG"], OPCODES["JGE"],
                OPCODES["JZ"], OPCODES["JNZ"]
            ):
                if not args:
                    raise ValueError("Missing jump target")
                target = args[0]
                cond_met = False

                if opcode == OPCODES["JMP"]:
                    cond_met = True
                else:
                    if self.last_cmp_pc is None or self.last_cmp_pc >= self.PC:
                        raise RuntimeError("Conditional jump without fresh CMP")
                    if opcode in (OPCODES["JE"], OPCODES["JZ"]):
                        cond_met = self.Z
                    elif opcode in (OPCODES["JNE"], OPCODES["JNZ"]):
                        cond_met = not self.Z
                    elif opcode == OPCODES["JL"]:
                        cond_met = self.N
                    elif opcode == OPCODES["JLE"]:
                        cond_met = self.N or self.Z
                    elif opcode == OPCODES["JG"]:
                        cond_met = not self.N and not self.Z
                    elif opcode == OPCODES["JGE"]:
                        cond_met = not self.N or self.Z

                if cond_met:
                    if isinstance(target, str):
                        # ищем метку в program
                        found = False
                        for idx, (op, a) in enumerate(program):
                            if op == "LABEL" and a[0] == target:
                                self.PC = idx
                                found = True
                                break
                        if not found:
                            raise RuntimeError(f"Jump target LABEL '{target}' not found")
                    else:
                        if not (0 <= target <= program_length):
                            raise RuntimeError(f"Invalid jump target {target}")

                        self.PC = target
                    jumped = True



            # -------------------------
            # Atomic
            # -------------------------
            elif opcode == OPCODES["ATOMIC_ADD"]:
                addr, value, dst = args
                with self._atomic_lock:
                    old = self.memory.get(addr, 0)
                    new = old + self.val(value)
                    self.memory[addr] = new
                self.write(dst, new)
            elif opcode == OPCODES["ATOMIC_CAS"]:
                addr, expected, newval, dst = args
                with self._atomic_lock:
                    old = self.memory.get(addr, 0)
                    success = (old == self.val(expected))
                    if success:
                        self.memory[addr] = self.val(newval)
                self.write(dst, int(success))



            # -------------------------
            # Output
            # -------------------------
            elif opcode == OPCODES["PRINTF"]:
                dst = args[0]
                with PRINT_LOCK:
                    val = None
                    if dst in self.freg:
                        val = self.freg[dst]
                    elif dst in self.reg:
                        val = self.reg[dst]
                    else:
                        with self._memory_lock:
                            val = self.memory.get(dst, 0)
                    print(val, flush=True)   # ← вот так



            elif opcode == OPCODES["PRINT"]:
                arg = args[0]
                with PRINT_LOCK:
                    if arg in self.freg:
                        val = self.freg[arg]
                    elif arg in self.reg:
                        val = self.reg[arg]
                    else:
                        with self._memory_lock:
                            val = self.memory.get(arg, 0)
                    if isinstance(val, float):
                        print(f"{val:.10g}", flush=True)
                    else:
                        print(val, flush=True)


                        
            elif opcode == OPCODES["PRINT_STR"]:
                            with PRINT_LOCK:
                                print(args[0], flush=True)



            # -------------------------
            # Call / Ret
            # -------------------------
            elif opcode == OPCODES["CALL"]:
                func_addr = args[0]

                # CALL с возвратом или void
                ret_reg = args[1] if len(args) > 1 else None

                # сохраняем старый RETVAL ВСЕГДА
                self.call_stack.append((self.PC + 1, ret_reg, self.RETVAL))

                # новый фрейм — RETVAL пуст
                self.RETVAL = None

                self.PC = func_addr
                jumped = True



            elif opcode == OPCODES["RET"]:
                # void-RET допустим
                if not self.call_stack:
                    self.halted = True
                else:
                    return_addr, ret_reg, old_retval = self.call_stack.pop()

                    # если вызывали с ожиданием результата
                    if ret_reg is not None and self.RETVAL is not None:
                        self.write(ret_reg, self.RETVAL)

                    # восстановление предыдущего RETVAL
                    self.RETVAL = old_retval
                    self.PC = return_addr

                jumped = True



            # -------------------------
            # Halt
            # -------------------------
            elif opcode == OPCODES["HALT"]:
                self.halted = True
                return

            else:
                if self.debug:
                    print(f"[Core {self.core_id}] Unknown opcode {opcode}")
                self.halted = True
                return

        except Exception as e:
            self.error = f"PC={self.PC}: {e}"
            print(f"[Core {self.core_id} ERROR] PC={self.PC}: {e}")
            self.halted = True
            return

        if opcode != OPCODES["CMP"]:
            self.last_cmp_pc = None



        # -------------------------
        # Increment PC
        # -------------------------
        if not jumped:
            self.PC += 1
        if self.PC >= program_length:
            self.halted = True
