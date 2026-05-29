# ~ATH style guide (C-like)

A house style for `.ath` source. The aim is a loud, regular, C-like layout:
Allman braces, one operation per line, tight brackets, and SHOUTING names.

> This style deliberately diverges from the older terse examples in `examples/`
> (which cuddle braces, pack several statements per line, and space their call
> brackets). Treat this document as the target; reformat examples as they are
> touched.

The companion files are `SPEC.md` (the language) and `TUTORIAL.md` (the
intro). When a rule here ever contradicts the grammar, the grammar wins —
report it.

---

## 1. Whitespace and files

- **Indent** with 4 spaces per level. No tabs.
- **One statement per line.** The `;` hugs the final token (no space before it),
  and a newline follows it. Never put two operations on one line.
- **80 columns** max. One-op-per-line keeps statements short, so this is rarely
  a constraint.
- Files use **LF** endings, end with a **single trailing newline**, and carry
  **no trailing whitespace**.
- At most **one blank line** in a row. Use a single blank line to separate
  logical sections (imports from body, import groups from each other).

## 2. Casing

Keywords are case-insensitive to the lexer; variable identifiers are
case-sensitive. This style picks one casing for each role and holds to it.

- **Keywords: UPPERCASE.** `IMPORT`, `IMPORTF`, `AS`, `TEXT`, `BIFURCATE`,
  `BRANCH`, `ELSE`, `CLONE`, `PRINT`, `INPUT`, `WATCH`, `LOOP`, `EVERY`,
  `SLEEP`, `TIMER`, `READ`, `WRITE`, `APPEND`, `CLOSE`, `EXECUTE`.
- **Sub-keywords and metadata words: UPPERCASE.** The `NUMBER`/`BUILTIN` after
  `IMPORT`, the `SIGNAL`/`PID`/`MTIME` after `WATCH`, and library lifetime
  words such as `UNIVERSE`, `MAYFLY`, `ONCE`.
- **`~ATH` and `.DIE`** are written exactly as shown.
- **Bindings: `SCREAMING_SNAKE_CASE`.** Every variable and import alias —
  constants, counters, data, and the boolean "verdict" that drives a `BRANCH`
  or `~ATH` — is ALL-CAPS. Separate words with `_` (`NEW_DIG`); digits are fine
  (`C256`).
- **Comments: ordinary prose.** Lowercase sentences. Do not SHOUT in comments —
  they are the one quiet thing on the page, and that is the point.
- **`PRINT` payload: verbatim.** The literal text prints exactly as written;
  case is whatever the output should be. Interpolated names (`$VAR`) are ALL-CAPS
  because they name bindings.

### 2.1. The case-sensitive exceptions

Three things look like part of a statement but are really **names on disk or in
C**, and are case-sensitive. Keep them lowercase regardless of the UPPERCASE
rule:

- the stem in `IMPORTF <stem>` — it resolves to `stdlib/<stem>.ath`, so
  `IMPORTF <SUB>` fails to find `sub.ath`;
- a quoted import path, `IMPORTF "lib/helper.ath" AS HELPER;`;
- the C symbol in `IMPORT BUILTIN ath_add AS ...`.

```
IMPORTF <sub> AS SUB;        // <sub> is a filename; SUB is your alias
```

### 2.2. Names you may not use

Because the lexer case-folds keywords, an ALL-CAPS binding spelled like a
keyword **is** that keyword. `LOOP`, `READ`, `WRITE`, `TEXT`, `INPUT`,
`NUMBER`, `AS`, `ELSE`, `EVERY`, `WATCH`, … are unavailable as variable names in
any case. `THIS` (the current object) and `NULL` (the null literal) are reserved
by meaning — never bind them.

## 3. Brackets and spacing

One rule covers every bracketed form:

- **`[` is always tight** against whatever precedes it — a name, or a keyword.
- **No padding inside brackets**: `[A, B]`, never `[ A, B ]`.
- **One space after each comma**, none before: `[A, B]`.
- **`..` in a slice is tight**: `SRC[LO..HI]`.
- A **result/target** name is separated from the closing `]` by **one space**:
  `ADD[X, Y] R;`.
- **Condition parens are tight** and unpadded: `~ATH(RUN)`, `BRANCH(FITS)`,
  and the inverted forms `~ATH(!V)`, `BRANCH(!V)` (no space after `!`).
- **`.DIE` is tight**: `THIS.DIE();`, `VAR.DIE();`, `THIS.DIE(PAYLOAD);`.

The statement forms, with their canonical spacing:

```
ADD[X, Y] R;            // call on compose(X, Y) -> R
ORD[CH, NULL] N;        // unary call: pad the second slot with NULL
FOO ARG[L, R];          // call on ARG, decompose the result -> L, R
SRC[I] CH;              // subscript
SRC[LO..HI] PART;       // slice
BIFURCATE[A, B] T;      // compose -> T   (tight, even after the keyword)
BIFURCATE SRC[L, R];    // decompose SRC -> L, R
CLONE SRC AS DST;       // dep-free shallow copy
THIS.DIE();             // end the program (or VAR.DIE() to kill one object)
```

## 4. Blocks and braces

**Allman.** Every block head — `~ATH`, `BRANCH`, `ELSE`, `LOOP`, `EVERY` — puts
its opening `{` alone on the next line at the head's indentation; the body is
indented one level; the closing `}` sits alone, aligned under the head. A block
statement is **not** terminated by `;`.

```
~ATH(RUN)
{
    PRINT $N;
    SUB[N, ONE] N;
    GT[N, ZERO] RUN;
}
```

`LOOP` and `EVERY` count an existing variable (not a literal):

```
LOOP TLEN
{
    CONCAT[TAPE, ZCH] TAPE;
}
```

### 4.1. BRANCH and ELSE

- **Omit `ELSE` when there is no else-work.** `ELSE` is optional in the grammar.
- When there is else-work, write the **`ELSE` keyword** on its own line, aligned
  under its `BRANCH`. Never use the bare second-block `{...}{...}` sugar.

```
EQ[R, ZERO] ISEVEN;
BRANCH(ISEVEN)
{
    DIV[N, TWO] N;
}
ELSE
{
    MUL[N, THREE] N;
    ADD[N, ONE] N;
}
```

```
// no else? leave it off.
BRANCH(FITS)
{
    ADD[ONE, ZERO] PLACED;
}
```

### 4.2. The EXECUTE postfix

The `~ATH` loop-exit hook goes on its **own line** after the closing `}`,
aligned with the `~ATH`, and carries the `;`:

```
~ATH(RUN)
{
    WORK[STATE, NULL] STATE;
}
EXECUTE(CLEANUP);
```

## 5. Imports

Group **function imports first**, then a blank line, then **constants and text**.
One import per line, a single space before `AS`, no column alignment.

```
IMPORTF <sub> AS SUB;
IMPORTF <gt> AS GT;
IMPORTF <mod> AS MOD;

IMPORT NUMBER 10 AS N;
IMPORT NUMBER 1 AS ONE;
IMPORT NUMBER 0 AS ZERO;
TEXT "" AS OUT;
```

A single space before `AS` — do not pad into aligned columns.

## 6. Comments

- **`//` only.** Do not use `/* */`. A multi-line note is a run of `//` lines.
- A **space follows** `//`.
- Comments sit on **their own line above** the code they describe. A short note
  may trail a statement, separated from the `;` by at least one space (align a
  column of trailing notes when several cluster). Keep the line within 80
  columns; if a trailing note would overflow, move it to its own line above.
- A **file header is optional**: when present it is `//` lines giving the
  purpose, then the build/run invocation and expected output.

```
// rebind V to a dead object so the ~ATH(V) loop falls through
BIFURCATE NULL[J, V];

ADD[PC, ONE] PC;        // step the program counter
```

## 7. PRINT

`PRINT` takes a single space, then the payload, which runs to the `;`. Literal
text is verbatim; `$VAR` interpolates an ALL-CAPS binding; the escapes are
`\; \\ \n \t \r \$`.

```
PRINT $N is even;
PRINT total\: $COUNT;
```

## 8. A complete file

```
// count down from N, flagging the even values, then die.
//
//   athc countdown.ath -o countdown
//   ./countdown

IMPORTF <sub> AS SUB;
IMPORTF <gt> AS GT;
IMPORTF <eq> AS EQ;
IMPORTF <mod> AS MOD;

IMPORT NUMBER 10 AS N;
IMPORT NUMBER 2 AS TWO;
IMPORT NUMBER 1 AS ONE;
IMPORT NUMBER 0 AS ZERO;

GT[N, ZERO] ALIVE;
~ATH(ALIVE)
{
    // tag even numbers as we pass them
    MOD[N, TWO] R;
    EQ[R, ZERO] ISEVEN;
    BRANCH(ISEVEN)
    {
        PRINT $N is even;
    }

    SUB[N, ONE] N;
    GT[N, ZERO] ALIVE;
}
THIS.DIE();
```
