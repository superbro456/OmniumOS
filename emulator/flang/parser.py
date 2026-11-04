from .lexer import tokenize, TYPES

class Parser:
    def __init__(self, code):
        self.tokens = tokenize(code)
        self.pos = 0
        self._var_types = {}
    # --------------------------
    # utils
    # --------------------------
    def peek(self):
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return (None, None)

    def eat(self, kind, value=None):
        k, v = self.peek()
        if k != kind:
            raise SyntaxError(f"Expected {kind}, got {k}")
        if value is not None and v != value:
            raise SyntaxError(f"Expected {value}, got {v}")
        self.pos += 1
        return v

    # --------------------------
    # entry
    # --------------------------
    def parse(self):
        stmts = []
        while self.peek()[0] is not None:
            stmts.append(self.statement())
        return stmts

    # --------------------------
    # statements
    # --------------------------
    def statement(self, parent=None):
        kind, val = self.peek()
        if kind == "TYPE":
            return self.decl()
        if kind == "ID":
            if val == "print":
                return self.print_stmt()
            if val == "if":
                return self.if_stmt()
            if val == "while":
                return self.while_stmt()
            if val == "for":
                return self.for_stmt()
            if val == "func":
                return self.func_decl(parent=parent)
            if val == "return":
                return self.return_stmt()
            if val == "stop":
                self.eat("ID")
                self.eat("SEMI")
                return ("stop",)
            if val == "continue":
                self.eat("ID")
                self.eat("SEMI")
                return ("continue",)
            return self.assign_stmt()

        raise SyntaxError(f"Unexpected token {kind} {val}")

    # --------------------------
    # Хранилище типов переменных
    # --------------------------
    _var_types = {}  # имя переменной -> тип ("int" или "float")

    # --------------------------
    # declarations / assign
    # --------------------------
    def decl(self):
        type_name = self.eat("TYPE")  # int, float, etc
        var_name = self.eat("ID")

        if self.peek()[0] == "SEMI":
            self.eat("SEMI")
            expr = ("num", 0 if type_name.startswith("int") else 0.0)
        else:
            self.eat("ASSIGN")
            expr = self.expr()
            self.eat("SEMI")

            # проверка типа литерала
            expr_type = self.deduce_type(expr)
            if type_name.startswith("int") and expr_type == "float":
                raise TypeError(f"Cannot assign float value to int variable '{var_name}'")
            if type_name.startswith("float") and expr_type == "int":
                # можно разрешить неявное преобразование int -> float
                expr = ("float", float(expr[1])) if expr[0] == "num" else expr

        # сохраняем тип
        self._var_types[var_name] = type_name
        return ("assign", var_name, expr, type_name)


    def deduce_type(self, expr):
        """Рекурсивно определяет тип выражения: int или float"""
        kind = expr[0]
        if kind == "convert":
            return "int"
        if kind == "float":
            return "float"
        if kind == "num":
            return "int"
        if kind == "binop":
            l_type = self.deduce_type(expr[1])
            r_type = self.deduce_type(expr[3])
            return "float" if l_type == "float" or r_type == "float" else "int"
        if kind == "var":
            return self._var_types.get(expr[1], "int")
        return "int"

    def assign_stmt(self):
        name = self.eat("ID")
        kind, _ = self.peek()

        if kind == "ASSIGN":
            self.eat("ASSIGN")
            expr = self.expr()
            var_type = self.deduce_type(expr)

            # проверка: если переменная уже объявлена
            if name in self._var_types:
                declared_type = self._var_types[name]
                if declared_type.startswith("int") and var_type == "float":
                    raise TypeError(f"Cannot assign float value to int variable '{name}'")
                if declared_type.startswith("float") and var_type == "int":
                    expr = ("float", float(expr[1])) if expr[0] == "num" else expr
            else:
                # новая переменная: сохраняем тип
                self._var_types[name] = var_type

        elif kind == "ASSIGN_OP":
            op = self.eat("ASSIGN_OP")
            right = self.expr()
            op_map = {'+=': '+', '-=': '-', '*=': '*', '/=': '/', '%=': '%'}
            expr = ("binop", ("var", name), op_map[op], right)
            var_type = self.deduce_type(expr)

        elif kind == "INC":
            self.eat("INC")
            expr = ("binop", ("var", name), "+", ("num", 1))
            var_type = self.deduce_type(expr)

        elif kind == "DEC":
            self.eat("DEC")
            expr = ("binop", ("var", name), "-", ("num", 1))
            var_type = self.deduce_type(expr)

        else:
            raise SyntaxError("Invalid assignment")

        self.eat("SEMI")

        self._var_types[name] = var_type
        return ("assign", name, expr, var_type)




    # --------------------------
    # print
    # --------------------------
    def print_stmt(self):
        self.eat("ID", "print")
        if self.peek()[0] == "LPAREN":
            self.eat("LPAREN")
            expr = self.expr()
            self.eat("RPAREN")
        else:
            expr = self.expr()
        self.eat("SEMI")
        return ("print_expr", expr)

    # --------------------------
    # control flow
    # --------------------------
    def if_stmt(self):
        self.eat("ID", "if")
        self.eat("LPAREN")
        cond = self.expr()
        self.eat("RPAREN")
        body = self.block()

        elifs = []
        while self.peek()[0] == "ID" and self.peek()[1] == "elif":
            self.eat("ID", "elif")
            self.eat("LPAREN")
            ec = self.expr()
            self.eat("RPAREN")
            eb = self.block()
            elifs.append((ec, eb))

        else_body = []
        if self.peek()[0] == "ID" and self.peek()[1] == "else":
            self.eat("ID", "else")
            else_body = self.block()

        return ("if", cond, body, elifs, else_body)

    def while_stmt(self):
        self.eat("ID", "while")
        self.eat("LPAREN")
        cond = self.expr()
        self.eat("RPAREN")
        body = self.block()
        return ("while", cond, body)

    def for_stmt(self):
        self.eat("ID", "for")
        self.eat("LPAREN")

        init = self.assign_for()
        self.eat("SEMI")
        cond = self.expr()
        self.eat("SEMI")
        step = self.assign_for()
        self.eat("RPAREN")

        body = self.block()
        return ("for", init, cond, step, body)

    def assign_for(self):
        if self.peek()[0] == "TYPE":
            self.eat("TYPE") 
        name = self.eat("ID")
        kind, _ = self.peek()

        if kind == "ASSIGN":
            self.eat("ASSIGN")
            expr = self.expr()
        elif kind == "ASSIGN_OP":
            op = self.eat("ASSIGN_OP")
            right = self.expr()
            op_map = {'+=': '+', '-=': '-', '*=': '*', '/=': '/', '%=': '%'}
            expr = ("binop", ("var", name), op_map[op], right)
        elif kind == "INC":
            self.eat("INC")
            expr = ("binop", ("var", name), "+", ("num", 1))
        elif kind == "DEC":
            self.eat("DEC")
            expr = ("binop", ("var", name), "-", ("num", 1))
        else:
            raise SyntaxError("Invalid for statement")

        return ("assign", name, expr, None)


    # --------------------------
    # function
    # --------------------------
    def func_decl(self, parent=None):
        self.eat("ID", "func")
        name = self.eat("ID")

        # сначала аргументы
        self.eat("LPAREN")
        args = []
        while self.peek()[0] != "RPAREN":
            arg_type = self.eat("TYPE")
            arg_name = self.eat("ID")
            args.append((arg_type, arg_name))
            if self.peek()[0] == "COMMA":
                self.eat("COMMA")
        self.eat("RPAREN")

        # если после аргументов =, то это короткая функция
        if self.peek()[0] == "ASSIGN":
            self.eat("ASSIGN")
            if self.peek()[0] == "LBRACE":
                self.eat("LBRACE")
                expr = self.expr()
                self.eat("RBRACE")
            else:
                expr = self.expr()
            self.eat("SEMI")
            return ("func", name, args, [("return", expr)])

        # обычная функция с блоком
        full_name = name if parent is None else f"{parent}.{name}"
        body = self.block(parent=full_name)
        return ("func", full_name, args, body)
    # --------------------------
    # return
    # --------------------------
    def return_stmt(self):
        self.eat("ID", "return")
        expr = self.expr()
        self.eat("SEMI")
        return ("return", expr)

    # --------------------------
    # block
    # --------------------------
    def block(self, parent=None):
        self.eat("LBRACE")
        stmts = []
        while self.peek()[0] != "RBRACE":
            stmts.append(self.statement(parent=parent))
        self.eat("RBRACE")
        return stmts

    # --------------------------
    # expressions
    # --------------------------
    def expr(self):
        return self.parse_binop(0)

    def parse_binop(self, min_prec):
        left = self.unary()
        while True:
            kind, op = self.peek()
            if kind != "OP":
                break
            prec, assoc = self.op_info(op)
            if prec < min_prec:
                break
            self.eat("OP")
            next_min = prec if assoc == "right" else prec + 1
            right = self.parse_binop(next_min)
            left = ("binop", left, op, right)
        return left

    def unary(self):
        kind, op = self.peek()
        if kind == "OP" and op in ("+", "-"):
            self.eat("OP")
            node = self.unary()
            if op == "-":
                return ("binop", ("num", 0), "-", node)
            return node
        return self.atom()

    def atom(self):
        kind, val = self.peek()
        # ✅ convert — оператор языка, НЕ функция
        if kind == "ID" and val == "convert":
            self.eat("ID", "convert")
            self.eat("LPAREN")
            inner = self.expr()
            self.eat("RPAREN")
            return ("convert", inner)
        # ------------------ float с ключевым словем ------------------
        if kind == "TYPE" and val.startswith("float"):
            self.eat("TYPE")  # пропускаем float
            kind2, val2 = self.peek()
            
            if kind2 == "NUMBER":
                int_part = self.eat("NUMBER")
                
                # проверяем, есть ли точка
                if self.peek()[0] == "OP" and self.peek()[1] == ".":
                    self.eat("OP", ".")
                    kind3, frac_part = self.peek()
                    if kind3 != "NUMBER":
                        raise SyntaxError("Expected number after '.' in float literal")
                    frac_part = self.eat("NUMBER")
                    value = float(f"{int_part}.{frac_part}")
                    return ("float", value)
                
                # если точки нет — просто int
                return ("num", int_part)
            
            # float без числа
            return ("float", 0.0)
        
        # ------------------ числа (включая float) ------------------
        if kind == "FLOAT":  # Новый токен для чисел с точкой
            self.eat("FLOAT")
            return ("float", val)
        
        if kind == "NUMBER":
            value = self.eat("NUMBER")
            
            # Дополнительная проверка на float с точкой
            if self.peek()[0] == "OP" and self.peek()[1] == ".":
                self.eat("OP", ".")
                kind2, frac_part = self.peek()
                if kind2 != "NUMBER":
                    raise SyntaxError("Expected number after '.'")
                frac_part = self.eat("NUMBER")
                value = float(f"{value}.{frac_part}")
                return ("float", value)
            
            return ("num", value)
        
        # ... остальной код ...

        # ------------------ строки ------------------
        if kind == "STRING":
            self.eat("STRING")
            return ("str", val)

        # ------------------ идентификаторы и вызовы ------------------
        if kind == "ID":
            name = self.eat("ID")

            # поддержка доступа через точку (obj.prop)
            while self.peek()[0] == "OP" and self.peek()[1] == ".":
                self.eat("OP", ".")
                name += "." + self.eat("ID")

            # вызов функции
            if self.peek()[0] == "LPAREN":
                self.eat("LPAREN")
                args = []
                while self.peek()[0] != "RPAREN":
                    args.append(self.expr())
                    if self.peek()[0] == "COMMA":
                        self.eat("COMMA")
                self.eat("RPAREN")
                return ("call", name, args)

            # ✅ ВОТ ЭТОГО НЕ ХВАТАЛО
            return ("var", name)
        # ------------------ скобки ------------------
        if kind == "LPAREN":
            self.eat("LPAREN")
            expr = self.expr()
            self.eat("RPAREN")
            return expr

        # ------------------ ошибка ------------------
        raise SyntaxError(f"Unexpected atom {kind} {val}")


    # --------------------------
    # operators
    # --------------------------
    def op_info(self, op):
        table = {
            '**': (40, 'right'),
            '*':  (30, 'left'),
            '/':  (30, 'left'),
            '%':  (30, 'left'),
            '+':  (20, 'left'),
            '-':  (20, 'left'),
            '<':  (10, 'left'),
            '<=': (10, 'left'),
            '>':  (10, 'left'),
            '>=': (10, 'left'),
            '==': (5, 'left'),
            '!=': (5, 'left'),
        }
        return table.get(op, (0, 'left'))
