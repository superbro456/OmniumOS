# compiler.py — Flang -> OmniumCPU assembler
#
# Правила генерации кода (почему так):
#   * Переменные живут в памяти. Перед каждым использованием переменная
#     загружается в НОВЫЙ временный регистр, после присваивания сохраняется.
#     Раньше регистр переменной переиспользовался как результат выражения,
#     и `int b = a + 1` портил `a`.
#   * Результат любой операции всегда пишется в новый временный регистр.
#   * Вызов функции:
#       caller: вычисляет аргументы -> PUSH живых временных регистров
#               -> PUSH аргументов -> CALL -> POP живых регистров -> LOAD RETVAL
#       callee: POP аргументов -> сохраняет старые значения своих локальных
#               переменных на стек -> пишет аргументы в свои параметры;
#               перед каждым RET восстанавливает локальные переменные.
#     Так работает рекурсия, а вызов функции не портит переменные вызывающего.
#   * Локальные переменные функции хранятся под именем "<функция>.<имя>",
#     поэтому не пересекаются с глобальными.

H_REGS = [f"H{i}" for i in range(31)]
F_REGS = [f"F{i}" for i in range(16)]

INT_TYPES = ("int", "uint", "big_int", "ubig_int", "light_int", "ulight_int",
             "light_uint", "big_uint")
FLOAT_TYPES = ("float", "ufloat", "big_float", "ubig_float", "light_float", "ulight_float")

CMP_JUMP = {"==": "JE", "!=": "JNE", "<": "JL", "<=": "JLE", ">": "JG", ">=": "JGE"}
CMP_INVERSE = {"==": "JNE", "!=": "JE", "<": "JGE", "<=": "JG", ">": "JLE", ">=": "JL"}
INT_OPS = {"+": "SUM", "-": "SUB", "*": "MUL", "/": "FLOORDIV", "//": "FLOORDIV",
           "%": "MOD", "**": "POW", "&": "AND", "|": "OR", "^": "XOR"}
FLOAT_OPS = {"+": "FSUM", "-": "FSUB", "*": "FMUL", "/": "FDIV", "**": "FPOW"}


def is_float_type(t):
    return t is not None and t.startswith("float") or t in FLOAT_TYPES


class CompileError(Exception):
    pass


class _Func:
    def __init__(self, name, args, body, label):
        self.name = name          # полное имя (outer.inner)
        self.args = args          # [(type, name)]
        self.body = body
        self.label = label
        self.locals = []          # имена параметров и локальных переменных


class Compiler:
    def __init__(self):
        self.asm = []
        self.free_h = list(reversed(H_REGS))
        self.free_f = list(reversed(F_REGS))
        self.label_id = 0
        self.loop_stack = []
        self.funcs = {}           # полное имя -> _Func
        self.cur = None           # текущая функция (_Func) или None
        self.types = {}           # адрес переменной -> тип

    # ---------------- утилиты ----------------
    def emit(self, op, args=None):
        self.asm.append((op, list(args or [])))

    def new_label(self, prefix):
        self.label_id += 1
        return f"__{prefix}_{self.label_id}"

    def alloc_h(self):
        if not self.free_h:
            raise CompileError("No free H registers (expression too complex)")
        return self.free_h.pop()

    def alloc_f(self):
        if not self.free_f:
            raise CompileError("No free F registers (expression too complex)")
        return self.free_f.pop()

    def free(self, r):
        if r in F_REGS:
            if r not in self.free_f:
                self.free_f.append(r)
        elif r in H_REGS:
            if r not in self.free_h:
                self.free_h.append(r)

    def live_regs(self):
        live_h = [r for r in H_REGS if r not in self.free_h]
        live_f = [r for r in F_REGS if r not in self.free_f]
        return live_h + live_f

    def addr(self, name):
        if self.cur is not None and name in self.cur.locals:
            return f"{self.cur.name}.{name}"
        return name

    def resolve_func(self, name):
        scope = self.cur.name if self.cur else None
        while scope:
            cand = f"{scope}.{name}"
            if cand in self.funcs:
                return self.funcs[cand]
            scope = scope.rpartition(".")[0] or None
        if name in self.funcs:
            return self.funcs[name]
        raise CompileError(f"Function {name} not found")

    # ---------------- предварительный проход ----------------
    def register_funcs(self, stmts):
        for s in stmts:
            if s[0] == "func":
                _, name, args, body = s
                f = _Func(name, args, body, self.new_label(f"func_{name}"))
                f.locals = self.collect_locals(args, body)
                self.funcs[name] = f
                self.register_funcs(body)

    @staticmethod
    def collect_locals(args, body):
        names = [a[1] for a in args]

        def walk(stmts):
            for s in stmts:
                k = s[0]
                if k == "assign" and s[1] not in names:
                    names.append(s[1])
                elif k == "if":
                    walk(s[2])
                    for _, eb in s[3]:
                        walk(eb)
                    walk(s[4])
                elif k == "while":
                    walk(s[2])
                elif k == "for":
                    walk([s[1], s[3]])
                    walk(s[4])
        walk(body)
        return names

    # ---------------- выражения ----------------
    def expr(self, e, target_type=None):
        kind = e[0]

        if kind == "num":
            r = self.alloc_h()
            self.emit("LOAD", [e[1], r])
            return r

        if kind == "float":
            r = self.alloc_f()
            self.emit("LOADF", [float(e[1]), r])
            return r

        if kind == "var":
            a = self.addr(e[1])
            if is_float_type(self.types.get(a)):
                r = self.alloc_f()
                self.emit("LOADF", [a, r])
            else:
                r = self.alloc_h()
                self.emit("LOAD", [a, r])
            return r

        if kind == "binop":
            _, l, op, rgt = e
            if op in CMP_JUMP:
                raise CompileError("Comparison is only allowed in if/while/for conditions")
            lr = self.expr(l)
            rr = self.expr(rgt)
            if lr in F_REGS or rr in F_REGS:
                if op not in FLOAT_OPS:
                    raise CompileError(f"Operator {op} is not supported for float")
                lr = self.to_float(lr)
                rr = self.to_float(rr)
                res = self.alloc_f()
                self.emit(FLOAT_OPS[op], [lr, rr, res])
            else:
                if op not in INT_OPS:
                    raise CompileError(f"Unknown operator {op}")
                res = self.alloc_h()
                self.emit(INT_OPS[op], [lr, rr, res])
            self.free(lr)
            self.free(rr)
            return res

        if kind == "convert":
            if target_type is None:
                raise CompileError("convert requires target type context")
            src = self.expr(e[1])
            if is_float_type(target_type):
                return self.to_float(src)
            if src in F_REGS:
                dst = self.alloc_h()
                self.emit("FTOI", [src, dst])
                self.free(src)
                return dst
            return src

        if kind == "call":
            return self.call(e[1], e[2])

        if kind == "str":
            return e[1]

        raise CompileError(f"Unknown expression kind: {e}")

    def to_float(self, r):
        if r in F_REGS:
            return r
        f = self.alloc_f()
        self.emit("ITOF", [r, f])
        self.free(r)
        return f

    def call(self, name, args_expr):
        f = self.resolve_func(name)
        if len(args_expr) != len(f.args):
            raise CompileError(f"{name}() expects {len(f.args)} args, got {len(args_expr)}")
        arg_regs = []
        for (atype, _), a in zip(f.args, args_expr):
            r = self.expr(a, target_type=atype)
            if is_float_type(atype):
                r = self.to_float(r)
            arg_regs.append(r)
        saved = [r for r in self.live_regs() if r not in arg_regs]
        for r in saved:
            self.emit("PUSH", [r])
        for r in arg_regs:
            self.emit("PUSH", [r])
            self.free(r)
        self.emit("CALL", [f.label])
        for r in reversed(saved):
            self.emit("POP", [r])
        res = self.alloc_h()
        self.emit("LOAD", ["RETVAL", res])
        return res

    # ---------------- условия ----------------
    def cond_jump(self, e, label, jump_if_false):
        if e[0] == "binop" and e[2] in CMP_JUMP:
            lr = self.expr(e[1])
            rr = self.expr(e[3])
            if lr in F_REGS or rr in F_REGS:
                raise CompileError("Float comparison is not supported yet (CMP is int-only)")
            self.emit("CMP", [lr, rr])
            jmp = CMP_INVERSE[e[2]] if jump_if_false else CMP_JUMP[e[2]]
            self.emit(jmp, [label])
            self.free(lr)
            self.free(rr)
        else:
            r = self.expr(e)
            z = self.alloc_h()
            self.emit("LOAD", [0, z])
            self.emit("CMP", [r, z])
            self.emit("JE" if jump_if_false else "JNE", [label])
            self.free(r)
            self.free(z)

    # ---------------- функции ----------------
    def function(self, f):
        outer = self.cur
        self.cur = f
        end = self.new_label(f"endfunc_{f.name}")
        self.emit("JMP", [end])
        self.emit("LABEL", [f.label])

        for atype, aname in f.args:
            self.types[self.addr(aname)] = atype

        # 1) снять аргументы со стека (последний положен последним)
        arg_regs = []
        for _ in f.args:
            arg_regs.append(self.alloc_h())
        for r in reversed(arg_regs):
            self.emit("POP", [r])
        # 2) сохранить старые значения локальных переменных (для рекурсии)
        for name in f.locals:
            t = self.alloc_h()
            self.emit("LOAD", [self.addr(name), t])
            self.emit("PUSH", [t])
            self.free(t)
        # 3) записать аргументы в параметры
        for (atype, aname), r in zip(f.args, arg_regs):
            self.emit("STORE", [self.addr(aname), r])
            self.free(r)

        self.block([s for s in f.body if s[0] != "func"])
        self.epilogue()

        self.emit("LABEL", [end])
        self.cur = outer
        for s in f.body:
            if s[0] == "func":
                self.function(self.funcs[s[1]])

    def epilogue(self):
        for name in reversed(self.cur.locals):
            t = self.alloc_h()
            self.emit("POP", [t])
            self.emit("STORE", [self.addr(name), t])
            self.free(t)
        self.emit("RET")

    # ---------------- инструкции ----------------
    def block(self, stmts):
        for s in stmts:
            self.stmt(s)

    def stmt(self, s):
        kind = s[0]

        if kind == "assign":
            _, name, e, var_type = s
            a = self.addr(name)
            if a not in self.types:
                self.types[a] = var_type or "int"
            declared = self.types[a]
            r = self.expr(e, target_type=declared)
            if is_float_type(declared):
                r = self.to_float(r)
                self.emit("STOREF", [a, r])
            else:
                if r in F_REGS:
                    raise CompileError(
                        f"Cannot assign float to int variable '{name}' (use convert)")
                self.emit("STORE", [a, r])
            self.free(r)
            return

        if kind == "print_expr":
            e = s[1]
            if e[0] == "str":
                self.emit("PRINT_STR", [e[1]])
                return
            r = self.expr(e)
            self.emit("PRINTF" if r in F_REGS else "PRINT", [r])
            self.free(r)
            return

        if kind == "print_str":
            self.emit("PRINT_STR", [s[1]])
            return

        if kind == "if":
            cond, body, elifs, else_body = s[1], s[2], s[3], s[4]
            end_l = self.new_label("ifend")
            next_l = self.new_label("ifelse")
            self.cond_jump(cond, next_l, jump_if_false=True)
            self.block(body)
            self.emit("JMP", [end_l])
            self.emit("LABEL", [next_l])
            for ec, eb in elifs:
                n2 = self.new_label("ifelse")
                self.cond_jump(ec, n2, jump_if_false=True)
                self.block(eb)
                self.emit("JMP", [end_l])
                self.emit("LABEL", [n2])
            self.block(else_body)
            self.emit("LABEL", [end_l])
            return

        if kind == "while":
            start = self.new_label("while")
            step = self.new_label("while_step")
            end = self.new_label("wend")
            self.emit("LABEL", [start])
            self.emit("LABEL", [step])
            self.loop_stack.append({"step": step, "end": end})
            self.cond_jump(s[1], end, jump_if_false=True)
            self.block(s[2])
            self.emit("JMP", [start])
            self.loop_stack.pop()
            self.emit("LABEL", [end])
            return

        if kind == "for":
            init, cond, step_s, body = s[1], s[2], s[3], s[4]
            self.stmt(init)
            start = self.new_label("for_start")
            step = self.new_label("for_step")
            end = self.new_label("for_end")
            self.emit("LABEL", [start])
            self.loop_stack.append({"step": step, "end": end})
            self.cond_jump(cond, end, jump_if_false=True)
            self.block(body)
            self.emit("LABEL", [step])
            self.stmt(step_s)
            self.emit("JMP", [start])
            self.loop_stack.pop()
            self.emit("LABEL", [end])
            return

        if kind == "func":
            if self.cur is None:
                self.function(self.funcs[s[1]])
            return

        if kind == "stop":
            if not self.loop_stack:
                raise CompileError("'stop' outside of a loop")
            self.emit("JMP", [self.loop_stack[-1]["end"]])
            return

        if kind == "continue":
            if not self.loop_stack:
                raise CompileError("'continue' outside of a loop")
            self.emit("JMP", [self.loop_stack[-1]["step"]])
            return

        if kind == "return":
            if self.cur is None:
                raise CompileError("'return' outside of a function")
            r = self.expr(s[1])
            self.emit("STORE" if r in H_REGS else "STOREF", ["RETVAL", r])
            self.free(r)
            self.epilogue()
            return

        raise CompileError(f"Unknown statement kind: {kind}")

    # ---------------- вход ----------------
    def compile(self, stmts):
        self.register_funcs(stmts)
        self.block(stmts)
        self.emit("HALT")
        return self.asm


def compile_source(code, verbose=False):
    from .parser import Parser
    stmts = Parser(code).parse()
    asm = Compiler().compile(stmts)
    if verbose:
        print("=== ASM ===")
        for i, (op, args) in enumerate(asm):
            print(f"{i:04}: {op} {args}")
    return asm


def compile_file(filename, verbose=False):
    with open(filename, "r", encoding="utf-8") as f:
        return compile_source(f.read(), verbose=verbose)
