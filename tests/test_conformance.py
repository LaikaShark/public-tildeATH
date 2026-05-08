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
    env: dict[str, str] | None = None,
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
        timeout=60.0,
    )
    assert compiled.returncode == 0, (
        f"compile failed for {source} (compose={compose}):\n{compiled.stderr}"
    )
    run_env = None
    if env is not None:
        import os
        run_env = {**os.environ, **env}
    run = subprocess.run(
        [str(out)],
        input=stdin,
        capture_output=True,
        text=True,
        timeout=10.0,
        env=run_env,
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
    pytest.param(
        "branch/main.ath",
        "3\n10\n",
        "less\nverdict was alive\n",
        id="tier6_branch_clone_alive_verdict",
    ),
    pytest.param(
        "branch/main.ath",
        "10\n3\n",
        "not less\nverdict was dead\n",
        id="tier6_branch_clone_dead_verdict",
    ),
    pytest.param(
        "branch/main.ath",
        "7\n7\n",
        "not less\nverdict was dead\n",
        id="tier6_branch_equal_is_not_less",
    ),
    pytest.param(
        "timer/main.ath",
        None,
        "go\nstop\n",
        id="tier5_timer_drives_loop",
    ),
    pytest.param(
        "file_io/main.ath",
        "ping pong\n",
        "9\nping pong\n",
        id="tier4_file_io_read_close",
    ),
    pytest.param(
        "file_io_owned/main.ath",
        "self destruct\n",
        "self destruct\nscratch file is now gone\n",
        id="tier4_file_io_owned_die",
    ),
    pytest.param(
        "search_replace/main.ath",
        "hello world hello\nhello\nhi\n",
        "0\nhi world hi\n",
        id="tier3c_search_replace_match",
    ),
    pytest.param(
        "search_replace/main.ath",
        "no matches here\nxyz\nXYZ\n",
        "not found\n\n",
        id="tier3c_search_replace_no_match",
    ),
    pytest.param(
        "verdicts/main.ath",
        "3\n7\n",
        "x le y\nx ne y\nboth positive\n",
        id="tier2b_verdicts_le_ne_and",
    ),
    pytest.param(
        "verdicts/main.ath",
        "4\n4\n",
        "x le y\nx ge y\nboth positive\n",
        id="tier2b_verdicts_le_ge_eq",
    ),
    pytest.param(
        "verdicts/main.ath",
        "0\n5\n",
        "x le y\nx ne y\nsome zero\n",
        id="tier2b_verdicts_or_picks_up_zero",
    ),
    pytest.param(
        "or_dynamic/main.ath",
        None,
        "both alive\none alive\nboth dead\n",
        id="tier2b_or_born_states",
    ),
    pytest.param(
        "entangle_vs_bifurcate/main.ath",
        None,
        "bifurcate alive\nentangle hold\nbifurcate still alive\nentangle dead\n",
        id="tier3d_entangle_propagates_dep",
    ),
    pytest.param(
        "text_demo/main.ath",
        None,
        "hello world\nvalue: 42\nx=7\ny=8\nsum=15\n",
        id="tier3e_text_primitive_and_interpolation",
    ),
    pytest.param(
        "string_predicates/main.ath",
        "hello\nhello\n",
        "equal\nprefix\nsuffix\nnot lt\nnot gt\n",
        id="tier3f_predicates_equal",
    ),
    pytest.param(
        "string_predicates/main.ath",
        "apple\napp\n",
        "not equal\nprefix\nno suffix\nnot lt\ngt\n",
        id="tier3f_predicates_prefix_gt",
    ),
    pytest.param(
        "string_predicates/main.ath",
        "x\n\n",
        "not equal\nprefix\nsuffix\nnot lt\ngt\n",
        id="tier3f_predicates_empty_suffix_alive",
    ),
    pytest.param(
        "string_transforms/main.ath",
        "  Hello,World,FOO  \n",
        "  hello,world,foo  \n  HELLO,WORLD,FOO  \nHello,World,FOO\nHello-World-FOO\n",
        id="tier3g_transforms_case_trim_splitjoin",
    ),
    pytest.param(
        "string_transforms/main.ath",
        "a,b,\n",
        "a,b,\nA,B,\na,b,\na-b-\n",
        id="tier3g_transforms_trailing_sep_empty",
    ),
    pytest.param(
        "string_search/main.ath",
        "abracadabra\na\n",
        "present\n5\n10\n",
        id="tier3h_search_present_count_rfind",
    ),
    pytest.param(
        "string_search/main.ath",
        "hello\nz\n",
        "absent\n0\nno match\n",
        id="tier3h_search_absent",
    ),
    pytest.param(
        "string_build/main.ath",
        "ab\n",
        "ababab\nba\n   ab\nab   \n97\na\n",
        id="tier3i_build_repeat_pad_ordchr",
    ),
    pytest.param(
        "string_build/main.ath",
        "hello\n",
        "hellohellohello\nolleh\nhello\nhello\n104\nh\n",
        id="tier3i_build_pad_noop_when_wide_enough",
    ),
]


# Cases that need a controlled environment (e.g. ATH_SEED for random).
ENV_CASES = [
    pytest.param(
        "random/main.ath",
        None,
        "1\n",
        {"ATH_SEED": "42"},
        id="tier5_random_seed_42",
    ),
    pytest.param(
        "random/main.ath",
        None,
        "3\n",
        {"ATH_SEED": "99"},
        id="tier5_random_seed_99",
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


@pytest.mark.parametrize("compose", ["fresh", "intern"])
@pytest.mark.parametrize("rel_path,stdin,expected,env", ENV_CASES)
def test_program_with_env(
    rel_path: str,
    stdin: str | None,
    expected: str,
    env: dict[str, str],
    compose: str,
    tmp_path: Path,
):
    """Conformance programs whose output depends on environment variables
    (notably ATH_SEED for deterministic randomness)."""
    source = PROGRAMS / rel_path
    assert source.exists(), f"missing program file: {source}"
    output = _compile_and_run(
        source, tmp_path, stdin=stdin, compose=compose, env=env
    )
    assert output == expected
