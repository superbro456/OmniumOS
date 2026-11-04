# main.py
import time
from flang.compiler import compile_file
from assembler.assembler import assemble
from emulator.multi_core import MultiCoreCPU
from flang.config import MODE

# =========================
# Файлы исходников (можешь добавить свои)
# =========================
SOURCE_FILES = ["test.fx"]  # <- сюда вставь свои файлы

# =========================
# Запуск интерпретатора
# =========================
def run_interpreter():
    print("=== Running in INTERPRETER mode ===\n")
    from flang.ir import run_code

    for src in SOURCE_FILES:
        with open(src, "r") as f:
            code = f.read()
        print(f"\n--- {src} ---")
        try:
            vars_after = run_code(code)
            print("Result:", vars_after)
        except Exception as e:
            print(f"[INTERPRETER ERROR] {e}")


# =========================
# Запуск компилированного кода на MultiCoreCPU
# =========================
def run_compile():
    print("=== Running in COMPILE mode ===\n")
    programs = []

    for src in SOURCE_FILES:
        try:
            asm_code = compile_file(src)
            bytecode = assemble(asm_code)
            programs.append(bytecode)
        except Exception as e:
            print(f"[COMPILER ERROR] {src}: {e}")

    if not programs:
        print("[MAIN] No programs to run")
        return

    # Создаем MultiCoreCPU с количеством ядер = числу файлов
    cpu = MultiCoreCPU(
        num_cores=len(programs),
        debug=False,
        step_delay=0,   # 0 = максимальная скорость
        timeout=30,
    )

    ok = cpu.run(programs)
    for core_id, err in cpu.errors.items():
        print(f"[MAIN] core {core_id} failed: {err}")
    if not ok:
        print("[MAIN] program did not finish (timeout)")


# =========================
# ENTRY POINT
# =========================
if __name__ == "__main__":
    if MODE == "INTERPRETER":
        run_interpreter()
    elif MODE == "COMPILE":
        run_compile()
    else:
        print(f"[MAIN ERROR] Unknown MODE: {MODE}")
