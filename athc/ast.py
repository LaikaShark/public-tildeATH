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


Stmt = Union[
    ImportStmt,
    DecomposeStmt,
    ComposeStmt,
    AthLoop,
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
]


@dataclass
class Program:
    statements: list = field(default_factory=list)
    source_path: Path | None = None
