# assembler.py — исправленная версия
PSEUDO = {"LABEL"}
from isa import OPCODES

def assemble(program):
    labels = {}
    bytecode = []

    # PASS 1: метки
    pc = 0
    for instr, args in program:
        if instr in PSEUDO:
            if instr == "LABEL":
                labels[args[0]] = pc
        else:
            pc += 1

    # PASS 2: генерация
    for instr, args in program:
        if instr in PSEUDO:
            continue

        if instr not in OPCODES:
            raise ValueError(f"Unknown instruction: {instr}")

        opcode = OPCODES[instr]
        resolved_args = []

        for a in args:
            if isinstance(a, str):
                if a in labels:
                    resolved_args.append(labels[a])
                else:
                    resolved_args.append(a)  # строка (регистр, переменная)
            else:
                # ← Ключевое изменение: сохраняем как есть!
                resolved_args.append(a)  # int или float — оставляем тип!

        bytecode.append((opcode, resolved_args))

    return bytecode
