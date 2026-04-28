from athc.ast import (
    AthLoop,
    ComposeStmt,
    DecomposeStmt,
    DieStmt,
    FuncCallComposeArg,
    FuncCallDecomposeRet,
    ImportFuncStmt,
    ImportStmt,
    InputStmt,
    Print2Stmt,
    PrintStmt,
    Program,
    WatchStmt,
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

    def _parse_import(self) -> ImportStmt:
        kw = self._expect(TokenKind.KW_IMPORT)
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

    def _parse_importf(self) -> ImportFuncStmt:
        kw = self._expect(TokenKind.KW_IMPORTF)
        path = self._expect(TokenKind.STRING)
        self._expect(TokenKind.KW_AS)
        name = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ImportFuncStmt(
            path=path.value, name=name.value, line=kw.line, col=kw.col
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
        raise ParseError(
            f"expected STRING or 'signal' after 'watch'; got {nxt.kind.name}",
            nxt.line,
            nxt.col,
        )

    def _parse_die_or_funcall(self):
        first = self._expect(TokenKind.IDENT)
        nxt = self._peek()
        if nxt.kind is TokenKind.DIE:
            return self._parse_die_after_var(first)
        if nxt.kind is TokenKind.LBRACKET:
            return self._parse_funcall_compose_arg(first)
        if nxt.kind is TokenKind.IDENT:
            return self._parse_funcall_decompose_ret(first)
        raise ParseError(
            f"expected '.DIE', '[', or identifier after {first.value!r}; "
            f"got {nxt.kind.name}",
            nxt.line,
            nxt.col,
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

    def _parse_funcall_compose_arg(self, name_tok: Token) -> FuncCallComposeArg:
        self._expect(TokenKind.LBRACKET)
        left = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.COMMA)
        right = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.RBRACKET)
        target = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return FuncCallComposeArg(
            name=name_tok.value,
            left=left.value,
            right=right.value,
            target=target.value,
            line=name_tok.line,
            col=name_tok.col,
        )

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
