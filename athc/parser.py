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
    Print2Stmt,
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
)
from athc.lexer import Token, TokenKind, tokenize


class ParseError(Exception):
    def __init__(self, msg: str, line: int, col: int):
        super().__init__(f"line {line}, col {col}: {msg}")
        self.msg = msg
        self.line = line
        self.col = col


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
                f"expected {kind.name}, got {tok.kind.name} ({tok.value!r})",
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
        if tok.kind is TokenKind.KW_PRINT2:
            return self._parse_print2()
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
        if tok.kind is TokenKind.IDENT:
            return self._parse_die_or_funcall()
        if tok.kind is TokenKind.RESERVED:
            raise ParseError(
                f"'{tok.value}' is reserved and not yet implemented",
                tok.line,
                tok.col,
            )
        raise ParseError(
            f"unexpected token {tok.kind.name} ({tok.value!r})",
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
        self._advance()  # consume 'builtin'
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
        self._advance()  # consume 'number'
        n_tok = self._expect(TokenKind.INT)
        self._expect(TokenKind.KW_AS)
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ImportNumberStmt(
            value=int(n_tok.value),
            var=var.value,
            line=kw.line,
            col=kw.col,
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
            f"expected identifier or '[' after BIFURCATE, got {nxt.kind.name}",
            nxt.line,
            nxt.col,
        )

    def _parse_print(self) -> PrintStmt:
        kw = self._expect(TokenKind.KW_PRINT)
        raw = self._expect(TokenKind.RAWTEXT)
        self._expect(TokenKind.SEMI)
        return PrintStmt(text=raw.value, line=kw.line, col=kw.col)

    def _parse_input(self) -> InputStmt:
        kw = self._expect(TokenKind.KW_INPUT)
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return InputStmt(var=var.value, line=kw.line, col=kw.col)

    def _parse_print2(self) -> Print2Stmt:
        kw = self._expect(TokenKind.KW_PRINT2)
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return Print2Stmt(var=var.value, line=kw.line, col=kw.col)

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
        if self._peek().kind is TokenKind.KW_EXECUTE:
            self._advance()
            self._expect(TokenKind.LPAREN)
            self._expect(TokenKind.IDENT)
            self._expect(TokenKind.RPAREN)
            self._expect(TokenKind.SEMI)
        return AthLoop(
            var=var.value, body=body, line=kw.line, col=kw.col, inverted=inverted
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
            # Bare second block — sugar for ELSE { ... }.
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
        # 'to' is a contextual marker: an IDENT whose value matches "to"
        # (case-insensitive). Anywhere else it's a normal identifier.
        to_tok = self._peek()
        if to_tok.kind is not TokenKind.IDENT or to_tok.value.lower() != "to":
            raise ParseError(
                f"expected 'to' between source and destination; got "
                f"{to_tok.kind.name} ({to_tok.value!r})",
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
            f"expected STRING or '<' after 'importf'; got {nxt.kind.name}",
            nxt.line,
            nxt.col,
        )

    def _parse_watch(self) -> WatchStmt:
        kw = self._expect(TokenKind.KW_WATCH)
        nxt = self._peek()
        if nxt.kind is TokenKind.STRING:
            # File form: watch "PATH" as VAR;
            path = self._advance().value
            self._expect(TokenKind.KW_AS)
            var = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return WatchStmt(
                var=var.value, line=kw.line, col=kw.col, path=path
            )
        if nxt.kind is TokenKind.IDENT and nxt.value.lower() == "signal":
            # Signal form: watch signal NAME as VAR;
            self._advance()  # consume 'signal'
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
            # Pid form: watch pid N as VAR;  (N is a number-payload var)
            self._advance()  # consume 'pid'
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
            # Mtime form: watch mtime "PATH" as VAR;
            self._advance()  # consume 'mtime'
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
            "expected STRING, 'signal', 'pid', or 'mtime' after 'watch'; "
            f"got {nxt.kind.name}",
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
        if nxt.kind is TokenKind.IDENT:
            return self._parse_funcall_decompose_ret(first)
        raise ParseError(
            f"expected '.DIE', '[', or identifier after {first.value!r}; "
            f"got {nxt.kind.name}",
            nxt.line,
            nxt.col,
        )

    def _parse_bracket_form(self, name_tok: Token):
        """Three statements share the IDENT '[' IDENT ... ']' IDENT ';'
        shape. Dispatch on what appears after the first inner ident:
        ',' → funcall compose-arg; '..' → slice; ']' → subscript."""
        self._expect(TokenKind.LBRACKET)
        inner = self._expect(TokenKind.IDENT)
        sep = self._peek()
        if sep.kind is TokenKind.COMMA:
            self._advance()
            right = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.RBRACKET)
            target = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return FuncCallComposeArg(
                name=name_tok.value,
                left=inner.value,
                right=right.value,
                target=target.value,
                line=name_tok.line,
                col=name_tok.col,
            )
        if sep.kind is TokenKind.DOTDOT:
            self._advance()
            end = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.RBRACKET)
            target = self._expect(TokenKind.IDENT)
            self._expect(TokenKind.SEMI)
            return SliceStmt(
                source=name_tok.value,
                start=inner.value,
                end=end.value,
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
                index=inner.value,
                target=target.value,
                line=name_tok.line,
                col=name_tok.col,
            )
        raise ParseError(
            f"expected ',', '..', or ']' after '[{inner.value}'; "
            f"got {sep.kind.name}",
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
        arg = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.LBRACKET)
        left = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.COMMA)
        right = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.RBRACKET)
        self._expect(TokenKind.SEMI)
        return FuncCallDecomposeRet(
            name=name_tok.value,
            arg=arg.value,
            left=left.value,
            right=right.value,
            line=name_tok.line,
            col=name_tok.col,
        )


def parse(src: str) -> Program:
    return Parser(tokenize(src)).parse_program()
