"""ctypes bindings to the ~ATH C runtime, for the REPL (athc/repl.py).

The REPL evaluates statements by calling the *real* runtime through this FFI,
so its behavior is identical to a compiled binary. Objects are opaque
`ath_obj *` handles (ctypes `c_void_p`) living in the runtime's
allocate-and-leak heap; they persist for the whole session.

CRITICAL: every function returning `ath_obj *` must have ``restype =
c_void_p``. ctypes defaults to ``c_int``, which silently truncates 64-bit
pointers — the classic footgun.
"""

import ctypes
from pathlib import Path

from athc import runtime_artifact

i64 = ctypes.c_int64
sz = ctypes.c_size_t
dbl = ctypes.c_double
ptr = ctypes.c_void_p


# Struct layout must mirror runtime/ath_runtime.h ath_obj; only REPL introspection reads it, so drift degrades inspection not correctness
class _Num(ctypes.Union):
    _fields_ = [("i", i64), ("f", dbl), ("b", ptr)]


class AthObj(ctypes.Structure):
    pass


AthObj._fields_ = [
    ("alive", ctypes.c_int),
    ("left", ctypes.POINTER(AthObj)),
    ("right", ctypes.POINTER(AthObj)),
    ("deadline_s", dbl),
    ("watch_path", ctypes.c_char_p),
    ("is_oneshot", ctypes.c_int),
    ("awaiting_signal", ctypes.c_int),
    ("owns_path", ctypes.c_int),
    ("num_kind", ctypes.c_int),
    ("num", _Num),
    ("dep1", ctypes.POINTER(AthObj)),
    ("dep2", ctypes.POINTER(AthObj)),
    ("dep_mode", ctypes.c_int),
    ("is_char", ctypes.c_int),
    ("char_code", ctypes.c_int),
    ("watch_pid", ctypes.c_int),
    ("mtime_path", ctypes.c_char_p),
    ("mtime_sec", i64),
    ("mtime_nsec", i64),
]

ATH_NUM_NONE, ATH_NUM_INT, ATH_NUM_FLOAT, ATH_NUM_BIG = 0, 1, 2, 3


def _default_lib(mode: str) -> Path:
    return runtime_artifact(mode, "so")


class RuntimeNotBuilt(Exception):
    pass


class AthRuntime:
    """A loaded ~ATH runtime shared library plus typed ABI handles."""

    # name -> (restype, [argtypes]) for non-generic entry points; other `ath_obj*(ath_obj*, ath_obj*)` builtins bound on demand by `builtin()`
    _SIGS = {
        "ath_alloc_alive": (ptr, []),
        "ath_alloc_number": (ptr, [i64]),
        "ath_alloc_float": (ptr, [dbl]),
        "ath_alloc_bignum_from_decimal": (ptr, [ctypes.c_char_p]),
        "ath_compose": (ptr, [ptr, ptr]),
        "ath_decompose": (None, [ptr, ctypes.POINTER(ptr), ctypes.POINTER(ptr)]),
        "ath_die": (None, [ptr]),
        "ath_is_alive": (ctypes.c_int, [ptr]),
        "ath_clone": (ptr, [ptr]),
        "ath_print_bytes": (None, [ctypes.c_char_p, sz]),
        "ath_print_obj_raw": (None, [ptr]),
        "ath_char_atom": (ptr, [ctypes.c_int]),
        "ath_string_from_bytes": (ptr, [ctypes.c_char_p, sz]),
        "ath_coerce_string": (ptr, [ptr]),
        "ath_to_string": (ptr, [ptr, ptr]),
        "ath_index": (ptr, [ptr, ptr]),
        "ath_slice": (ptr, [ptr, ptr]),
        "ath_concat": (ptr, [ptr, ptr]),
        "ath_count_of": (i64, [ptr]),
        "ath_alloc_from_library": (ptr, [ctypes.c_char_p]),
        "ath_register_lifetime": (None, [ctypes.c_char_p, dbl, dbl]),
        "ath_alloc_watching_file": (ptr, [ctypes.c_char_p]),
        "ath_alloc_watching_signal_by_name": (ptr, [ctypes.c_char_p]),
        "ath_alloc_watching_pid": (ptr, [ptr]),
        "ath_alloc_watching_mtime": (ptr, [ctypes.c_char_p]),
        "ath_sleep_ms": (None, [ptr]),
        "ath_alloc_timer_ms": (ptr, [ptr]),
        "ath_alloc_read_file": (ptr, [ctypes.c_char_p]),
        "ath_write_file": (ptr, [ptr, ctypes.c_char_p]),
        "ath_append_file": (ptr, [ptr, ctypes.c_char_p]),
        "ath_close": (None, [ptr]),
        "ath_big_to_decimal": (ctypes.c_char_p, [ptr]),
    }

    def __init__(self, mode: str = "fresh"):
        self.mode = mode
        path = _default_lib(mode)
        if not path.exists():
            raise RuntimeNotBuilt(
                f"runtime shared library not found at {path}; run 'make runtime' or reinstall athc"
            )
        self.lib = ctypes.CDLL(str(path))
        for name, (restype, argtypes) in self._SIGS.items():
            fn = getattr(self.lib, name)
            fn.restype = restype
            fn.argtypes = argtypes
        # Dead singleton (extern ath_obj *ath_NULL)
        self.NULL = ctypes.c_void_p.in_dll(self.lib, "ath_NULL").value

    def builtin(self, c_symbol: str):
        """A bound `ath_obj*(ath_obj*, ath_obj*)` builtin by C symbol name."""
        fn = getattr(self.lib, c_symbol)
        fn.restype = ptr
        fn.argtypes = [ptr, ptr]
        return fn

    def alive(self, obj) -> bool:
        return bool(self.lib.ath_is_alive(obj))

    def decompose(self, obj):
        l = ptr()
        r = ptr()
        self.lib.ath_decompose(obj, ctypes.byref(l), ctypes.byref(r))
        return l.value, r.value

    def string_from_bytes(self, data: bytes):
        return self.lib.ath_string_from_bytes(data, len(data))

    def print_bytes(self, data: bytes):
        self.lib.ath_print_bytes(data, len(data))

    def _struct(self, obj):
        return ctypes.cast(ctypes.c_void_p(obj), ctypes.POINTER(AthObj)).contents

    def read_string(self, obj) -> str:
        """Walk a string-cons-list into a Python str (no mutation, no ABI)."""
        out = bytearray()
        cur = obj
        while cur and cur != self.NULL:
            s = self._struct(cur)
            if not s.alive:
                break
            left = s.left
            if not left:
                break
            ls = left.contents
            if not ls.is_char:
                break
            out.append(ls.char_code & 0xFF)
            r = s.right
            cur = ctypes.cast(r, ctypes.c_void_p).value if r else None
        return out.decode("latin-1")

    def to_text(self, obj) -> str:
        """Render any object as text, exactly as TO_STRING / `print $X` would:
        numbers via their decimal form, strings by their characters."""
        if obj is None or obj == self.NULL:
            return ""
        s = self._struct(obj)
        if s.num_kind != ATH_NUM_NONE:
            strobj = self.lib.ath_to_string(obj, self.NULL)
            return self.read_string(strobj)
        return self.read_string(obj)

    def describe(self, obj) -> str:
        """Short `:inspect` summary: liveness, kind, and value."""
        if obj is None or obj == self.NULL:
            return "dead · NULL"
        alive = "live" if self.alive(obj) else "dead"
        s = self._struct(obj)
        kind = {
            ATH_NUM_INT: "int", ATH_NUM_FLOAT: "float", ATH_NUM_BIG: "bignum",
        }.get(s.num_kind)
        if kind:
            return f"{alive} · {kind} · {self.to_text(obj)}"
        text = self.read_string(obj)
        if text:
            return f"{alive} · string · {text!r}"
        return f"{alive} · object"
