"""Conformance suite: compile-and-run each canonical .ath program and
diff stdout against an expected value. Programs live under
tests/conformance/programs/."""

import subprocess
import sys
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PROGRAMS = PROJECT_ROOT / "tests" / "conformance" / "programs"
RUNTIME_LIB = PROJECT_ROOT / "runtime" / "libath_fresh.a"


def _ensure_runtime():
    if RUNTIME_LIB.exists():
        return
    result = subprocess.run(
        ["make", "runtime"], cwd=PROJECT_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0 or not RUNTIME_LIB.exists():
        pytest.skip(f"could not build runtime: {result.stderr.strip()}")


def _compile_and_run(source: Path, tmp_path: Path, stdin: str | None = None) -> str:
    _ensure_runtime()
    out = tmp_path / "prog"
    compiled = subprocess.run(
        [sys.executable, "-m", "athc.cli", str(source), "-o", str(out)],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
    )
    assert compiled.returncode == 0, (
        f"compile failed for {source}:\n{compiled.stderr}"
    )
    run = subprocess.run(
        [str(out)], input=stdin, capture_output=True, text=True, timeout=10.0
    )
    assert run.returncode == 0, (
        f"binary exited {run.returncode} for {source}:\n{run.stderr}"
    )
    return run.stdout


CASES = [
    pytest.param(
        "hello.ath",
        None,
        "Hello, ~ATH!\n",
        id="hello",
    ),
    pytest.param(
        "echo.ath",
        "roundtrip\n",
        "roundtrip\n",
        id="echo",
    ),
    pytest.param(
        "multi_word_import.ath",
        None,
        "speaking once before she dies\nsilence\n",
        id="multi_word_import",
    ),
    pytest.param(
        "inverted_loop.ath",
        None,
        "V is currently dead\nafter the loop\n",
        id="inverted_loop",
    ),
    pytest.param(
        "homestuck_canonical.ath",
        None,
        "the universe has ended.\nand yet, the program continues.\n",
        id="homestuck_canonical",
    ),
    pytest.param(
        "function_hello/main.ath",
        None,
        "hello from a function\nmain is done\n",
        id="function_hello",
    ),
    pytest.param(
        "identity/main.ath",
        "hello\n",
        "ello\ndone\n",
        id="identity",
    ),
    pytest.param(
        "first_char/main.ath",
        "xyz\n",
        "x\n",
        id="first_char",
    ),
    pytest.param(
        "countdown/main.ath",
        None,
        "tick\ntick\ntick\ndone\n",
        id="recursive_countdown",
    ),
    pytest.param(
        "addition/main.ath",
        None,
        "tick\ntick\ntick\ntick\ntick\ndone\n",
        id="add_then_countdown",
    ),
    pytest.param(
        "fizzbuzz/main.ath",
        None,
        "fizz\nbuzz\nfizzbuzz\nfizz\nbuzz\nfizzbuzz\n",
        id="fizzbuzz_mutual_recursion",
    ),
]


@pytest.mark.parametrize("rel_path,stdin,expected", CASES)
def test_program(rel_path: str, stdin: str | None, expected: str, tmp_path: Path):
    source = PROGRAMS / rel_path
    assert source.exists(), f"missing program file: {source}"
    output = _compile_and_run(source, tmp_path, stdin=stdin)
    assert output == expected
