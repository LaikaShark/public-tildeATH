import os
from pathlib import Path

from athc.ast import AthLoop, EveryStmt, ImportFuncStmt, Program, LoopStmt
from athc.lexer import LexError
from athc.parser import ParseError, parse


def _default_stdlib_dir() -> Path:
    """Locate the stdlib/ directory: bundled in the wheel (athc/_stdlib/),
    else the repo stdlib/ sibling of `athc` (editable/dev)."""
    here = Path(__file__).resolve().parent
    bundled = here / "_stdlib"
    return bundled if bundled.exists() else here.parent / "stdlib"


def _search_path_dirs() -> list[Path]:
    """Ordered directory list for angle-bracket importf resolution (§5.4)."""
    dirs: list[Path] = []
    env = os.environ.get("ATH_PATH", "")
    for entry in env.split(":") if env else []:
        if entry:
            dirs.append(Path(entry))
    dirs.append(_default_stdlib_dir())
    return dirs


def _resolve_search_path(stem: str) -> Path | None:
    """Find STEM.ath in ATH_PATH dirs + default stdlib. None if missing."""
    filename = f"{stem}.ath"
    for d in _search_path_dirs():
        candidate = (d / filename).resolve()
        if candidate.exists():
            return candidate
    return None


class LoaderError(Exception):
    def __init__(
        self,
        msg: str,
        *,
        path: Path | None = None,
        source_text: str | None = None,
        line: int = 0,
        col: int = 0,
        help: str | None = None,
    ):
        super().__init__(msg)
        self.msg = msg
        self.path = path
        self.source_text = source_text
        self.line = line
        self.col = col
        self.help = help


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
            e.msg, path=main_path, source_text=src, line=e.line, col=e.col,
            help=e.help
        ) from e

    main_program.source_path = main_path
    function_table: dict[str, Program] = {}
    registered: dict[str, Path] = {}
    _resolve_imports(
        main_program, main_path.parent, function_table, set(), registry,
        main_path, registered,
    )
    return main_program, function_table, registry


def _iter_importfs(stmts):
    for s in stmts:
        if isinstance(s, ImportFuncStmt):
            yield s
        elif isinstance(s, (AthLoop, LoopStmt, EveryStmt)):
            yield from _iter_importfs(s.body)


def _resolve_imports(
    program: Program,
    base_dir: Path,
    table: dict[str, Program],
    visiting: set,
    registry: dict[Path, str],
    importing_from: Path,
    registered: dict[str, Path],
) -> None:
    importing_text = registry.get(importing_from)
    for imp in _iter_importfs(program.statements):
        if imp.search_path:
            resolved = _resolve_search_path(imp.path)
            if resolved is None:
                searched = [str(d) for d in _search_path_dirs()]
                raise LoaderError(
                    f"importf <{imp.path}>: not found in ATH_PATH or stdlib "
                    f"(searched {searched})",
                    path=importing_from,
                    source_text=importing_text,
                    line=imp.line,
                    col=imp.col,
                )
            target = resolved
        else:
            target = (base_dir / imp.path).resolve()
        # Function name binds to one file: rebinding same name to same file is idempotent (diamond imports); different file is a conflict
        name = imp.name.lower()
        prior = registered.get(name)
        if prior is not None:
            if prior == target:
                continue
            raise LoaderError(
                f"importf as '{imp.name}': name already bound to {prior}; "
                f"cannot rebind to {target}",
                path=importing_from,
                source_text=importing_text,
                line=imp.line,
                col=imp.col,
                help="each importf name must map to a single file",
            )
        registered[name] = target
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
                e.msg, path=target, source_text=src, line=e.line, col=e.col,
                help=e.help
            ) from e
        fn_program.source_path = target
        table[imp.name.lower()] = fn_program
        visiting.add(target)
        try:
            _resolve_imports(
                fn_program, target.parent, table, visiting, registry,
                target, registered,
            )
        finally:
            visiting.discard(target)
