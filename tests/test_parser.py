from pathlib import Path

import pytest

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
)
from athc.parser import ParseError, parse


def test_empty_program():
    p = parse("")
    assert isinstance(p, Program)
    assert p.statements == []


def test_import_statement():
    p = parse("import flavor A;")
    assert len(p.statements) == 1
    s = p.statements[0]
    assert isinstance(s, ImportStmt)
    assert s.name == "flavor"
    assert s.var == "A"


def test_import_multi_word_metadata():
    p = parse("import dead grandmother G;")
    s = p.statements[0]
    assert isinstance(s, ImportStmt)
    assert s.name == "dead grandmother"
    assert s.var == "G"


def test_import_with_only_var_rejected():
    with pytest.raises(ParseError, match="metadata word"):
        parse("import G;")


def test_import_with_no_idents_rejected():
    with pytest.raises(ParseError, match="metadata word"):
        parse("import ;")


def test_decompose_statement():
    p = parse("BIFURCATE V[L,R];")
    s = p.statements[0]
    assert isinstance(s, DecomposeStmt)
    assert s.source == "V"
    assert s.left == "L"
    assert s.right == "R"


def test_compose_statement():
    p = parse("BIFURCATE [L,R]V;")
    s = p.statements[0]
    assert isinstance(s, ComposeStmt)
    assert s.left == "L"
    assert s.right == "R"
    assert s.target == "V"


def test_die_statement():
    p = parse("THIS.DIE();")
    s = p.statements[0]
    assert isinstance(s, DieStmt)
    assert s.var == "THIS"


def test_print_statement():
    p = parse("print hello world;")
    s = p.statements[0]
    assert isinstance(s, PrintStmt)
    assert s.text == "hello world"


def test_input_statement():
    p = parse("INPUT line;")
    s = p.statements[0]
    assert isinstance(s, InputStmt)
    assert s.var == "line"


def test_print2_statement():
    p = parse("PRINT2 line;")
    s = p.statements[0]
    assert isinstance(s, Print2Stmt)
    assert s.var == "line"


def test_input_print2_case_insensitive():
    p = parse("input X; print2 X;")
    assert isinstance(p.statements[0], InputStmt)
    assert isinstance(p.statements[1], Print2Stmt)


def test_ath_loop_with_body():
    p = parse("~ATH(V) { print inside; V.DIE(); }")
    s = p.statements[0]
    assert isinstance(s, AthLoop)
    assert s.var == "V"
    assert s.inverted is False
    assert len(s.body) == 2
    assert isinstance(s.body[0], PrintStmt)
    assert isinstance(s.body[1], DieStmt)


def test_ath_loop_with_inversion():
    p = parse("~ATH(!V) { print inverted; }")
    s = p.statements[0]
    assert isinstance(s, AthLoop)
    assert s.var == "V"
    assert s.inverted is True


def test_ath_loop_with_execute_suffix():
    p = parse("~ATH(V) { print x; } EXECUTE(NULL);")
    s = p.statements[0]
    assert isinstance(s, AthLoop)
    assert s.var == "V"


def test_ath_loop_inversion_plus_execute():
    p = parse("~ATH(!V) { } EXECUTE(F);")
    s = p.statements[0]
    assert isinstance(s, AthLoop)
    assert s.inverted is True


def test_ath_loop_empty_body():
    p = parse("~ATH(V) {}")
    s = p.statements[0]
    assert isinstance(s, AthLoop)
    assert s.body == []


def test_nested_ath_loops():
    p = parse("~ATH(A) { ~ATH(B) { print x; } }")
    outer = p.statements[0]
    assert isinstance(outer, AthLoop)
    inner = outer.body[0]
    assert isinstance(inner, AthLoop)
    assert inner.var == "B"
    assert isinstance(inner.body[0], PrintStmt)


def test_keywords_case_insensitive_through_parser():
    p = parse("Import x A; bifurcate A[L,R]; ~ath(L) {} l.die();")
    assert isinstance(p.statements[0], ImportStmt)
    assert isinstance(p.statements[1], DecomposeStmt)
    assert isinstance(p.statements[2], AthLoop)
    assert isinstance(p.statements[3], DieStmt)


def test_identifiers_case_sensitive_through_parser():
    p = parse("import x Foo; import y foo;")
    assert p.statements[0].var == "Foo"
    assert p.statements[1].var == "foo"


def test_importf_requires_string_literal_path():
    with pytest.raises(ParseError, match="expected STRING"):
        parse("importf foo as bar;")


def test_importf_statement():
    p = parse('importf "lib/add.ath" as ADD;')
    s = p.statements[0]
    assert isinstance(s, ImportFuncStmt)
    assert s.path == "lib/add.ath"
    assert s.name == "ADD"


def test_die_with_no_arg():
    p = parse("V.DIE();")
    s = p.statements[0]
    assert isinstance(s, DieStmt)
    assert s.var == "V"
    assert s.arg is None


def test_die_with_arg():
    p = parse("THIS.DIE(R);")
    s = p.statements[0]
    assert isinstance(s, DieStmt)
    assert s.var == "THIS"
    assert s.arg == "R"


def test_funcall_compose_arg_form():
    p = parse("ADD [X, Y] R;")
    s = p.statements[0]
    assert isinstance(s, FuncCallComposeArg)
    assert s.name == "ADD"
    assert s.left == "X"
    assert s.right == "Y"
    assert s.target == "R"


def test_funcall_decompose_ret_form():
    p = parse("SPLIT V [A, B];")
    s = p.statements[0]
    assert isinstance(s, FuncCallDecomposeRet)
    assert s.name == "SPLIT"
    assert s.arg == "V"
    assert s.left == "A"
    assert s.right == "B"


def test_funcall_with_lowercase_name_parses():
    p = parse("add [X, Y] R;")
    s = p.statements[0]
    assert isinstance(s, FuncCallComposeArg)
    assert s.name == "add"  # case-insensitive resolution happens later


def test_missing_semicolon():
    with pytest.raises(ParseError):
        parse("import x A")


def test_bifurcate_with_garbage_after_keyword():
    with pytest.raises(ParseError, match="after BIFURCATE"):
        parse("BIFURCATE ;")


def test_unclosed_ath_loop():
    with pytest.raises(ParseError, match="inside ~ATH loop"):
        parse("~ATH(V) { print x;")


def test_unexpected_top_level_punctuation():
    with pytest.raises(ParseError):
        parse(";")


def test_positions_propagate_to_ast():
    p = parse("import x A;\n  BIFURCATE A[L,R];")
    assert (p.statements[0].line, p.statements[0].col) == (1, 1)
    assert (p.statements[1].line, p.statements[1].col) == (2, 3)


def test_looptest_sample_parses():
    sample = Path(__file__).parent / "conformance" / "looptest.ath"
    p = parse(sample.read_text())

    # Counts:
    # 1 import + 12 bifurcate (outside loop) + 1 bifurcate (peel preceding loop)
    # + 1 ath_loop + 1 die(THIS) = 16 top-level statements
    assert len(p.statements) == 16
    assert isinstance(p.statements[0], ImportStmt)
    assert p.statements[0].var == "A"
    assert isinstance(p.statements[-1], DieStmt)
    assert p.statements[-1].var == "THIS"

    loop = next(s for s in p.statements if isinstance(s, AthLoop))
    assert loop.var == "Z"
    assert len(loop.body) == 4
    assert isinstance(loop.body[0], DecomposeStmt)
    assert isinstance(loop.body[1], DieStmt)
    assert isinstance(loop.body[2], PrintStmt)
    assert loop.body[2].text == "APPLE"
    assert isinstance(loop.body[3], PrintStmt)
    assert loop.body[3].text == "ORANGE"
