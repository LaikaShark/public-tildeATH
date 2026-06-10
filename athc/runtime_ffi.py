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
import signal
import time
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
ATH_DEP_AND, ATH_DEP_OR = 0, 1


def _signal_name(num: int) -> str:
    """POSIX signal number -> name (e.g. 10 -> 'SIGUSR1'), falling back to the number."""
    try:
        return signal.Signals(num).name
    except ValueError:
        return f"signal {num}"


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
        "ath_observe_alive": (ctypes.c_int, [ptr]),
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

    def observe(self, obj) -> bool:
        """Non-mutating liveness: never consumes a one-shot or caches death. Use for inspection."""
        return bool(self.lib.ath_observe_alive(obj))

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
        # Accept an opaque int handle or a POINTER(AthObj) (e.g. s.left); normalize to an address.
        return ctypes.cast(ctypes.c_void_p(self._ptr_val(obj)), ctypes.POINTER(AthObj)).contents

    def read_string(self, obj) -> str:
        """Walk a string-cons-list into a Python str (no mutation, no ABI)."""
        out = bytearray()
        cur = self._ptr_val(obj)
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
            cur = self._ptr_val(s.right)
        return out.decode("latin-1")

    def to_text(self, obj) -> str:
        """Render any object as text, exactly as TO_STRING / `print $X` would:
        numbers via their decimal form, strings by their characters."""
        addr = self._ptr_val(obj)
        if addr is None or addr == self.NULL:
            return ""
        s = self._struct(addr)
        if s.num_kind != ATH_NUM_NONE:
            strobj = self.lib.ath_to_string(addr, self.NULL)
            return self.read_string(strobj)
        return self.read_string(addr)

    def describe(self, obj) -> str:
        """Short one-line summary: liveness, kind, and value. Non-mutating (used by `:env` and as
        the per-node line in the richer views)."""
        addr = self._ptr_val(obj)
        if addr is None or addr == self.NULL:
            return "dead · NULL"
        alive = "live" if self.observe(addr) else "dead"
        s = self._struct(addr)
        if s.is_char:
            return f"{alive} · char · {chr(s.char_code)!r} ({s.char_code})"
        kind = {
            ATH_NUM_INT: "int", ATH_NUM_FLOAT: "float", ATH_NUM_BIG: "bignum",
        }.get(s.num_kind)
        if kind:
            return f"{alive} · {kind} · {self.to_text(obj)}"
        text = self.read_string(obj)
        if text:
            return f"{alive} · string · {text!r}"
        return f"{alive} · object"

    # ---- richer inspection (`:inspect` and `:inspect -v` / `:tree`) ---------------------------

    def _ptr_val(self, p) -> int | None:
        """Normalize any handle form — int address, None, or a ctypes POINTER(AthObj)/c_void_p —
        to its integer address (or None for null)."""
        if not p:
            return None
        if isinstance(p, int):
            return p
        return ctypes.cast(p, ctypes.c_void_p).value

    def _alias(self, obj, env_items) -> str | None:
        """The bound name whose handle is this object, if any (first match by env order)."""
        target = self._ptr_val(obj)
        if target is None:
            return None
        for name, p in env_items:
            if name != "THIS" and self._ptr_val(p) == target:
                return name
        return None

    def _ref(self, obj, env_items) -> str:
        """Render a referenced object: its bound name if any, else a compact describe()."""
        if obj is None or self._ptr_val(obj) is None:
            return "NULL"
        name = self._alias(obj, env_items)
        if name:
            return f"{name} ({self.describe(obj)})"
        return self.describe(obj)

    def _lifetime_lines(self, s) -> list[str]:
        """Only the lifetime/mortality conditions actually set on the object."""
        out = []
        if s.deadline_s > 0.0:
            rem = s.deadline_s - time.monotonic()
            out.append(f"deadline: ~{rem:.1f}s remaining" if rem > 0 else "deadline: expired")
        if s.watch_path:
            out.append(f'watching file "{s.watch_path.decode("utf-8", "replace")}"')
        if s.is_oneshot:
            out.append("one-shot (dies after first observation)")
        if s.awaiting_signal:
            out.append(f"awaiting {_signal_name(s.awaiting_signal)}")
        if s.watch_pid:
            out.append(f"watching pid {s.watch_pid}")
        if s.mtime_path:
            out.append(f'watching mtime "{s.mtime_path.decode("utf-8", "replace")}"')
        return out

    def inspect(self, obj, env_items) -> str:
        """Multi-line relationship/mortality view for `:inspect VAR`."""
        if obj is None or obj == self.NULL:
            return "  dead · NULL"
        s = self._struct(obj)
        lines = [f"  {self.describe(obj)}"]

        life = self._lifetime_lines(s)
        if life:
            lines.append("  lifetime:")
            lines += [f"    {x}" for x in life]

        # Children: composition halves (BIFURCATE). Only for generic composites — suppress the
        # cons-cell spine of strings/atoms/numbers, which `-v` shows instead.
        is_stringish = s.is_char or s.num_kind != ATH_NUM_NONE or bool(self.read_string(obj))
        if not is_stringish and (s.left or s.right):
            lines.append("  children (composition):")
            if s.left:
                lines.append(f"    left  = {self._ref(s.left, env_items)}")
            if s.right:
                lines.append(f"    right = {self._ref(s.right, env_items)}")

        # Entanglements / inherited mortality: dep1/dep2 + dep_mode.
        if s.dep1 or s.dep2:
            if s.dep_mode == ATH_DEP_OR:
                lines.append("  mortality: alive until all entangled deps die (OR):")
            else:
                lines.append("  mortality: dies when any entangled dep dies (AND):")
            if s.dep1:
                lines.append(f"    dep1 = {self._ref(s.dep1, env_items)}")
            if s.dep2:
                lines.append(f"    dep2 = {self._ref(s.dep2, env_items)}")

        # Parents: no back-pointer exists, so reverse-scan the bound names.
        target = self._ptr_val(obj)
        parents = []
        for name, p in env_items:
            if self._ptr_val(p) == target:
                continue  # the variable itself
            ps = self._struct(p) if self._ptr_val(p) is not None else None
            if ps is None:
                continue
            if self._ptr_val(ps.left) == target:
                parents.append(f"{name} (left half)")
            if self._ptr_val(ps.right) == target:
                parents.append(f"{name} (right half)")
            if self._ptr_val(ps.dep1) == target:
                parents.append(f"{name} (entangled, dep1)")
            if self._ptr_val(ps.dep2) == target:
                parents.append(f"{name} (entangled, dep2)")
        if parents:
            lines.append("  parents:")
            lines += [f"    {p}" for p in parents]

        return "\n".join(lines)

    def render_graph(self, obj, env_items, max_nodes: int = 200) -> str:
        """Verbose `:inspect -v` / `:tree`: the whole observable object graph, depth-first, with
        #id sharing markers (so intern-mode sharing and dep cycles are visible and bounded)."""
        visited: dict[int, int] = {}
        lines: list[str] = []
        truncated = [False]

        def node_label(o) -> str:
            s = self._struct(o)
            name = self._alias(o, env_items)
            tag = f"[{name}] " if name else ""
            return f"{tag}{self.describe(o)}"

        def walk(o, prefix: str, is_last: bool, edge: str, is_root: bool) -> None:
            if len(visited) >= max_nodes:
                truncated[0] = True
                return
            branch = "" if is_root else ("└─ " if is_last else "├─ ")
            pv = self._ptr_val(o)
            if pv is None:
                lines.append(f"{prefix}{branch}{edge}NULL")
                return
            if pv in visited:
                lines.append(f"{prefix}{branch}{edge}→ #{visited[pv]} (seen)")
                return
            nid = len(visited) + 1
            visited[pv] = nid
            lines.append(f"{prefix}{branch}{edge}#{nid} {node_label(o)}")

            s = self._struct(o)
            # Structural edges in fixed order: left, right, then dep1/dep2.
            edges = []
            if s.left:
                edges.append(("", s.left))
            if s.right:
                edges.append(("", s.right))
            if s.dep1:
                edges.append(("dep1: ", s.dep1))
            if s.dep2:
                edges.append(("dep2: ", s.dep2))
            child_prefix = prefix if is_root else prefix + ("   " if is_last else "│  ")
            for i, (lbl, child) in enumerate(edges):
                walk(child, child_prefix, i == len(edges) - 1, lbl, False)

        walk(obj, "", True, "", True)
        if truncated[0]:
            lines.append(f"  … (truncated; {len(visited)} nodes shown, max {max_nodes})")
        return "\n".join(f"  {ln}" for ln in lines)
