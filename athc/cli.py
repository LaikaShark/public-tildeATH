import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from athc.codegen import CodegenError, emit_object, generate_ir
from athc.diagnostics import render_diagnostic
from athc.loader import LoaderError, load_program
from athc.sema import SemaError, analyze


def _default_runtime(mode: str = "fresh") -> Path:
    return (
        Path(__file__).resolve().parent.parent
        / "runtime"
        / f"libath_{mode}.a"
    )


def _parse_lifetime_spec(spec: str) -> tuple[str, float, float]:
    """Parse a --define-lifetime NAME:MIN:MAX value. Names may contain
    spaces; only the last two `:` characters delimit the numbers."""
    parts = spec.rsplit(":", 2)
    if len(parts) != 3:
        raise ValueError(
            f"--define-lifetime: expected NAME:MIN:MAX, got {spec!r}"
        )
    name, min_str, max_str = parts
    if not name.strip():
        raise ValueError("--define-lifetime: name cannot be empty")
    try:
        min_s = float(min_str)
        max_s = float(max_str)
    except ValueError as e:
        raise ValueError(
            f"--define-lifetime: min and max must be numbers; got "
            f"{min_str!r} and {max_str!r}"
        ) from e
    if min_s < 0.0 or max_s < 0.0:
        raise ValueError("--define-lifetime: min and max must be non-negative")
    if min_s > max_s:
        raise ValueError(
            f"--define-lifetime: min ({min_s}) must not exceed max ({max_s})"
        )
    return name, min_s, max_s


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="athc", description="Compiler for ~ATH.")
    ap.add_argument(
        "source", nargs="?", help="path to .ath source file (omit for the REPL)"
    )
    ap.add_argument(
        "--repl",
        action="store_true",
        help="start the interactive REPL (the default when no source is given)",
    )
    ap.add_argument("-o", "--output", default="a.out", help="output executable path")
    ap.add_argument(
        "--emit-ir", action="store_true", help="print LLVM IR to stdout and exit"
    )
    ap.add_argument(
        "--emit-obj", metavar="PATH", help="write object file to PATH and exit"
    )
    ap.add_argument(
        "--compose",
        choices=["fresh", "intern"],
        default="fresh",
        help=(
            "composition discipline (default: fresh). "
            "intern hash-conses ath_compose by raw pointer pair, so "
            "structurally equal composites share storage and die together."
        ),
    )
    ap.add_argument(
        "--runtime",
        metavar="PATH",
        help="path to runtime library (default: libath_<compose>.a)",
    )
    ap.add_argument(
        "--cc",
        default=os.environ.get("CC", "gcc"),
        help="C compiler/linker to invoke (default: gcc, or $CC)",
    )
    ap.add_argument(
        "-D",
        "--define-lifetime",
        action="append",
        default=[],
        metavar="NAME:MIN:MAX",
        help=(
            "register a custom library entry (lifetime in seconds). "
            "Repeatable. NAME may contain spaces. User entries override "
            "built-ins of the same name."
        ),
    )
    args = ap.parse_args(argv)

    if args.repl or args.source is None:
        from athc.repl import Repl
        from athc.runtime_ffi import RuntimeNotBuilt
        try:
            Repl(mode=args.compose).run()
        except RuntimeNotBuilt as e:
            print(f"athc: {e}", file=sys.stderr)
            return 1
        return 0

    user_lifetimes: list[tuple[str, float, float]] = []
    for spec in args.define_lifetime:
        try:
            user_lifetimes.append(_parse_lifetime_spec(spec))
        except ValueError as e:
            print(f"athc: {e}", file=sys.stderr)
            return 1

    source_path = Path(args.source)
    sources: dict[Path, str] = {}

    try:
        program, function_table, loaded_sources = load_program(source_path)
        sources.update(loaded_sources)
    except LoaderError as e:
        print(
            render_diagnostic(
                kind="error",
                msg=e.msg,
                path=e.path or source_path,
                source_text=e.source_text,
                line=e.line,
                col=e.col,
                help=e.help,
            ),
            file=sys.stderr,
        )
        return 1

    try:
        analyze(program, function_table)
    except SemaError as e:
        error_path = e.path or program.source_path or source_path.resolve()
        print(
            render_diagnostic(
                kind="error",
                msg=e.msg,
                path=error_path,
                source_text=sources.get(error_path),
                line=e.line,
                col=e.col,
                help=e.help,
            ),
            file=sys.stderr,
        )
        return 1

    try:
        ir_text = generate_ir(
            program,
            function_table,
            module_name=source_path.stem or "ath",
            user_lifetimes=user_lifetimes,
        )
    except CodegenError as e:
        print(
            render_diagnostic(
                kind="error",
                msg=str(e),
                path=source_path,
                source_text=sources.get(source_path.resolve()),
            ),
            file=sys.stderr,
        )
        return 1

    if args.emit_ir:
        sys.stdout.write(ir_text)
        return 0

    obj_bytes = emit_object(ir_text)

    if args.emit_obj:
        Path(args.emit_obj).write_bytes(obj_bytes)
        return 0

    runtime = (
        Path(args.runtime) if args.runtime else _default_runtime(args.compose)
    )
    if not runtime.exists():
        print(
            f"athc: runtime library not found at {runtime}; run 'make runtime'",
            file=sys.stderr,
        )
        return 1

    with tempfile.NamedTemporaryFile(suffix=".o", delete=False) as f:
        f.write(obj_bytes)
        obj_path = f.name
    try:
        result = subprocess.run(
            [args.cc, obj_path, str(runtime), "-lm", "-o", args.output],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stderr)
            print(f"athc: linker failed (exit {result.returncode})", file=sys.stderr)
            return result.returncode
    finally:
        os.unlink(obj_path)

    return 0


if __name__ == "__main__":
    sys.exit(main())
