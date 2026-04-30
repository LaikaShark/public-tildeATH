from dataclasses import dataclass
from enum import Enum, auto


class TokenKind(Enum):
    KW_IMPORT = auto()
    KW_IMPORTF = auto()
    KW_AS = auto()
    KW_BIFURCATE = auto()
    KW_PRINT = auto()
    KW_INPUT = auto()
    KW_PRINT2 = auto()
    KW_EXECUTE = auto()
    KW_WATCH = auto()
    KW_BRANCH = auto()
    KW_ELSE = auto()
    KW_CLONE = auto()
    ATH = auto()
    DIE = auto()
    IDENT = auto()
    STRING = auto()
    LPAREN = auto()
    RPAREN = auto()
    LBRACKET = auto()
    RBRACKET = auto()
    LBRACE = auto()
    RBRACE = auto()
    LANGLE = auto()
    RANGLE = auto()
    COMMA = auto()
    SEMI = auto()
    BANG = auto()
    DOTDOT = auto()
    INT = auto()
    RAWTEXT = auto()
    RESERVED = auto()
    EOF = auto()


@dataclass(frozen=True)
class Token:
    kind: TokenKind
    value: str
    line: int
    col: int


class LexError(Exception):
    def __init__(self, msg: str, line: int, col: int):
        super().__init__(f"line {line}, col {col}: {msg}")
        self.msg = msg
        self.line = line
        self.col = col


KEYWORDS = {
    "import": TokenKind.KW_IMPORT,
    "importf": TokenKind.KW_IMPORTF,
    "as": TokenKind.KW_AS,
    "bifurcate": TokenKind.KW_BIFURCATE,
    "print": TokenKind.KW_PRINT,
    "input": TokenKind.KW_INPUT,
    "print2": TokenKind.KW_PRINT2,
    "execute": TokenKind.KW_EXECUTE,
    "watch": TokenKind.KW_WATCH,
    "branch": TokenKind.KW_BRANCH,
    "else": TokenKind.KW_ELSE,
    "clone": TokenKind.KW_CLONE,
}

RESERVED_V1: set[str] = set()

PUNCT = {
    "(": TokenKind.LPAREN,
    ")": TokenKind.RPAREN,
    "[": TokenKind.LBRACKET,
    "]": TokenKind.RBRACKET,
    "{": TokenKind.LBRACE,
    "}": TokenKind.RBRACE,
    "<": TokenKind.LANGLE,
    ">": TokenKind.RANGLE,
    ",": TokenKind.COMMA,
    ";": TokenKind.SEMI,
    "!": TokenKind.BANG,
}

INT64_MIN = -(2**63)
INT64_MAX = 2**63 - 1


class Lexer:
    def __init__(self, src: str):
        self.src = src
        self.pos = 0
        self.line = 1
        self.col = 1
        self.tokens: list[Token] = []

    def _peek(self, offset: int = 0) -> str:
        p = self.pos + offset
        return self.src[p] if p < len(self.src) else ""

    def _advance(self) -> str:
        c = self.src[self.pos]
        self.pos += 1
        if c == "\n":
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return c

    def _emit(self, kind: TokenKind, value: str, line: int, col: int) -> None:
        self.tokens.append(Token(kind, value, line, col))

    def _skip_ws_and_comments(self) -> None:
        while self.pos < len(self.src):
            c = self._peek()
            if c in " \t\r\n":
                self._advance()
            elif c == "/" and self._peek(1) == "/":
                while self.pos < len(self.src) and self._peek() != "\n":
                    self._advance()
            elif c == "/" and self._peek(1) == "*":
                start_line, start_col = self.line, self.col
                self._advance()
                self._advance()
                closed = False
                while self.pos < len(self.src):
                    if self._peek() == "*" and self._peek(1) == "/":
                        self._advance()
                        self._advance()
                        closed = True
                        break
                    self._advance()
                if not closed:
                    raise LexError("unterminated /* */ comment", start_line, start_col)
            else:
                return

    def _read_word(self) -> str:
        start = self.pos
        c = self._peek()
        if not (c.isalpha() or c == "_"):
            return ""
        while self.pos < len(self.src):
            c = self._peek()
            if c.isalnum() or c == "_":
                self._advance()
            else:
                break
        return self.src[start:self.pos]

    def _read_int(self, line: int, col: int) -> None:
        start = self.pos
        if self._peek() == "-":
            self._advance()
        if not self._peek().isdigit():
            raise LexError("expected digits after '-'", line, col)
        while self.pos < len(self.src) and self._peek().isdigit():
            self._advance()
        text = self.src[start:self.pos]
        try:
            n = int(text)
        except ValueError:
            raise LexError(f"invalid integer literal {text!r}", line, col)
        if n < INT64_MIN or n > INT64_MAX:
            raise LexError(
                f"integer literal {text} does not fit signed 64-bit range",
                line,
                col,
            )
        self._emit(TokenKind.INT, text, line, col)

    def _read_print_payload(self) -> None:
        if self._peek() != " ":
            raise LexError(
                "'print' must be followed by a single ASCII space", self.line, self.col
            )
        self._advance()
        start = self.pos
        start_line, start_col = self.line, self.col
        while self.pos < len(self.src) and self._peek() != ";":
            self._advance()
        if self.pos >= len(self.src):
            raise LexError(
                "unterminated 'print' statement (missing ';')", start_line, start_col
            )
        self._emit(TokenKind.RAWTEXT, self.src[start:self.pos], start_line, start_col)

    def tokenize(self) -> list[Token]:
        while True:
            self._skip_ws_and_comments()
            if self.pos >= len(self.src):
                break

            line, col = self.line, self.col
            c = self._peek()

            if c == "~":
                self._advance()
                word = self._read_word()
                if word.lower() != "ath":
                    raise LexError(
                        f"expected '~ATH', got '~{word}'", line, col
                    )
                self._emit(TokenKind.ATH, "~" + word, line, col)
                continue

            if c == ".":
                if self._peek(1) == ".":
                    self._advance()
                    self._advance()
                    self._emit(TokenKind.DOTDOT, "..", line, col)
                    continue
                self._advance()
                word = self._read_word()
                if word.lower() != "die":
                    raise LexError(
                        f"expected '.DIE', got '.{word}'", line, col
                    )
                self._emit(TokenKind.DIE, "." + word, line, col)
                continue

            if c == '"':
                self._advance()
                start = self.pos
                while self.pos < len(self.src) and self._peek() != '"':
                    self._advance()
                if self.pos >= len(self.src):
                    raise LexError("unterminated string literal", line, col)
                text = self.src[start:self.pos]
                self._advance()
                self._emit(TokenKind.STRING, text, line, col)
                continue

            if c in PUNCT:
                self._advance()
                self._emit(PUNCT[c], c, line, col)
                continue

            if c.isdigit() or (c == "-" and self._peek(1).isdigit()):
                self._read_int(line, col)
                continue

            if c.isalpha() or c == "_":
                word = self._read_word()
                folded = word.lower()
                if folded in KEYWORDS:
                    kind = KEYWORDS[folded]
                    self._emit(kind, word, line, col)
                    if kind is TokenKind.KW_PRINT:
                        self._read_print_payload()
                elif folded in RESERVED_V1:
                    self._emit(TokenKind.RESERVED, word, line, col)
                else:
                    self._emit(TokenKind.IDENT, word, line, col)
                continue

            raise LexError(f"unexpected character {c!r}", line, col)

        self._emit(TokenKind.EOF, "", self.line, self.col)
        return self.tokens


def tokenize(src: str) -> list[Token]:
    return Lexer(src).tokenize()
