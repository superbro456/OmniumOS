# ir.py
from .parser import Parser

# Исключения для управления потоками
class StopIterationSignal(Exception):
    pass

class ContinueIterationSignal(Exception):
    pass

class Interpreter:
    def __init__(self):
        self.vars = {}

    def eval_expr(self, expr):
        typ = expr[0]

        if typ == "num":
            return expr[1]
        elif typ == "var":
            return self.vars.get(expr[1], 0)
        elif typ == "binop":
            left = self.eval_expr(expr[1])
            op = expr[2]
            right = self.eval_expr(expr[3])
            if op == "+": return left + right
            if op == "-": return left - right
            if op == "*": return left * right
            if op == "/": return left // right  # целочисленное деление
            if op == "%": return left % right
            if op == "**": return left ** right
            if op == "//": return left // right
            if op == "<": return int(left < right)
            if op == "<=": return int(left <= right)
            if op == ">": return int(left > right)
            if op == ">=": return int(left >= right)
            if op == "==": return int(left == right)
            if op == "!=": return int(left != right)
            raise ValueError(f"Unknown operator: {op}")
        else:
            raise ValueError(f"Unknown expr type: {typ}")

    def run_stmt(self, stmt):
        typ = stmt[0]

        if typ == "assign":
            self.vars[stmt[1]] = self.eval_expr(stmt[2])
        elif typ == "print_expr":
            print(self.eval_expr(stmt[1]))
        elif typ == "print_str":
            print(stmt[1])
        elif typ == "if":
            cond = self.eval_expr(stmt[1])
            if cond:
                self.run(stmt[2])
            else:
                for ec, eb in stmt[3]:
                    if self.eval_expr(ec):
                        self.run(eb)
                        return
                if stmt[4]:
                    self.run(stmt[4])
        elif typ == "while":
            while self.eval_expr(stmt[1]):
                try:
                    self.run(stmt[2])
                except ContinueIterationSignal:
                    continue
                except StopIterationSignal:
                    break
        elif typ == "for":
            self.run_stmt(stmt[1])  # init
            while self.eval_expr(stmt[2]):
                try:
                    self.run(stmt[4])  # body
                except ContinueIterationSignal:
                    pass
                except StopIterationSignal:
                    break
                # step
                if isinstance(stmt[3], list):
                    for s in stmt[3]:
                        self.run_stmt(s)
                else:
                    self.run_stmt(stmt[3])
        elif typ == "stop":
            raise StopIterationSignal()
        elif typ == "continue":
            raise ContinueIterationSignal()
        else:
            raise ValueError(f"Unknown statement type: {typ}")

    def run(self, stmts):
        for stmt in stmts:
            self.run_stmt(stmt)


# Удобная функция для исполнения кода из строки
def run_code(code):
    parser = Parser(code)
    stmts = parser.parse()
    interp = Interpreter()
    interp.run(stmts)
    return interp.vars
