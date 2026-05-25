"""REPL tests: the ctypes FFI to the runtime (R1) and, later, the evaluator
and REPL loop."""

import subprocess
from pathlib import Path

import pytest

from athc.runtime_ffi import AthRuntime, _default_lib

PROJECT_ROOT = Path(__file__).resolve().parent.parent


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
