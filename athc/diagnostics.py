"""Compile-error rendering with source snippets.

All compile errors (lex / parse / sema / loader) flow through
`render_diagnostic` for display. The renderer prints a file/line/column
header, the offending source line with one line of leading context, and
a caret pointing at the column."""

from pathlib import Path


def render_diagnostic(
    kind: str,
    msg: str,
    path: Path | str | None,
    source_text: str | None,
    line: int = 0,
    col: int = 0,
    context_before: int = 1,
    help: str | None = None,
) -> str:
    path_str = str(path) if path else "<unknown>"

    def with_help(text: str) -> str:
        return f"{text}\n  help: {help}" if help else text

    # No source or no position: bare one-line message
    if not source_text or line < 1:
        if line > 0 and col > 0:
            return with_help(f"athc: {path_str}:{line}:{col}: {kind}: {msg}")
        return with_help(f"athc: {path_str}: {kind}: {msg}")

    lines = source_text.splitlines()
    if not lines:
        return with_help(f"athc: {path_str}:{line}:{col}: {kind}: {msg}")

    # Clamp line numbers past EOF (unterminated brace, etc.)
    if line > len(lines):
        line = len(lines)

    header = f"athc: {path_str}:{line}:{col}: {kind}: {msg}"
    out = [header]

    start = max(1, line - context_before)
    width = len(str(line))
    for i in range(start, line + 1):
        text = lines[i - 1] if i - 1 < len(lines) else ""
        out.append(f"  {i:>{width}} | {text}")

    pad = " " * max(0, col - 1)
    out.append(f"  {' ':>{width}} | {pad}^")
    if help:
        out.append(f"  help: {help}")

    return "\n".join(out)
