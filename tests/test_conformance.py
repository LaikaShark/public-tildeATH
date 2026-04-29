"""Conformance suite: compile-and-run each canonical .ath program and
diff stdout against an expected value. Programs live under examples/."""

import subprocess
import sys
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PROGRAMS = PROJECT_ROOT / "examples"
RUNTIME_LIB = PROJECT_ROOT / "runtime" / "libath_fresh.a"


def _ensure_runtime():
    if RUNTIME_LIB.exists():
        return
    result = subprocess.run(
        ["make", "runtime"], cwd=PROJECT_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0 or not RUNTIME_LIB.exists():
        pytest.skip(f"could not build runtime: {result.stderr.strip()}")


def _compile_and_run(
    source: Path,
    tmp_path: Path,
    stdin: str | None = None,
    compose: str = "fresh",
) -> str:
    _ensure_runtime()
    out = tmp_path / "prog"
    compiled = subprocess.run(
        [
            sys.executable,
            "-m",
            "athc.cli",
            "--compose",
            compose,
            str(source),
            "-o",
            str(out),
        ],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
    )
    assert compiled.returncode == 0, (
        f"compile failed for {source} (compose={compose}):\n{compiled.stderr}"
    )
    run = subprocess.run(
        [str(out)], input=stdin, capture_output=True, text=True, timeout=10.0
    )
    assert run.returncode == 0, (
        f"binary exited {run.returncode} for {source} (compose={compose}):\n{run.stderr}"
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
    pytest.param(
        "instant_skipper.ath",
        None,
        "fell through\n",
        id="instant_lifetime_skips_loop",
    ),
    pytest.param(
        "short_lived/main.ath",
        None,
        "tick is dead\n",
        id="tick_lifetime_dies_in_loop",
    ),
    pytest.param(
        "once_runner.ath",
        None,
        "exactly one run\nafter\n",
        id="once_lifetime_runs_body_exactly_once",
    ),
    pytest.param(
        "arithmetic/main.ath",
        "17\n25\n",
        "42\n",
        id="tier1_arithmetic_add",
    ),
    pytest.param(
        "arithmetic/main.ath",
        "-3\n10\n",
        "7\n",
        id="tier1_arithmetic_negative",
    ),
    pytest.param(
        "arithmetic/main.ath",
        "banana\n5\n",
        "\n",
        id="tier1_arithmetic_dead_chain_on_bad_parse",
    ),
    pytest.param(
        "comparison/main.ath",
        "3\n10\n",
        "less\n",
        id="tier2_comparison_less",
    ),
    pytest.param(
        "comparison/main.ath",
        "10\n3\n",
        "greater\n",
        id="tier2_comparison_greater",
    ),
    pytest.param(
        "comparison/main.ath",
        "7\n7\n",
        "equal\n",
        id="tier2_comparison_equal",
    ),
    pytest.param(
        "comparison/main.ath",
        "banana\n5\n",
        "",
        id="tier2_comparison_dead_chain_silent",
    ),
    pytest.param(
        "strings/main.ath",
        "hello\nworld\n",
        "5\nh\nell\nhelloworld\n",
        id="tier3_strings_golden_path",
    ),
    pytest.param(
        "strings/main.ath",
        "abcdef\nXYZ\n",
        "6\na\nbcd\nabcdefXYZ\n",
        id="tier3_strings_alt_input",
    ),
    pytest.param(
        "strings/main.ath",
        "\nfoo\n",
        "0\n\n\nfoo\n",
        id="tier3_strings_empty_first_line",
    ),
]


@pytest.mark.parametrize("compose", ["fresh", "intern"])
@pytest.mark.parametrize("rel_path,stdin,expected", CASES)
def test_program(
    rel_path: str,
    stdin: str | None,
    expected: str,
    compose: str,
    tmp_path: Path,
):
    """Every conformance program must produce the same output under both
    composition disciplines. This catches programs that accidentally rely
    on fresh-mode object identity."""
    source = PROGRAMS / rel_path
    assert source.exists(), f"missing program file: {source}"
    output = _compile_and_run(source, tmp_path, stdin=stdin, compose=compose)
    assert output == expected
