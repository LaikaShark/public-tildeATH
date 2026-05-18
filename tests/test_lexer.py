from pathlib import Path

import pytest

from athc.lexer import LexError, TokenKind, tokenize


def kinds(src):
    return [t.kind for t in tokenize(src) if t.kind is not TokenKind.EOF]


def values(src):
    return [t.value for t in tokenize(src) if t.kind is not TokenKind.EOF]


def test_empty_source_emits_only_eof():
    toks = tokenize("")
    assert len(toks) == 1
    assert toks[0].kind is TokenKind.EOF


def test_whitespace_and_comments_are_skipped():
    src = "  // line comment\n  /* block\n   spans lines */  \n"
    assert kinds(src) == []


def test_block_comments_do_not_nest():
    # The first */ closes the comment regardless of any /* before it.
    # After the close, only the identifier 'c' remains.
    toks = tokenize("/* outer /* inner */ c")
    assert [t.value for t in toks if t.kind is not TokenKind.EOF] == ["c"]


def test_unterminated_block_comment_errors():
    with pytest.raises(LexError):
        tokenize("/* never closed")


def test_int_literal_positive():
    toks = tokenize("42")
    assert toks[0].kind is TokenKind.INT
    assert toks[0].value == "42"


def test_int_literal_negative():
    toks = tokenize("-7")
    assert toks[0].kind is TokenKind.INT
    assert toks[0].value == "-7"


def test_int_literal_max_int64():
    toks = tokenize("9223372036854775807")
    assert toks[0].kind is TokenKind.INT


def test_int_literal_overflow_rejected():
    with pytest.raises(LexError, match="signed 64-bit range"):
        tokenize("99999999999999999999")


def test_int_literal_negative_overflow_rejected():
    with pytest.raises(LexError, match="signed 64-bit range"):
        tokenize("-99999999999999999999")


def test_angle_brackets_are_punct():
    toks = tokenize("<add>")
    assert toks[0].kind is TokenKind.LANGLE
    assert toks[1].kind is TokenKind.IDENT
    assert toks[1].value == "add"
    assert toks[2].kind is TokenKind.RANGLE


def test_lone_minus_without_digits_errors():
    # `-foo` is not a valid token; bare `-` outside a number has no meaning.
    with pytest.raises(LexError):
        tokenize("-foo")


def test_dotdot_token():
    toks = tokenize("..")
    assert toks[0].kind is TokenKind.DOTDOT
    assert toks[0].value == ".."


def test_single_dot_starts_die():
    toks = tokenize(".DIE")
    assert toks[0].kind is TokenKind.DIE


def test_dotdot_in_range_subscript():
    toks = tokenize("S[I..J]X;")
    # We just want to confirm a DOTDOT appears between the two idents.
    kinds_only = [t.kind for t in toks if t.kind is not TokenKind.EOF]
    assert TokenKind.DOTDOT in kinds_only
    dotdot_idx = kinds_only.index(TokenKind.DOTDOT)
    assert kinds_only[dotdot_idx - 1] is TokenKind.IDENT
    assert kinds_only[dotdot_idx + 1] is TokenKind.IDENT


def test_punctuation():
    assert kinds("(){}[],;") == [
        TokenKind.LPAREN,
        TokenKind.RPAREN,
        TokenKind.LBRACE,
        TokenKind.RBRACE,
        TokenKind.LBRACKET,
        TokenKind.RBRACKET,
        TokenKind.COMMA,
        TokenKind.SEMI,
    ]


def test_identifiers_are_case_sensitive():
    toks = [t for t in tokenize("Foo foo FOO _bar baz1") if t.kind is not TokenKind.EOF]
    assert all(t.kind is TokenKind.IDENT for t in toks)
    assert [t.value for t in toks] == ["Foo", "foo", "FOO", "_bar", "baz1"]


@pytest.mark.parametrize("spelling", ["import", "IMPORT", "Import", "ImPoRt"])
def test_keyword_import_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.KW_IMPORT
    assert toks[0].value == spelling


@pytest.mark.parametrize("spelling", ["BIFURCATE", "bifurcate", "Bifurcate"])
def test_keyword_bifurcate_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.KW_BIFURCATE


@pytest.mark.parametrize("spelling", ["~ATH", "~ath", "~Ath", "~aTh"])
def test_ath_loopstart_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.ATH


@pytest.mark.parametrize("spelling", [".DIE", ".die", ".Die"])
def test_die_method_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.DIE


@pytest.mark.parametrize("spelling", ["INPUT", "input", "Input"])
def test_keyword_input_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.KW_INPUT
    assert toks[0].value == spelling


@pytest.mark.parametrize("spelling", ["PRINT2", "print2", "Print2"])
def test_keyword_print2_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.KW_PRINT2
    assert toks[0].value == spelling


@pytest.mark.parametrize("spelling", ["importf", "IMPORTF", "ImportF"])
def test_keyword_importf_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.KW_IMPORTF


@pytest.mark.parametrize("spelling", ["as", "AS", "As"])
def test_keyword_as_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.KW_AS


def test_string_literal_basic():
    toks = tokenize('"hello"')
    assert toks[0].kind is TokenKind.STRING
    assert toks[0].value == "hello"


def test_string_literal_empty():
    toks = tokenize('""')
    assert toks[0].kind is TokenKind.STRING
    assert toks[0].value == ""


def test_string_literal_with_special_chars():
    toks = tokenize('"path/to/file.ath"')
    assert toks[0].value == "path/to/file.ath"


def test_string_literal_can_span_newlines():
    toks = tokenize('"first\nsecond"')
    assert toks[0].value == "first\nsecond"


def test_string_literal_unterminated_errors():
    with pytest.raises(LexError, match="unterminated string"):
        tokenize('"never closed')


def test_string_literal_escape_quote_and_backslash():
    toks = tokenize(r'"he said \"hi\" \\done"')
    assert toks[0].kind is TokenKind.STRING
    assert toks[0].value == 'he said "hi" \\done'


def test_string_literal_escape_newline_tab_cr():
    toks = tokenize(r'"line\nrow\tcol\rend"')
    assert toks[0].value == "line\nrow\tcol\rend"


def test_string_literal_unknown_escape_errors():
    with pytest.raises(LexError, match=r"unknown escape sequence '\\x'"):
        tokenize(r'"hello\xworld"')


def test_string_literal_trailing_backslash_errors():
    with pytest.raises(LexError, match="trailing backslash"):
        tokenize('"oops\\')


@pytest.mark.parametrize("spelling", ["EXECUTE", "execute", "Execute"])
def test_keyword_execute_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.KW_EXECUTE


def test_bang_is_a_token():
    toks = tokenize("!V")
    assert toks[0].kind is TokenKind.BANG
    assert toks[1].kind is TokenKind.IDENT
    assert toks[1].value == "V"


@pytest.mark.parametrize("spelling", ["watch", "WATCH", "Watch"])
def test_keyword_watch_is_case_insensitive(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.KW_WATCH


def test_printer_is_identifier_not_print_keyword():
    toks = tokenize("printer importer importfoo")
    assert all(t.kind is TokenKind.IDENT for t in toks[:3])
    assert [t.value for t in toks[:3]] == ["printer", "importer", "importfoo"]


def test_print_payload_basic():
    toks = tokenize("print hello world;")
    assert [t.kind for t in toks[:3]] == [
        TokenKind.KW_PRINT,
        TokenKind.RAWTEXT,
        TokenKind.SEMI,
    ]
    assert toks[1].value == "hello world"


def test_print_payload_can_be_empty():
    # An empty payload emits no literal part — just the keyword and ';'.
    toks = tokenize("print ;")
    assert toks[0].kind is TokenKind.KW_PRINT
    assert toks[1].kind is TokenKind.SEMI
    assert not any(t.kind is TokenKind.RAWTEXT for t in toks)


def test_print_payload_keeps_punctuation_verbatim():
    toks = tokenize("print {[, ~stuff~ /*not a comment*/;")
    assert toks[1].kind is TokenKind.RAWTEXT
    assert toks[1].value == "{[, ~stuff~ /*not a comment*/"


def test_print_payload_can_span_newlines():
    toks = tokenize("print line one\nline two;")
    assert toks[1].value == "line one\nline two"


def test_print_without_space_after_keyword_errors():
    with pytest.raises(LexError):
        tokenize("print;")


def test_print_payload_escapes_semi_and_backslash():
    toks = tokenize(r"print a\;b\\c;")
    assert toks[1].kind is TokenKind.RAWTEXT
    assert toks[1].value == "a;b\\c"


def test_print_payload_escapes_newline_tab_cr():
    toks = tokenize(r"print line\nrow\tcol\rend;")
    assert toks[1].value == "line\nrow\tcol\rend"


def test_print_payload_unknown_escape_errors():
    with pytest.raises(LexError, match=r"unknown escape sequence '\\x'"):
        tokenize(r"print bad\xthing;")


def test_print_payload_trailing_backslash_errors():
    with pytest.raises(LexError, match="trailing backslash"):
        tokenize("print oops\\")


def test_print_payload_interpolation_splits_into_parts():
    toks = tokenize("print Hello $W done;")
    # RAWTEXT("Hello ") PRINTVAR(W) RAWTEXT(" done") SEMI
    kinds = [t.kind for t in toks[1:-1]]
    assert kinds == [
        TokenKind.RAWTEXT,
        TokenKind.PRINTVAR,
        TokenKind.RAWTEXT,
        TokenKind.SEMI,
    ]
    assert toks[1].value == "Hello "
    assert toks[2].value == "W"
    assert toks[3].value == " done"


def test_print_payload_adjacent_interpolations():
    toks = tokenize("print $a$b;")
    parts = [t for t in toks if t.kind is TokenKind.PRINTVAR]
    assert [t.value for t in parts] == ["a", "b"]
    # No empty literal run between adjacent vars.
    assert not any(t.kind is TokenKind.RAWTEXT for t in toks)


def test_print_payload_interpolation_stops_at_nonident():
    # `$W.` — the name is just W; the '.' is literal text.
    toks = tokenize("print $W.x;")
    assert toks[1].kind is TokenKind.PRINTVAR
    assert toks[1].value == "W"
    assert toks[2].kind is TokenKind.RAWTEXT
    assert toks[2].value == ".x"


def test_print_payload_escaped_dollar_is_literal():
    toks = tokenize(r"print cost \$5 and \$N;")
    assert not any(t.kind is TokenKind.PRINTVAR for t in toks)
    assert toks[1].kind is TokenKind.RAWTEXT
    assert toks[1].value == "cost $5 and $N"


def test_print_payload_bare_dollar_errors():
    with pytest.raises(LexError, match=r"literal '\$' must be written"):
        tokenize("print cost is $ 5;")


def test_print_payload_dollar_before_semicolon_errors():
    with pytest.raises(LexError, match=r"literal '\$' must be written"):
        tokenize("print trailing $;")


def test_print_without_semicolon_errors():
    with pytest.raises(LexError):
        tokenize("print no terminator")


def test_unexpected_character_errors():
    with pytest.raises(LexError):
        tokenize("@")


def test_tilde_without_ath_errors():
    with pytest.raises(LexError):
        tokenize("~foo")


def test_dot_without_die_errors():
    with pytest.raises(LexError):
        tokenize(".foo")


def test_token_positions_are_one_based():
    toks = tokenize("import\n  foo bar;")
    assert (toks[0].line, toks[0].col) == (1, 1)
    assert (toks[1].line, toks[1].col) == (2, 3)
    assert (toks[2].line, toks[2].col) == (2, 7)
    assert (toks[3].line, toks[3].col) == (2, 10)


def test_looptest_sample_tokenizes():
    sample = Path(__file__).resolve().parent.parent / "examples" / "looptest.ath"
    src = sample.read_text()
    toks = tokenize(src)

    assert toks[-1].kind is TokenKind.EOF
    assert toks[0].kind is TokenKind.KW_IMPORT
    assert (toks[1].kind, toks[1].value) == (TokenKind.IDENT, "blah")
    assert (toks[2].kind, toks[2].value) == (TokenKind.IDENT, "A")
    assert toks[3].kind is TokenKind.SEMI

    ath_tokens = [t for t in toks if t.kind is TokenKind.ATH]
    assert len(ath_tokens) == 1

    die_tokens = [t for t in toks if t.kind is TokenKind.DIE]
    assert len(die_tokens) == 2

    raw_payloads = [t.value for t in toks if t.kind is TokenKind.RAWTEXT]
    assert raw_payloads == ["APPLE", "ORANGE"]
