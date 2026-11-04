# isa.py — единый источник правды для ISA

OPCODES = {
    # Arithmetic
    "SUM": 0x01, "SUB": 0x02, "MUL": 0x03, "DIV": 0x04, "MOD": 0x05,
    "POW": 0x30, "FLOORDIV": 0x31,
    # Float Arithmetic
    "FSUM": 0x32,   # float сложение
    "FSUB": 0x33,   # float вычитание
    "FMUL": 0x34,   # float умножение
    "FDIV": 0x35,   # float деление
    "FPOW": 0x36,   # float возведение в степень


    # Logic
    "AND": 0x06, "OR": 0x07, "XOR": 0x08, "NOT": 0x09,

    # Memory / Stack
    "LOAD": 0x0A, "STORE": 0x0B,
    "PUSH": 0x0C, "POP": 0x0D,
    "LOADF": 0x0E,   # загрузка float в регистр F
    "STOREF": 0x0F,  # сохранение регистра F в память

    # Compare / Branch
    "CMP": 0x10,
    "JMP": 0x11, "JE": 0x12, "JNE": 0x13,
    "JL": 0x14, "JLE": 0x15,
    "JG": 0x16, "JGE": 0x17,
    "JZ": 0x18, "JNZ": 0x19,

    # Atomic
    "ATOMIC_ADD": 0x40,
    "ATOMIC_CAS": 0x41,

    # Output / Control
    "PRINT": 0x20, "PRINT_STR": 0x21,"PRINTF": 0x22,

    "FTOI": 0x37,   # Float to Int (truncate toward zero)
    "ITOF": 0x38,   # Int to Float

    "HALT": 0xFF,

    "CALL": 0x50,
    "RET":  0x51,
}
