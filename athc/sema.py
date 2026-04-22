from athc.ast import (
    AthLoop,
    ComposeStmt,
    DecomposeStmt,
    DieStmt,
    ImportStmt,
    PrintStmt,
    Program,
)


PREDEFINED = frozenset({"THIS", "NULL"})
READ_ONLY = frozenset({"NULL"})


class SemaError(Exception):
    def __init__(self, msg: str, line: int, col: int):
        super().__init__(f"line {line}, col {col}: {msg}")
        self.msg = msg
        self.line = line
        self.col = col


def analyze(program: Program) -> None:
    defined = set(PREDEFINED)
    _walk(program.statements, defined)


def _walk(stmts: list, defined: set) -> None:
    for s in stmts:
        if isinstance(s, ImportStmt):
            _check_write(s.var, s)
            defined.add(s.var)
        elif isinstance(s, DecomposeStmt):
            _check_read(s.source, defined, s)
            _check_write(s.left, s)
            _check_write(s.right, s)
            defined.add(s.left)
            defined.add(s.right)
        elif isinstance(s, ComposeStmt):
            _check_read(s.left, defined, s)
            _check_read(s.right, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, AthLoop):
            _check_read(s.var, defined, s)
            _walk(s.body, defined)
        elif isinstance(s, DieStmt):
            _check_read(s.var, defined, s)
        elif isinstance(s, PrintStmt):
            pass
        else:
            raise SemaError(
                f"unknown statement {type(s).__name__}", getattr(s, "line", 0), getattr(s, "col", 0)
            )


def _check_read(name: str, defined: set, stmt) -> None:
    if name not in defined:
        raise SemaError(f"variable '{name}' is not in scope", stmt.line, stmt.col)


def _check_write(name: str, stmt) -> None:
    if name in READ_ONLY:
        raise SemaError(
            f"cannot bind '{name}': predefined name is read-only",
            stmt.line,
            stmt.col,
        )
