"""Interactive ~ATH REPL.

Reads statements, evaluates them against the live C runtime (athc/repl_eval),
and prints results. Multiline blocks (``~ATH(...){ ... }``, ``BRANCH``, etc.)
are accumulated until they parse; ``:`` lines are meta-commands.

Run with ``python -m athc.repl`` or ``athc --repl``.
"""

import sys
from pathlib import Path

from athc.diagnostics import render_diagnostic
from athc.lexer import LexError
from athc.parser import ParseError, parse
from athc.repl_eval import Evaluator, ReplError
from athc.runtime_ffi import AthRuntime, RuntimeNotBuilt

PRIMARY = "~ATH> "
CONTINUE = "  ... "

_HELP = """\
commands:
  :inspect VAR   show a variable's liveness, kind, and value (alias :i)
  :env           list bound variables and their state
  :load FILE     evaluate the statements in FILE
  :reset         start a fresh environment
  :compose M     switch runtime to fresh|intern (clears state)
  :help          this message
  :quit          exit (also Ctrl-D)   (alias :q)
statements end with ';'; blocks span lines until their '}'."""


def _incomplete(msg: str) -> bool:
    """A parse/lex failure that means 'keep reading' rather than a real error
    — the input ended mid-statement or mid-block."""
    return "end of input" in msg or "unterminated" in msg


class Repl:
    def __init__(self, mode="fresh", input_fn=None, out=None):
        self.mode = mode
        self.out = out or sys.stdout
        self.input_fn = input_fn          # () -> str | raises EOFError
        self.buffer = ""
        self._make_runtime(mode)

    def _make_runtime(self, mode):
        self.rt = AthRuntime(mode)
        # The evaluator's INPUT pulls from the same line source as the REPL.
        self.ev = Evaluator(self.rt, read_line=self._next_raw)

    def _emit(self, text):
        self.out.write(text)
        self.out.flush()

    def _next_raw(self):
        if self.input_fn is not None:
            try:
                return self.input_fn()
            except EOFError:
                return ""
        return sys.stdin.readline()

    def _prompt(self, primary):
        self._emit(PRIMARY if primary else CONTINUE)
        if self.input_fn is not None:
            return self.input_fn()        # raises EOFError at end
        line = sys.stdin.readline()
        if line == "":
            raise EOFError
        return line.rstrip("\n")

    # -- main loop -----------------------------------------------------------
    def run(self):
        self._emit("~ATH REPL — :help for commands, :quit to exit\n")
        while True:
            try:
                line = self._prompt(primary=(self.buffer == ""))
            except EOFError:
                self._emit("\n")
                return
            if not self.buffer and line.strip().startswith(":"):
                if self._meta(line.strip()):
                    return                # :quit
                continue
            self.buffer += line + "\n"
            self._try_eval()

    def _try_eval(self):
        src = self.buffer
        try:
            program = parse(src)
        except (LexError, ParseError) as e:
            if _incomplete(e.msg):
                return                    # keep reading (continuation)
            self._render(e, src)
            self.buffer = ""
            return
        # Parsed cleanly — evaluate and reset.
        self.buffer = ""
        try:
            self.ev.run(program.statements)
        except ReplError as e:
            self._emit(f"athc: error: {e}\n")

    def _render(self, e, src):
        self._emit(
            render_diagnostic(
                kind="error", msg=e.msg, path="<repl>", source_text=src,
                line=e.line, col=e.col, help=getattr(e, "help", None),
            )
            + "\n"
        )

    # -- meta-commands -------------------------------------------------------
    def _meta(self, line) -> bool:
        parts = line[1:].split(None, 1)
        cmd = parts[0] if parts else ""
        arg = parts[1].strip() if len(parts) > 1 else ""
        if cmd in ("quit", "q"):
            return True
        elif cmd == "help":
            self._emit(_HELP + "\n")
        elif cmd in ("inspect", "i"):
            if not arg:
                self._emit("usage: :inspect VAR\n")
            elif arg not in self.ev.env:
                self._emit(f"'{arg}' is not bound\n")
            else:
                self._emit(f"{arg}: {self.rt.describe(self.ev._read(arg))}\n")
        elif cmd == "env":
            names = sorted(n for n in self.ev.env if n != "THIS")
            if not names:
                self._emit("(no variables bound)\n")
            for n in names:
                self._emit(f"  {n}: {self.rt.describe(self.ev._read(n))}\n")
        elif cmd == "reset":
            self._make_runtime(self.mode)
            self._emit("(environment reset)\n")
        elif cmd == "compose":
            if arg not in ("fresh", "intern"):
                self._emit("usage: :compose fresh|intern\n")
            else:
                self.mode = arg
                self._make_runtime(arg)
                self._emit(f"(runtime: {arg}; state cleared)\n")
        elif cmd == "load":
            self._load(arg)
        else:
            self._emit(f"unknown command ':{cmd}' — :help for the list\n")
        return False

    def _load(self, arg):
        if not arg:
            self._emit("usage: :load FILE\n")
            return
        path = Path(arg)
        if not path.exists():
            self._emit(f"file not found: {arg}\n")
            return
        try:
            program = parse(path.read_text())
        except (LexError, ParseError) as e:
            self._render(e, path.read_text())
            return
        try:
            self.ev.run(program.statements)
        except ReplError as e:
            self._emit(f"athc: error: {e}\n")


def main(argv=None) -> int:
    mode = "fresh"
    args = list(sys.argv[1:] if argv is None else argv)
    if "--compose" in args:
        i = args.index("--compose")
        if i + 1 < len(args):
            mode = args[i + 1]
    try:
        Repl(mode=mode).run()
    except RuntimeNotBuilt as e:
        print(f"athc: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
