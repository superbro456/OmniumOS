from .lexer import tokenize
from .parser import Parser
from .ir import Interpreter
from .config import MODE

def run(code):
    tokens = tokenize(code)
    parser = Parser(tokens)
    stmts = parser.parse()

    if MODE == "INTERPRETER":
        interp = Interpreter()
        interp.run(stmts)
        return interp.vars
    else:
        # Здесь будет генерация IR -> assembler -> Core
        print("Compile mode: not implemented yet")
