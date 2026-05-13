from pathlib import Path

from athc.ast import (
    AppendStmt,
    AthLoop,
    BranchStmt,
    CloneStmt,
    CloseStmt,
    ComposeStmt,
    DecomposeStmt,
    DieStmt,
    FuncCallComposeArg,
    FuncCallDecomposeRet,
    ImportBuiltinStmt,
    ImportFuncStmt,
    ImportNumberStmt,
    ImportStmt,
    InputStmt,
    Print2Stmt,
    PrintStmt,
    Program,
    ReadStmt,
    SleepStmt,
    SliceStmt,
    SubscriptStmt,
    TextStmt,
    TimerStmt,
    WatchStmt,
    WriteStmt,
)


PREDEFINED_MAIN = frozenset({"THIS", "NULL"})
PREDEFINED_FUNC = frozenset({"THIS", "NULL", "ARGS"})
READ_ONLY = frozenset({"NULL"})


class SemaError(Exception):
    def __init__(
        self,
        msg: str,
        line: int,
        col: int,
        path: Path | None = None,
    ):
        super().__init__(f"line {line}, col {col}: {msg}")
        self.msg = msg
        self.line = line
        self.col = col
        self.path = path


def analyze(program: Program, function_table: dict | None = None) -> None:
    if function_table is None:
        function_table = {}
    fnames = {n.lower() for n in function_table}
    try:
        _analyze_program(program, set(PREDEFINED_MAIN), fnames)
    except SemaError as e:
        if e.path is None:
            e.path = program.source_path
        raise
    for fname, fprog in function_table.items():
        try:
            _analyze_program(fprog, set(PREDEFINED_FUNC), fnames)
        except SemaError as e:
            if e.path is None:
                e.path = fprog.source_path
            raise


def _collect_local_builtins(stmts: list) -> set[str]:
    """Top-level ImportBuiltinStmt names are callable like functions but
    scoped to the containing file (§4.4.13)."""
    return {
        s.name.lower() for s in stmts if isinstance(s, ImportBuiltinStmt)
    }


def _analyze_program(program: Program, defined: set, fnames: set) -> None:
    local_builtins = _collect_local_builtins(program.statements)
    _walk(program.statements, defined, fnames, local_builtins)


def _walk(stmts: list, defined: set, fnames: set, local_builtins: set) -> None:
    for s in stmts:
        if isinstance(s, ImportStmt):
            _check_write(s.var, s)
            defined.add(s.var)
        elif isinstance(s, ImportNumberStmt):
            _check_write(s.var, s)
            defined.add(s.var)
        elif isinstance(s, ImportBuiltinStmt):
            # Declaration only; no variable binding, no runtime effect.
            # Local builtin set was pre-collected by _analyze_program.
            pass
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
            _walk(s.body, defined, fnames, local_builtins)
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
            _check_function(s.name, s, fnames, local_builtins)
            _check_read(s.left, defined, s)
            _check_read(s.right, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, FuncCallDecomposeRet):
            _check_function(s.name, s, fnames, local_builtins)
            _check_read(s.arg, defined, s)
            _check_write(s.left, s)
            _check_write(s.right, s)
            defined.add(s.left)
            defined.add(s.right)
        elif isinstance(s, WatchStmt):
            if s.pid_var is not None:
                _check_read(s.pid_var, defined, s)
            _check_write(s.var, s)
            defined.add(s.var)
        elif isinstance(s, SubscriptStmt):
            _check_read(s.source, defined, s)
            _check_read(s.index, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, SliceStmt):
            _check_read(s.source, defined, s)
            _check_read(s.start, defined, s)
            _check_read(s.end, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, BranchStmt):
            _check_read(s.var, defined, s)
            _walk(s.then_body, defined, fnames, local_builtins)
            if s.else_body is not None:
                _walk(s.else_body, defined, fnames, local_builtins)
        elif isinstance(s, CloneStmt):
            _check_read(s.source, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, SleepStmt):
            _check_read(s.duration, defined, s)
        elif isinstance(s, TimerStmt):
            _check_read(s.duration, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, ReadStmt):
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, (WriteStmt, AppendStmt)):
            _check_read(s.source, defined, s)
            if s.verdict is not None:
                _check_write(s.verdict, s)
                defined.add(s.verdict)
        elif isinstance(s, CloseStmt):
            _check_read(s.target, defined, s)
        elif isinstance(s, TextStmt):
            for part in s.parts:
                if part.kind == "ident":
                    _check_read(part.value, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
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


def _check_function(name: str, stmt, fnames: set, local_builtins: set) -> None:
    folded = name.lower()
    if folded not in fnames and folded not in local_builtins:
        raise SemaError(
            f"function '{name}' is not declared by any importf or import builtin",
            stmt.line,
            stmt.col,
        )
