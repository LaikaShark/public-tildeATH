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
    sample = Path(__file__).parent / "conformance" / "looptest.ath"
    check(sample.read_text())


def test_input_introduces_variable():
    check("INPUT line; PRINT2 line;")


def test_input_to_NULL_rejected():
    with pytest.raises(SemaError, match="NULL.*read-only"):
        check("INPUT NULL;")


def test_print2_of_unbound_errors():
    with pytest.raises(SemaError, match="missing.*not in scope|line.*not in scope"):
        check("PRINT2 line;")


def test_print2_of_predefined_NULL_ok():
    check("PRINT2 NULL;")


def test_print2_of_THIS_ok():
    check("PRINT2 THIS;")
