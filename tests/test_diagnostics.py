"""Tests for the source-snippet diagnostic renderer (athc/diagnostics.py)
and its end-to-end use in the CLI."""

import subprocess
import sys
from pathlib import Path

import pytest

from athc.diagnostics import render_diagnostic
from athc.parser import ParseError, parse
from athc.sema import SemaError, analyze
from athc.suggest import closest, edit_distance

PROJECT_ROOT = Path(__file__).resolve().parent.parent


# --- Renderer unit tests ---------------------------------------------------


def test_renderer_basic_snippet():
    src = "import x A;\nBIFURCATE A[L, R]\nprint x;\n"
    out = render_diagnostic(
        "error",
        "expected SEMI",
        Path("/tmp/p.ath"),
        src,
        line=3,
        col=1,
    )
    assert "/tmp/p.ath:3:1" in out
    assert "expected SEMI" in out
    assert "BIFURCATE A[L, R]" in out
    assert "print x;" in out
    assert "^" in out


def test_renderer_caret_alignment():
    src = "import x A;\n  BIFURCATE V[L,R];\n"
    out = render_diagnostic(
        "error", "msg", "p.ath", src, line=2, col=3
    )
    # Find the caret line and verify the caret is at column 3 of the visible content
    caret_line = next(ln for ln in out.splitlines() if "^" in ln)
    # The format is "  2 | source"; the caret is in the same "after |" region.
    pipe_idx = caret_line.index("|")
    after_pipe = caret_line[pipe_idx + 1 :]
    # Caret should be preceded by (col-1) spaces, plus the one space that
    # immediately follows the pipe.
    assert after_pipe == "   ^"


def test_renderer_no_source_falls_back_to_one_liner():
    out = render_diagnostic(
        "error", "ouch", Path("p.ath"), None, line=5, col=7
    )
    assert out == "athc: p.ath:5:7: error: ouch"


def test_renderer_no_position_omits_location():
    out = render_diagnostic(
        "error", "ouch", Path("p.ath"), "some text"
    )
    assert out == "athc: p.ath: error: ouch"


def test_renderer_handles_line_past_eof():
    # Line number past end clamps to last line; still renders a snippet.
    out = render_diagnostic(
        "error", "msg", "p.ath", "one\ntwo\n", line=99, col=1
    )
    assert "two" in out
    assert "99" not in out.splitlines()[0].split(":")[2:3]


def test_renderer_first_line_no_context():
    src = "line one\nline two\n"
    out = render_diagnostic(
        "error", "msg", "p.ath", src, line=1, col=1
    )
    lines = out.splitlines()
    # Header + one source line + caret line == 3 lines total.
    assert len(lines) == 3
    assert "line one" in lines[1]


# --- End-to-end: real compile errors ---------------------------------------


def _compile(source_path: Path, tmp_path: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            sys.executable,
            "-m",
            "athc.cli",
            str(source_path),
            "-o",
            str(tmp_path / "prog"),
        ],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
    )


def test_e2e_parse_error_renders_snippet(tmp_path):
    src = tmp_path / "bad.ath"
    src.write_text(
        "import x A;\n"
        "BIFURCATE A[L, R]\n"           # missing semicolon
        "print after;\n"
        "THIS.DIE();\n"
    )
    result = _compile(src, tmp_path)
    assert result.returncode != 0
    out = result.stderr
    assert str(src) in out
    assert "expected ';'" in out
    assert "BIFURCATE A[L, R]" in out
    assert "^" in out


def test_e2e_sema_error_renders_snippet(tmp_path):
    src = tmp_path / "unbound.ath"
    src.write_text(
        "print fine here;\n"
        "this.DIE();\n"                 # lowercase, not in scope
    )
    result = _compile(src, tmp_path)
    assert result.returncode != 0
    out = result.stderr
    assert "not in scope" in out
    assert "this.DIE();" in out
    assert "^" in out


def test_e2e_lex_error_renders_snippet(tmp_path):
    src = tmp_path / "lex.ath"
    src.write_text(
        'import x A;\n'
        'importf "no_close_quote as F;\n'
    )
    result = _compile(src, tmp_path)
    assert result.returncode != 0
    out = result.stderr
    assert "unterminated string" in out
    assert "^" in out


def test_e2e_importf_missing_file_renders_against_main_source(tmp_path):
    src = tmp_path / "main.ath"
    src.write_text(
        'importf "nope.ath" as F;\n'
        "THIS.DIE();\n"
    )
    result = _compile(src, tmp_path)
    assert result.returncode != 0
    out = result.stderr
    assert "file not found" in out
    # Caret renders against main.ath (where the importf statement is).
    assert "importf" in out
    assert "^" in out


def test_e2e_sema_error_in_imported_function_renders_against_that_file(tmp_path):
    (tmp_path / "buggy.ath").write_text(
        "print ok;\n"
        "some_unbound.DIE();\n"         # error here
        "THIS.DIE();\n"
    )
    main = tmp_path / "main.ath"
    main.write_text(
        'importf "buggy.ath" as F;\n'
        "import x A;\n"
        "import y B;\n"
        "F [A, B] R;\n"
        "THIS.DIE();\n"
    )
    result = _compile(main, tmp_path)
    assert result.returncode != 0
    out = result.stderr
    assert "buggy.ath" in out
    assert "some_unbound" in out
    assert "not in scope" in out
    # Must NOT render against main.ath
    assert "main.ath" not in out or "main.ath" in out and out.count("main.ath") < out.count("buggy.ath")


# --- task #13: humanized tokens, suggestions, keyword typos ----------------


def _check(src: str) -> None:
    analyze(parse(src))


def test_edit_distance_basic():
    assert edit_distance("", "") == 0
    assert edit_distance("abc", "abc") == 0
    assert edit_distance("printt", "print") == 1   # insertion
    assert edit_distance("improt", "import") == 1   # transposition (Damerau)
    assert edit_distance("nmae", "name") == 1       # transposition
    assert edit_distance("cat", "dog") == 3


def test_closest_picks_near_and_skips_far():
    cands = {"add", "sub", "mul", "div"}
    assert closest("ADXD", cands, fold=True) == "add"
    assert closest("zzzzzz", cands) is None
    assert closest("add", cands, fold=True) is None   # identical → no suggestion


def test_closest_short_name_guard():
    assert closest("x", {"y", "z"}) is None


def test_parse_error_humanizes_tokens():
    with pytest.raises(ParseError) as ei:
        parse("BIFURCATE V[L];")
    msg = ei.value.msg
    assert "expected ',' but found ']'" in msg
    assert "COMMA" not in msg and "RBRACKET" not in msg


def test_parse_error_missing_semicolon():
    with pytest.raises(ParseError) as ei:
        parse("import number 1 as N\nTHIS.DIE();")
    assert "expected ';' but found 'THIS'" in ei.value.msg


def test_unbound_variable_suggests_in_scope_name():
    with pytest.raises(SemaError) as ei:
        _check('text "x" as name;\nprint hi $nmae;')
    assert ei.value.help == "did you mean 'name'?"


def test_unbound_variable_no_suggestion_when_far():
    with pytest.raises(SemaError) as ei:
        _check("print hi $zzzzzz;")
    assert ei.value.help is None


def test_unknown_function_suggests_registered_name():
    # The loader normally populates the function table; pass it directly here.
    prog = parse("import number 1 as N;\nADXD [N, N] R;")
    with pytest.raises(SemaError) as ei:
        analyze(prog, {"ADD": object()})
    assert ei.value.help == "did you mean 'ADD'?"   # casing echoed


def test_keyword_typo_suggests_keyword():
    with pytest.raises(ParseError) as ei:
        parse("printt hello;")
    assert ei.value.msg == "unknown statement 'printt'"
    assert ei.value.help == "did you mean the keyword 'print'?"


def test_keyword_typo_transposition():
    with pytest.raises(ParseError) as ei:
        parse("improt number 1 as N;")
    assert ei.value.help == "did you mean the keyword 'import'?"


def test_valid_funcall_not_hijacked():
    p = parse("ADD [A, B] C;")
    assert p.statements  # parsed without raising


def test_render_diagnostic_appends_help():
    out = render_diagnostic(
        kind="error", msg="boom", path="x.ath",
        source_text="print hi;\n", line=1, col=1, help="try this",
    )
    assert out.splitlines()[-1] == "  help: try this"


def test_render_diagnostic_no_help_line_without_help():
    out = render_diagnostic(
        kind="error", msg="boom", path="x.ath",
        source_text="print hi;\n", line=1, col=1,
    )
    assert "help:" not in out
