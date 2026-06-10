#!/usr/bin/env python3
"""Check that editors/vim/syntax/ath.vim stays in sync with the lexer and parser.

Run from the repo root:
    python editors/sync_check.py

Exits 0 when everything is in sync, 1 with diagnostics otherwise.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def _extract_lexer_keywords() -> set[str]:
    """Hard keywords from the KEYWORDS dict in athc/lexer.py."""
    src = (ROOT / "athc" / "lexer.py").read_text()
    return set(re.findall(r'"(\w+)":\s*TokenKind\.KW_', src))

def _extract_parser_soft_keywords() -> set[str]:
    """Soft keywords from _STATEMENT_KEYWORDS in athc/parser.py minus hard keywords."""
    src = (ROOT / "athc" / "parser.py").read_text()
    m = re.search(r'_STATEMENT_KEYWORDS\s*=\s*frozenset\(\{([^}]+)\}', src, re.DOTALL)
    if not m:
        sys.exit("ERROR: cannot find _STATEMENT_KEYWORDS in athc/parser.py")
    all_stmt = set(re.findall(r'"(\w+)"', m.group(1)))
    hard = _extract_lexer_keywords()
    return all_stmt - hard

# Vim syntax directives that appear after `syn keyword <group>` but are not
# language keywords.
_VIM_DIRECTIVES = {"contained", "nextgroup", "skipwhite", "skipnl", "skipempty"}

# Groups whose words are NOT ~ATH keywords (e.g. athTodo holds TODO/FIXME).
_SKIP_GROUPS = {"athtodo"}

def _extract_vim_keywords() -> set[str]:
    """All `syn keyword` words from the vim syntax file (excluding group names)."""
    src = (ROOT / "editors" / "vim" / "syntax" / "ath.vim").read_text()
    words: set[str] = set()
    for line in src.splitlines():
        m = re.match(r'\s*syn\s+keyword\s+(\w+)\s+(.*)', line)
        if m:
            group = m.group(1).lower()
            if group in _SKIP_GROUPS:
                continue
            for w in m.group(2).lower().split():
                if w not in _VIM_DIRECTIVES:
                    words.add(w)
    return words

def main() -> int:
    hard = _extract_lexer_keywords()
    soft = _extract_parser_soft_keywords()
    vim = _extract_vim_keywords()

    # `print` is handled as a region, not a keyword — exclude it from the check
    expected = (hard | soft) - {"print"}

    missing = expected - vim
    extra = vim - expected - {
        # Contextual sub-keywords and conditionals that are correct but live
        # outside the KEYWORDS/soft-keyword sets:
        "builtin", "number", "signal", "pid", "mtime",  # athType
        "null", "this",                                   # athConstant
    }

    ok = True
    if missing:
        print("MISSING from vim syntax (present in lexer/parser):")
        for kw in sorted(missing):
            print(f"  {kw}")
        ok = False
    if extra:
        print("EXTRA in vim syntax (not in lexer/parser):")
        for kw in sorted(extra):
            print(f"  {kw}")
        ok = False
    if ok:
        print("vim syntax is in sync with lexer and parser ✓")
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
