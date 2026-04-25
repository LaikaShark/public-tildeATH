from athc.ast import (
    AthLoop,
    ComposeStmt,
    DecomposeStmt,
    DieStmt,
    FuncCallComposeArg,
    FuncCallDecomposeRet,
    ImportFuncStmt,
    ImportStmt,
    InputStmt,
    Print2Stmt,
    PrintStmt,
    Program,
    WatchStmt,
)


PREDEFINED_MAIN = frozenset({"THIS", "NULL"})
PREDEFINED_FUNC = frozenset({"THIS", "NULL", "ARGS"})
READ_ONLY = frozenset({"NULL"})


class SemaError(Exception):
    def __init__(self, msg: str, line: int, col: int):
        super().__init__(f"line {line}, col {col}: {msg}")
        self.msg = msg
        self.line = line
        self.col = col


def analyze(program: Program, function_table: dict | None = None) -> None:
    if function_table is None:
        function_table = {}
    fnames = {n.lower() for n in function_table}
    _walk(program.statements, set(PREDEFINED_MAIN), fnames)
    for fname, fprog in function_table.items():
        _walk(fprog.statements, set(PREDEFINED_FUNC), fnames)


def _walk(stmts: list, defined: set, fnames: set) -> None:
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
            _walk(s.body, defined, fnames)
        elif isinstance(s, DieStmt):
            _check_read(s.var, defined, s)
            if s.arg is not None:
                _check_read(s.arg, defined, s)
        elif isinstance(s, PrintStmt):
            pass
        elif isinstance(s, InputStmt):
            _check_write(s.var, s)
            defined.add(s.var)
        elif isinstance(s, Print2Stmt):
            _check_read(s.var, defined, s)
        elif isinstance(s, ImportFuncStmt):
            pass
        elif isinstance(s, FuncCallComposeArg):
            _check_function(s.name, s, fnames)
            _check_read(s.left, defined, s)
            _check_read(s.right, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, FuncCallDecomposeRet):
            _check_function(s.name, s, fnames)
            _check_read(s.arg, defined, s)
            _check_write(s.left, s)
            _check_write(s.right, s)
            defined.add(s.left)
            defined.add(s.right)
        elif isinstance(s, WatchStmt):
            _check_write(s.var, s)
            defined.add(s.var)
        else:
            raise SemaError(
                f"unknown statement {type(s).__name__}",
                getattr(s, "line", 0),
                getattr(s, "col", 0),
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


def _check_function(name: str, stmt, fnames: set) -> None:
    if name.lower() not in fnames:
        raise SemaError(
            f"function '{name}' is not declared by any importf statement",
            stmt.line,
            stmt.col,
        )
