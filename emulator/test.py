from flang import run

code = """
x = 2 + 3;
y = x * 4;
"""

vars = run(code)
print("Vars:", vars)
