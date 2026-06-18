from dataclasses import dataclass, field
from pathlib import Path
from typing import Union


@dataclass
class Operand:
    """An inline literal in a read-operand position (§4.4 bracket forms).

    A bare ``str`` operand still denotes an identifier (a bound name); an
    ``Operand`` denotes a literal whose value is materialized at the call
    site. ``value`` mirrors ImportNumberStmt: an int (kind="int"), a float
    (kind="float"), the decimal string of an over-int64 literal
    (kind="bignum"), or the decoded text (kind="string")."""
    kind: str           # "int" | "float" | "bignum" | "string"
    value: "int | float | str"
    line: int = 0
    col: int = 0


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
    # EXECUTE(IDENT) postfix: function called once loop exits, with then-dead subject as arg; "NULL" is no-op; None means absent
    execute: str | None = None


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
class PrintPart:
    # kind="lit": value is decoded literal bytes; kind="var": value is variable name interpolated as string
    kind: str
    value: str
    line: int = 0
    col: int = 0


@dataclass
class PrintStmt:
    # list[PrintPart] in source order
    parts: list
    line: int
    col: int


@dataclass
class InputStmt:
    var: str
    line: int
    col: int


@dataclass
class ImportFuncStmt:
    path: str
    name: str
    line: int
    col: int
    # True for angle-bracket form: importf <stem> as NAME;
    search_path: bool = False


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
    """import number N as VAR; — eternal-alive object with a numeric payload
    (§4.4.14). `value` is an int (INT), a float (is_float), or the decimal
    string of an over-int64 bignum literal (is_big)."""
    value: "int | float | str"
    var: str
    line: int
    col: int
    is_float: bool = False
    is_big: bool = False


@dataclass
class WatchStmt:
    var: str
    line: int
    col: int
    # file form: watch "PATH" as VAR;
    path: str | None = None
    # signal form: watch signal NAME as VAR;
    signal_name: str | None = None
    # pid form: watch pid N as VAR;
    pid_var: str | None = None
    # mtime form: watch mtime "PATH" as VAR;
    mtime_path: str | None = None


@dataclass
class FuncCallComposeArg:
    """FN [L, R] V;  -- compose(L, R) -> result -> V

    `left`/`right` are read operands: a bound name (str) or an Operand literal."""
    name: str
    left: "str | Operand"
    right: "str | Operand"
    target: str
    line: int
    col: int


@dataclass
class FuncCallDecomposeRet:
    """FN A [B, C];  -- A -> result -> decompose into B, C

    `arg` is a read operand (str name or Operand literal); `left`/`right` are
    write targets and stay identifiers."""
    name: str
    arg: "str | Operand"
    left: str
    right: str
    line: int
    col: int


@dataclass
class SubscriptStmt:
    """S[N] X; — read the Nth right-spine head of S into X (§4.4.15).

    `index` is a read operand (str name or Operand literal)."""
    source: str
    index: "str | Operand"
    target: str
    line: int
    col: int


@dataclass
class SliceStmt:
    """S[I..J] X; — fresh cons-list of elements I..J-1 from S (§4.4.16).

    `start`/`end` are read operands (str name or Operand literal)."""
    source: str
    start: "str | Operand"
    end: "str | Operand"
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
    # None = no else clause
    else_body: list | None = None
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
    """read "PATH" as VAR; or read VAR as TARGET; — slurp file into a
    string-cons-list owning the file (§4.4.21)."""
    path: str | None
    path_var: str | None
    target: str
    line: int
    col: int


@dataclass
class WriteStmt:
    """write SRC to "PATH" [as VERDICT]; or write SRC to VAR [as VERDICT]; (§4.4.22)."""
    source: str
    path: str | None
    path_var: str | None
    verdict: str | None
    line: int
    col: int


@dataclass
class AppendStmt:
    """append SRC to "PATH" [as VERDICT]; or append SRC to VAR [as VERDICT]; (§4.4.23)."""
    source: str
    path: str | None
    path_var: str | None
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
class MkdirStmt:
    """mkdir "PATH"; or mkdir VAR; — create directory (recursive)."""
    path: str | None
    path_var: str | None
    line: int
    col: int


@dataclass
class ListdirStmt:
    """listdir "PATH" as VAR; or listdir PATH_VAR as VAR; — list directory entries."""
    path: str | None
    path_var: str | None
    target: str
    line: int
    col: int


@dataclass
class ExistsStmt:
    """exists "PATH" as VAR; or exists PATH_VAR as VAR; — verdict: alive if path exists."""
    path: str | None
    path_var: str | None
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
    # list[TextPart]
    parts: list
    target: str
    line: int
    col: int


@dataclass
class SpawnStmt:
    """spawn FN <operand> [into N] as A; — start an actor running user function FN with the
    composed argument, binding live handle A; if `into` is given, scope the actor to universe N
    (cancelled when N dies, keeps N alive while running) (§concurrency)."""
    name: str
    # str (a bound name) or Operand (an inline literal)
    arg: "str | Operand"
    # universe name, or None
    into: "str | None"
    target: str
    line: int
    col: int


@dataclass
class SendStmt:
    """send <operand> to DEST; — FIFO-enqueue a message onto actor/channel DEST (non-blocking)."""
    # str (a bound name) or Operand (an inline literal)
    message: "str | Operand"
    dest: str
    line: int
    col: int


@dataclass
class RecvStmt:
    """recv [from SRC] as M; — dequeue a message (blocks/yields). Without `from`, reads the
    running actor's own mailbox; with `from`, reads channel/handle SRC (dead+drained -> EOF)."""
    # source name, or None for the actor's own mailbox
    source: "str | None"
    target: str
    line: int
    col: int


@dataclass
class YieldStmt:
    """yield; — cooperative yield to the scheduler."""
    line: int
    col: int


@dataclass
class JoinStmt:
    """join A; — drive the scheduler until handle A (actor or universe) is dead."""
    handle: str
    line: int
    col: int


@dataclass
class ChannelStmt:
    """channel as C; — bind C to a fresh closeable channel (close via `close C;` or C.DIE())."""
    target: str
    line: int
    col: int


@dataclass
class UniverseStmt:
    """universe as N; — bind N to a fresh universe: a supervision scope, alive while any scoped
    child runs; DIE cancels the whole subtree. (Distinct from the `universe` lifetime concept.)"""
    target: str
    line: int
    col: int


@dataclass
class ListenStmt:
    """listen <port> as L;  |  listen "unix:/path" as L; — bind a listening socket. The handle L
    is alive while the socket is open. A bound name/operand port selects TCP; a "unix:/p" string
    literal selects a Unix-domain socket. Accept connections with `accept from L as C;`."""
    # str (a "unix:/p" literal) for Unix-domain, else None (TCP)
    spec: "str | None"
    # port operand (name or literal) for TCP, else None (Unix-domain)
    port: "str | Operand | None"
    target: str
    line: int
    col: int


@dataclass
class AcceptStmt:
    """accept from L as C; — block (parking the actor) until a client connects to listener L,
    binding the fresh connection handle C. C is a channel: `send`/`recv` on it cross the wire,
    and peer-close makes C dead."""
    listener: str
    target: str
    line: int
    col: int


@dataclass
class ConnectStmt:
    """connect "host" <port> as C;  |  connect HOST_VAR <port> as C;  |  connect "unix:/path" as C;
    Open a connection, binding handle C (alive while connected, born dead on failure)."""
    host: "str | None"
    host_var: "str | None"
    # port operand (name or literal) for TCP, else None (Unix-domain)
    port: "str | Operand | None"
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
    ExistsStmt,
]


@dataclass
class Program:
    statements: list = field(default_factory=list)
    source_path: Path | None = None
