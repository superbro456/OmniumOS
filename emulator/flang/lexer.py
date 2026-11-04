import re

TYPES = {
    "int", "uint", "light_int", "light_uint", "big_int", "big_uint",
    "float", "ufloat", "big_float", "ubig_float", "light_float", "ulight_float",
    "str", "symbol", "cord2", "cord3", "state2", "state3", "color",
    "reg", "mem_block", "bitfield", "ptr", "handle", "mutex",
    "array", "list", "map", "event", "timer", "socket_handle"
}

TOKEN_SPEC = [
    ('BLOCK_COMMENT', r'#[\s\S]*?#|#[\s\S]*$'),
    ('STRING',    r'"[^"]*"'),
    
    # ЧИСЛА (ЦЕЛЫЕ И С ПЛАВАЮЩЕЙ ТОЧКОЙ)
    ('FLOAT',     r'\d+\.\d+'),           # 2.65, 3.14 и т.д.
    ('NUMBER',    r'\d+'),                # 123, 45 и т.д.
    
    # ИДЕНТИФИКАТОРЫ / ТИПЫ
    ('ID',        r'[A-Za-z_][A-Za-z0-9_]*'),
    
    # ОПЕРАТОРЫ
    ('INC',       r'\+\+'),
    ('DEC',       r'--'),
    ('ASSIGN_OP', r'\+=|-=|\*=|/=|%='),
    ('OP',        r'\*\*|//|==|!=|<=|>=|\+|-|\*|/|%|<|>|\.'),
    ('ASSIGN',    r'='),
    
    # СКОБКИ И РАЗДЕЛИТЕЛИ
    ('LPAREN',    r'\('),
    ('RPAREN',    r'\)'),
    ('LBRACE',    r'\{'),
    ('RBRACE',    r'\}'),
    ('SEMI',      r';'),
    ('COMMA',     r','),
    
    # ПРОБЕЛЫ
    ('SKIP',      r'[ \t]+'),
    ('NEWLINE',   r'\r?\n'),
    ('MISMATCH',  r'.'),
]

# Важно: FLOAT должен быть перед NUMBER в регулярном выражении
token_re = re.compile('|'.join(
    f'(?P<{name}>{pattern})' for name, pattern in TOKEN_SPEC
))

def tokenize(code):
    tokens = []
    for mo in token_re.finditer(code):
        kind = mo.lastgroup
        value = mo.group()
        if kind in ('SKIP', 'NEWLINE', 'BLOCK_COMMENT'):
            continue
        if kind == 'MISMATCH':
            line = code.count('\n', 0, mo.start()) + 1
            raise SyntaxError(f"Unexpected character {value!r} at line {line}")
        
        if kind == 'NUMBER':
            value = int(value)
        
        elif kind == 'FLOAT':
            value = float(value)  # Преобразуем в float
        
        elif kind == 'STRING':
            value = value[1:-1]
        
        elif kind == 'ID' and value in TYPES:
            kind = 'TYPE'
        
        tokens.append((kind, value))
    
    return tokens