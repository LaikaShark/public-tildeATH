from dataclasses import dataclass, field
from pathlib import Path
from typing import Union


@dataclass
class ImportStmt:
    name: str
    var: str
    line: int
    col: int


@dataclass
class DecomposeStmt:
    source: str
    left: str
    right: str
    line: int
    col: int


@dataclass
class ComposeStmt:
    left: str
    right: str
    target: str
    line: int
    col: int


@dataclass
class AthLoop:
    var: str
    body: list = field(default_factory=list)
    line: int = 0
    col: int = 0
    inverted: bool = False


@dataclass
class LoopStmt:
    """loop N { body }  -- run body N.value times (count loop, §4.4.26)."""
    count_var: str
    body: list = field(default_factory=list)
    line: int = 0
    col: int = 0


@dataclass
class EveryStmt:
    """every N { body }  -- run body, sleep N.value ms, forever (§4.4.27)."""
    interval_var: str
    body: list = field(default_factory=list)
    line: int = 0
    col: int = 0


@dataclass
class DieStmt:
    var: str
    line: int
    col: int
    arg: str | None = None


@dataclass
class PrintStmt:
    text: str
    line: int
    col: int


@dataclass
class InputStmt:
    var: str
    line: int
    col: int


@dataclass
class Print2Stmt:
    var: str
    line: int
    col: int


@dataclass
class ImportFuncStmt:
    path: str
    name: str
    line: int
    col: int
    search_path: bool = False   # True for the angle-bracket form: importf <stem> as NAME;


@dataclass
class ImportBuiltinStmt:
    """import builtin SYM as NAME; — registers a C-ABI function locally
    in the containing file's builtin table (§4.4.13)."""
    symbol: str
    name: str
    line: int
    col: int


@dataclass
class ImportNumberStmt:
    """import number N as VAR; — eternal-alive object with int64 payload (§4.4.14)."""
    value: int
    var: str
    line: int
    col: int


@dataclass
class WatchStmt:
    var: str
    line: int
    col: int
    path: str | None = None         # file form: watch "PATH" as VAR;
    signal_name: str | None = None  # signal form: watch signal NAME as VAR;
    pid_var: str | None = None      # pid form:   watch pid N as VAR;
    mtime_path: str | None = None   # mtime form: watch mtime "PATH" as VAR;


@dataclass
class FuncCallComposeArg:
    """FN [L, R] V;  -- compose(L, R) -> result -> V"""
    name: str
    left: str
    right: str
    target: str
    line: int
    col: int


@dataclass
class FuncCallDecomposeRet:
    """FN A [B, C];  -- A -> result -> decompose into B, C"""
    name: str
    arg: str
    left: str
    right: str
    line: int
    col: int


@dataclass
class SubscriptStmt:
    """S[N] X; — read the Nth right-spine head of S into X (§4.4.15)."""
    source: str
    index: str
    target: str
    line: int
    col: int


@dataclass
class SliceStmt:
    """S[I..J] X; — fresh cons-list of elements I..J-1 from S (§4.4.16)."""
    source: str
    start: str
    end: str
    target: str
    line: int
    col: int


@dataclass
class BranchStmt:
    """BRANCH(V) { then } [ELSE] { else }; — one-shot dispatch (§4.4.17).

    Consumes V at the end of dispatch."""
    var: str
    inverted: bool
    then_body: list = field(default_factory=list)
    else_body: list | None = None  # None = no else clause
    line: int = 0
    col: int = 0


@dataclass
class CloneStmt:
    """CLONE V as W; — shallow snapshot, independent identity (§4.4.18)."""
    source: str
    target: str
    line: int
    col: int


@dataclass
class SleepStmt:
    """sleep N; — block for N.value ms; no-op on dead/no-payload N (§4.4.19)."""
    duration: str
    line: int
    col: int


@dataclass
class TimerStmt:
    """TIMER N as T; — fresh alive object with deadline = now + N ms (§4.4.20)."""
    duration: str
    target: str
    line: int
    col: int


@dataclass
class ReadStmt:
    """read "PATH" as VAR; — slurp file into a string-cons-list owning the
    file (§4.4.21). Explicit .DIE() or BRANCH consumption deletes it."""
    path: str
    target: str
    line: int
    col: int


@dataclass
class WriteStmt:
    """write SRC to "PATH" [as VERDICT]; — truncate-and-write (§4.4.22)."""
    source: str
    path: str
    verdict: str | None      # None when the 'as' clause is omitted
    line: int
    col: int


@dataclass
class AppendStmt:
    """append SRC to "PATH" [as VERDICT]; — like write but appends (§4.4.23)."""
    source: str
    path: str
    verdict: str | None
    line: int
    col: int


@dataclass
class CloseStmt:
    """close VAR; — disown the file (if owned) and kill VAR (§4.4.24)."""
    target: str
    line: int
    col: int


@dataclass
class TextPart:
    """A single part inside a `text` statement (§4.4.25). `kind` is
    'str' (a STRING literal, with escapes already decoded) or 'ident'
    (a name to be read and coerced to string via ath_coerce_string)."""
    kind: str
    value: str


@dataclass
class TextStmt:
    """text PART+ as VAR;  — bind VAR to the cons-list formed by
    concatenating each part left to right. STRING parts are built as
    literal cons-lists; IDENT parts are coerced via ath_coerce_string
    (payload-bearing values are routed through ath_to_string) (§4.4.25)."""
    parts: list  # list[TextPart]
    target: str
    line: int
    col: int


Stmt = Union[
    ImportStmt,
    DecomposeStmt,
    ComposeStmt,
    AthLoop,
    LoopStmt,
    EveryStmt,
    DieStmt,
    PrintStmt,
    InputStmt,
    Print2Stmt,
    ImportFuncStmt,
    ImportBuiltinStmt,
    ImportNumberStmt,
    FuncCallComposeArg,
    FuncCallDecomposeRet,
    WatchStmt,
    SubscriptStmt,
    SliceStmt,
    BranchStmt,
    CloneStmt,
    SleepStmt,
    TimerStmt,
    ReadStmt,
    WriteStmt,
    AppendStmt,
    CloseStmt,
    TextStmt,
]


@dataclass
class Program:
    statements: list = field(default_factory=list)
    source_path: Path | None = None
