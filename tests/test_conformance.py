"""Conformance suite: compile-and-run each canonical .ath program and
diff stdout against an expected value. Programs live under examples/."""

import socket as _socket
import subprocess
import sys
import tempfile as _tempfile
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PROGRAMS = PROJECT_ROOT / "examples"
RUNTIME_LIB = PROJECT_ROOT / "runtime" / "libath_fresh.a"


def _af_unix_available() -> bool:
    """Whether AF_UNIX bind works here. CI sandboxes (seccomp/no-socket) may forbid it, in which
    case the networking conformance cases skip rather than fail."""
    probe = Path(_tempfile.gettempdir()) / "ath_afunix_probe.sock"
    try:
        probe.unlink()
    except OSError:
        pass
    try:
        s = _socket.socket(_socket.AF_UNIX, _socket.SOCK_STREAM)
        try:
            s.bind(str(probe))
        finally:
            s.close()
        return True
    except (OSError, AttributeError):
        return False
    finally:
        try:
            probe.unlink()
        except OSError:
            pass


_HAVE_AF_UNIX = _af_unix_available()


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
        "basics/hello.ath",
        None,
        "Hello, ~ATH!\n",
        id="hello",
    ),
    pytest.param(
        "basics/echo.ath",
        "roundtrip\n",
        "roundtrip\n",
        id="echo",
    ),
    pytest.param(
        "basics/print_interpolation.ath",
        None,
        "Hello, world! N is 7.\nplain line\na $literal dollar and world again\n",
        id="print_interpolation",
    ),
    pytest.param(
        "numbers/float_arithmetic.ath",
        None,
        "pi = 3.14\n3.14 + 7 = 10.14\n7 / 2.0 = 3.5\n"
        "floor(3.14) = 3.0\ntrunc(3.14) = 3\nparse 2.5e3 = 2500.0\n",
        id="float_arithmetic",
    ),
    pytest.param(
        "numbers/float_transcendentals.ath",
        None,
        "sqrt(16) = 4.0\nsqrt(2.25) = 1.5\nhypot(3, 4) = 5.0\n"
        "exp(0) = 1.0\nlog(1) = 0.0\ncos(0) = 1.0\n",
        id="float_transcendentals",
    ),
    pytest.param(
        "numbers/bignum.ath",
        None,
        "99999999999999999999 + 1 = 100000000000000000000\n"
        "square = 9999999999999999999800000000000000000001\n"
        "square / big = 99999999999999999999\n"
        "big - (big-1) = 1\n",
        id="bignum",
    ),
    pytest.param(
        "numbers/bignum_factorial.ath",
        None,
        "25! = 15511210043330985984000000\n",
        id="bignum_factorial",
    ),
    pytest.param(
        "basics/multi_word_import.ath",
        None,
        "speaking once before she dies\nsilence\n",
        id="multi_word_import",
    ),
    pytest.param(
        "control_flow/inverted_loop.ath",
        None,
        "V is currently dead\nafter the loop\n",
        id="inverted_loop",
    ),
    pytest.param(
        "control_flow/execute_postfix.ath",
        None,
        "the universe has ended.\nand yet, the program continues.\n",
        id="execute_postfix",
    ),
    pytest.param(
        "basics/function_hello/main.ath",
        None,
        "hello from a function\nmain is done\n",
        id="function_hello",
    ),
    pytest.param(
        "basics/identity/main.ath",
        "hello\n",
        "ello\ndone\n",
        id="identity",
    ),
    pytest.param(
        "strings/first_char/main.ath",
        "xyz\n",
        "x\n",
        id="first_char",
    ),
    pytest.param(
        "object_model/countdown/main.ath",
        None,
        "tick\ntick\ntick\ndone\n",
        id="recursive_countdown",
    ),
    pytest.param(
        "numbers/addition/main.ath",
        None,
        "tick\ntick\ntick\ntick\ntick\ndone\n",
        id="add_then_countdown",
    ),
    pytest.param(
        "programs/fizzbuzz/main.ath",
        None,
        "fizz\nbuzz\nfizzbuzz\nfizz\nbuzz\nfizzbuzz\n",
        id="fizzbuzz_mutual_recursion",
    ),
    pytest.param(
        "liveness/instant_skipper.ath",
        None,
        "fell through\n",
        id="instant_lifetime_skips_loop",
    ),
    pytest.param(
        "liveness/short_lived/main.ath",
        None,
        "tick is dead\n",
        id="tick_lifetime_dies_in_loop",
    ),
    pytest.param(
        "liveness/once_runner.ath",
        None,
        "exactly one run\nafter\n",
        id="once_lifetime_runs_body_exactly_once",
    ),
    pytest.param(
        "numbers/arithmetic/main.ath",
        "17\n25\n",
        "42\n",
        id="tier1_arithmetic_add",
    ),
    pytest.param(
        "numbers/arithmetic/main.ath",
        "-3\n10\n",
        "7\n",
        id="tier1_arithmetic_negative",
    ),
    pytest.param(
        "numbers/arithmetic/main.ath",
        "banana\n5\n",
        "\n",
        id="tier1_arithmetic_dead_chain_on_bad_parse",
    ),
    pytest.param(
        "numbers/comparison/main.ath",
        "3\n10\n",
        "less\n",
        id="tier2_comparison_less",
    ),
    pytest.param(
        "numbers/comparison/main.ath",
        "10\n3\n",
        "greater\n",
        id="tier2_comparison_greater",
    ),
    pytest.param(
        "numbers/comparison/main.ath",
        "7\n7\n",
        "equal\n",
        id="tier2_comparison_equal",
    ),
    pytest.param(
        "numbers/comparison/main.ath",
        "banana\n5\n",
        "",
        id="tier2_comparison_dead_chain_silent",
    ),
    pytest.param(
        "strings/string_ops/main.ath",
        "hello\nworld\n",
        "5\nh\nell\nhelloworld\n",
        id="tier3_strings_golden_path",
    ),
    pytest.param(
        "strings/string_ops/main.ath",
        "abcdef\nXYZ\n",
        "6\na\nbcd\nabcdefXYZ\n",
        id="tier3_strings_alt_input",
    ),
    pytest.param(
        "strings/string_ops/main.ath",
        "\nfoo\n",
        "0\n\n\nfoo\n",
        id="tier3_strings_empty_first_line",
    ),
    pytest.param(
        "control_flow/branch/main.ath",
        "3\n10\n",
        "less\nverdict was alive\n",
        id="tier6_branch_clone_alive_verdict",
    ),
    pytest.param(
        "control_flow/branch/main.ath",
        "10\n3\n",
        "not less\nverdict was dead\n",
        id="tier6_branch_clone_dead_verdict",
    ),
    pytest.param(
        "control_flow/branch/main.ath",
        "7\n7\n",
        "not less\nverdict was dead\n",
        id="tier6_branch_equal_is_not_less",
    ),
    pytest.param(
        "liveness/timer/main.ath",
        None,
        "go\nstop\n",
        id="tier5_timer_drives_loop",
    ),
    pytest.param(
        "io/file_io/main.ath",
        "ping pong\n",
        "9\nping pong\n",
        id="tier4_file_io_read_close",
    ),
    pytest.param(
        "io/file_io_owned/main.ath",
        "self destruct\n",
        "self destruct\nscratch file is now gone\n",
        id="tier4_file_io_owned_die",
    ),
    pytest.param(
        "strings/search_replace/main.ath",
        "hello world hello\nhello\nhi\n",
        "0\nhi world hi\n",
        id="tier3c_search_replace_match",
    ),
    pytest.param(
        "strings/search_replace/main.ath",
        "no matches here\nxyz\nXYZ\n",
        "not found\n\n",
        id="tier3c_search_replace_no_match",
    ),
    pytest.param(
        "numbers/verdicts/main.ath",
        "3\n7\n",
        "x le y\nx ne y\nboth positive\n",
        id="tier2b_verdicts_le_ne_and",
    ),
    pytest.param(
        "numbers/verdicts/main.ath",
        "4\n4\n",
        "x le y\nx ge y\nboth positive\n",
        id="tier2b_verdicts_le_ge_eq",
    ),
    pytest.param(
        "numbers/verdicts/main.ath",
        "0\n5\n",
        "x le y\nx ne y\nsome zero\n",
        id="tier2b_verdicts_or_picks_up_zero",
    ),
    pytest.param(
        "liveness/or_dynamic/main.ath",
        None,
        "both alive\none alive\nboth dead\n",
        id="tier2b_or_born_states",
    ),
    pytest.param(
        "object_model/entangle_vs_bifurcate/main.ath",
        None,
        "bifurcate alive\nentangle hold\nbifurcate still alive\nentangle dead\n",
        id="tier3d_entangle_propagates_dep",
    ),
    pytest.param(
        "strings/text_demo/main.ath",
        None,
        "hello world\nvalue: 42\nx=7\ny=8\nsum=15\n",
        id="tier3e_text_primitive_and_interpolation",
    ),
    pytest.param(
        "strings/string_predicates/main.ath",
        "hello\nhello\n",
        "equal\nprefix\nsuffix\nnot lt\nnot gt\n",
        id="tier3f_predicates_equal",
    ),
    pytest.param(
        "strings/string_predicates/main.ath",
        "apple\napp\n",
        "not equal\nprefix\nno suffix\nnot lt\ngt\n",
        id="tier3f_predicates_prefix_gt",
    ),
    pytest.param(
        "strings/string_predicates/main.ath",
        "x\n\n",
        "not equal\nprefix\nsuffix\nnot lt\ngt\n",
        id="tier3f_predicates_empty_suffix_alive",
    ),
    pytest.param(
        "strings/string_transforms/main.ath",
        "  Hello,World,FOO  \n",
        "  hello,world,foo  \n  HELLO,WORLD,FOO  \nHello,World,FOO\nHello-World-FOO\n",
        id="tier3g_transforms_case_trim_splitjoin",
    ),
    pytest.param(
        "strings/string_transforms/main.ath",
        "a,b,\n",
        "a,b,\nA,B,\na,b,\na-b-\n",
        id="tier3g_transforms_trailing_sep_empty",
    ),
    pytest.param(
        "strings/string_search/main.ath",
        "abracadabra\na\n",
        "present\n5\n10\n",
        id="tier3h_search_present_count_rfind",
    ),
    pytest.param(
        "strings/string_search/main.ath",
        "hello\nz\n",
        "absent\n0\nno match\n",
        id="tier3h_search_absent",
    ),
    pytest.param(
        "strings/string_build/main.ath",
        "ab\n",
        "ababab\nba\n   ab\nab   \n97\na\n",
        id="tier3i_build_repeat_pad_ordchr",
    ),
    pytest.param(
        "strings/string_build/main.ath",
        "hello\n",
        "hellohellohello\nolleh\nhello\nhello\n104\nh\n",
        id="tier3i_build_pad_noop_when_wide_enough",
    ),
    pytest.param(
        "object_model/index_snapshot/main.ath",
        None,
        "97\n97\n",
        id="tier3j_index_snapshot_no_atom_poison",
    ),
    pytest.param(
        "strings/string_polish/main.ath",
        "hello world\n",
        "Hello world\nHello World\nh\n7\nhello world\n.hello world\n",
        id="tier3k_string_polish",
    ),
    pytest.param(
        "numbers/numeric_ops/main.ath",
        None,
        "1024\n6\n12\n18\n6\n48\n-1\n18\n",
        id="tier4b_numeric_second_wave",
    ),
    pytest.param(
        "numbers/list_ops/main.ath",
        None,
        "15\n96\n8\n3\nhas 3\nno 99\n2\n7\n8\n",
        id="tier4c_list_ops",
    ),
    pytest.param(
        "basics/inline_literals/main.ath",
        None,
        "42\n2.0\n100000000000000000042\nhi!\n8\n15\n",
        id="inline_literals",
    ),
    pytest.param(
        "liveness/lifetime_combinators/main.ath",
        None,
        "all before alive\nall after dead\nany after alive\nany final dead\n",
        id="tier4d_lifetime_combinators",
    ),
    pytest.param(
        "liveness/watch_sources/main.ath",
        None,
        "pid alive\nother dead\n",
        id="tier4e_watch_pid",
    ),
    pytest.param(
        "control_flow/repeat_loop/main.ath",
        None,
        "row\ndot\ndot\nrow\ndot\ndot\ndone\n",
        id="tier4f_repeat_loop",
    ),
    pytest.param(
        "control_flow/every_loop/main.ath",
        None,
        "beat\n",
        id="tier4g_every_loop",
    ),
    pytest.param(
        "control_flow/execute_hook/main.ath",
        None,
        "working\nfarewell\nafter\n",
        id="tier4h_execute_postfix_hook",
    ),

    pytest.param("liveness/universe_ends.ath", None,
        "the universe is alive; we wait.\nthe universe has ended.\n"
        "and yet, the program continues.\n", id="ex_universe_ends"),
    pytest.param("programs/collatz.ath", None,
        "6\n3\n10\n5\n16\n8\n4\n2\n1\nreached 1 in 8 steps\n", id="ex_collatz"),
    pytest.param("programs/primes.ath", None,
        "2\n3\n5\n7\n11\n13\n17\n19\n23\n29\n", id="ex_primes"),
    pytest.param("programs/modexp.ath", None,
        "123456789 ^ 20 mod 98765432109876543211 = 82630247944240163692\n",
        id="ex_modexp"),
    pytest.param("programs/newton_sqrt.ath", None,
        "sqrt(2) ~ 1.414213562373095\n", id="ex_newton_sqrt"),
    pytest.param("programs/rule110.ath", None,
        "...............................#\n..............................##\n"
        ".............................###\n............................##.#\n"
        "...........................#####\n..........................##...#\n"
        ".........................###..##\n........................##.#.###\n"
        ".......................#######.#\n......................##.....###\n"
        ".....................###....##.#\n....................##.#...#####\n"
        "...................#####..##...#\n..................##...#.###..##\n"
        ".................###..####.#.###\n................##.#.##..#####.#\n",
        id="ex_rule110"),
    pytest.param("strings/rot13.ath", "Hello, World!\n", "Uryyb, Jbeyq!\n", id="ex_rot13"),
    pytest.param("programs/balanced.ath", "([]{})\n", "balanced\n", id="ex_balanced_ok"),
    pytest.param("programs/balanced.ath", "([)]\n", "unbalanced\n", id="ex_balanced_bad"),
    pytest.param("io/wc.ath", "hello world\nfoo bar baz\n",
        "lines: 2\nwords: 5\nchars: 24\n", id="ex_wc"),
    pytest.param("io/grep.ath", "err\nerror 1\nok\nerror 2\n",
        "error 1\nerror 2\n", id="ex_grep"),
    pytest.param("programs/rle.ath", "aaabbbbc\n",
        "encoded: 3a4b1c\ndecoded: aaabbbbc\n", id="ex_rle"),
    pytest.param("programs/wordfreq.ath", "the cat sat the cat\n",
        "the: 2\ncat: 2\nsat: 1\n", id="ex_wordfreq"),
    pytest.param("programs/calculator.ath", "3 + 4 * 2 - 1\n", "10\n", id="ex_calc1"),
    pytest.param("programs/calculator.ath", "2 * 3 + 4 * 5\n", "26\n", id="ex_calc2"),
    pytest.param("programs/brainfuck.ath", None, "Hello World!\n", id="ex_brainfuck"),
    pytest.param("programs/maze.ath", None,
        "generated:\n"
        "#################\n#               #\n# # # # ##### # #\n"
        "# # # # #     # #\n# ######### # # #\n# #         # # #\n"
        "### ### # # # # #\n#   #   # # # # #\n##### ### ##### #\n"
        "#     #   #     #\n# # ### # # # # #\n# # #   # # # # #\n"
        "#################\n"
        "solved:\n"
        "#################\n#* * * * * * * *#\n# # # # ##### # #\n"
        "# # # # #     #*#\n# ######### # # #\n# #         # #*#\n"
        "### ### # # # # #\n#   #   # # # #*#\n##### ### ##### #\n"
        "#     #   #    *#\n# # ### # # # # #\n# # #   # # # #*#\n"
        "#################\n",
        id="ex_maze"),
    pytest.param("programs/sudoku.ath", None,
        "puzzle:\n"
        "....78912\n..21953..\n19834....\n859....23\n4....3791\n"
        "...92485.\n.61537...\n2874....5\n34....179\n"
        "solved:\n"
        "534678912\n672195348\n198342567\n859761423\n426853791\n"
        "713924856\n961537284\n287419635\n345286179\n",
        id="ex_sudoku"),

    # Cooperative actor concurrency. Output is fully determined by spawn order, FIFO
    # mailboxes, and round-robin scheduling, so it must match under both compose modes.
    # A graded progression from a single actor up to a dataflow pipeline.
    pytest.param("actors/hello_actor/main.ath", None,
        "hello from actor\nmain done\n",
        id="actors_hello"),
    pytest.param("actors/yield_interleave/main.ath", None,
        "spawned\na1\nb1\na2\nb2\n",
        id="actors_yield_interleave"),
    pytest.param("actors/mailbox/main.ath", None,
        "100\n200\n300\ndone\n",
        id="actors_mailbox"),
    pytest.param("actors/fan_out/main.ath", None,
        "worker 1 got 10\nworker 2 got 20\nworker 1 got 30\nworker 2 got 40\ndone\n",
        id="actors_fan_out"),
    pytest.param("actors/pipeline/main.ath", None,
        "2\n4\n6\ndone\n",
        id="actors_pipeline"),
    pytest.param("actors/actor_sleep/main.ath", None,
        "fast done\nslow done\n",
        id="actors_actor_sleep"),
    pytest.param("actors/producer_consumer/main.ath", None,
        "10\n20\n30\ndone\n",
        id="actors_producer_consumer"),
    pytest.param("actors/fork_join/main.ath", None,
        "1\n2\n3\ndone\n",
        id="actors_fork_join"),
    pytest.param("actors/ping_pong/main.ath", None,
        "ping\npong\nping\npong\nping\npong\ndone\n",
        id="actors_ping_pong"),
    pytest.param("actors/supervisor_cancel/main.ath", None,
        "spawned\ncancelled\nworker stopped\nworker stopped\nworker stopped\n",
        id="actors_supervisor_cancel"),
]


# Cases needing controlled environment, e.g. ATH_SEED for random.
ENV_CASES = [
    pytest.param(
        "numbers/random/main.ath",
        None,
        "1\n",
        {"ATH_SEED": "42"},
        id="tier5_random_seed_42",
    ),
    pytest.param(
        "numbers/random/main.ath",
        None,
        "3\n",
        {"ATH_SEED": "99"},
        id="tier5_random_seed_99",
    ),
    pytest.param(
        "programs/guess.ath",
        # secret is 71 under ATH_SEED=1
        "50\n71\n",
        "I am thinking of a number from 1 to 100.\n"
        "your guess?\ntoo low\nyour guess?\ncorrect!\n",
        {"ATH_SEED": "1"},
        id="ex_guess",
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


@pytest.mark.skipif(not _HAVE_AF_UNIX, reason="AF_UNIX sockets unavailable (sandboxed)")
@pytest.mark.parametrize("compose", ["fresh", "intern"])
def test_networking_echo_unix(compose: str, tmp_path: Path):
    """An echo server + client talking over a Unix-domain socket, both as actors in one process.
    A connection is a channel whose liveness is the socket: peer-close ends the ~ATH(CONN) loop.
    Output is byte-identical under both compose modes — socket handles are plain alive objects
    (never interned) and message framing reuses the same string builders as the actor cases."""
    source = PROGRAMS / "net/echo_unix/main.ath"
    assert source.exists(), f"missing program file: {source}"
    output = _compile_and_run(source, tmp_path, compose=compose)
    assert output == "hello\nworld\ndone\n"
