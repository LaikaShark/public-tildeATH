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
    EveryStmt,
    LoopStmt,
    DieStmt,
    FuncCallComposeArg,
    FuncCallDecomposeRet,
    ImportBuiltinStmt,
    ImportFuncStmt,
    ImportNumberStmt,
    ImportStmt,
    InputStmt,
    Operand,
    PrintStmt,
    Program,
    ReadStmt,
    SleepStmt,
    SliceStmt,
    SubscriptStmt,
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
    assert len(s.parts) == 1
    assert s.parts[0].kind == "lit"
    assert s.parts[0].value == "hello world"


def test_print_statement_with_interpolation():
    p = parse("print Hi $NAME, count $N!;")
    s = p.statements[0]
    assert isinstance(s, PrintStmt)
    shape = [(part.kind, part.value) for part in s.parts]
    assert shape == [
        ("lit", "Hi "),
        ("var", "NAME"),
        ("lit", ", count "),
        ("var", "N"),
        ("lit", "!"),
    ]


def test_print_statement_empty_has_no_parts():
    p = parse("print ;")
    s = p.statements[0]
    assert isinstance(s, PrintStmt)
    assert s.parts == []


def test_input_statement():
    p = parse("INPUT line;")
    s = p.statements[0]
    assert isinstance(s, InputStmt)
    assert s.var == "line"


def test_print_single_interpolation_statement():
    p = parse("print $line;")
    s = p.statements[0]
    assert isinstance(s, PrintStmt)
    assert len(s.parts) == 1
    assert s.parts[0].kind == "var"
    assert s.parts[0].value == "line"


def test_input_then_print_interpolation():
    p = parse("input X; print $X;")
    assert isinstance(p.statements[0], InputStmt)
    assert isinstance(p.statements[1], PrintStmt)
    assert p.statements[1].parts[0].kind == "var"


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


def test_repeat_loop():
    p = parse("loop N { print x; print y; }")
    s = p.statements[0]
    assert isinstance(s, LoopStmt)
    assert s.count_var == "N"
    assert len(s.body) == 2
    assert isinstance(s.body[0], PrintStmt)


def test_every_loop():
    p = parse("every MS { print beat; }")
    s = p.statements[0]
    assert isinstance(s, EveryStmt)
    assert s.interval_var == "MS"
    assert len(s.body) == 1


def test_repeat_every_nest():
    p = parse("loop N { every MS { print x; } }")
    outer = p.statements[0]
    assert isinstance(outer, LoopStmt)
    assert isinstance(outer.body[0], EveryStmt)


def test_ath_loop_with_execute_suffix():
    p = parse("~ATH(V) { print x; } EXECUTE(NULL);")
    s = p.statements[0]
    assert isinstance(s, AthLoop)
    assert s.var == "V"
    assert s.execute == "NULL"


def test_ath_loop_inversion_plus_execute():
    p = parse("~ATH(!V) { } EXECUTE(F);")
    s = p.statements[0]
    assert isinstance(s, AthLoop)
    assert s.inverted is True
    assert s.execute == "F"


def test_ath_loop_without_execute_has_none():
    p = parse("~ATH(V) { print x; }")
    assert p.statements[0].execute is None


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
    with pytest.raises(ParseError, match="expected a string literal"):
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
    # case-insensitive resolution happens later
    assert s.name == "add"


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


def test_watch_pid_form():
    p = parse("watch pid P as V;")
    s = p.statements[0]
    assert isinstance(s, WatchStmt)
    assert s.pid_var == "P"
    assert s.path is None and s.signal_name is None and s.mtime_path is None
    assert s.var == "V"


def test_watch_mtime_form():
    p = parse('watch mtime "config.toml" as V;')
    s = p.statements[0]
    assert isinstance(s, WatchStmt)
    assert s.mtime_path == "config.toml"
    assert s.path is None and s.signal_name is None and s.pid_var is None
    assert s.var == "V"


def test_watch_pid_mtime_keywords_are_contextual():
    # 'pid' and 'mtime' are ordinary identifiers outside the watch slot
    p = parse("import number 1 as pid; watch pid pid as V;")
    s = p.statements[1]
    assert isinstance(s, WatchStmt)
    assert s.pid_var == "pid" and s.var == "V"


def test_watch_rejects_bare_identifier_after_watch():
    # foo is none of STRING / 'signal' / 'pid' / 'mtime' -> parse error
    with pytest.raises(ParseError, match="after 'watch'"):
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


def test_import_number_requires_number_literal():
    with pytest.raises(ParseError, match="expected a number literal"):
        parse("import number foo as N;")


def test_import_number_accepts_float():
    p = parse("import number 3.14 as F;")
    s = p.statements[0]
    assert isinstance(s, ImportNumberStmt)
    assert s.is_float is True
    assert s.value == 3.14


def test_import_number_int_is_not_float():
    p = parse("import number 5 as N;")
    s = p.statements[0]
    assert isinstance(s, ImportNumberStmt)
    assert s.is_float is False
    assert s.value == 5


def test_import_number_over_int64_is_bignum():
    p = parse("import number 99999999999999999999 as BIG;")
    s = p.statements[0]
    assert isinstance(s, ImportNumberStmt)
    assert s.is_big is True
    assert s.is_float is False
    # decimal string, parsed at runtime
    assert s.value == "99999999999999999999"


def test_import_with_metadata_word_builtin_is_not_a_marker():
    # 'builtin' not the second token after import is an ordinary identifier; this is a 3-word concept form
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
    with pytest.raises(ParseError, match=r"string literal or '<'"):
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


# --- inline literal operands (§4.4): a read-operand may be a name or a literal ---

def test_compose_arg_int_literal_operand():
    p = parse("ADD [A, 1] NEXT_A;")
    s = p.statements[0]
    assert isinstance(s, FuncCallComposeArg)
    assert s.left == "A"  # an identifier stays a bare string
    assert isinstance(s.right, Operand)
    assert s.right.kind == "int" and s.right.value == 1
    assert s.target == "NEXT_A"


def test_compose_arg_string_literal_operand():
    p = parse('REPEAT ["string", N3] REP;')
    s = p.statements[0]
    assert isinstance(s, FuncCallComposeArg)
    assert isinstance(s.left, Operand)
    assert s.left.kind == "string" and s.left.value == "string"
    assert s.right == "N3"


def test_compose_arg_float_and_bignum_literals():
    p = parse("ADD [1.5, 99999999999999999999] R;")
    s = p.statements[0]
    assert isinstance(s.left, Operand)
    assert s.left.kind == "float" and s.left.value == 1.5
    assert isinstance(s.right, Operand)
    assert s.right.kind == "bignum" and s.right.value == "99999999999999999999"


def test_subscript_literal_index():
    p = parse("S[0] X;")
    s = p.statements[0]
    assert isinstance(s, SubscriptStmt)
    assert s.source == "S"  # the source stays an identifier
    assert isinstance(s.index, Operand)
    assert s.index.kind == "int" and s.index.value == 0


def test_slice_literal_bounds():
    p = parse("S[0..2] X;")
    s = p.statements[0]
    assert isinstance(s, SliceStmt)
    assert isinstance(s.start, Operand) and s.start.value == 0
    assert isinstance(s.end, Operand) and s.end.value == 2


def test_decompose_ret_literal_arg():
    # arg is a read-operand (literal ok); left/right remain write-target names
    p = parse('SPLIT "ab" [A, B];')
    s = p.statements[0]
    assert isinstance(s, FuncCallDecomposeRet)
    assert isinstance(s.arg, Operand)
    assert s.arg.kind == "string" and s.arg.value == "ab"
    assert s.left == "A" and s.right == "B"


def test_bracket_form_dispatch_funcall_compose_arg():
    # comma between bracket contents -> funcall, not subscript
    p = parse("F [L, R] V;")
    assert isinstance(p.statements[0], FuncCallComposeArg)


def test_subscript_distinct_from_funcall():
    # no comma -> subscript
    p = parse("F [L] V;")
    assert isinstance(p.statements[0], SubscriptStmt)


def test_bracket_form_rejects_unknown_separator():
    # two consecutive idents: no COMMA, DOTDOT, nor RBRACKET after the first
    with pytest.raises(ParseError, match=r"',', '\.\.', or '\]'"):
        parse("S [I J K] X;")


def test_dot_alone_still_lexes_DIE():
    # don't break .DIE by treating its leading '.' as start of '..'
    p = parse("import x A; A.DIE();")
    assert isinstance(p.statements[1], DieStmt)


def test_looptest_sample_parses():
    sample = Path(__file__).resolve().parent.parent / "examples" / "control_flow" / "looptest.ath"
    p = parse(sample.read_text())

    # 1 import + 12 bifurcate + 1 peel bifurcate + 1 ath_loop + 1 die(THIS) = 16
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
    assert loop.body[2].parts[0].value == "APPLE"
    assert isinstance(loop.body[3], PrintStmt)
    assert loop.body[3].parts[0].value == "ORANGE"


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
    # 'else' is a keyword, can't be a variable name
    with pytest.raises(ParseError):
        parse("import x else;")


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
        # raw INT not allowed; must be a bound name
        parse("sleep 100;")


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
    # 'to' must still be usable as an ordinary identifier name
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


def test_text_statement_primitive():
    from athc.ast import TextStmt
    p = parse('text "hello" as S;')
    s = p.statements[0]
    assert isinstance(s, TextStmt)
    assert s.target == "S"
    assert len(s.parts) == 1
    assert s.parts[0].kind == "str"
    assert s.parts[0].value == "hello"


def test_text_statement_interpolation():
    from athc.ast import TextStmt
    p = parse('import x N; text "value: " N " end" as MSG;')
    s = p.statements[1]
    assert isinstance(s, TextStmt)
    assert s.target == "MSG"
    assert [(p.kind, p.value) for p in s.parts] == [
        ("str", "value: "),
        ("ident", "N"),
        ("str", " end"),
    ]


def test_text_statement_no_parts_errors():
    with pytest.raises(Exception, match="requires at least one part"):
        parse("text as M;")


def test_text_statement_case_insensitive():
    from athc.ast import TextStmt
    p = parse('TEXT "hi" as M;')
    assert isinstance(p.statements[0], TextStmt)


# --- concurrency statements ---------------------------------------------------


def test_spawn_statement():
    p = parse('importf <w> as W; spawn W ARGV as A;')
    s = p.statements[1]
    assert isinstance(s, SpawnStmt)
    assert s.name == "W"
    assert s.arg == "ARGV"
    assert s.into is None
    assert s.target == "A"


def test_spawn_with_literal_arg():
    p = parse('importf <w> as W; spawn W 5 as A;')
    s = p.statements[1]
    assert isinstance(s, SpawnStmt)
    assert isinstance(s.arg, Operand)
    assert s.arg.kind == "int" and s.arg.value == 5


def test_spawn_into_nursery():
    p = parse('importf <w> as W; nursery as N; spawn W ARGV into N as A;')
    s = p.statements[2]
    assert isinstance(s, SpawnStmt)
    assert s.into == "N"
    assert s.target == "A"


def test_send_statement():
    p = parse('channel as C; send M to C;')
    s = p.statements[1]
    assert isinstance(s, SendStmt)
    assert s.message == "M"
    assert s.dest == "C"


def test_send_literal_to_channel():
    p = parse('channel as C; send 42 to C;')
    s = p.statements[1]
    assert isinstance(s, SendStmt)
    assert isinstance(s.message, Operand)
    assert s.message.value == 42


def test_send_requires_to_marker():
    with pytest.raises(ParseError, match="'to'"):
        parse('channel as C; send M C;')


def test_recv_own_mailbox():
    p = parse('recv as M;')
    s = p.statements[0]
    assert isinstance(s, RecvStmt)
    assert s.source is None
    assert s.target == "M"


def test_recv_from_channel():
    p = parse('channel as C; recv from C as M;')
    s = p.statements[1]
    assert isinstance(s, RecvStmt)
    assert s.source == "C"
    assert s.target == "M"


def test_yield_statement():
    p = parse('yield;')
    assert isinstance(p.statements[0], YieldStmt)


def test_join_is_soft_keyword_statement():
    p = parse('channel as C; join C;')
    s = p.statements[1]
    assert isinstance(s, JoinStmt)
    assert s.handle == "C"


def test_join_still_usable_as_function_name():
    # `join` is a stdlib function: `importf <join> as JOIN; JOIN [L, S] R;` must still parse
    # as a function call, not the JOIN statement (those need brackets).
    p = parse('importf <join> as JOIN; import x L; import x S; JOIN [L, S] R;')
    s = p.statements[-1]
    assert isinstance(s, FuncCallComposeArg)
    assert s.name == "JOIN"
    assert s.target == "R"


def test_channel_statement():
    p = parse('channel as C;')
    s = p.statements[0]
    assert isinstance(s, ChannelStmt)
    assert s.target == "C"


def test_nursery_statement():
    p = parse('nursery as N;')
    s = p.statements[0]
    assert isinstance(s, NurseryStmt)
    assert s.target == "N"


def test_concurrency_keywords_case_insensitive():
    p = parse('importf <w> as W; CHANNEL as C; NURSERY as N; SPAWN W C INTO N as A; '
              'SEND 1 TO C; RECV FROM C as M; YIELD;')
    kinds = [type(s).__name__ for s in p.statements]
    assert kinds == ["ImportFuncStmt", "ChannelStmt", "NurseryStmt", "SpawnStmt",
                     "SendStmt", "RecvStmt", "YieldStmt"]
