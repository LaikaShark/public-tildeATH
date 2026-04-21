from dataclasses import dataclass, field
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


@dataclass
class DieStmt:
    var: str
    line: int
    col: int


@dataclass
class PrintStmt:
    text: str
    line: int
    col: int


Stmt = Union[ImportStmt, DecomposeStmt, ComposeStmt, AthLoop, DieStmt, PrintStmt]


@dataclass
class Program:
    statements: list = field(default_factory=list)
