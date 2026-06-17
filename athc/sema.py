from pathlib import Path

from athc.suggest import closest
from athc.ast import (
    AppendStmt,
    AthLoop,
    BranchStmt,
    CloneStmt,
    EveryStmt,
    LoopStmt,
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
    ListdirStmt,
    MkdirStmt,
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
    SpawnStmt,
    SendStmt,
    RecvStmt,
    YieldStmt,
    JoinStmt,
    ChannelStmt,
    UniverseStmt,
    ListenStmt,
    AcceptStmt,
    ConnectStmt,
)


PREDEFINED_MAIN = frozenset({"THIS", "NULL", "ARGS"})
PREDEFINED_FUNC = frozenset({"THIS", "NULL", "ARGS"})
READ_ONLY = frozenset({"NULL"})


class SemaError(Exception):
    def __init__(
        self,
        msg: str,
        line: int,
        col: int,
        path: Path | None = None,
        help: str | None = None,
    ):
        super().__init__(f"line {line}, col {col}: {msg}")
        self.msg = msg
        self.line = line
        self.col = col
        self.path = path
        self.help = help


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
            # Declaration only; local builtin set pre-collected by _analyze_program
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
            # EXECUTE(F): F must be a declared function (or NULL no-op)
            if s.execute is not None and s.execute != "NULL":
                _check_function(s.execute, s, fnames, local_builtins)
            _walk(s.body, defined, fnames, local_builtins)
        elif isinstance(s, LoopStmt):
            _check_read(s.count_var, defined, s)
            _walk(s.body, defined, fnames, local_builtins)
        elif isinstance(s, EveryStmt):
            _check_read(s.interval_var, defined, s)
            _walk(s.body, defined, fnames, local_builtins)
        elif isinstance(s, DieStmt):
            _check_read(s.var, defined, s)
            if s.arg is not None:
                _check_read(s.arg, defined, s)
        elif isinstance(s, PrintStmt):
            for part in s.parts:
                if part.kind == "var":
                    _check_read(part.value, defined, part)
        elif isinstance(s, InputStmt):
            _check_write(s.var, s)
            defined.add(s.var)
        elif isinstance(s, ImportFuncStmt):
            pass
        elif isinstance(s, FuncCallComposeArg):
            _check_function(s.name, s, fnames, local_builtins)
            _check_operand(s.left, defined, s)
            _check_operand(s.right, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, FuncCallDecomposeRet):
            _check_function(s.name, s, fnames, local_builtins)
            _check_operand(s.arg, defined, s)
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
            _check_operand(s.index, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, SliceStmt):
            _check_read(s.source, defined, s)
            _check_operand(s.start, defined, s)
            _check_operand(s.end, defined, s)
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
            if s.path_var is not None:
                _check_read(s.path_var, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, (WriteStmt, AppendStmt)):
            _check_read(s.source, defined, s)
            if s.path_var is not None:
                _check_read(s.path_var, defined, s)
            if s.verdict is not None:
                _check_write(s.verdict, s)
                defined.add(s.verdict)
        elif isinstance(s, MkdirStmt):
            if s.path_var is not None:
                _check_read(s.path_var, defined, s)
        elif isinstance(s, ListdirStmt):
            if s.path_var is not None:
                _check_read(s.path_var, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, CloseStmt):
            _check_read(s.target, defined, s)
        elif isinstance(s, TextStmt):
            for part in s.parts:
                if part.kind == "ident":
                    _check_read(part.value, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, SpawnStmt):
            _check_function(s.name, s, fnames, local_builtins)
            # Only importf user functions are spawnable; builtins have no activation/THIS
            if s.name.lower() not in fnames:
                raise SemaError(
                    f"cannot spawn '{s.name}': only importf functions are spawnable, "
                    f"not builtins",
                    s.line,
                    s.col,
                )
            _check_operand(s.arg, defined, s)
            if s.into is not None:
                _check_read(s.into, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, SendStmt):
            _check_operand(s.message, defined, s)
            _check_read(s.dest, defined, s)
        elif isinstance(s, RecvStmt):
            if s.source is not None:
                _check_read(s.source, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, YieldStmt):
            pass
        elif isinstance(s, JoinStmt):
            _check_read(s.handle, defined, s)
        elif isinstance(s, (ChannelStmt, UniverseStmt)):
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, ListenStmt):
            # spec/host are string literals; the port may be a bound name
            _check_operand(s.port, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, AcceptStmt):
            _check_read(s.listener, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        elif isinstance(s, ConnectStmt):
            _check_operand(s.port, defined, s)
            _check_write(s.target, s)
            defined.add(s.target)
        else:
            raise SemaError(
                f"unknown statement {type(s).__name__}",
                getattr(s, "line", 0),
                getattr(s, "col", 0),
            )


def _check_operand(op, defined: set, stmt) -> None:
    # A read-operand is either a bound name (str, must be in scope) or an
    # inline literal (Operand), which is self-contained and always valid.
    if isinstance(op, str):
        _check_read(op, defined, stmt)


def _check_read(name: str, defined: set, stmt) -> None:
    if name not in defined:
        # identifiers are case-sensitive
        sug = closest(name, defined)
        help = f"did you mean '{sug}'?" if sug else None
        raise SemaError(
            f"variable '{name}' is not in scope", stmt.line, stmt.col, help=help
        )


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
        # Function names case-insensitive; suggest nearest registered one, echoing caller's casing
        sug = closest(name, fnames | local_builtins, fold=True)
        if sug and name.isupper():
            sug = sug.upper()
        help = f"did you mean '{sug}'?" if sug else None
        raise SemaError(
            f"function '{name}' is not declared by any importf or import builtin",
            stmt.line,
            stmt.col,
            help=help,
        )
