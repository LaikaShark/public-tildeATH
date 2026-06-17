"""Statement evaluator for the ~ATH REPL.

Mirrors codegen's ``_emit_stmt`` dispatch, but drives the real C runtime
through the ctypes FFI (athc/runtime_ffi.py) instead of emitting LLVM IR.
The environment maps variable names to live ``ath_obj *`` handles that
persist across REPL lines. Behavior is identical to a compiled binary
because it *is* the runtime.
"""

import sys
from pathlib import Path

from athc.ast import (
    AthLoop, BranchStmt, CloneStmt, CloseStmt, ComposeStmt, DecomposeStmt,
    DieStmt, EveryStmt, FuncCallComposeArg, FuncCallDecomposeRet,
    ImportBuiltinStmt, ImportFuncStmt, ImportNumberStmt, ImportStmt,
    InputStmt, ListdirStmt, LoopStmt, MkdirStmt, PrintStmt, ReadStmt,
    SliceStmt, SubscriptStmt,
    TextStmt, TimerStmt, WatchStmt, WriteStmt, AppendStmt, SleepStmt,
)
from athc.loader import _resolve_search_path
from athc.parser import parse


class ReplError(Exception):
    """A user-facing REPL evaluation error (e.g. an unsupported statement)."""


class _ThisDied(Exception):
    """THIS.DIE(): unwinds the current activation (function body / top line)."""

    def __init__(self, ret):
        self.ret = ret


# `every` is infinite; cap it so the REPL never hangs
_EVERY_CAP = 1000


class Evaluator:
    def __init__(self, rt, read_line=None):
        self.rt = rt
        # lower(name) -> ('builtin', c_symbol) | ('user', Program)
        self.functions = {}
        self.read_line = read_line or (lambda: sys.stdin.readline())
        top = {"THIS": rt.lib.ath_alloc_alive(), "ARGS": rt.lib.ath_NULL}
        self.frames = [top]

    @property
    def env(self):
        return self.frames[-1]

    def _read(self, name):
        if name == "NULL":
            return self.rt.NULL
        return self.env.get(name, self.rt.NULL)

    def _write(self, name, obj):
        if name == "NULL":
            raise ReplError("cannot bind the predefined name 'NULL'")
        self.env[name] = obj

    def _lit(self, s: str):
        b = s.encode("utf-8")
        if not b:
            return self.rt.NULL
        return self.rt.string_from_bytes(b)

    def _eval_operand(self, op):
        # A read-operand: a bound name resolves through the env; an inline
        # literal builds a fresh object with the same runtime constructors as
        # _import_number / _lit, so REPL behavior matches a compiled binary.
        if isinstance(op, str):
            return self._read(op)
        if op.kind == "string":
            return self._lit(op.value)
        if op.kind == "float":
            return self.rt.lib.ath_alloc_float(float(op.value))
        if op.kind == "bignum":
            return self.rt.lib.ath_alloc_bignum_from_decimal(
                str(op.value).encode("ascii")
            )
        return self.rt.lib.ath_alloc_number(int(op.value))

    def run(self, stmts):
        """Run a list of statements in the current (top) frame. A top-level
        THIS.DIE() ends this batch but not the session."""
        try:
            for s in stmts:
                self._eval(s)
        except _ThisDied:
            # Top-level activation ended; reset THIS so session continues
            self.env["THIS"] = self.rt.lib.ath_alloc_alive()

    def _run_body(self, stmts):
        for s in stmts:
            self._eval(s)

    # Concurrency and networking statements need the ucontext scheduler, which doesn't compose
    # with the REPL's synchronous Python-driven loop; they are compiled-only in v1. Networking
    # rides on send/recv (already here), so listen/accept/connect join the same set.
    _ACTOR_STMTS = frozenset({
        "SpawnStmt", "SendStmt", "RecvStmt", "YieldStmt", "JoinStmt",
        "ChannelStmt", "UniverseStmt",
        "ListenStmt", "AcceptStmt", "ConnectStmt",
    })

    def _eval(self, s):
        m = self._DISPATCH.get(type(s))
        if m is None:
            name = type(s).__name__
            if name in self._ACTOR_STMTS:
                raise ReplError(
                    "actor and networking statements (spawn/send/recv/yield/join/channel/"
                    "universe/listen/accept/connect) are only supported in compiled programs, "
                    "not the REPL"
                )
            raise ReplError(f"{name} is not supported in the REPL")
        m(self, s)

    def _import(self, s: ImportStmt):
        # idempotent
        if s.var in self.env:
            return
        obj = self.rt.lib.ath_alloc_from_library(s.name.encode("utf-8"))
        self._write(s.var, obj)

    def _import_number(self, s: ImportNumberStmt):
        if s.var in self.env:
            return
        if s.is_big:
            obj = self.rt.lib.ath_alloc_bignum_from_decimal(
                str(s.value).encode("ascii")
            )
        elif s.is_float:
            obj = self.rt.lib.ath_alloc_float(float(s.value))
        else:
            obj = self.rt.lib.ath_alloc_number(int(s.value))
        self._write(s.var, obj)

    def _import_builtin(self, s: ImportBuiltinStmt):
        self.functions[s.name.lower()] = ("builtin", s.symbol)

    def _import_func(self, s: ImportFuncStmt):
        prog = self._load_func_program(s)
        self.functions[s.name.lower()] = ("user", prog)

    def _load_func_program(self, s: ImportFuncStmt):
        if s.search_path:
            path = _resolve_search_path(s.path)
            if path is None:
                raise ReplError(f"importf <{s.path}>: not found in ATH_PATH or stdlib")
        else:
            path = Path(s.path)
            if not path.exists():
                raise ReplError(f"importf \"{s.path}\": file not found")
        return parse(path.read_text())

    def _decompose(self, s: DecomposeStmt):
        left, right = self.rt.decompose(self._read(s.source))
        self._write(s.left, left)
        self._write(s.right, right)

    def _compose(self, s: ComposeStmt):
        obj = self.rt.lib.ath_compose(self._read(s.left), self._read(s.right))
        self._write(s.target, obj)

    def _ath_loop(self, s: AthLoop):
        while True:
            alive = self.rt.alive(self._read(s.var))
            cond = (not alive) if s.inverted else alive
            if not cond:
                break
            # in-body THIS.DIE() propagates past EXECUTE
            self._run_body(s.body)
        if s.execute and s.execute != "NULL":
            self._call_user(s.execute, self._read(s.var))

    def _loop(self, s: LoopStmt):
        n = self.rt.lib.ath_count_of(self._read(s.count_var))
        for _ in range(n):
            self._run_body(s.body)

    def _every(self, s: EveryStmt):
        # Infinite in a compiled program; bounded here so REPL never hangs
        for _ in range(_EVERY_CAP):
            self._run_body(s.body)
            self.rt.lib.ath_sleep_ms(self._read(s.interval_var))
        print(
            f"  [repl: 'every' stopped after {_EVERY_CAP} iterations]",
            file=sys.stderr,
        )

    def _die(self, s: DieStmt):
        if s.var == "THIS":
            ret = self._read(s.arg) if s.arg else self.rt.NULL
            raise _ThisDied(ret)
        self.rt.lib.ath_die(self._read(s.var))

    def _print(self, s: PrintStmt):
        for part in s.parts:
            if part.kind == "lit":
                self.rt.print_bytes(part.value.encode("utf-8"))
            else:
                self.rt.lib.ath_print_obj_raw(self._read(part.value))
        self.rt.print_bytes(b"\n")
        sys.stdout.flush()

    def _input(self, s: InputStmt):
        line = self.read_line()
        if line.endswith("\n"):
            line = line[:-1]
        if line.endswith("\r"):
            line = line[:-1]
        self._write(s.var, self._lit(line))

    def _funcall_compose(self, s: FuncCallComposeArg):
        l, r = self._eval_operand(s.left), self._eval_operand(s.right)
        kind, ref = self._lookup_fn(s.name)
        if kind == "builtin":
            result = self.rt.builtin(ref)(l, r)
        else:
            result = self._run_user(ref, self.rt.lib.ath_compose(l, r))
        self._write(s.target, result)

    def _funcall_decompose(self, s: FuncCallDecomposeRet):
        arg = self._eval_operand(s.arg)
        kind, ref = self._lookup_fn(s.name)
        if kind == "builtin":
            result = self.rt.builtin(ref)(arg, self.rt.NULL)
        else:
            result = self._run_user(ref, arg)
        left, right = self.rt.decompose(result)
        self._write(s.left, left)
        self._write(s.right, right)

    def _lookup_fn(self, name):
        entry = self.functions.get(name.lower())
        if entry is None:
            raise ReplError(
                f"function '{name}' is not declared by any importf or "
                f"import builtin"
            )
        return entry

    def _call_user(self, name, arg):
        kind, ref = self._lookup_fn(name)
        if kind == "builtin":
            l, r = self.rt.decompose(arg)
            return self.rt.builtin(ref)(l, r)
        return self._run_user(ref, arg)

    def _run_user(self, program, args):
        """Run a user function body in a fresh frame; return its value."""
        frame = {"THIS": self.rt.lib.ath_alloc_alive(), "ARGS": args}
        self.frames.append(frame)
        try:
            for st in program.statements:
                self._eval(st)
            # fell off the end
            return self.rt.NULL
        except _ThisDied as d:
            return d.ret
        finally:
            self.frames.pop()

    def _subscript(self, s: SubscriptStmt):
        obj = self.rt.lib.ath_index(self._read(s.source), self._eval_operand(s.index))
        self._write(s.target, obj)

    def _slice(self, s: SliceStmt):
        i, j = self._eval_operand(s.start), self._eval_operand(s.end)
        pair = self.rt.lib.ath_compose(i, j)
        obj = self.rt.lib.ath_slice(self._read(s.source), pair)
        self._write(s.target, obj)

    def _branch(self, s: BranchStmt):
        alive = self.rt.alive(self._read(s.var))
        take_then = (not alive) if s.inverted else alive
        if take_then:
            self._run_body(s.then_body)
        elif s.else_body is not None:
            self._run_body(s.else_body)
        # Consume subject; re-read in case a body rebound it
        self.rt.lib.ath_die(self._read(s.var))

    def _clone(self, s: CloneStmt):
        self._write(s.target, self.rt.lib.ath_clone(self._read(s.source)))

    def _text(self, s: TextStmt):
        acc = None
        for part in s.parts:
            if part.kind == "str":
                val = self._lit(part.value)
            else:
                val = self.rt.lib.ath_coerce_string(self._read(part.value))
            acc = val if acc is None else self.rt.lib.ath_concat(acc, val)
        self._write(s.target, acc if acc is not None else self.rt.NULL)

    def _watch(self, s: WatchStmt):
        if s.var in self.env:
            return
        if s.path is not None:
            obj = self.rt.lib.ath_alloc_watching_file(s.path.encode("utf-8"))
        elif s.signal_name is not None:
            obj = self.rt.lib.ath_alloc_watching_signal_by_name(
                s.signal_name.encode("utf-8")
            )
        elif s.pid_var is not None:
            obj = self.rt.lib.ath_alloc_watching_pid(self._read(s.pid_var))
        else:
            obj = self.rt.lib.ath_alloc_watching_mtime(
                s.mtime_path.encode("utf-8")
            )
        self._write(s.var, obj)

    def _sleep(self, s: SleepStmt):
        self.rt.lib.ath_sleep_ms(self._read(s.duration))

    def _timer(self, s: TimerStmt):
        self._write(s.target, self.rt.lib.ath_alloc_timer_ms(self._read(s.duration)))

    def _read_file(self, s: ReadStmt):
        if s.path is not None:
            self._write(s.target, self.rt.lib.ath_alloc_read_file(s.path.encode("utf-8")))
        else:
            self._write(s.target, self.rt.lib.ath_alloc_read_file_obj(self._read(s.path_var)))

    def _write_file(self, s):
        src = self._read(s.source)
        if s.path is not None:
            fn = self.rt.lib.ath_append_file if isinstance(s, AppendStmt) else self.rt.lib.ath_write_file
            verdict = fn(src, s.path.encode("utf-8"))
        else:
            fn = self.rt.lib.ath_append_file_obj if isinstance(s, AppendStmt) else self.rt.lib.ath_write_file_obj
            verdict = fn(src, self._read(s.path_var))
        if s.verdict is not None:
            self._write(s.verdict, verdict)

    def _close(self, s: CloseStmt):
        self.rt.lib.ath_close(self._read(s.target))

    def _mkdir(self, s):
        if s.path is not None:
            self.rt.lib.ath_mkdir(s.path.encode("utf-8"))
        else:
            self.rt.lib.ath_mkdir_obj(self._read(s.path_var))

    def _listdir(self, s):
        if s.path is not None:
            self._write(s.target, self.rt.lib.ath_listdir(s.path.encode("utf-8")))
        else:
            self._write(s.target, self.rt.lib.ath_listdir_obj(self._read(s.path_var)))


Evaluator._DISPATCH = {
    ImportStmt: Evaluator._import,
    ImportNumberStmt: Evaluator._import_number,
    ImportBuiltinStmt: Evaluator._import_builtin,
    ImportFuncStmt: Evaluator._import_func,
    DecomposeStmt: Evaluator._decompose,
    ComposeStmt: Evaluator._compose,
    AthLoop: Evaluator._ath_loop,
    LoopStmt: Evaluator._loop,
    EveryStmt: Evaluator._every,
    DieStmt: Evaluator._die,
    PrintStmt: Evaluator._print,
    InputStmt: Evaluator._input,
    FuncCallComposeArg: Evaluator._funcall_compose,
    FuncCallDecomposeRet: Evaluator._funcall_decompose,
    SubscriptStmt: Evaluator._subscript,
    SliceStmt: Evaluator._slice,
    BranchStmt: Evaluator._branch,
    CloneStmt: Evaluator._clone,
    TextStmt: Evaluator._text,
    WatchStmt: Evaluator._watch,
    SleepStmt: Evaluator._sleep,
    TimerStmt: Evaluator._timer,
    ReadStmt: Evaluator._read_file,
    WriteStmt: Evaluator._write_file,
    AppendStmt: Evaluator._write_file,
    CloseStmt: Evaluator._close,
    MkdirStmt: Evaluator._mkdir,
    ListdirStmt: Evaluator._listdir,
}
