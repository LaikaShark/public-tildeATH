" Vim syntax file
" Language:    ~ATH (the athc dialect of the Homestuck esolang)
" Maintainer:  athc project
" Filenames:   *.ath
" Reference:   SPEC.md, athc/lexer.py, athc/parser.py
"
" Notes on the grammar that drive the choices below:
"   * Keywords and function names are case-insensitive (the lexer case-folds
"     them), so this file uses `syn case ignore`. Variable identifiers ARE
"     case-sensitive, but they get no special colour, so that is invisible.
"   * `print` switches the lexer into a payload mode that runs to the next ';',
"     interpolating `$NAME` and honouring \; \\ \n \t \r \$ escapes — modelled
"     here as its own region.
"   * `~ATH`, `.DIE`, `..` (slice) and `!` (loop inversion) are distinct tokens.

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

syn case ignore

" --- comments --------------------------------------------------------------
syn keyword athTodo     contained TODO FIXME XXX NOTE HACK
syn match   athComment  "//.*$"      contains=athTodo,@Spell
syn region  athComment  start="/\*" end="\*/" contains=athTodo,@Spell

" --- statement keywords ----------------------------------------------------
" (`print` is handled as a region below; do not list it here.)
" Hard keywords (from KEYWORDS in athc/lexer.py):
syn keyword athStatement  import importf bifurcate input execute watch clone
syn keyword athStatement  sleep timer read write append close text loop every
syn keyword athStatement  spawn send recv yield channel
syn keyword athStatement  listen accept connect
syn keyword athConditional branch else
syn keyword athKeyword    as

" Soft keywords — plain IDENTs recognised as statement leaders by the parser.
" `join` is a stdlib function name reused as a soft keyword for `join HANDLE;`.
" `universe` doubles as a lifetime-library concept and a supervision scope.
syn keyword athStatement  join universe

" Contextual sub-keywords: `import builtin`, `import number`, and the
" `watch signal|pid|mtime` heads. They are plain identifiers to the lexer, so
" highlighting them everywhere is a (benign) approximation.
syn keyword athType       builtin number signal pid mtime

" --- loop head and lifetime verbs ------------------------------------------
syn match athAth  /\~ath\>/
syn match athDie  /\.die\>/

" --- special constants (case-sensitive: only the SHOUTING forms) -----------
syn case match
syn keyword athConstant NULL THIS
syn case ignore

" --- importf <name> search-path target -------------------------------------
syn match athImportName /<\h\w*>/

" --- numbers: int / bigint / float (SPEC §4.8) -----------------------------
syn match athNumber /-\=\<\d\+\%(\.\d\+\)\=\%([eE][-+]\=\d\+\)\=\>/

" --- operators that are their own tokens -----------------------------------
syn match athOperator /\.\./
syn match athOperator /!/

" --- string literals -------------------------------------------------------
syn match  athStringEscape /\\["\\ntr]/ contained
syn region athString start=/"/ skip=/\\./ end=/"/ contains=athStringEscape,@Spell

" --- print payload ---------------------------------------------------------
" `print <payload>;` — literal text with $NAME interpolation and a small
" escape set. The payload runs to the terminating ';' (escaped as '\;').
syn match  athPrintVar    /\$\h\w*/        contained
syn match  athPrintEscape /\\[;\\ntr$]/    contained
syn region athPrint matchgroup=athStatement
      \ start=/\<print\>/ skip=/\\;/ end=/;/ keepend
      \ contains=athPrintVar,athPrintEscape,@Spell

" --- highlight links -------------------------------------------------------
hi def link athComment      Comment
hi def link athTodo         Todo
hi def link athStatement    Statement
hi def link athConditional  Conditional
hi def link athKeyword      Keyword
hi def link athType         Type
hi def link athAth          Special
hi def link athDie          Special
hi def link athConstant     Constant
hi def link athImportName   Include
hi def link athNumber       Number
hi def link athOperator     Operator
hi def link athString       String
hi def link athStringEscape SpecialChar
hi def link athPrint        String
hi def link athPrintVar     Identifier
hi def link athPrintEscape  SpecialChar

let b:current_syntax = "ath"

let &cpo = s:cpo_save
unlet s:cpo_save
