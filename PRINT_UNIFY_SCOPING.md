# Plan: unify `print` / `PRINT2` via `$var` interpolation

Planning doc — **not** the spec. Fold into `SPEC.md`/`TUTORIAL.md` as it lands,
then trim. Sibling of `FLOAT_SCOPING.md`.

## Locked decision (2026-06-02)

Collapse the two printing statements into **one keyword, `print`**, which stays
raw-text by default and gains `$NAME` interpolation of a runtime string object.
`PRINT2` is removed. All existing literal `print` statements are **unchanged**;
only `PRINT2` sites migrate (`PRINT2 X;` → `print $X;`).

```
print Hello world;        // literal — identical to today
print $S;                 // was: PRINT2 S
print Count: $N done.;    // mixed: literal + interpolation + literal
```

## 1. Surface syntax / lexing rules (§2.4 rewrite)

After `print` + exactly one ASCII space (unchanged), the lexer stays in raw
mode but now splits the payload into an ordered list of **parts**:

- A run of literal characters → a literal part (decoded bytes).
- `$` immediately followed by an identifier start `[A-Za-z_]` → an
  **interpolation part** naming a variable; the name consumes
  `[A-Za-z_][A-Za-z0-9_]*` (case-sensitive, like any IDENT).
- The payload still terminates at the first **unescaped `;`**.

Escapes (the §2.4 table) gain one row:

| Source | Decoded |
|--------|---------|
| `\$`   | `$` (U+0024) |

(existing `\;` `\\` `\n` `\t` `\r` unchanged.)

**Bare `$` rule:** a `$` *not* followed by an identifier-start character is a
**lexical error** ("a literal `$` must be written `\$`"). This keeps the marker
unambiguous and matches §2.4's strict-escape philosophy. (`$` is not a lexer
character anywhere else today — verified — so this is additive.)

Empty payload still legal (`print ` then `;` → emits just the newline).

## 2. Lexer implementation (`athc/lexer.py`)

`_read_print_payload` currently emits one `RAWTEXT`. Change it to emit an
ordered token run, one token per part, consumed by the parser up to the `;`:

- `RAWTEXT` token (existing kind) for each literal run — value = decoded bytes.
- new `PRINTVAR` token for each `$NAME` — value = the bare name.

Adjacent literal/interp parts alternate naturally; two adjacent `$a$b` emit two
`PRINTVAR`s with no literal between. A leading/trailing/again-empty literal run
simply emits no `RAWTEXT` for that gap. The `;` is emitted as today.

(Alternative considered: a single structured token carrying a parts list. The
token-run form is more in keeping with the existing lexer and keeps the parser
in charge of AST shape.)

## 3. AST (`athc/ast.py`)

Replace `PrintStmt(text: str)` with parts:

```python
@dataclass
class PrintStmt:
    parts: list[PrintPart]   # ordered
    line: int; col: int

PrintPart = PrintLiteral | PrintVar
class PrintLiteral: text: str            # decoded bytes
class PrintVar:     name: str; line/col  # for sema diagnostics
```

`Print2Stmt` is **deleted**.

## 4. Parser (`athc/parser.py`)

- `_parse_print`: after the `print` keyword, loop collecting `RAWTEXT` →
  `PrintLiteral` and `PRINTVAR` → `PrintVar` until `SEMI`; build `PrintStmt`.
- Delete `_parse_print2` and its dispatch (`KW_PRINT2` branch).

## 5. Sema (`athc/sema.py`)

Each `PrintVar.name` is a **read position** → runs through the existing
unbound-name check (§6.1). This is a *strict improvement*: a typo'd variable in
an interpolation is now a compile error, where a typo in old RAWTEXT silently
printed the literal text. (Worth calling out in the changelog/tutorial.)

## 6. Runtime / ABI (`runtime/ath_runtime.h`, `runtime_common.c`)

Both existing primitives hard-code a trailing `\n` (`ath_print` line ~146,
`ath_print_obj` line ~224). A unified `print` must emit exactly **one** LF per
*statement*, so add newline-free variants:

```c
void ath_print_bytes(const char *text, size_t len);  /* no trailing LF */
void ath_print_obj_raw(ath_obj *s);                  /* §4.6 walk, no LF */
```

- Refactor: `ath_print`      = `ath_print_bytes` + LF (keep, or retire).
  `ath_print_obj` = `ath_print_obj_raw` + LF (keep, or retire).
- `ath_print_obj_raw` carries the §4.4.8 walk verbatim minus the final
  `fputc('\n')`: stop on dead / NULL / non-char-atom; never crash (§6.2).
- §5.2 ABI table + §5.1-adjacent docs updated.

Old `ath_print`/`ath_print_obj` become dead once codegen switches and PRINT2 is
gone; retain them (harmless, still spec-listed) or drop in the same commit.

## 7. Codegen (`athc/codegen.py`)

`_emit_print` rewrite — walk `stmt.parts` in order:

- `PrintLiteral` → make string global, `ath_print_bytes(ptr, len)`.
- `PrintVar`     → `ath_print_obj_raw(read_var(name))`.

After the loop, emit the single line feed: `ath_print_bytes("\n", 1)`
(or a dedicated `ath_print_nl()`). Delete `_emit_print2`.

Net: one statement → N part-calls + 1 newline call. Output for a pure-literal
`print` is byte-identical to today (one literal call + newline == old
`ath_print` with LF).

## 8. PRINT2 removal + migration

- Remove `KW_PRINT2` (lexer KEYWORDS), `print2-stmt` (grammar §3), the §4.4.8
  section (rewrite as "removed; see §4.4.6 interpolation"), and the §2.2
  reserved-word entry.
- **Codemod** over `.ath` (examples, stdlib, tests/fixtures):
  `PRINT2 <IDENT>;` → `print $<IDENT>;`. ~67 sites; regex:
  `s/\bPRINT2\s+([A-Za-z_][A-Za-z0-9_]*)\s*;/print $\1;/` (case-insensitive on
  the keyword). Hand-check any multiline/odd cases.
- Python tests asserting on `PRINT2` tokens/AST nodes get updated to the new
  `print` parts shape.

## 9. Spec / Tutorial updates

- §2.4 — rewrite for parts + `$` rule + `\$` row.
- §2.2 — drop `PRINT2` keyword from KEYWORD list and reserved-words list.
- §3 — `print-stmt` new shape (`'print' RAWTEXT-with-interpolation ';'`,
  described as a part sequence); delete `print2-stmt` from the statement list.
- §4.4.6 — document interpolation, the dead/NULL/non-string walk-stop for
  `$VAR` (inherited from old §4.4.8), the single trailing newline, the
  unbound-var-is-now-a-compile-error note.
- §4.4.8 — remove (leave a one-line "merged into §4.4.6" stub or renumber).
- §5.1/§5.2 — add `ath_print_bytes` / `ath_print_obj_raw`; note disposition of
  `ath_print` / `ath_print_obj`.
- TUTORIAL.md — replace every PRINT2 example; show interpolation.

## 10. Semantics / edge cases to pin

- `$VAR` where VAR is dead / NULL / not a well-formed string → walk stops early
  exactly as PRINT2 did (§4.4.8 step 3); partial/empty output, never a crash.
- Interpolating a **number** payload does nothing useful (it's not a char list);
  to print a number, `to_string` it first, as today. (Optionally a future
  convenience: auto-`to_string` numeric payloads in interpolation — out of scope,
  list under deferred.)
- Mixed newline accounting: exactly one trailing LF per `print` statement,
  regardless of part count (including zero parts).
- `\$`, `\;`, `\\` interactions unchanged otherwise.

## 11. Test plan + the hard invariant

- **Conformance is the safety net:** after the codemod + codegen switch, every
  example's stdout MUST be **byte-identical** to before, in *both* compose
  modes. Capture golden stdout for all examples pre-change, diff post-change.
- New unit tests: lexer parts split, `\$` escape, bare-`$` error, `$name`
  boundary (`$a.`, `$a$b`, `$a;`), unbound-`$var` sema error.
- Runtime tests: `ath_print_obj_raw` no-LF, dead/partial walk.
- E2E: `print Count: $N done.;` with N a to_string'd number; adjacent interps.

## 12. Phasing

- **U1 — runtime:** add `ath_print_bytes` / `ath_print_obj_raw` (+ refactor old
  two to delegate). Green, no behavior change.
- **U2 — frontend:** lexer parts + `PRINTVAR`, AST parts, parser, codegen; keep
  `PRINT2` working in parallel for one step if convenient, else cut together.
- **U3 — remove PRINT2 + codemod** examples/stdlib/tests; conformance diff clean.
- **U4 — spec/tutorial.**

Each step keeps `make test-runtime` (both modes) + `pytest` green.

## Deferred / optional

- Auto-`to_string` of a numeric payload in `$interp` (ergonomic, but couples
  print to the numeric ABI; revisit after #5 float work).
- Keeping `PRINT2` as a deprecated alias instead of hard removal (rejected for
  cleanliness given the small surface and mechanical codemod; revisit only if a
  back-compat guarantee is wanted).
