from pathlib import Path

from athc.ast import AthLoop, ImportFuncStmt, Program
from athc.parser import ParseError, parse


class LoaderError(Exception):
    pass


def load_program(main_path: Path) -> tuple[Program, dict[str, Program]]:
    main_path = main_path.resolve()
    try:
        src = main_path.read_text()
    except OSError as e:
        raise LoaderError(f"could not read {main_path}: {e}") from e
    try:
        main_program = parse(src)
    except ParseError as e:
        raise LoaderError(f"{main_path}: {e}") from e
    function_table: dict[str, Program] = {}
    _resolve_imports(main_program, main_path.parent, function_table, set())
    return main_program, function_table


def _iter_importfs(stmts):
    for s in stmts:
        if isinstance(s, ImportFuncStmt):
            yield s
        elif isinstance(s, AthLoop):
            yield from _iter_importfs(s.body)


def _resolve_imports(
    program: Program,
    base_dir: Path,
    table: dict[str, Program],
    visiting: set,
) -> None:
    for imp in _iter_importfs(program.statements):
        target = (base_dir / imp.path).resolve()
        if target in visiting:
            raise LoaderError(
                f"line {imp.line}, col {imp.col}: circular importf of {target}"
            )
        if not target.exists():
            raise LoaderError(
                f"line {imp.line}, col {imp.col}: importf: file not found: "
                f"{imp.path} (resolved to {target})"
            )
        try:
            src = target.read_text()
        except OSError as e:
            raise LoaderError(
                f"line {imp.line}, col {imp.col}: importf: cannot read {target}: {e}"
            ) from e
        try:
            fn_program = parse(src)
        except ParseError as e:
            raise LoaderError(f"{target}: {e}") from e
        table[imp.name.lower()] = fn_program
        visiting.add(target)
        try:
            _resolve_imports(fn_program, target.parent, table, visiting)
        finally:
            visiting.discard(target)
