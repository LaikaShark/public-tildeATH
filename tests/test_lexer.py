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


@pytest.mark.parametrize("spelling", ["importf", "IMPORTF", "ImportF"])
def test_reserved_v1_words_become_reserved_tokens(spelling):
    toks = tokenize(spelling)
    assert toks[0].kind is TokenKind.RESERVED
    assert toks[0].value == spelling


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
    toks = tokenize("print ;")
    assert toks[1].kind is TokenKind.RAWTEXT
    assert toks[1].value == ""


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
    sample = Path(__file__).parent / "conformance" / "looptest.ath"
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
