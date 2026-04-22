import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from athc.codegen import emit_object, generate_ir
from athc.parser import ParseError, parse
from athc.sema import SemaError, analyze


def _default_runtime() -> Path:
    return Path(__file__).resolve().parent.parent / "runtime" / "libath_fresh.a"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="athc", description="Compiler for ~ATH.")
    ap.add_argument("source", help="path to .ath source file")
    ap.add_argument("-o", "--output", default="a.out", help="output executable path")
    ap.add_argument(
        "--emit-ir", action="store_true", help="print LLVM IR to stdout and exit"
    )
    ap.add_argument(
        "--emit-obj", metavar="PATH", help="write object file to PATH and exit"
    )
    ap.add_argument(
        "--runtime",
        metavar="PATH",
        help="path to libath_fresh.a (default: $project/runtime/libath_fresh.a)",
    )
    ap.add_argument(
        "--cc",
        default=os.environ.get("CC", "gcc"),
        help="C compiler/linker to invoke (default: gcc, or $CC)",
    )
    args = ap.parse_args(argv)

    source_path = Path(args.source)
    try:
        src = source_path.read_text()
    except OSError as e:
        print(f"athc: could not read {source_path}: {e}", file=sys.stderr)
        return 1

    try:
        program = parse(src)
    except ParseError as e:
        print(f"athc: {source_path}: {e}", file=sys.stderr)
        return 1

    try:
        analyze(program)
    except SemaError as e:
        print(f"athc: {source_path}: {e}", file=sys.stderr)
        return 1

    ir_text = generate_ir(program, module_name=source_path.stem or "ath")

    if args.emit_ir:
        sys.stdout.write(ir_text)
        return 0

    obj_bytes = emit_object(ir_text)

    if args.emit_obj:
        Path(args.emit_obj).write_bytes(obj_bytes)
        return 0

    runtime = Path(args.runtime) if args.runtime else _default_runtime()
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
            [args.cc, obj_path, str(runtime), "-o", args.output],
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
