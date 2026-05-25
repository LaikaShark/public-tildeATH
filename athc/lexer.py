from dataclasses import dataclass
from enum import Enum, auto


class TokenKind(Enum):
    KW_IMPORT = auto()
    KW_IMPORTF = auto()
    KW_AS = auto()
    KW_BIFURCATE = auto()
    KW_PRINT = auto()
    KW_INPUT = auto()
    KW_EXECUTE = auto()
    KW_WATCH = auto()
    KW_BRANCH = auto()
    KW_ELSE = auto()
    KW_CLONE = auto()
    KW_SLEEP = auto()
    KW_TIMER = auto()
    KW_READ = auto()
    KW_WRITE = auto()
    KW_APPEND = auto()
    KW_CLOSE = auto()
    KW_TEXT = auto()
    KW_LOOP = auto()
    KW_EVERY = auto()
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
    FLOAT = auto()
    BIGINT = auto()
    RAWTEXT = auto()
    PRINTVAR = auto()
    RESERVED = auto()
    EOF = auto()


@dataclass(frozen=True)
class Token:
    kind: TokenKind
    value: str
    line: int
    col: int


# Human-readable names for token kinds, used in parser diagnostics so users
# never see internal enum names like COMMA or RBRACKET (diagnostics plan).
_TOKEN_DISPLAY = {
    TokenKind.SEMI: "';'",
    TokenKind.COMMA: "','",
    TokenKind.LBRACKET: "'['",
    TokenKind.RBRACKET: "']'",
    TokenKind.LPAREN: "'('",
    TokenKind.RPAREN: "')'",
    TokenKind.LBRACE: "'{'",
    TokenKind.RBRACE: "'}'",
    TokenKind.LANGLE: "'<'",
    TokenKind.RANGLE: "'>'",
    TokenKind.DOTDOT: "'..'",
    TokenKind.BANG: "'!'",
    TokenKind.IDENT: "a name",
    TokenKind.INT: "a number",
    TokenKind.FLOAT: "a number",
    TokenKind.BIGINT: "a number",
    TokenKind.STRING: "a string literal",
    TokenKind.RAWTEXT: "text",
    TokenKind.PRINTVAR: "an interpolation",
    TokenKind.DIE: "'.DIE'",
    TokenKind.ATH: "'~ATH'",
    TokenKind.RESERVED: "a reserved word",
    TokenKind.EOF: "end of input",
}

# Token kinds whose literal value is worth showing when reporting what was
# *found* (e.g. found 'THIS', found 'foo').
_VALUE_KINDS = frozenset({
    TokenKind.IDENT, TokenKind.INT, TokenKind.FLOAT, TokenKind.BIGINT,
    TokenKind.STRING, TokenKind.RESERVED,
})


def describe_kind(kind: TokenKind) -> str:
    """A friendly name for an *expected* token kind."""
    if kind in _TOKEN_DISPLAY:
        return _TOKEN_DISPLAY[kind]
    if kind.name.startswith("KW_"):
        return f"'{kind.name[3:].lower()}'"
    return kind.name.lower()


def describe_token(tok: "Token") -> str:
    """A friendly description of a *found* token, showing its value when useful."""
    if tok.kind is TokenKind.EOF:
        return "end of input"
    if tok.kind in _VALUE_KINDS:
        return f"'{tok.value}'"
    if tok.kind.name.startswith("KW_"):
        return f"'{tok.value}'"
    return describe_kind(tok.kind)


class LexError(Exception):
    def __init__(self, msg: str, line: int, col: int, help: str | None = None):
        super().__init__(f"line {line}, col {col}: {msg}")
        self.msg = msg
        self.line = line
        self.col = col
        self.help = help


KEYWORDS = {
    "import": TokenKind.KW_IMPORT,
    "importf": TokenKind.KW_IMPORTF,
    "as": TokenKind.KW_AS,
    "bifurcate": TokenKind.KW_BIFURCATE,
    "print": TokenKind.KW_PRINT,
    "input": TokenKind.KW_INPUT,
    "execute": TokenKind.KW_EXECUTE,
    "watch": TokenKind.KW_WATCH,
    "branch": TokenKind.KW_BRANCH,
    "else": TokenKind.KW_ELSE,
    "clone": TokenKind.KW_CLONE,
    "sleep": TokenKind.KW_SLEEP,
    "timer": TokenKind.KW_TIMER,
    "read": TokenKind.KW_READ,
    "write": TokenKind.KW_WRITE,
    "append": TokenKind.KW_APPEND,
    "close": TokenKind.KW_CLOSE,
    "text": TokenKind.KW_TEXT,
    "loop": TokenKind.KW_LOOP,
    "every": TokenKind.KW_EVERY,
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

        # Float forms (SPEC §4.8): a '.' *followed by a digit* opens a
        # fractional part, and 'e'/'E' opens an exponent. A '.' followed by
        # another '.' stays DOTDOT (slice, e.g. 1..3); a '.' followed by a
        # non-digit stays a separate '.DIE' token (e.g. 3.die). Either part
        # makes the literal a FLOAT.
        is_float = False
        if self._peek() == "." and self._peek(1).isdigit():
            is_float = True
            self._advance()  # consume '.'
            while self.pos < len(self.src) and self._peek().isdigit():
                self._advance()
        if self._peek() in ("e", "E"):
            # Exponent requires at least one digit (after an optional sign).
            # Look ahead without consuming so `1exit` keeps `1` and `exit`
            # separate (and doesn't drift line/col on rollback).
            off = 1
            if self._peek(off) in ("+", "-"):
                off += 1
            if self._peek(off).isdigit():
                is_float = True
                self._advance()  # 'e'/'E'
                if self._peek() in ("+", "-"):
                    self._advance()
                while self.pos < len(self.src) and self._peek().isdigit():
                    self._advance()

        text = self.src[start:self.pos]
        if is_float:
            try:
                f = float(text)
            except ValueError:
                raise LexError(f"invalid float literal {text!r}", line, col)
            if f != f or f in (float("inf"), float("-inf")):
                raise LexError(
                    f"float literal {text} is not finite", line, col
                )
            self._emit(TokenKind.FLOAT, text, line, col)
            return
        try:
            n = int(text)
        except ValueError:
            raise LexError(f"invalid integer literal {text!r}", line, col)
        if n < INT64_MIN or n > INT64_MAX:
            # Too big for int64 → an arbitrary-precision bignum literal (§4.8).
            self._emit(TokenKind.BIGINT, text, line, col)
            return
        self._emit(TokenKind.INT, text, line, col)

    # Escape tables (SPEC §2.3, §2.4). Mapping: input-char -> decoded byte(s).
    _PRINT_ESCAPES = {
        ";": ";",
        "\\": "\\",
        "n": "\n",
        "t": "\t",
        "r": "\r",
        "$": "$",
    }
    _STRING_ESCAPES = {
        '"': '"',
        "\\": "\\",
        "n": "\n",
        "t": "\t",
        "r": "\r",
    }

    def _read_print_payload(self) -> None:
        if self._peek() != " ":
            raise LexError(
                "'print' must be followed by a single ASCII space", self.line, self.col
            )
        self._advance()
        start_line, start_col = self.line, self.col
        buf: list[str] = []
        lit_line, lit_col = start_line, start_col

        def flush_literal() -> None:
            # Emit the accumulated literal run, if any, as one RAWTEXT part.
            if buf:
                self._emit(TokenKind.RAWTEXT, "".join(buf), lit_line, lit_col)
                buf.clear()

        while self.pos < len(self.src) and self._peek() != ";":
            c = self._peek()
            if c == "\\":
                bs_line, bs_col = self.line, self.col
                self._advance()
                nxt = self._peek()
                if nxt == "":
                    raise LexError(
                        "trailing backslash in 'print' payload",
                        bs_line, bs_col,
                    )
                if nxt not in self._PRINT_ESCAPES:
                    raise LexError(
                        f"unknown escape sequence '\\{nxt}' in 'print' payload "
                        f"(recognized: \\; \\\\ \\n \\t \\r \\$)",
                        bs_line, bs_col,
                    )
                if not buf:
                    lit_line, lit_col = bs_line, bs_col
                buf.append(self._PRINT_ESCAPES[nxt])
                self._advance()
            elif c == "$":
                # `$NAME` interpolates the runtime string bound to NAME
                # (§4.4.6). A `$` not before an identifier must be `\$`.
                dollar_line, dollar_col = self.line, self.col
                self._advance()
                name = self._read_word()
                if not name:
                    raise LexError(
                        "a literal '$' must be written '\\$'; otherwise '$' "
                        "must be followed by an identifier to interpolate",
                        dollar_line, dollar_col,
                    )
                flush_literal()
                self._emit(TokenKind.PRINTVAR, name, dollar_line, dollar_col)
            else:
                if not buf:
                    lit_line, lit_col = self.line, self.col
                buf.append(c)
                self._advance()
        if self.pos >= len(self.src):
            raise LexError(
                "unterminated 'print' statement (missing ';')", start_line, start_col
            )
        flush_literal()

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
                buf: list[str] = []
                while self.pos < len(self.src) and self._peek() != '"':
                    ch = self._peek()
                    if ch == "\\":
                        bs_line, bs_col = self.line, self.col
                        self._advance()
                        nxt = self._peek()
                        if nxt == "":
                            raise LexError(
                                "trailing backslash in string literal",
                                bs_line, bs_col,
                            )
                        if nxt not in self._STRING_ESCAPES:
                            raise LexError(
                                f"unknown escape sequence '\\{nxt}' in string "
                                f"literal (recognized: \\\" \\\\ \\n \\t \\r)",
                                bs_line, bs_col,
                            )
                        buf.append(self._STRING_ESCAPES[nxt])
                        self._advance()
                    else:
                        buf.append(ch)
                        self._advance()
                if self.pos >= len(self.src):
                    raise LexError("unterminated string literal", line, col)
                self._advance()
                self._emit(TokenKind.STRING, "".join(buf), line, col)
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
