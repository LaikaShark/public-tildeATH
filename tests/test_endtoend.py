import subprocess
import sys
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parent.parent
RUNTIME_LIB = PROJECT_ROOT / "runtime" / "libath_fresh.a"
CONFORMANCE = PROJECT_ROOT / "tests" / "conformance"


def _ensure_runtime():
    if RUNTIME_LIB.exists():
        return
    result = subprocess.run(
        ["make", "runtime"], cwd=PROJECT_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0 or not RUNTIME_LIB.exists():
        pytest.skip(f"could not build runtime: {result.stderr.strip()}")


def _compile(source_path: Path, output: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, "-m", "athc.cli", str(source_path), "-o", str(output)],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
    )


def _run(binary: Path, timeout: float = 5.0, stdin_input: str | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(binary)],
        input=stdin_input,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def _build_and_run(source_path: Path, tmp_path: Path, stdin_input: str | None = None) -> str:
    _ensure_runtime()
    out = tmp_path / "prog"
    compiled = _compile(source_path, out)
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


def test_reserved_v1_word_rejected_at_cli(tmp_path):
    src = tmp_path / "reserved.ath"
    src.write_text("importf foo as bar;\n")
    out = tmp_path / "prog"
    compiled = _compile(src, out)
    assert compiled.returncode != 0
    assert "reserved" in compiled.stderr.lower()


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
    src.write_text("INPUT s;\nPRINT2 s;\nTHIS.DIE();\n")
    assert _build_and_run(src, tmp_path, stdin_input="hello, ~ATH!\n") == "hello, ~ATH!\n"


def test_print2_of_NULL_prints_blank_line(tmp_path):
    src = tmp_path / "blank.ath"
    src.write_text("PRINT2 NULL;\nTHIS.DIE();\n")
    assert _build_and_run(src, tmp_path) == "\n"


def test_input_at_eof_yields_empty_string(tmp_path):
    src = tmp_path / "eof.ath"
    src.write_text("INPUT s;\nPRINT2 s;\nprint after;\nTHIS.DIE();\n")
    # Empty stdin -> input returns empty string -> PRINT2 emits one newline
    assert _build_and_run(src, tmp_path, stdin_input="") == "\nafter\n"


def test_input_strips_trailing_newline(tmp_path):
    src = tmp_path / "strip.ath"
    src.write_text(
        "INPUT a;\nINPUT b;\nPRINT2 a;\nPRINT2 b;\nTHIS.DIE();\n"
    )
    # Two lines; trailing \n on each should be stripped before encoding
    assert _build_and_run(src, tmp_path, stdin_input="first\nsecond\n") == "first\nsecond\n"
