import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parent.parent
RUNTIME_LIB = PROJECT_ROOT / "runtime" / "libath_fresh.a"
CONFORMANCE = PROJECT_ROOT / "examples"


def _ensure_runtime():
    if RUNTIME_LIB.exists():
        return
    result = subprocess.run(
        ["make", "runtime"], cwd=PROJECT_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0 or not RUNTIME_LIB.exists():
        pytest.skip(f"could not build runtime: {result.stderr.strip()}")


def _compile(
    source_path: Path,
    output: Path,
    extra_args: list[str] | None = None,
) -> subprocess.CompletedProcess:
    cmd = [sys.executable, "-m", "athc.cli", str(source_path), "-o", str(output)]
    if extra_args:
        cmd.extend(extra_args)
    return subprocess.run(
        cmd, cwd=PROJECT_ROOT, capture_output=True, text=True
    )


def _run(binary: Path, timeout: float = 5.0, stdin_input: str | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(binary)],
        input=stdin_input,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def _build_and_run(
    source_path: Path,
    tmp_path: Path,
    stdin_input: str | None = None,
    extra_args: list[str] | None = None,
) -> str:
    _ensure_runtime()
    out = tmp_path / "prog"
    compiled = _compile(source_path, out, extra_args=extra_args)
    assert compiled.returncode == 0, f"compile failed:\nstderr:\n{compiled.stderr}"
    assert out.exists(), "compiler did not produce output binary"
    run = _run(out, stdin_input=stdin_input)
    assert run.returncode == 0, f"binary exited {run.returncode}, stderr:\n{run.stderr}"
    return run.stdout


def test_hello_world(tmp_path):
    src = tmp_path / "hello.ath"
    src.write_text("print Hello, ~ATH!;\nTHIS.DIE();\n")
    assert _build_and_run(src, tmp_path) == "Hello, ~ATH!\n"


def test_implicit_termination_at_eof(tmp_path):
    src = tmp_path / "noop.ath"
    src.write_text("print fell off the end;\n")
    assert _build_and_run(src, tmp_path) == "fell off the end\n"


def test_text_target_only_referenced_by_text_compiles(tmp_path):
    # Regression: `text ... as V` where V is never read elsewhere must still
    # get a codegen slot. Previously _collect_names had no TextStmt branch,
    # so this crashed with KeyError: 'V'.
    src = tmp_path / "text_only.ath"
    src.write_text('text "hi" as V;\nTHIS.DIE();\n')
    assert _build_and_run(src, tmp_path) == ""


def test_text_ident_part_only_use_compiles(tmp_path):
    # An IDENT part is a read; its slot must be collected too even if the
    # name appears nowhere else.
    src = tmp_path / "text_ident.ath"
    src.write_text('import number 5 as N;\ntext "n=" N as MSG;\nprint $MSG;\nTHIS.DIE();\n')
    assert _build_and_run(src, tmp_path) == "n=5\n"


def test_print_interpolates_numeric_payload(tmp_path):
    # `print $N` renders a numeric payload as its decimal form directly,
    # no TO_STRING needed (int and float).
    src = tmp_path / "num_interp.ath"
    src.write_text(
        "import number 42 as N;\n"
        "import number 3.14 as PI;\n"
        "print int $N float $PI;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "int 42 float 3.14\n"


def test_bignum_literal_and_exact_arithmetic(tmp_path):
    # A literal beyond int64 is an exact bignum; arithmetic stays exact and
    # interpolates as its full decimal.
    src = tmp_path / "big.ath"
    src.write_text(
        "importf <mul> as MUL;\n"
        "import number 99999999999999999999 as BIG;\n"
        "MUL [BIG, BIG] SQ;\n"
        "print $SQ;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == (
        "9999999999999999999800000000000000000001\n"
    )


def test_int_overflow_still_dies(tmp_path):
    # int64 overflow stays born-dead (failure-as-death preserved); it does
    # NOT auto-promote to bignum.
    src = tmp_path / "ov.ath"
    src.write_text(
        "importf <add> as ADD;\n"
        "import number 9223372036854775807 as M;\n"
        "import number 1 as ONE;\n"
        "ADD [M, ONE] OV;\n"
        "~ATH(OV) { print ALIVE_BUG; BIFURCATE NULL[z, OV]; }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "done\n"


def test_die_immediately_terminates(tmp_path):
    src = tmp_path / "early.ath"
    src.write_text(
        "print before;\n"
        "THIS.DIE();\n"
        "print after;\n"
    )
    assert _build_and_run(src, tmp_path) == "before\n"


def test_ath_loop_skipped_when_var_already_dead(tmp_path):
    src = tmp_path / "skip.ath"
    src.write_text(
        "import x V;\n"
        "V.DIE();\n"
        "~ATH(V) { print never; }\n"
        "print after;\n"
    )
    assert _build_and_run(src, tmp_path) == "after\n"


def test_ath_loop_runs_once_then_var_dies(tmp_path):
    src = tmp_path / "once.ath"
    src.write_text(
        "import x V;\n"
        "~ATH(V) { print tick; V.DIE(); }\n"
        "print after;\n"
    )
    assert _build_and_run(src, tmp_path) == "tick\nafter\n"


def test_keyword_case_insensitive_end_to_end(tmp_path):
    # Keywords, ~ATH, and .DIE are case-insensitive (per spec §2.2).
    # Identifiers (V, THIS) remain case-sensitive.
    src = tmp_path / "cases.ath"
    src.write_text(
        "Import x V;\n"
        "~ath(V) { Print mixed case keywords; V.die(); }\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "mixed case keywords\n"


def test_looptest_conformance(tmp_path):
    out = _build_and_run(CONFORMANCE / "looptest.ath", tmp_path)
    lines = out.strip().splitlines()
    assert all(line in ("APPLE", "ORANGE") for line in lines)
    assert lines.count("APPLE") == lines.count("ORANGE")
    assert len(lines) >= 2


def test_importf_path_must_be_string_literal(tmp_path):
    src = tmp_path / "bad_importf.ath"
    src.write_text("importf foo as bar;\n")
    out = tmp_path / "prog"
    compiled = _compile(src, out)
    assert compiled.returncode != 0
    assert "expected STRING" in compiled.stderr or "expected string" in compiled.stderr.lower()


def test_unbound_variable_rejected_at_cli(tmp_path):
    src = tmp_path / "typo.ath"
    src.write_text("this.DIE();\n")  # lowercase 'this' is not the predefined THIS
    out = tmp_path / "prog"
    compiled = _compile(src, out)
    assert compiled.returncode != 0
    assert "not in scope" in compiled.stderr.lower()
    assert not out.exists()


def test_writing_to_NULL_rejected_at_cli(tmp_path):
    src = tmp_path / "null_write.ath"
    src.write_text("import x NULL;\n")
    out = tmp_path / "prog"
    compiled = _compile(src, out)
    assert compiled.returncode != 0
    assert "null" in compiled.stderr.lower() and "read-only" in compiled.stderr.lower()


def test_read_of_flow_unreachable_binding_does_not_crash(tmp_path):
    # W is introduced inside a loop body that runs zero times (V is killed
    # before the loop). Sema accepts the program because W is syntactically
    # in scope after the loop. The runtime must not segfault on W.DIE().
    src = tmp_path / "unreachable.ath"
    src.write_text(
        "import x V;\n"
        "V.DIE();\n"
        "~ATH(V) { import y W; }\n"
        "W.DIE();\n"
        "print done;\n"
    )
    assert _build_and_run(src, tmp_path) == "done\n"


def test_decompose_of_unbound_variable_yields_NULL_halves(tmp_path):
    # Same shape: U is introduced in an unreached loop body, then decomposed.
    # Decompose of an unbound (null-pointer) slot must safely yield NULL halves
    # and not allocate onto a null pointer.
    src = tmp_path / "decompose_unbound.ath"
    src.write_text(
        "import x V;\n"
        "V.DIE();\n"
        "~ATH(V) { import y U; }\n"
        "BIFURCATE U[L, R];\n"
        "L.DIE();\n"
        "R.DIE();\n"
        "print survived;\n"
    )
    assert _build_and_run(src, tmp_path) == "survived\n"


def test_echo_via_input_and_print2(tmp_path):
    src = tmp_path / "echo.ath"
    src.write_text("INPUT s;\nprint $s;\nTHIS.DIE();\n")
    assert _build_and_run(src, tmp_path, stdin_input="hello, ~ATH!\n") == "hello, ~ATH!\n"


def test_print2_of_NULL_prints_blank_line(tmp_path):
    src = tmp_path / "blank.ath"
    src.write_text("print $NULL;\nTHIS.DIE();\n")
    assert _build_and_run(src, tmp_path) == "\n"


def test_input_at_eof_yields_empty_string(tmp_path):
    src = tmp_path / "eof.ath"
    src.write_text("INPUT s;\nprint $s;\nprint after;\nTHIS.DIE();\n")
    # Empty stdin -> input returns empty string -> print $s emits one newline
    assert _build_and_run(src, tmp_path, stdin_input="") == "\nafter\n"


def test_input_strips_trailing_newline(tmp_path):
    src = tmp_path / "strip.ath"
    src.write_text(
        "INPUT a;\nINPUT b;\nprint $a;\nprint $b;\nTHIS.DIE();\n"
    )
    # Two lines; trailing \n on each should be stripped before encoding
    assert _build_and_run(src, tmp_path, stdin_input="first\nsecond\n") == "first\nsecond\n"


# --- Functions ---


def test_function_call_basic(tmp_path):
    (tmp_path / "hello.ath").write_text(
        "print Hello from HELLO.;\nTHIS.DIE();\n"
    )
    main = tmp_path / "main.ath"
    main.write_text(
        'importf "hello.ath" as HELLO;\n'
        "import x A;\n"
        "import y B;\n"
        "HELLO [A, B] R;\n"
        "print bye;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(main, tmp_path) == "Hello from HELLO.\nbye\n"


def test_function_returns_args_via_die(tmp_path):
    (tmp_path / "idfn.ath").write_text("THIS.DIE(ARGS);\n")
    main = tmp_path / "main.ath"
    main.write_text(
        'importf "idfn.ath" as ID;\n'
        "INPUT s;\n"
        "ID s [H, T];\n"
        "print $T;\n"  # T = tail of "hi" = "i"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(main, tmp_path, stdin_input="hi\n") == "i\ndone\n"


def test_function_default_return_is_NULL(tmp_path):
    (tmp_path / "noop.ath").write_text("THIS.DIE();\n")  # returns NULL
    main = tmp_path / "main.ath"
    main.write_text(
        'importf "noop.ath" as NOOP;\n'
        "import x A;\n"
        "import y B;\n"
        "NOOP [A, B] R;\n"
        "print $R;\n"  # R = NULL -> blank line
        "print after;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(main, tmp_path) == "\nafter\n"


def test_die_with_arg_sets_return_then_falls_off_end(tmp_path):
    (tmp_path / "midret.ath").write_text(
        # Sets return_obj = ARGS, kills V, falls off end -> returns ARGS.
        "import x V;\nV.DIE(ARGS);\n"
    )
    main = tmp_path / "main.ath"
    main.write_text(
        'importf "midret.ath" as F;\n'
        "INPUT s;\n"
        "F s [H, T];\n"
        "print $T;\n"  # T = "ello"
        "THIS.DIE();\n"
    )
    assert _build_and_run(main, tmp_path, stdin_input="hello\n") == "ello\n"


def test_function_call_compose_arg_form_then_decompose(tmp_path):
    # Function picks the left half of ARGS and returns it.
    (tmp_path / "pickleft.ath").write_text(
        "BIFURCATE ARGS[L, R];\nTHIS.DIE(L);\n"
    )
    main = tmp_path / "main.ath"
    main.write_text(
        'importf "pickleft.ath" as PICK;\n'
        "INPUT s;\n"
        "BIFURCATE s[H, T];\n"
        # Call PICK with compose(H, T) — should return H back.
        "PICK [H, T] R;\n"
        # If everything worked, R is the H atom; print its alive-ness via a loop.
        "~ATH(R) { print alive; R.DIE(); }\n"
        "THIS.DIE();\n"
    )
    out = _build_and_run(main, tmp_path, stdin_input="ab\n")
    assert out == "alive\n"


def test_importf_nested_function_imports(tmp_path):
    (tmp_path / "inner.ath").write_text("print inner;\nTHIS.DIE();\n")
    (tmp_path / "outer.ath").write_text(
        'importf "inner.ath" as INNER;\n'
        "import x A;\n"
        "import y B;\n"
        "INNER [A, B] R;\n"
        "print outer;\n"
        "THIS.DIE();\n"
    )
    main = tmp_path / "main.ath"
    main.write_text(
        'importf "outer.ath" as OUTER;\n'
        "import x A;\n"
        "import y B;\n"
        "OUTER [A, B] R;\n"
        "print main;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(main, tmp_path) == "inner\nouter\nmain\n"


def test_importf_missing_file_rejected(tmp_path):
    main = tmp_path / "main.ath"
    main.write_text(
        'importf "nope.ath" as F;\nTHIS.DIE();\n'
    )
    out = tmp_path / "prog"
    compiled = _compile(main, out)
    assert compiled.returncode != 0
    assert "file not found" in compiled.stderr.lower()


def test_unknown_function_call_rejected_at_cli(tmp_path):
    main = tmp_path / "main.ath"
    main.write_text(
        "import x A;\nimport y B;\nNOSUCH [A, B] R;\nTHIS.DIE();\n"
    )
    out = tmp_path / "prog"
    compiled = _compile(main, out)
    assert compiled.returncode != 0
    assert "not declared" in compiled.stderr.lower()


# --- Homestuck surface ---


def test_inverted_loop_runs_after_var_dies(tmp_path):
    # V starts alive, gets killed, then ~ATH(!V) fires because V is now dead.
    # Body rebinds V to a live composite so the next check fails and the
    # loop exits (rather than running forever).
    src = tmp_path / "inv.ath"
    src.write_text(
        "import x V;\n"
        "V.DIE();\n"
        "~ATH(!V) {\n"
        "    print V is dead;\n"
        "    BIFURCATE [NULL, NULL] V;\n"
        "}\n"
        "print after;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "V is dead\nafter\n"


def test_inverted_loop_skipped_when_var_alive(tmp_path):
    # V is alive, so ~ATH(!V) body never runs.
    src = tmp_path / "inv_skip.ath"
    src.write_text(
        "import x V;\n"
        "~ATH(!V) { print never; }\n"
        "print after;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "after\n"


def test_execute_postfix_is_accepted(tmp_path):
    src = tmp_path / "exec.ath"
    src.write_text(
        "import x V;\n"
        "V.DIE();\n"
        "~ATH(V) { print never; } EXECUTE(NULL);\n"
        "print after;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "after\n"


def test_homestuck_canonical_shape_parses_and_runs(tmp_path):
    # Patterned after esolangs wiki canonical shape.
    src = tmp_path / "hs.ath"
    src.write_text(
        "import dead universe U;\n"
        "U.DIE();\n"
        "~ATH(!U) {\n"
        "    print universe ended;\n"
        "    THIS.DIE();\n"
        "} EXECUTE(NULL);\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "universe ended\n"


def test_multi_word_import_works_end_to_end(tmp_path):
    src = tmp_path / "multi.ath"
    src.write_text(
        "import dead grandmother G;\n"
        "~ATH(G) { print alive; G.DIE(); }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "alive\ndone\n"


# --- Lifetime library ---


def test_instant_lifetime_is_born_dead(tmp_path):
    # "instant" library entry has lifetime [0, 0], so the object is dead
    # at allocation time and the loop body never runs.
    src = tmp_path / "instant.ath"
    src.write_text(
        "import instant V;\n"
        "~ATH(V) { print never; }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "done\n"


def test_tick_lifetime_dies_within_a_few_iterations(tmp_path):
    # "tick" lifetime is 0.001-0.01 seconds. A tight ~ATH spin should
    # observe it dying well within the 5-second subprocess timeout.
    src = tmp_path / "tick.ath"
    src.write_text(
        "import tick T;\n"
        "~ATH(T) { }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "done\n"


def test_library_lookup_is_case_insensitive(tmp_path):
    src = tmp_path / "case.ath"
    src.write_text(
        "import INSTANT V;\n"
        "~ATH(V) { print never; }\n"
        "print ok;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "ok\n"


def test_unknown_library_name_falls_through_to_plain_alive(tmp_path):
    # "notarealconcept" is not in the library — must behave like the
    # original v0 import (plain alive object, killed manually).
    src = tmp_path / "fallthrough.ath"
    src.write_text(
        "import notarealconcept V;\n"
        "~ATH(V) { print alive once; V.DIE(); }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "alive once\ndone\n"


def test_once_library_entry_runs_body_exactly_once(tmp_path):
    # "once" is alive for exactly one ath_is_alive observation. The body
    # runs once, the second check returns dead, the loop exits cleanly.
    src = tmp_path / "once.ath"
    src.write_text(
        "import once V;\n"
        "~ATH(V) {\n"
        "    print exactly once;\n"
        "}\n"
        "print after;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "exactly once\nafter\n"


def test_once_can_be_explicitly_killed_before_observation(tmp_path):
    # A oneshot killed before any ~ATH check skips its body entirely.
    src = tmp_path / "once_pre_killed.ath"
    src.write_text(
        "import once V;\n"
        "V.DIE();\n"
        "~ATH(V) { print never; }\n"
        "print after;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "after\n"


def test_long_lived_concept_lets_loop_run_then_kill(tmp_path):
    # "sequoia" lives for ~1000-3500 years, so it's effectively immortal
    # for the duration of the test. Body must explicitly kill it.
    src = tmp_path / "sequoia.ath"
    src.write_text(
        "import sequoia V;\n"
        "~ATH(V) { print stately; V.DIE(); }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "stately\ndone\n"


# --- File watching ---


def test_watch_missing_file_is_born_dead(tmp_path):
    src = tmp_path / "watch_missing.ath"
    nonexistent = tmp_path / "definitely_not_here_xyz"
    src.write_text(
        f'watch "{nonexistent}" as F;\n'
        "~ATH(F) { print never; }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "done\n"


def test_watch_existing_file_then_loop_runs(tmp_path):
    target = tmp_path / "present.txt"
    target.write_text("ok")
    src = tmp_path / "watch_exists.ath"
    src.write_text(
        f'watch "{target}" as F;\n'
        "~ATH(F) { print file is here; F.DIE(); }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    assert _build_and_run(src, tmp_path) == "file is here\ndone\n"


def test_define_lifetime_registers_new_entry(tmp_path):
    # tortoise is not in the built-in library. With --define-lifetime
    # it becomes available with a 1-3ms lifetime; the spin loop exits.
    src = tmp_path / "tortoise.ath"
    src.write_text(
        "import tortoise T;\n"
        "~ATH(T) { }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    out = _build_and_run(
        src, tmp_path, extra_args=["-D", "tortoise:0.001:0.003"]
    )
    assert out == "done\n"


def test_define_lifetime_overrides_builtin_entry(tmp_path):
    # Built-in "fly" lives 1-3 days. Override to 0,0 => born dead.
    src = tmp_path / "override.ath"
    src.write_text(
        "import fly F;\n"
        "~ATH(F) { print never; }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    out = _build_and_run(
        src, tmp_path, extra_args=["--define-lifetime", "fly:0:0"]
    )
    assert out == "done\n"


def test_define_lifetime_supports_multi_word_names(tmp_path):
    # Multi-word import metadata joined with spaces; user entry uses the
    # same joined form.
    src = tmp_path / "mw.ath"
    src.write_text(
        "import giant tortoise T;\n"
        "~ATH(T) { }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    out = _build_and_run(
        src, tmp_path, extra_args=["-D", "giant tortoise:0.001:0.003"]
    )
    assert out == "done\n"


def test_define_lifetime_multiple_flags(tmp_path):
    src = tmp_path / "many.ath"
    src.write_text(
        "import alpha A;\n"
        "import beta B;\n"
        "~ATH(A) { print A; A.DIE(); }\n"
        "~ATH(B) { print B-skipped; }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    # alpha is plain alive (so loop runs once via explicit DIE)
    # beta has zero lifetime (so loop never runs)
    out = _build_and_run(
        src, tmp_path,
        extra_args=["-D", "alpha:60:120", "-D", "beta:0:0"],
    )
    assert out == "A\ndone\n"


def test_define_lifetime_invalid_spec_rejected(tmp_path):
    src = tmp_path / "p.ath"
    src.write_text("THIS.DIE();\n")
    out = tmp_path / "prog"
    compiled = _compile(src, out, extra_args=["-D", "broken"])
    assert compiled.returncode != 0
    assert "expected NAME:MIN:MAX" in compiled.stderr


def test_define_lifetime_negative_rejected(tmp_path):
    src = tmp_path / "p.ath"
    src.write_text("THIS.DIE();\n")
    out = tmp_path / "prog"
    compiled = _compile(src, out, extra_args=["-D", "neg:-1:5"])
    assert compiled.returncode != 0
    assert "non-negative" in compiled.stderr


def test_define_lifetime_min_exceeds_max_rejected(tmp_path):
    src = tmp_path / "p.ath"
    src.write_text("THIS.DIE();\n")
    out = tmp_path / "prog"
    compiled = _compile(src, out, extra_args=["-D", "bad:10:5"])
    assert compiled.returncode != 0
    assert "must not exceed max" in compiled.stderr


# --- Composition discipline (--compose fresh|intern) ---


def _ensure_runtime_intern():
    intern_lib = PROJECT_ROOT / "runtime" / "libath_intern.a"
    if intern_lib.exists():
        return
    result = subprocess.run(
        ["make", "runtime"], cwd=PROJECT_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0 or not intern_lib.exists():
        pytest.skip(f"could not build intern runtime: {result.stderr.strip()}")


def test_intern_mode_shares_structurally_equal_composites(tmp_path):
    # Under fresh, X and Y are distinct: killing Y leaves X alive.
    # Under intern, X and Y are the same object: killing Y kills X.
    src = tmp_path / "diff.ath"
    src.write_text(
        "import x A;\n"
        "import y B;\n"
        "BIFURCATE [A, B] X;\n"
        "BIFURCATE [A, B] Y;\n"
        "Y.DIE();\n"
        "~ATH(X) { print X is alive; X.DIE(); }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )

    fresh = _build_and_run(src, tmp_path)
    assert fresh == "X is alive\ndone\n"

    _ensure_runtime_intern()
    intern = _build_and_run(src, tmp_path, extra_args=["--compose", "intern"])
    assert intern == "done\n"


def test_intern_mode_runtime_library_path_default(tmp_path):
    # Sanity: invoking with --compose intern picks libath_intern.a by default.
    _ensure_runtime_intern()
    src = tmp_path / "hello.ath"
    src.write_text("print hi from intern;\nTHIS.DIE();\n")
    out = _build_and_run(src, tmp_path, extra_args=["--compose", "intern"])
    assert out == "hi from intern\n"


def test_signal_watch_dies_on_signal(tmp_path):
    _ensure_runtime()
    import signal as _signal

    src = tmp_path / "sig.ath"
    src.write_text(
        "watch signal SIGUSR1 as RUNNING;\n"
        "import once SHOWN;\n"
        "~ATH(RUNNING) {\n"
        "    ~ATH(SHOWN) { print serving requests; }\n"
        "}\n"
        "print received signal;\n"
        "THIS.DIE();\n"
    )
    out_bin = tmp_path / "prog"
    compiled = _compile(src, out_bin)
    assert compiled.returncode == 0, compiled.stderr

    log = tmp_path / "out.log"
    with open(log, "w") as f:
        proc = subprocess.Popen([str(out_bin)], stdout=f)
    time.sleep(0.1)
    proc.send_signal(_signal.SIGUSR1)
    proc.wait(timeout=5.0)
    assert proc.returncode == 0

    output = log.read_text()
    assert "serving requests" in output
    assert output.endswith("received signal\n"), f"got: {output!r}"


def test_signal_watch_unknown_signal_name_yields_born_dead(tmp_path):
    src = tmp_path / "unk.ath"
    src.write_text(
        "watch signal NOTASIGNAL as V;\n"
        "~ATH(V) { print never; }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    out_bin = tmp_path / "prog"
    compiled = _compile(src, out_bin)
    assert compiled.returncode == 0
    run = subprocess.run(
        [str(out_bin)], capture_output=True, text=True, timeout=5.0
    )
    assert run.returncode == 0
    assert run.stdout == "done\n"
    # Runtime should have warned about the unknown signal name.
    assert "unknown signal" in run.stderr.lower()


def test_watch_dies_when_file_deleted_mid_run(tmp_path):
    _ensure_runtime()
    target = tmp_path / "watched.txt"
    target.write_text("ok")

    src = tmp_path / "watch_run.ath"
    src.write_text(
        f'watch "{target}" as F;\n'
        "~ATH(F) { print alive; }\n"
        "print done;\n"
        "THIS.DIE();\n"
    )
    out_bin = tmp_path / "prog"
    compiled = _compile(src, out_bin)
    assert compiled.returncode == 0, compiled.stderr

    # Redirect stdout to a file so the tight loop doesn't deadlock on a
    # pipe buffer.
    out_log = tmp_path / "out.log"
    with open(out_log, "w") as f:
        proc = subprocess.Popen([str(out_bin)], stdout=f)
    # Give the program a moment to start iterating, then yank the file.
    time.sleep(0.1)
    os.unlink(target)
    proc.wait(timeout=5.0)
    assert proc.returncode == 0

    output = out_log.read_text()
    assert "alive" in output, f"expected at least one alive line; got:\n{output!r}"
    # After the file disappears, the loop must exit and the trailing
    # "done" must be the last output.
    assert output.endswith("done\n"), f"expected to end with 'done'; got:\n{output!r}"
