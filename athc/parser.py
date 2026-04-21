from athc.ast import (
    AthLoop,
    ComposeStmt,
    DecomposeStmt,
    DieStmt,
    ImportStmt,
    PrintStmt,
    Program,
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
        if tok.kind is TokenKind.KW_BIFURCATE:
            return self._parse_bifurcate()
        if tok.kind is TokenKind.KW_PRINT:
            return self._parse_print()
        if tok.kind is TokenKind.ATH:
            return self._parse_ath_loop()
        if tok.kind is TokenKind.IDENT:
            return self._parse_die()
        if tok.kind is TokenKind.RESERVED:
            raise ParseError(
                f"'{tok.value}' is reserved for v1+ and not implemented in v0",
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
        name = self._expect(TokenKind.IDENT)
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.SEMI)
        return ImportStmt(name=name.value, var=var.value, line=kw.line, col=kw.col)

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

    def _parse_ath_loop(self) -> AthLoop:
        kw = self._expect(TokenKind.ATH)
        self._expect(TokenKind.LPAREN)
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
        return AthLoop(var=var.value, body=body, line=kw.line, col=kw.col)

    def _parse_die(self) -> DieStmt:
        var = self._expect(TokenKind.IDENT)
        self._expect(TokenKind.DIE)
        self._expect(TokenKind.LPAREN)
        self._expect(TokenKind.RPAREN)
        self._expect(TokenKind.SEMI)
        return DieStmt(var=var.value, line=var.line, col=var.col)


def parse(src: str) -> Program:
    return Parser(tokenize(src)).parse_program()
