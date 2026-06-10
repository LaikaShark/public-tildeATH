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
    Operand,
    ImportFuncStmt,
    ImportNumberStmt,
    ImportStmt,
    InputStmt,
    PrintPart,
    PrintStmt,
    Program,
    ReadStmt,
    SleepStmt,
    SliceStmt,
    SubscriptStmt,
    TextPart,
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
    NurseryStmt,
    ListenStmt,
    AcceptStmt,
    ConnectStmt,
)
from athc.lexer import (
    Token,
    TokenKind,
    describe_kind,
    describe_token,
    tokenize,
)
from athc.suggest import closest

# Statement-leading words; suggest a keyword when a misspelled one (lexed as IDENT) fails to parse
_STATEMENT_KEYWORDS = frozenset({
    "import", "importf", "bifurcate", "print", "input", "watch", "branch",
    "clone", "sleep", "timer", "read", "write", "append", "close", "text",
    "loop", "every",
    "spawn", "send", "recv", "yield", "join", "channel", "nursery",
    "listen", "accept", "connect",
})

# Literal tokens accepted as inline read-operands inside bracket forms.
_LITERAL_TOKEN_KINDS = frozenset({
    TokenKind.INT, TokenKind.FLOAT, TokenKind.BIGINT, TokenKind.STRING,
})


def _operand_display(op) -> str:
    """Human-readable rendering of a read-operand for error messages."""
    if isinstance(op, Operand):
        return repr(op.value) if op.kind == "string" else str(op.value)
    return op


class ParseError(Exception):
    def __init__(self, msg: str, line: int, col: int, help: str | None = None):
        super().__init__(f"line {line}, col {col}: {msg}")
        self.msg = msg
        self.line = line
        self.col = col
        self.help = help


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.pos = 0

    def _peek(self, offset: int = 0) -> Token:
        return self.tokens[self.pos + offset]

    def _advance(self) -> Token:
        tok = self.tokens[self.pos]
        self.pos += 1
        return tok

    def _expect(self, kind: TokenKind) -> Token:
        tok = self._peek()
        if tok.kind is not kind:
            raise ParseError(
                f"expected {describe_kind(kind)} but found {describe_token(tok)}",
                tok.line,
                tok.col,
            )
        return self._advance()

    def parse_program(self) -> Program:
        program = Program()
        while self._peek().kind is not TokenKind.EOF:
            program.statements.append(self._parse_statement())
        return program

    def _parse_statement(self):
        tok = self._peek()
        if tok.kind is TokenKind.KW_IMPORT:
            return self._parse_import()
        if tok.kind is TokenKind.KW_IMPORTF:
            return self._parse_importf()
        if tok.kind is TokenKind.KW_WATCH:
            return self._parse_watch()
        if tok.kind is TokenKind.KW_BIFURCATE:
            return self._parse_bifurcate()
        if tok.kind is TokenKind.KW_PRINT:
            return self._parse_print()
        if tok.kind is TokenKind.KW_INPUT:
            return self._parse_input()
        if tok.kind is TokenKind.ATH:
            return self._parse_ath_loop()
        if tok.kind is TokenKind.KW_BRANCH:
            return self._parse_branch()
        if tok.kind is TokenKind.KW_CLONE:
            return self._parse_clone()
        if tok.kind is TokenKind.KW_SLEEP:
            return self._parse_sleep()
        if tok.kind is TokenKind.KW_TIMER:
            return self._parse_timer()
        if tok.kind is TokenKind.KW_LOOP:
            return self._parse_loop()
        if tok.kind is TokenKind.KW_EVERY:
            return self._parse_every()
        if tok.kind is TokenKind.KW_READ:
            return self._parse_read()
        if tok.kind is TokenKind.KW_WRITE:
            return self._parse_write_or_append(append=False)
        if tok.kind is TokenKind.KW_APPEND:
            return self._parse_write_or_append(append=True)
        if tok.kind is TokenKind.KW_CLOSE:
            return self._parse_close()
        if tok.kind is TokenKind.KW_TEXT:
            return self._parse_text()
        if tok.kind is TokenKind.KW_SPAWN:
            return self._parse_spawn()
        if tok.kind is TokenKind.KW_SEND:
            return self._parse_send()
        if tok.kind is TokenKind.KW_RECV:
            return self._parse_recv()
        if tok.kind is TokenKind.KW_YIELD:
            return self._parse_yield()
        if tok.kind is TokenKind.KW_CHANNEL:
            return self._parse_channel()
        if tok.kind is TokenKind.KW_NURSERY:
            return self._parse_nursery()
        if tok.kind is TokenKind.KW_LISTEN:
            return self._parse_listen()
        if tok.kind is TokenKind.KW_ACCEPT:
            return self._parse_accept()
        if tok.kind is TokenKind.KW_CONNECT:
            return self._parse_connect()
        if tok.kind is TokenKind.IDENT:
            # 'join' is a soft keyword: `join HANDLE ;` (IDENT IDENT ';') is unambiguous because
            # function calls always use brackets, so this shape is otherwise a parse error. This
            # keeps `join` usable as a function name (importf <join> as JOIN; JOIN [L, S] R;).
            if (tok.value.lower() == "join"
                    and self._peek(1).kind is TokenKind.IDENT
                    and self._peek(2).kind is TokenKind.SEMI):
                return self._parse_join()
            # IDENT leads funcall/subscript/.DIE, but a misspelled keyword also lexes as IDENT; if the IDENT-led parse fails and the word is one typo from a statement keyword, report that
            try:
                return self._parse_die_or_funcall()
            except ParseError as orig:
                kw = closest(
                    tok.value, _STATEMENT_KEYWORDS, fold=True, max_distance=1
                )
                if kw is not None:
                    raise ParseError(
                        f"unknown statement '{tok.value}'",
                        tok.line,
                        tok.col,
                        help=f"did you mean the keyword '{kw}'?",
                    ) from orig
                raise
        if tok.kind is TokenKind.RESERVED:
            raise ParseError(
                f"'{tok.value}' is reserved and not yet implemented",
                tok.line,
                tok.col,
            )
        raise ParseError(
            f"unexpected {describe_token(tok)} at the start of a statement",
            tok.line,
            tok.col,
        )

    def _parse_import(self):
        kw = self._expect(TokenKind.KW_IMPORT)
        first = self._peek()
        if first.kind is TokenKind.IDENT:
            folded = first.value.lower()
            if folded == "builtin":
                return self._parse_import_builtin(kw)
            if folded == "number":
                return self._parse_import_number(kw)
        idents: list[str] = []
        while self._peek().kind is TokenKind.IDENT:
            idents.append(self._advance().value)
        self._expect(TokenKind.SEMI)
        if len(idents) < 2:
            raise ParseError(
                "'import' requires at least one metadata word followed by "
                "the variable name",
                kw.line,
                kw.col,
            )
        return ImportStmt(
            name=" ".join(idents[:-1]),
            var=idents[-1],
            line=kw.line,
            col=kw.col,
        )

    def _parse_import_builtin(self, kw: Token) -> ImportBuiltinStmt:
        # consume 'builtin'
        self._advance()
        sym = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.KW_AS)
        name = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ImportBuiltinStmt(
            symbol=sym.value,
            name=name.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_import_number(self, kw: Token) -> ImportNumberStmt:
        # consume 'number'
        self._advance()
        tok = self._peek()
        is_float = False
        is_big = False
        if tok.kind is TokenKind.FLOAT:
            self._advance()
            value: "int | float | str" = float(tok.value)
            is_float = True
        elif tok.kind is TokenKind.BIGINT:
            self._advance()
            # decimal string; parsed at runtime
            value = tok.value
            is_big = True
        elif tok.kind is TokenKind.INT:
            self._advance()
            value = int(tok.value)
        else:
            raise ParseError(
                f"expected a number literal after 'import number' but found "
                f"{describe_token(tok)}",
                tok.line,
                tok.col,
            )
        self._expect(TokenKind.KW_AS)
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ImportNumberStmt(
            value=value,
            var=var.value,
            line=kw.line,
            col=kw.col,
            is_float=is_float,
            is_big=is_big,
        )

    def _parse_bifurcate(self):
        kw = self._expect(TokenKind.KW_BIFURCATE)
        nxt = self._peek()
        if nxt.kind is TokenKind.IDENT:
            source = self._advance()
            self._expect(TokenKind.LBRACKET)
            left = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.COMMA)
            right = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.RBRACKET)
            self._expect(TokenKind.SEMI)
            return DecomposeStmt(
                source=source.value,
                left=left.value,
                right=right.value,
                line=kw.line,
                col=kw.col,
            )
        if nxt.kind is TokenKind.LBRACKET:
            self._advance()
            left = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.COMMA)
            right = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.RBRACKET)
            target = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return ComposeStmt(
                left=left.value,
                right=right.value,
                target=target.value,
                line=kw.line,
                col=kw.col,
            )
        raise ParseError(
            f"expected a name or '[' after BIFURCATE but found "
            f"{describe_token(nxt)}",
            nxt.line,
            nxt.col,
        )

    def _parse_print(self) -> PrintStmt:
        kw = self._expect(TokenKind.KW_PRINT)
        # Lexer emits payload as alternating RAWTEXT (literal) and PRINTVAR (interpolation) tokens, terminated by ';'
        parts: list[PrintPart] = []
        while True:
            tok = self._peek()
            if tok.kind is TokenKind.RAWTEXT:
                self._advance()
                parts.append(PrintPart(kind="lit", value=tok.value,
                                       line=tok.line, col=tok.col))
            elif tok.kind is TokenKind.PRINTVAR:
                self._advance()
                parts.append(PrintPart(kind="var", value=tok.value,
                                       line=tok.line, col=tok.col))
            else:
                break
        self._expect(TokenKind.SEMI)
        return PrintStmt(parts=parts, line=kw.line, col=kw.col)

    def _parse_input(self) -> InputStmt:
        kw = self._expect(TokenKind.KW_INPUT)
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return InputStmt(var=var.value, line=kw.line, col=kw.col)

    def _parse_ath_loop(self) -> AthLoop:
        kw = self._expect(TokenKind.ATH)
        self._expect(TokenKind.LPAREN)
        inverted = False
        if self._peek().kind is TokenKind.BANG:
            self._advance()
            inverted = True
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.RPAREN)
        self._expect(TokenKind.LBRACE)
        body = []
        while self._peek().kind is not TokenKind.RBRACE:
            if self._peek().kind is TokenKind.EOF:
                raise ParseError(
                    "unexpected end of input inside ~ATH loop body",
                    self._peek().line,
                    self._peek().col,
                )
            body.append(self._parse_statement())
        self._expect(TokenKind.RBRACE)
        execute: str | None = None
        if self._peek().kind is TokenKind.KW_EXECUTE:
            self._advance()
            self._expect(TokenKind.LPAREN)
            execute = self._expect(TokenKind.IDENT).value
            self._expect(TokenKind.RPAREN)
            self._expect(TokenKind.SEMI)
        return AthLoop(
            var=var.value,
            body=body,
            line=kw.line,
            col=kw.col,
            inverted=inverted,
            execute=execute,
        )

    def _parse_loop(self) -> LoopStmt:
        kw = self._expect(TokenKind.KW_LOOP)
        count = self._expect(TokenKind.IDENT)
        body = self._parse_brace_block()
        return LoopStmt(
            count_var=count.value, body=body, line=kw.line, col=kw.col
        )

    def _parse_every(self) -> EveryStmt:
        kw = self._expect(TokenKind.KW_EVERY)
        interval = self._expect(TokenKind.IDENT)
        body = self._parse_brace_block()
        return EveryStmt(
            interval_var=interval.value, body=body, line=kw.line, col=kw.col
        )

    def _parse_branch(self) -> BranchStmt:
        kw = self._expect(TokenKind.KW_BRANCH)
        self._expect(TokenKind.LPAREN)
        inverted = False
        if self._peek().kind is TokenKind.BANG:
            self._advance()
            inverted = True
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.RPAREN)
        then_body = self._parse_brace_block()
        else_body: list | None = None
        nxt = self._peek()
        if nxt.kind is TokenKind.KW_ELSE:
            self._advance()
            else_body = self._parse_brace_block()
        elif nxt.kind is TokenKind.LBRACE:
            # Bare second block: sugar for ELSE { ... }
            else_body = self._parse_brace_block()
        return BranchStmt(
            var=var.value,
            inverted=inverted,
            then_body=then_body,
            else_body=else_body,
            line=kw.line,
            col=kw.col,
        )

    def _parse_brace_block(self) -> list:
        self._expect(TokenKind.LBRACE)
        body: list = []
        while self._peek().kind is not TokenKind.RBRACE:
            if self._peek().kind is TokenKind.EOF:
                raise ParseError(
                    "unexpected end of input inside { } block",
                    self._peek().line,
                    self._peek().col,
                )
            body.append(self._parse_statement())
        self._expect(TokenKind.RBRACE)
        return body

    def _parse_clone(self) -> CloneStmt:
        kw = self._expect(TokenKind.KW_CLONE)
        src = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return CloneStmt(
            source=src.value,
            target=tgt.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_sleep(self) -> SleepStmt:
        kw = self._expect(TokenKind.KW_SLEEP)
        dur = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return SleepStmt(
            duration=dur.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_timer(self) -> TimerStmt:
        kw = self._expect(TokenKind.KW_TIMER)
        dur = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return TimerStmt(
            duration=dur.value,
            target=tgt.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_read(self) -> ReadStmt:
        kw = self._expect(TokenKind.KW_READ)
        path = self._expect(TokenKind.STRING)
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ReadStmt(
            path=path.value,
            target=tgt.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_write_or_append(self, *, append: bool):
        kw = self._expect(
            TokenKind.KW_APPEND if append else TokenKind.KW_WRITE
        )
        src = self._expect(TokenKind.IDENT)
        # 'to' is contextual: IDENT whose value matches "to" (case-insensitive); elsewhere a normal identifier
        to_tok = self._peek()
        if to_tok.kind is not TokenKind.IDENT or to_tok.value.lower() != "to":
            raise ParseError(
                f"expected 'to' between source and destination but found "
                f"{describe_token(to_tok)}",
                to_tok.line,
                to_tok.col,
            )
        self._advance()
        path = self._expect(TokenKind.STRING)
        verdict: str | None = None
        if self._peek().kind is TokenKind.KW_AS:
            self._advance()
            verdict_tok = self._expect(TokenKind.IDENT)
            verdict = verdict_tok.value
        self._expect(TokenKind.SEMI)
        cls = AppendStmt if append else WriteStmt
        return cls(
            source=src.value,
            path=path.value,
            verdict=verdict,
            line=kw.line,
            col=kw.col,
        )

    def _parse_close(self) -> CloseStmt:
        kw = self._expect(TokenKind.KW_CLOSE)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return CloseStmt(
            target=tgt.value,
            line=kw.line,
            col=kw.col,
        )

    def _match_contextual(self, word: str) -> bool:
        """Consume a contextual marker word (an IDENT, like the write/append 'to') if present."""
        tok = self._peek()
        if tok.kind is TokenKind.IDENT and tok.value.lower() == word:
            self._advance()
            return True
        return False

    def _expect_contextual(self, word: str) -> None:
        if not self._match_contextual(word):
            tok = self._peek()
            raise ParseError(
                f"expected '{word}' but found {describe_token(tok)}",
                tok.line,
                tok.col,
            )

    def _parse_spawn(self) -> SpawnStmt:
        kw = self._expect(TokenKind.KW_SPAWN)
        name = self._expect(TokenKind.IDENT)
        arg = self._parse_operand()
        into: str | None = None
        if self._match_contextual("into"):
            into = self._expect(TokenKind.IDENT).value
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return SpawnStmt(
            name=name.value,
            arg=arg,
            into=into,
            target=tgt.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_send(self) -> SendStmt:
        kw = self._expect(TokenKind.KW_SEND)
        msg = self._parse_operand()
        self._expect_contextual("to")
        dest = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return SendStmt(
            message=msg,
            dest=dest.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_recv(self) -> RecvStmt:
        kw = self._expect(TokenKind.KW_RECV)
        source: str | None = None
        if self._match_contextual("from"):
            source = self._expect(TokenKind.IDENT).value
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return RecvStmt(
            source=source,
            target=tgt.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_yield(self) -> YieldStmt:
        kw = self._expect(TokenKind.KW_YIELD)
        self._expect(TokenKind.SEMI)
        return YieldStmt(line=kw.line, col=kw.col)

    def _parse_join(self) -> JoinStmt:
        kw = self._advance()  # the soft keyword 'join' (an IDENT)
        h = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return JoinStmt(handle=h.value, line=kw.line, col=kw.col)

    def _parse_channel(self) -> ChannelStmt:
        kw = self._expect(TokenKind.KW_CHANNEL)
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ChannelStmt(target=tgt.value, line=kw.line, col=kw.col)

    def _parse_nursery(self) -> NurseryStmt:
        kw = self._expect(TokenKind.KW_NURSERY)
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return NurseryStmt(target=tgt.value, line=kw.line, col=kw.col)

    def _parse_listen(self) -> ListenStmt:
        # listen PORT as L;  (TCP)  |  listen "unix:/path" as L;  (Unix-domain)
        kw = self._expect(TokenKind.KW_LISTEN)
        spec: str | None = None
        port = None
        if self._peek().kind is TokenKind.STRING:
            stok = self._advance()
            if not stok.value.startswith("unix:"):
                raise ParseError(
                    "a string 'listen' address must be a unix-domain path "
                    '("unix:/path"); for TCP use a port: listen PORT as L;',
                    stok.line,
                    stok.col,
                )
            spec = stok.value
        else:
            port = self._parse_operand()  # TCP port (a name or numeric literal)
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ListenStmt(
            spec=spec, port=port, target=tgt.value, line=kw.line, col=kw.col
        )

    def _parse_accept(self) -> AcceptStmt:
        # accept from L as C;
        kw = self._expect(TokenKind.KW_ACCEPT)
        self._expect_contextual("from")
        lis = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return AcceptStmt(
            listener=lis.value, target=tgt.value, line=kw.line, col=kw.col
        )

    def _parse_connect(self) -> ConnectStmt:
        # connect "host" PORT as C;  (TCP)  |  connect "unix:/path" as C;  (Unix-domain)
        kw = self._expect(TokenKind.KW_CONNECT)
        host = self._expect(TokenKind.STRING)
        port = None
        if self._peek().kind is not TokenKind.KW_AS:
            port = self._parse_operand()
        is_unix = host.value.startswith("unix:")
        if is_unix and port is not None:
            raise ParseError(
                "a unix-domain 'connect' takes no port", kw.line, kw.col
            )
        if not is_unix and port is None:
            raise ParseError(
                'a TCP \'connect\' needs a port: connect "host" PORT as C;',
                kw.line,
                kw.col,
            )
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ConnectStmt(
            host=host.value, port=port, target=tgt.value, line=kw.line, col=kw.col
        )

    def _parse_text(self) -> TextStmt:
        kw = self._expect(TokenKind.KW_TEXT)
        parts: list[TextPart] = []
        while True:
            tok = self._peek()
            if tok.kind is TokenKind.STRING:
                parts.append(TextPart(kind="str", value=self._advance().value))
            elif tok.kind is TokenKind.IDENT:
                parts.append(TextPart(kind="ident", value=self._advance().value))
            else:
                break
        if not parts:
            raise ParseError(
                "'text' requires at least one part (a string literal or an "
                "identifier) before 'as'",
                kw.line,
                kw.col,
            )
        self._expect(TokenKind.KW_AS)
        tgt = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return TextStmt(
            parts=parts,
            target=tgt.value,
            line=kw.line,
            col=kw.col,
        )

    def _parse_importf(self) -> ImportFuncStmt:
        kw = self._expect(TokenKind.KW_IMPORTF)
        nxt = self._peek()
        if nxt.kind is TokenKind.STRING:
            path = self._advance().value
            self._expect(TokenKind.KW_AS)
            name = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return ImportFuncStmt(
                path=path, name=name.value, line=kw.line, col=kw.col,
            )
        if nxt.kind is TokenKind.LANGLE:
            self._advance()
            stem = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.RANGLE)
            self._expect(TokenKind.KW_AS)
            name = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return ImportFuncStmt(
                path=stem.value,
                name=name.value,
                line=kw.line,
                col=kw.col,
                search_path=True,
            )
        raise ParseError(
            f"expected a string literal or '<' after 'importf' but found "
            f"{describe_token(nxt)}",
            nxt.line,
            nxt.col,
        )

    def _parse_watch(self) -> WatchStmt:
        kw = self._expect(TokenKind.KW_WATCH)
        nxt = self._peek()
        if nxt.kind is TokenKind.STRING:
            # watch "PATH" as VAR;
            path = self._advance().value
            self._expect(TokenKind.KW_AS)
            var = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return WatchStmt(
                var=var.value, line=kw.line, col=kw.col, path=path
            )
        if nxt.kind is TokenKind.IDENT and nxt.value.lower() == "signal":
            # watch signal NAME as VAR;
            self._advance()
            sig = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.KW_AS)
            var = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return WatchStmt(
                var=var.value,
                line=kw.line,
                col=kw.col,
                signal_name=sig.value,
            )
        if nxt.kind is TokenKind.IDENT and nxt.value.lower() == "pid":
            # watch pid N as VAR; (N is a number-payload var)
            self._advance()
            pidvar = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.KW_AS)
            var = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return WatchStmt(
                var=var.value,
                line=kw.line,
                col=kw.col,
                pid_var=pidvar.value,
            )
        if nxt.kind is TokenKind.IDENT and nxt.value.lower() == "mtime":
            # watch mtime "PATH" as VAR;
            self._advance()
            path = self._expect(TokenKind.STRING).value
            self._expect(TokenKind.KW_AS)
            var = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return WatchStmt(
                var=var.value,
                line=kw.line,
                col=kw.col,
                mtime_path=path,
            )
        raise ParseError(
            "expected a string literal, 'signal', 'pid', or 'mtime' after "
            f"'watch' but found {describe_token(nxt)}",
            nxt.line,
            nxt.col,
        )

    def _parse_die_or_funcall(self):
        first = self._expect(TokenKind.IDENT)
        nxt = self._peek()
        if nxt.kind is TokenKind.DIE:
            return self._parse_die_after_var(first)
        if nxt.kind is TokenKind.LBRACKET:
            return self._parse_bracket_form(first)
        if nxt.kind is TokenKind.IDENT or nxt.kind in _LITERAL_TOKEN_KINDS:
            # IDENT or a literal here begins the decompose-ret arg (FN A [B,C];)
            return self._parse_funcall_decompose_ret(first)
        raise ParseError(
            f"expected '.DIE', '[', or an operand after '{first.value}' but found "
            f"{describe_token(nxt)}",
            nxt.line,
            nxt.col,
        )

    def _parse_operand(self):
        """A read-operand: a bound name (returns its str) or an inline literal
        (returns an Operand). Value decoding mirrors _parse_import_number."""
        tok = self._peek()
        if tok.kind is TokenKind.IDENT:
            self._advance()
            return tok.value
        if tok.kind is TokenKind.INT:
            self._advance()
            return Operand("int", int(tok.value), tok.line, tok.col)
        if tok.kind is TokenKind.FLOAT:
            self._advance()
            return Operand("float", float(tok.value), tok.line, tok.col)
        if tok.kind is TokenKind.BIGINT:
            self._advance()
            # decimal string; parsed at runtime
            return Operand("bignum", tok.value, tok.line, tok.col)
        if tok.kind is TokenKind.STRING:
            self._advance()
            return Operand("string", tok.value, tok.line, tok.col)
        raise ParseError(
            f"expected a name or a literal but found {describe_token(tok)}",
            tok.line,
            tok.col,
        )

    def _parse_bracket_form(self, name_tok: Token):
        """Three statements share the IDENT '[' OPERAND ... ']' IDENT ';'
        shape. Dispatch on what appears after the first inner operand:
        ',' → funcall compose-arg; '..' → slice; ']' → subscript. Read
        operands may be names or inline literals; the target stays a name."""
        self._expect(TokenKind.LBRACKET)
        inner = self._parse_operand()
        sep = self._peek()
        if sep.kind is TokenKind.COMMA:
            self._advance()
            right = self._parse_operand()
            self._expect(TokenKind.RBRACKET)
            target = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return FuncCallComposeArg(
                name=name_tok.value,
                left=inner,
                right=right,
                target=target.value,
                line=name_tok.line,
                col=name_tok.col,
            )
        if sep.kind is TokenKind.DOTDOT:
            self._advance()
            end = self._parse_operand()
            self._expect(TokenKind.RBRACKET)
            target = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return SliceStmt(
                source=name_tok.value,
                start=inner,
                end=end,
                target=target.value,
                line=name_tok.line,
                col=name_tok.col,
            )
        if sep.kind is TokenKind.RBRACKET:
            self._advance()
            target = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return SubscriptStmt(
                source=name_tok.value,
                index=inner,
                target=target.value,
                line=name_tok.line,
                col=name_tok.col,
            )
        raise ParseError(
            f"expected ',', '..', or ']' after '[{_operand_display(inner)}' but found "
            f"{describe_token(sep)}",
            sep.line,
            sep.col,
        )

    def _parse_die_after_var(self, var_tok: Token) -> DieStmt:
        self._expect(TokenKind.DIE)
        self._expect(TokenKind.LPAREN)
        arg = None
        if self._peek().kind is TokenKind.IDENT:
            arg_tok = self._advance()
            arg = arg_tok.value
        self._expect(TokenKind.RPAREN)
        self._expect(TokenKind.SEMI)
        return DieStmt(var=var_tok.value, arg=arg, line=var_tok.line, col=var_tok.col)

    def _parse_funcall_decompose_ret(self, name_tok: Token) -> FuncCallDecomposeRet:
        arg = self._parse_operand()
        self._expect(TokenKind.LBRACKET)
        left = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.COMMA)
        right = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.RBRACKET)
        self._expect(TokenKind.SEMI)
        return FuncCallDecomposeRet(
            name=name_tok.value,
            arg=arg,
            left=left.value,
            right=right.value,
            line=name_tok.line,
            col=name_tok.col,
        )


def parse(src: str) -> Program:
    return Parser(tokenize(src)).parse_program()
