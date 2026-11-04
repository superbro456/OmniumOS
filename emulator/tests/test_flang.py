# Тесты компилятора Flang и эмулятора OmniumCPU.
# Запуск из папки emulator:  python3 -m pytest tests
import contextlib
import io
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from flang.compiler import compile_source, compile_file  # noqa: E402
from flang.lexer import tokenize  # noqa: E402
from assembler.assembler import assemble  # noqa: E402
from emulator.multi_core import MultiCoreCPU  # noqa: E402


def run(src=None, path=None, timeout=20, max_steps=None):
    asm = compile_file(path) if path else compile_source(src)
    out = io.StringIO()
    cpu = MultiCoreCPU(step_delay=0, timeout=timeout, max_steps=max_steps)
    with contextlib.redirect_stdout(out):
        ok = cpu.run([assemble(asm)])
    return out.getvalue().split(), cpu, ok


def test_binop_does_not_clobber_variable():
    out, _, _ = run("int a = 5; int b = a + 1; print(a); print(b);")
    assert out == ["5", "6"]


def test_recursion():
    out, _, _ = run("""
func f(int n) {
  if (n <= 1) { return 1; }
  int m = n - 1;
  int r = f(m);
  return n * r;
}
int x = f(5);
print(x);
""")
    assert out == ["120"]


def test_call_keeps_caller_variables():
    out, _, _ = run("""
func g(int v) { int w = v * 10; return w; }
int k = 3;
int y = g(k);
print(k);
print(y);
""")
    assert out == ["3", "30"]


def test_forward_call():
    out, _, _ = run("""
int r = later(4);
print(r);
func later(int v) { return v + 1; }
""")
    assert out == ["5"]


@pytest.mark.parametrize("expr", ["x / y", "x % y"])
def test_division_by_zero_is_an_error(expr):
    _, cpu, _ = run(f"int x = 7; int y = 0; int z = {expr}; print(z);")
    assert "by zero" in cpu.errors[0]


def test_long_loop_finishes():
    out, _, ok = run("int i = 0; int s = 0; while (i < 200000) { s = s + i; i = i + 1; } print(s);")
    assert ok and out == ["19999900000"]


def test_infinite_loop_is_reported():
    out, _, ok = run("int t = 1; while (t > 0) { t = t + 1; }", max_steps=50000)
    assert not ok
    assert "stopped" in " ".join(out)


def test_lexer_rejects_unknown_character():
    with pytest.raises(SyntaxError):
        tokenize("int a = 5 @ 3;")


def test_lexer_unterminated_comment_runs_to_end():
    assert tokenize("int a = 1; # comment without end") == [
        ("TYPE", "int"), ("ID", "a"), ("ASSIGN", "="), ("NUMBER", 1), ("SEMI", ";")]


def test_example_fx():
    out, _, _ = run(path=os.path.join(ROOT, "test.fx"))
    assert out == ["2", "3", "6", "2.0", "2.65"]


def test_example_flx():
    out, _, _ = run(path=os.path.join(ROOT, "test.flx"))
    assert out == ["100", "10", "3", "6", "2", "4", "6", "8", "10"]
