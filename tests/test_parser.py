from pathlib import Path

import pytest

from athc.ast import (
    AppendStmt,
    AthLoop,
    BranchStmt,
    CloneStmt,
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
    TimerStmt,
    WatchStmt,
    WriteStmt,
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


def test_watch_file_form():
    p = parse('watch "target.txt" as F;')
    s = p.statements[0]
    assert isinstance(s, WatchStmt)
    assert s.path == "target.txt"
    assert s.signal_name is None
    assert s.var == "F"


def test_watch_signal_form():
    p = parse("watch signal SIGTERM as T;")
    s = p.statements[0]
    assert isinstance(s, WatchStmt)
    assert s.path is None
    assert s.signal_name == "SIGTERM"
    assert s.var == "T"


def test_watch_signal_keyword_is_contextual_and_case_insensitive():
    p = parse("watch SIGNAL SIGTERM as T;")
    assert isinstance(p.statements[0], WatchStmt)
    assert p.statements[0].signal_name == "SIGTERM"


def test_watch_rejects_bare_identifier_after_watch():
    # foo is neither STRING nor 'signal' -> parse error.
    with pytest.raises(ParseError, match="STRING or 'signal'"):
        parse("watch foo as F;")


def test_watch_signal_missing_name_errors():
    with pytest.raises(ParseError):
        parse("watch signal as F;")


def test_watch_requires_as():
    with pytest.raises(ParseError):
        parse('watch "foo.txt" F;')


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


def test_import_builtin_statement():
    p = parse("import builtin ath_add as ADD;")
    s = p.statements[0]
    assert isinstance(s, ImportBuiltinStmt)
    assert s.symbol == "ath_add"
    assert s.name == "ADD"


def test_import_builtin_marker_case_insensitive():
    p = parse("Import Builtin ath_sub as SUB;")
    assert isinstance(p.statements[0], ImportBuiltinStmt)


def test_import_builtin_requires_as():
    with pytest.raises(ParseError):
        parse("import builtin ath_add ADD;")


def test_import_number_statement():
    p = parse("import number 42 as N;")
    s = p.statements[0]
    assert isinstance(s, ImportNumberStmt)
    assert s.value == 42
    assert s.var == "N"


def test_import_number_negative():
    p = parse("import number -7 as N;")
    assert p.statements[0].value == -7


def test_import_number_marker_case_insensitive():
    p = parse("Import NUMBER 1 as O;")
    assert isinstance(p.statements[0], ImportNumberStmt)


def test_import_number_requires_int():
    with pytest.raises(ParseError, match="expected INT"):
        parse("import number foo as N;")


def test_import_with_metadata_word_builtin_is_not_a_marker():
    # When 'builtin' is not the second token after import, it's an ordinary
    # identifier. `import the builtin BUILTIN;` is a 3-word concept form.
    p = parse("import the builtin BUILTIN;")
    s = p.statements[0]
    assert isinstance(s, ImportStmt)
    assert s.name == "the builtin"
    assert s.var == "BUILTIN"


def test_importf_angle_form():
    p = parse("importf <add> as ADD;")
    s = p.statements[0]
    assert isinstance(s, ImportFuncStmt)
    assert s.path == "add"
    assert s.name == "ADD"
    assert s.search_path is True


def test_importf_quoted_form_still_works():
    p = parse('importf "lib/add.ath" as ADD;')
    s = p.statements[0]
    assert isinstance(s, ImportFuncStmt)
    assert s.search_path is False


def test_importf_angle_requires_closing_bracket():
    with pytest.raises(ParseError):
        parse("importf <add as ADD;")


def test_importf_after_keyword_must_be_string_or_angle():
    with pytest.raises(ParseError, match="STRING or '<'"):
        parse("importf foo as F;")


def test_subscript_statement():
    p = parse("S[N] X;")
    s = p.statements[0]
    assert isinstance(s, SubscriptStmt)
    assert s.source == "S" and s.index == "N" and s.target == "X"


def test_slice_statement():
    p = parse("S[I..J] X;")
    s = p.statements[0]
    assert isinstance(s, SliceStmt)
    assert s.source == "S" and s.start == "I" and s.end == "J" and s.target == "X"


def test_bracket_form_dispatch_funcall_compose_arg():
    # Comma between bracket contents → funcall, not subscript.
    p = parse("F [L, R] V;")
    assert isinstance(p.statements[0], FuncCallComposeArg)


def test_subscript_distinct_from_funcall():
    # No comma → subscript.
    p = parse("F [L] V;")
    assert isinstance(p.statements[0], SubscriptStmt)


def test_bracket_form_rejects_unknown_separator():
    # Two consecutive idents inside brackets — neither COMMA, DOTDOT, nor RBRACKET
    # follows the first inner ident.
    with pytest.raises(ParseError, match=r"',', '\.\.', or '\]'"):
        parse("S [I J K] X;")


def test_dot_alone_still_lexes_DIE():
    # Regression: don't accidentally break .DIE by treating its leading
    # '.' as the start of '..'.
    p = parse("import x A; A.DIE();")
    assert isinstance(p.statements[1], DieStmt)


def test_looptest_sample_parses():
    sample = Path(__file__).resolve().parent.parent / "examples" / "looptest.ath"
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


# --- BRANCH / CLONE ---


def test_branch_with_else_keyword():
    p = parse("import x V; BRANCH(V) { print yes; } ELSE { print no; }")
    s = p.statements[1]
    assert isinstance(s, BranchStmt)
    assert s.var == "V" and not s.inverted
    assert len(s.then_body) == 1 and len(s.else_body) == 1


def test_branch_with_bare_second_block():
    p = parse("import x V; BRANCH(V) { print yes; } { print no; }")
    s = p.statements[1]
    assert isinstance(s, BranchStmt)
    assert s.else_body is not None
    assert len(s.else_body) == 1


def test_branch_without_else():
    p = parse("import x V; BRANCH(V) { print yes; }")
    s = p.statements[1]
    assert isinstance(s, BranchStmt)
    assert s.else_body is None


def test_branch_inversion():
    p = parse("import x V; BRANCH(!V) { print dead; }")
    s = p.statements[1]
    assert s.inverted is True


def test_branch_case_insensitive():
    p = parse("import x V; branch(V) { print yes; } else { print no; }")
    s = p.statements[1]
    assert isinstance(s, BranchStmt)
    assert s.else_body is not None


def test_branch_nested():
    p = parse(
        "import x A; import y B;"
        " BRANCH(A) { BRANCH(B) { print both; } }"
    )
    outer = p.statements[2]
    assert isinstance(outer, BranchStmt)
    inner = outer.then_body[0]
    assert isinstance(inner, BranchStmt)
    assert inner.var == "B"


def test_branch_unclosed_then_block():
    with pytest.raises(ParseError, match="end of input"):
        parse("import x V; BRANCH(V) { print never;")


def test_clone_statement():
    p = parse("import x V; CLONE V as W;")
    s = p.statements[1]
    assert isinstance(s, CloneStmt)
    assert s.source == "V" and s.target == "W"


def test_clone_case_insensitive():
    p = parse("import x V; clone V as W;")
    assert isinstance(p.statements[1], CloneStmt)


def test_clone_requires_as():
    with pytest.raises(ParseError):
        parse("import x V; CLONE V W;")


def test_else_alone_is_reserved():
    # 'else' is a keyword, so it can't be used as a variable name.
    with pytest.raises(ParseError):
        parse("import x else;")


# --- sleep / TIMER ---


def test_sleep_statement():
    p = parse("import number 100 as N; sleep N;")
    s = p.statements[1]
    assert isinstance(s, SleepStmt)
    assert s.duration == "N"


def test_sleep_case_insensitive():
    p = parse("import number 100 as N; SLEEP N;")
    assert isinstance(p.statements[1], SleepStmt)


def test_sleep_requires_identifier():
    with pytest.raises(ParseError):
        parse("sleep 100;")  # raw INT not allowed; must be a bound name


def test_timer_statement():
    p = parse("import number 100 as N; TIMER N as T;")
    s = p.statements[1]
    assert isinstance(s, TimerStmt)
    assert s.duration == "N" and s.target == "T"


def test_timer_case_insensitive():
    p = parse("import number 100 as N; timer N as T;")
    assert isinstance(p.statements[1], TimerStmt)


def test_timer_requires_as():
    with pytest.raises(ParseError):
        parse("import number 100 as N; TIMER N T;")


# --- read / write / append / close ---


def test_read_statement():
    p = parse('read "foo.txt" as S;')
    s = p.statements[0]
    assert isinstance(s, ReadStmt)
    assert s.path == "foo.txt" and s.target == "S"


def test_read_case_insensitive():
    p = parse('READ "foo.txt" as S;')
    assert isinstance(p.statements[0], ReadStmt)


def test_write_without_verdict():
    p = parse('import x S; write S to "out.txt";')
    s = p.statements[1]
    assert isinstance(s, WriteStmt)
    assert s.source == "S" and s.path == "out.txt"
    assert s.verdict is None


def test_write_with_verdict():
    p = parse('import x S; write S to "out.txt" as OK;')
    s = p.statements[1]
    assert isinstance(s, WriteStmt)
    assert s.verdict == "OK"


def test_append_with_verdict():
    p = parse('import x S; append S to "log.txt" as OK;')
    s = p.statements[1]
    assert isinstance(s, AppendStmt)
    assert s.source == "S" and s.path == "log.txt" and s.verdict == "OK"


def test_write_requires_to_marker():
    with pytest.raises(ParseError, match="'to'"):
        parse('import x S; write S "out.txt";')


def test_to_is_contextual_not_reserved():
    # 'to' must still be usable as an ordinary identifier name.
    p = parse("import x to;")
    s = p.statements[0]
    assert isinstance(s, ImportStmt)
    assert s.var == "to"


def test_close_statement():
    p = parse("import x S; close S;")
    s = p.statements[1]
    assert isinstance(s, CloseStmt)
    assert s.target == "S"


def test_close_case_insensitive():
    p = parse("import x S; CLOSE S;")
    assert isinstance(p.statements[1], CloseStmt)
