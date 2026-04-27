from pathlib import Path

from athc.ast import AthLoop, ImportFuncStmt, Program
from athc.lexer import LexError
from athc.parser import ParseError, parse


class LoaderError(Exception):
    def __init__(
        self,
        msg: str,
        *,
        path: Path | None = None,
        source_text: str | None = None,
        line: int = 0,
        col: int = 0,
    ):
        super().__init__(msg)
        self.msg = msg
        self.path = path
        self.source_text = source_text
        self.line = line
        self.col = col


def load_program(
    main_path: Path,
) -> tuple[Program, dict[str, Program], dict[Path, str]]:
    main_path = main_path.resolve()
    try:
        src = main_path.read_text()
    except OSError as e:
        raise LoaderError(
            f"could not read {main_path}: {e}", path=main_path
        ) from e

    registry: dict[Path, str] = {main_path: src}

    try:
        main_program = parse(src)
    except (LexError, ParseError) as e:
        raise LoaderError(
            e.msg, path=main_path, source_text=src, line=e.line, col=e.col
        ) from e

    main_program.source_path = main_path
    function_table: dict[str, Program] = {}
    _resolve_imports(
        main_program, main_path.parent, function_table, set(), registry, main_path
    )
    return main_program, function_table, registry


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
    registry: dict[Path, str],
    importing_from: Path,
) -> None:
    importing_text = registry.get(importing_from)
    for imp in _iter_importfs(program.statements):
        target = (base_dir / imp.path).resolve()
        if target in visiting:
            raise LoaderError(
                f"circular importf of {target}",
                path=importing_from,
                source_text=importing_text,
                line=imp.line,
                col=imp.col,
            )
        if not target.exists():
            raise LoaderError(
                f"importf: file not found: {imp.path} (resolved to {target})",
                path=importing_from,
                source_text=importing_text,
                line=imp.line,
                col=imp.col,
            )
        try:
            src = target.read_text()
        except OSError as e:
            raise LoaderError(
                f"importf: cannot read {target}: {e}",
                path=importing_from,
                source_text=importing_text,
                line=imp.line,
                col=imp.col,
            ) from e
        registry[target] = src
        try:
            fn_program = parse(src)
        except (LexError, ParseError) as e:
            raise LoaderError(
                e.msg, path=target, source_text=src, line=e.line, col=e.col
            ) from e
        fn_program.source_path = target
        table[imp.name.lower()] = fn_program
        visiting.add(target)
        try:
            _resolve_imports(fn_program, target.parent, table, visiting, registry, target)
        finally:
            visiting.discard(target)
