"""REPL tests: the ctypes FFI to the runtime (R1), the statement evaluator
(R2), and the REPL loop (R3)."""

import ctypes
import os
import subprocess
import tempfile
from pathlib import Path

import pytest

from athc.parser import parse
from athc.repl_eval import Evaluator
from athc.runtime_ffi import AthRuntime, _default_lib

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CLI = ["python", "-m", "athc.cli"]


def _ensure_so(mode: str = "fresh"):
    if _default_lib(mode).exists():
        return
    result = subprocess.run(
        ["make", "runtime"], cwd=PROJECT_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0 or not _default_lib(mode).exists():
        pytest.skip(f"could not build runtime .so: {result.stderr.strip()}")


@pytest.fixture(scope="module")
def rt():
    _ensure_so("fresh")
    return AthRuntime("fresh")


def test_ffi_number_roundtrip(rt):
    assert rt.to_text(rt.lib.ath_alloc_number(5)) == "5"
    assert rt.to_text(rt.lib.ath_alloc_number(-42)) == "-42"
    assert rt.to_text(rt.lib.ath_alloc_float(3.5)) == "3.5"


def test_ffi_bignum_roundtrip(rt):
    big = rt.lib.ath_alloc_bignum_from_decimal(b"99999999999999999999")
    assert rt.to_text(big) == "99999999999999999999"


def test_ffi_null_is_dead(rt):
    assert rt.alive(rt.NULL) is False


def test_ffi_arithmetic_via_builtin(rt):
    add = rt.builtin("ath_add")
    n = rt.lib.ath_alloc_number(5)
    assert rt.to_text(add(n, n)) == "10"


def test_ffi_string_and_describe(rt):
    s = rt.string_from_bytes(b"hello")
    assert rt.read_string(s) == "hello"
    assert rt.describe(s) == "live · string · 'hello'"
    assert rt.describe(rt.lib.ath_alloc_number(7)) == "live · int · 7"


def test_ffi_compose_decompose_identity(rt):
    a = rt.lib.ath_alloc_number(1)
    b = rt.lib.ath_alloc_number(2)
    c = rt.lib.ath_compose(a, b)
    left, right = rt.decompose(c)
    assert left == a and right == b


# --- R2: the evaluator, captured at the C stdout fd ------------------------

def _run_repl(src: str, mode: str = "fresh", stdin_lines=None) -> str:
    """Evaluate `src` through the REPL evaluator, capturing what the C
    runtime writes to fd 1."""
    _ensure_so(mode)
    rt = AthRuntime(mode)
    feed = iter(stdin_lines or [])
    ev = Evaluator(rt, read_line=lambda: next(feed, ""))
    r_fd, w_fd = os.pipe()
    saved = os.dup(1)
    os.dup2(w_fd, 1)
    os.close(w_fd)
    try:
        ev.run(parse(src).statements)
        ctypes.CDLL(None).fflush(None)
    finally:
        os.dup2(saved, 1)
        os.close(saved)
    chunks = []
    while True:
        c = os.read(r_fd, 4096)
        if not c:
            break
        chunks.append(c)
    os.close(r_fd)
    return b"".join(chunks).decode("utf-8")


def _compile_and_run(src: str, tmp_path, mode: str = "fresh") -> str:
    _ensure_so(mode)
    s = tmp_path / "p.ath"
    s.write_text(src)
    out = tmp_path / "p"
    c = subprocess.run(
        CLI + [str(s), "-o", str(out), "--compose", mode],
        cwd=PROJECT_ROOT, capture_output=True, text=True,
    )
    assert c.returncode == 0, c.stderr
    return subprocess.run([str(out)], capture_output=True, text=True).stdout


# Snippets whose REPL output must match a compiled binary exactly.
_DIFF_SNIPPETS = [
    "print Hello, ~ATH!;\nTHIS.DIE();\n",
    "importf <add> as ADD;\nimport number 5 as N;\nADD [N, N] R;\nprint $R;\nTHIS.DIE();\n",
    "text \"a\" \"b\" \"c\" as S;\nprint $S;\nTHIS.DIE();\n",
    # ~ATH loop with the kill idiom
    "import a A;\n~ATH(A) {\n print tick;\n BIFURCATE NULL[j, A];\n}\nprint done;\nTHIS.DIE();\n",
    # bignum exact arithmetic, interpolated
    "importf <mul> as MUL;\nimport number 99999999999999999999 as B;\nMUL [B, B] SQ;\nprint $SQ;\nTHIS.DIE();\n",
    # float tower + to_string
    "importf <div> as DIV;\nimport number 7 as S;\nimport number 2.0 as T;\nDIV [S, T] H;\nprint $H;\nTHIS.DIE();\n",
    # subscript + branch
    "import number 1 as I;\ntext \"hi\" as S;\nS[I] C;\nBRANCH(C) { print alive; } { print dead; }\nTHIS.DIE();\n",
]


@pytest.mark.parametrize("src", _DIFF_SNIPPETS)
def test_repl_matches_compiled(src, tmp_path):
    assert _run_repl(src) == _compile_and_run(src, tmp_path)


def test_repl_state_persists_across_calls():
    # Separate run() batches share the environment, like REPL lines.
    _ensure_so("fresh")
    rt = AthRuntime("fresh")
    ev = Evaluator(rt)
    ev.run(parse("import number 21 as N;").statements)
    ev.run(parse("importf <add> as ADD;\nADD [N, N] R;").statements)
    assert rt.to_text(ev._read("R")) == "42"


def test_repl_input_reads_from_callback():
    out = _run_repl("INPUT line;\nprint got $line;\nTHIS.DIE();\n",
                    stdin_lines=["typed text\n"])
    assert out == "got typed text\n"


def test_repl_top_level_die_does_not_end_session():
    _ensure_so("fresh")
    rt = AthRuntime("fresh")
    ev = Evaluator(rt)
    ev.run(parse("THIS.DIE();").statements)   # would terminate a program
    ev.run(parse("import number 5 as N;").statements)
    assert rt.to_text(ev._read("N")) == "5"   # session continued
