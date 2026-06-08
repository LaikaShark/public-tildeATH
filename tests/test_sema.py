from pathlib import Path

import pytest

from athc.parser import parse
from athc.sema import SemaError, analyze


def check(src: str) -> None:
    analyze(parse(src))


def test_empty_program_passes():
    check("")


def test_THIS_is_predefined():
    check("THIS.DIE();")


def test_NULL_readable_as_compose_operand():
    check("import x A; BIFURCATE [NULL, A]B;")


def test_NULL_readable_as_decompose_source():
    check("BIFURCATE NULL[L, R];")


def test_NULL_readable_in_ath_loop_header():
    check("~ATH(NULL) { print never; }")


def test_unbound_read_in_die_errors():
    with pytest.raises(SemaError, match="X.*not in scope"):
        check("X.DIE();")


def test_unbound_read_in_ath_errors():
    with pytest.raises(SemaError, match="X.*not in scope"):
        check("~ATH(X) {}")


def test_unbound_read_in_decompose_source_errors():
    with pytest.raises(SemaError, match="V.*not in scope"):
        check("BIFURCATE V[L, R];")


def test_unbound_read_in_compose_operand_errors():
    with pytest.raises(SemaError, match="L.*not in scope"):
        check("BIFURCATE [L, R]V;")


def test_literal_subscript_index_needs_no_binding():
    # an inline literal operand is self-contained: no prior IMPORT NUMBER
    check("import x S; S[2] X;")


def test_literal_slice_bounds_need_no_binding():
    check("import x S; S[0..3] Y;")


def test_literal_index_does_not_mask_unbound_source():
    # the literal index is fine, but the source S is still checked
    with pytest.raises(SemaError, match="S.*not in scope"):
        check("S[0] X;")


def test_lowercase_this_is_unbound():
    with pytest.raises(SemaError, match="'this'.*not in scope"):
        check("this.DIE();")


def test_import_introduces_variable():
    check("import x V; V.DIE();")


def test_decompose_outputs_become_in_scope():
    check("import x V; BIFURCATE V[L, R]; L.DIE(); R.DIE();")


def test_compose_target_becomes_in_scope():
    check("import x A; import y B; BIFURCATE [A, B]C; C.DIE();")


def test_loop_var_must_precede_loop():
    with pytest.raises(SemaError, match="V.*not in scope"):
        check("~ATH(V) { import x V; }")


def test_loop_body_introductions_visible_after_loop_flat_scope():
    check("import x V; ~ATH(V) { import y W; V.DIE(); } W.DIE();")


def test_nested_loop_reads_outer_var():
    check("import x V; import y W; ~ATH(V) { ~ATH(W) { } }")


def test_write_to_NULL_via_import_rejected():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("import x NULL;")


def test_write_to_NULL_via_decompose_left_rejected():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("import x V; BIFURCATE V[NULL, X];")


def test_write_to_NULL_via_decompose_right_rejected():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("import x V; BIFURCATE V[X, NULL];")


def test_write_to_NULL_via_compose_rejected():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("import x A; import y B; BIFURCATE [A, B]NULL;")


def test_THIS_is_rebindable_via_decompose():
    check("import x V; BIFURCATE V[THIS, X];")


def test_error_reports_position():
    try:
        check("import x A;\n  X.DIE();")
    except SemaError as e:
        assert e.line == 2
        assert e.col == 3
    else:
        pytest.fail("expected SemaError")


def test_looptest_sample_passes_sema():
    sample = Path(__file__).resolve().parent.parent / "examples" / "control_flow" / "looptest.ath"
    check(sample.read_text())


def test_input_introduces_variable():
    check("INPUT line; print $line;")


def test_input_to_NULL_rejected():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("INPUT NULL;")


def test_print_interp_of_unbound_errors():
    with pytest.raises(SemaError, match="missing.*not in scope|line.*not in scope"):
        check("print $line;")


def test_print_interp_of_predefined_NULL_ok():
    check("print $NULL;")


def test_print_interp_of_THIS_ok():
    check("print $THIS;")


def _check_with_funcs(src: str, fnames: list[str]) -> None:
    """Sema-check `src` against a synthetic function table containing
    function bodies that are no-ops (so each function passes its own sema)."""
    program = parse(src)
    # empty programs are valid function bodies in the function table
    function_table = {name.lower(): parse("") for name in fnames}
    analyze(program, function_table)


def test_funcall_unknown_function_rejected():
    with pytest.raises(SemaError, match="ADD.*not declared"):
        _check_with_funcs("import x A; import y B; ADD [A, B] R;", [])


def test_funcall_known_function_passes():
    _check_with_funcs("import x A; import y B; ADD [A, B] R;", ["ADD"])


def test_funcall_resolves_case_insensitively():
    _check_with_funcs("import x A; import y B; add [A, B] R;", ["ADD"])
    _check_with_funcs("import x A; import y B; ADD [A, B] R;", ["add"])


def test_funcall_compose_arg_args_must_be_in_scope():
    with pytest.raises(SemaError, match="X.*not in scope"):
        _check_with_funcs("ADD [X, Y] R;", ["ADD"])


def test_funcall_decompose_ret_args_must_be_in_scope():
    with pytest.raises(SemaError, match="V.*not in scope"):
        _check_with_funcs("SPLIT V [A, B];", ["SPLIT"])


def test_funcall_target_introduced():
    _check_with_funcs(
        "import x A; import y B; ADD [A, B] R; R.DIE();", ["ADD"]
    )


def test_funcall_decompose_outputs_introduced():
    _check_with_funcs(
        "import x V; SPLIT V [A, B]; A.DIE(); B.DIE();", ["SPLIT"]
    )


def test_funcall_cannot_write_to_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        _check_with_funcs("import x A; import y B; ADD [A, B] NULL;", ["ADD"])


def test_args_is_in_scope_in_function_body():
    # empty main
    program = parse("")
    function_table = {"id": parse("THIS.DIE(ARGS);")}
    analyze(program, function_table)


def test_args_not_in_scope_at_top_level():
    with pytest.raises(SemaError, match="ARGS.*not in scope"):
        analyze(parse("ARGS.DIE();"))


def test_die_arg_must_be_in_scope():
    with pytest.raises(SemaError, match="R.*not in scope"):
        check("THIS.DIE(R);")


def test_die_arg_can_be_predefined():
    check("THIS.DIE(THIS);")
    check("THIS.DIE(NULL);")


def test_watch_introduces_variable():
    check('watch "foo.txt" as F; F.DIE();')


def test_watch_var_cannot_be_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check('watch "foo.txt" as NULL;')


def test_import_number_introduces_variable():
    check("import number 42 as N; N.DIE();")


def test_import_number_var_cannot_be_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("import number 42 as NULL;")


def test_import_builtin_is_local_callable():
    # ImportBuiltinStmt makes ATH_ADD callable in the same file without importf
    check(
        "import builtin ath_add as ATH_ADD;"
        " import number 1 as A; import number 2 as B;"
        " ATH_ADD [A, B] R;"
    )


def test_import_builtin_scope_is_per_file():
    # without the local import builtin, calling ATH_ADD is rejected
    with pytest.raises(SemaError, match="ATH_ADD.*not declared"):
        check(
            "import number 1 as A; import number 2 as B;"
            " ATH_ADD [A, B] R;"
        )


def test_import_builtin_call_resolves_case_insensitively():
    check(
        "import builtin ath_add as ATH_ADD;"
        " import number 1 as A; import number 2 as B;"
        " ath_add [A, B] R;"
    )


def test_subscript_introduces_target():
    check("import x S; import number 0 as N; S[N] X; X.DIE();")


def test_subscript_source_must_be_in_scope():
    with pytest.raises(SemaError, match="S.*not in scope"):
        check("import number 0 as N; S[N] X;")


def test_subscript_index_must_be_in_scope():
    with pytest.raises(SemaError, match="N.*not in scope"):
        check("import x S; S[N] X;")


def test_subscript_target_cannot_be_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("import x S; import number 0 as N; S[N] NULL;")


def test_slice_introduces_target():
    check(
        "import x S; import number 1 as I; import number 4 as J;"
        " S[I..J] R; R.DIE();"
    )


def test_slice_start_must_be_in_scope():
    with pytest.raises(SemaError, match="I.*not in scope"):
        check("import x S; import number 4 as J; S[I..J] R;")


def test_slice_end_must_be_in_scope():
    with pytest.raises(SemaError, match="J.*not in scope"):
        check("import x S; import number 1 as I; S[I..J] R;")


def test_branch_var_must_be_in_scope():
    with pytest.raises(SemaError, match="V.*not in scope"):
        check("BRANCH(V) { print yes; }")


def test_branch_bodies_share_scope():
    # then-body vars visible after the branch; sema is flat per-activation
    check("import x V; BRANCH(V) { import y W; } W.DIE();")


def test_branch_NULL_in_condition_is_fine():
    # NULL is readable; BRANCH(NULL) runs the else (dead) branch
    check("BRANCH(NULL) { print never; } ELSE { print always; }")


def test_clone_writes_target():
    check("import x V; CLONE V as W; W.DIE();")


def test_clone_source_must_be_in_scope():
    with pytest.raises(SemaError, match="V.*not in scope"):
        check("CLONE V as W;")


def test_clone_target_cannot_be_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("import x V; CLONE V as NULL;")


def test_sleep_duration_must_be_in_scope():
    with pytest.raises(SemaError, match="N.*not in scope"):
        check("sleep N;")


def test_sleep_duration_can_be_NULL():
    # NULL is readable; runtime treats it as a no-op
    check("sleep NULL;")


def test_timer_writes_target():
    check("import number 100 as N; TIMER N as T; T.DIE();")


def test_timer_duration_must_be_in_scope():
    with pytest.raises(SemaError, match="N.*not in scope"):
        check("TIMER N as T;")


def test_timer_target_cannot_be_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("import number 100 as N; TIMER N as NULL;")


def test_read_introduces_target():
    check('read "/tmp/foo" as S; S.DIE();')


def test_read_target_cannot_be_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check('read "/tmp/foo" as NULL;')


def test_write_source_must_be_in_scope():
    with pytest.raises(SemaError, match="S.*not in scope"):
        check('write S to "/tmp/out";')


def test_write_optional_verdict_writes_target():
    check('import x S; write S to "/tmp/out" as OK; OK.DIE();')


def test_write_verdict_cannot_be_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check('import x S; write S to "/tmp/out" as NULL;')


def test_append_source_must_be_in_scope():
    with pytest.raises(SemaError, match="S.*not in scope"):
        check('append S to "/tmp/log";')


def test_close_target_must_be_in_scope():
    with pytest.raises(SemaError, match="S.*not in scope"):
        check("close S;")


def test_text_target_cannot_be_NULL():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check('text "hi" as NULL;')


def test_text_ident_part_must_be_in_scope():
    with pytest.raises(SemaError, match="N.*not in scope"):
        check('text "value: " N as M;')


def test_text_introduces_target():
    check('text "hi" as M; print done;')


def test_print_interpolation_var_must_be_in_scope():
    with pytest.raises(SemaError, match="GHOST.*not in scope"):
        check("print hello $GHOST;")


def test_print_literal_only_needs_no_scope():
    check("print just a literal;")


def test_print_interpolation_var_in_scope_ok():
    check('text "hi" as S; print value $S;')


def test_execute_undeclared_function_errors():
    with pytest.raises(SemaError, match="GHOST.*not declared"):
        check("import number 1 as V; ~ATH(V) { } EXECUTE(GHOST);")


def test_execute_null_is_noop_ok():
    check("import number 1 as V; ~ATH(V) { } EXECUTE(NULL);")


# --- concurrency statements ---------------------------------------------------


def test_spawn_user_function_ok():
    # User functions come from the loader's function_table (see _check_with_funcs above),
    # not from the importf statements in the source.
    _check_with_funcs('spawn W THIS as A; join A;', ["W"])


def test_spawn_into_nursery_ok():
    _check_with_funcs('nursery as N; spawn W THIS into N as A; join N;', ["W"])


def test_spawn_undeclared_function_errors():
    with pytest.raises(SemaError, match="not declared"):
        check('spawn GHOST THIS as A;')


def test_spawn_builtin_rejected():
    # builtins have no activation/THIS, so they are not spawnable
    with pytest.raises(SemaError, match="only importf functions are spawnable"):
        check('import builtin ath_add as ADD; spawn ADD THIS as A;')


def test_spawn_binds_handle_in_scope():
    _check_with_funcs('spawn W THIS as A; join A;', ["W"])


def test_send_to_unbound_dest_errors():
    with pytest.raises(SemaError, match="not in scope"):
        check('send 1 to NOPE;')


def test_send_literal_ok():
    check('channel as C; send 42 to C;')


def test_recv_binds_target():
    check('channel as C; recv from C as M; ~ATH(M) { print x; }')


def test_recv_from_unbound_source_errors():
    with pytest.raises(SemaError, match="not in scope"):
        check('recv from NOPE as M;')


def test_join_unbound_handle_errors():
    with pytest.raises(SemaError, match="not in scope"):
        check('join NOPE;')


def test_channel_and_nursery_bind_targets():
    check('channel as C; nursery as N; send 1 to C; join N;')


def test_yield_ok():
    check('yield;')
