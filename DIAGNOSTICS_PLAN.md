# Plan: better compiler diagnostics

Planning doc — **not** the spec. Trim once shipped. Sibling of the float/print
plans (now deleted). Task #13 (ergonomics).

## Locked scope (2026-06-06)

Three improvements, in priority order:
1. **Humanize token names** in parser messages (no more `COMMA`/`SEMI`/`IDENT`).
2. **"Did you mean?" suggestions** for unbound variables and unknown functions.
3. **Keyword-typo detection** — `printt hello;` → suggest the keyword `print`.

Out of scope (deferred): caret span underlines (`^~~~`), multi-error batching
(parser recovery). Both are noted in §7.

## Current state

Mechanics are solid — every error already renders `athc: path:line:col: error:
msg` + one context line + a caret, via `diagnostics.render_diagnostic`. Errors
carry `msg`/`line`/`col` (`SemaError` and `LoaderError` also `path`). The gaps
are message *content*:

- Parser messages leak token-kind enum names: `expected COMMA, got RBRACKET
  (']')`, `expected SEMI, got IDENT ('THIS')`, `expected LBRACKET`.
- No suggestions: `variable 'nmae' is not in scope`, `function 'ADXD' is not
  declared …`, and a misspelled statement keyword (`printt`) produces a
  confusing downstream `expected LBRACKET, got SEMI`.

Test coupling is light: only ~3 tests match raw token-kind names and ~2 match
`expected …`; the 27 sema assertions key on preserved semantic text (`not in
scope`, `not declared`). So humanizing is cheap.

## 0. Shared infra: a `help:` line

- Add an optional `help: str | None = None` field to `LexError`, `ParseError`,
  `SemaError`, and `LoaderError` (the loader already wraps lex/parse errors —
  propagate `help` when re-raising).
- Extend `render_diagnostic(...)` with `help: str | None = None`; when present,
  append a final `  help: <text>` line under the caret.
- `cli.py` passes `e.help` through at each `render_diagnostic` call site
  (loader / sema / codegen).

This is the one cross-cutting change; D2 and D3 hang off it.

## 1. Humanize token names (D1)

Add a token-display map + helpers in `lexer.py` (next to `TokenKind`):

```python
_TOKEN_DISPLAY = {
    TokenKind.SEMI: "';'", TokenKind.COMMA: "','",
    TokenKind.LBRACKET: "'['", TokenKind.RBRACKET: "']'",
    TokenKind.LPAREN: "'('", TokenKind.RPAREN: "')'",
    TokenKind.LBRACE: "'{'", TokenKind.RBRACE: "'}'",
    TokenKind.LANGLE: "'<'", TokenKind.RANGLE: "'>'",
    TokenKind.DOTDOT: "'..'", TokenKind.BANG: "'!'",
    TokenKind.IDENT: "a name", TokenKind.INT: "a number",
    TokenKind.FLOAT: "a number", TokenKind.BIGINT: "a number",
    TokenKind.STRING: "a string literal", TokenKind.RAWTEXT: "text",
    TokenKind.DIE: "'.DIE'", TokenKind.ATH: "'~ATH'",
    TokenKind.EOF: "end of input",
    # KW_* fall through to the keyword spelled in quotes, e.g. "'as'".
}

def describe_kind(kind) -> str:        # what was EXPECTED
    if kind in _TOKEN_DISPLAY: return _TOKEN_DISPLAY[kind]
    if kind.name.startswith("KW_"): return f"'{kind.name[3:].lower()}'"
    return kind.name.lower()

def describe_token(tok) -> str:        # what was FOUND (include the value)
    if tok.kind is TokenKind.EOF: return "end of input"
    if tok.kind in (IDENT/INT/FLOAT/BIGINT/STRING): return f"'{tok.value}'"
    return describe_kind(tok.kind)
```

Rewrite the parser's central `_expect` and the bespoke `ParseError(...)` sites:

- `expected COMMA, got RBRACKET (']')` → `expected ',' but found ']'`
- `expected SEMI, got IDENT ('THIS')` → `expected ';' but found 'THIS'`
- `expected STRING` → `expected a string literal but found …`

`_expect` is the main funnel (one rewrite covers most). Then sweep the ~10
hand-written `ParseError(f"expected … got {tok.kind.name}")` sites in
`parser.py` to use `describe_kind`/`describe_token`.

Update the ~3–5 tests that match `expected SEMI`/etc. to the new wording.

## 2. "Did you mean?" suggestions (D2)

New `athc/suggest.py`:

```python
def edit_distance(a: str, b: str) -> int:   # Damerau-Levenshtein (adj. transposition = 1)
    ...
def closest(name, candidates, *, fold=False, max_distance=2) -> str | None:
    # return the single nearest candidate within threshold (ties → first by
    # sort), or None. `fold` lower-cases both sides (for case-insensitive
    # function names). Guard against silly matches: require
    # max_distance <= max(1, len(name)//2) so short names don't over-suggest.
```

Wire into `sema.py`:

- **Unbound variable** (`_check_read`): when a name isn't in `defined`, compute
  `closest(name, defined)` (case-sensitive — identifiers are). If found, attach
  `help="did you mean '<sug>'?"` to the `SemaError`.
- **Unknown function** (`_check_function`): `closest(name, registered_fn_names,
  fold=True)` (functions are case-insensitive). Attach the help line.

Result:
```
error: variable 'nmae' is not in scope
  1 | print hi $nmae;
    |          ^
  help: did you mean 'name'?
```

## 3. Keyword-typo detection (D3)

The failure mode: a statement-leading word that is a misspelled keyword lexes as
`IDENT`, so the parser routes it to the IDENT-led forms (funcall / subscript /
`.DIE`) and fails partway with a cryptic token error (`printt hello;` →
`expected '['`).

Approach — catch and reinterpret at the dispatch point in `_parse_statement`:

```python
STATEMENT_KEYWORDS = {"import","importf","bifurcate","print","input","watch",
    "branch","clone","sleep","timer","read","write","append","close","text",
    "loop","every"}   # words that begin a statement

# when the leading token is an IDENT, remember it, then try the IDENT-led parse;
# on ParseError, if the leading word is within edit-distance 1 (Damerau) of a
# statement keyword, raise a clearer error instead:
lead = self._peek()
try:
    return self._parse_die_or_funcall()
except ParseError as orig:
    if lead.kind is TokenKind.IDENT:
        kw = closest(lead.value, STATEMENT_KEYWORDS, fold=True, max_distance=1)
        if kw:
            raise ParseError(
                f"unknown statement '{lead.value}'",
                lead.line, lead.col,
                help=f"did you mean the keyword '{kw}'?",
            ) from orig
    raise
```

- Distance **1** (Damerau, so a transposition counts as 1: `improt`→`import`)
  keeps it conservative — only obvious typos, never hijacking a real funcall.
- Points the caret at the leading word, not the downstream token.

```
error: unknown statement 'printt'
  1 | printt hello;
    | ^
  help: did you mean the keyword 'print'?
```

(Variable identifiers legitimately lead funcalls, so we only *suggest* on a
parse failure — a valid `FOO [A,B] C;` is untouched because it parses.)

## 4. Edit-distance notes

- One shared Damerau-Levenshtein in `suggest.py`; D2 uses ≤2, D3 uses ≤1.
- Case folding: identifiers case-sensitive (`closest` default), keywords and
  function names case-insensitive (`fold=True`).
- Short-name guard so `closest("x", {...})` doesn't suggest every 1-char name.

## 5. Test plan

- **Update** the ~3–5 parser tests matching `expected SEMI`/`STRING`/token names
  to the humanized wording.
- **New** (`tests/test_diagnostics.py`, exercising the renderer + messages):
  - humanized parse errors (`BIFURCATE V[L];` → `expected ',' but found ']'`).
  - unbound-var suggestion (`$nmae` → help `did you mean 'name'?`); and the
    *no-suggestion* case (a name far from anything → no help line).
  - unknown-function suggestion (`ADXD` → `ADD`, case-insensitive).
  - keyword typo (`printt hello;` → `unknown statement 'printt'` + help).
  - `render_diagnostic` appends a `help:` line when given one.
- Run end-to-end through `python -m athc.cli … --emit-ir` to confirm the help
  line reaches stderr (the cli path).

## 6. Phasing

- **G1 — infra:** `help` field on the four error types + renderer + cli plumbing.
- **G2 — humanize:** token-display map + `_expect`/bespoke rewrites + test fixups.
- **G3 — suggestions:** `suggest.py` + sema unbound-var / unknown-function help.
- **G4 — keyword typo:** dispatch catch in `_parse_statement` + tests.

Pure Python; no runtime/C changes, no `make`. Each step keeps `pytest` green.

## 7. Deferred

- **Caret span underlines** (`^~~~`): needs token length plumbed onto every
  error; modest value once messages are humanized. Revisit if wanted.
- **Multi-error batching**: report many errors per compile instead of aborting
  at the first. A real parser-recovery effort; large for a small language.
- Misspelled keyword in a *non-leading* position (rare) — not handled.
