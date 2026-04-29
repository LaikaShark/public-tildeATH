from llvmlite import binding, ir

from athc.ast import (
    AthLoop,
    ComposeStmt,
    DecomposeStmt,
    DieStmt,
    FuncCallComposeArg,
    FuncCallDecomposeRet,
    ImportBuiltinStmt,
    ImportFuncStmt,
    ImportNumberStmt,
    ImportStmt,
    InputStmt,
    Print2Stmt,
    PrintStmt,
    Program,
    SliceStmt,
    SubscriptStmt,
    WatchStmt,
)

binding.initialize_native_target()
binding.initialize_native_asmprinter()


class CodegenError(Exception):
    pass


def _collect_names(stmts, names: set) -> None:
    for s in stmts:
        if isinstance(s, ImportStmt):
            names.add(s.var)
        elif isinstance(s, DecomposeStmt):
            names.add(s.source)
            names.add(s.left)
            names.add(s.right)
        elif isinstance(s, ComposeStmt):
            names.add(s.left)
            names.add(s.right)
            names.add(s.target)
        elif isinstance(s, AthLoop):
            names.add(s.var)
            _collect_names(s.body, names)
        elif isinstance(s, DieStmt):
            names.add(s.var)
            if s.arg is not None:
                names.add(s.arg)
        elif isinstance(s, (InputStmt, Print2Stmt)):
            names.add(s.var)
        elif isinstance(s, WatchStmt):
            names.add(s.var)
        elif isinstance(s, ImportNumberStmt):
            names.add(s.var)
        elif isinstance(s, FuncCallComposeArg):
            names.add(s.left)
            names.add(s.right)
            names.add(s.target)
        elif isinstance(s, FuncCallDecomposeRet):
            names.add(s.arg)
            names.add(s.left)
            names.add(s.right)
        elif isinstance(s, SubscriptStmt):
            names.add(s.source)
            names.add(s.index)
            names.add(s.target)
        elif isinstance(s, SliceStmt):
            names.add(s.source)
            names.add(s.start)
            names.add(s.end)
            names.add(s.target)
        # ImportFuncStmt and PrintStmt contribute no variable names.


class Codegen:
    def __init__(
        self,
        main_program: Program,
        function_table: dict[str, Program] | None = None,
        module_name: str = "ath",
        user_lifetimes: list[tuple[str, float, float]] | None = None,
    ):
        self.main_program = main_program
        self.function_table = function_table or {}
        self.user_lifetimes = list(user_lifetimes or [])

        target = binding.Target.from_default_triple()
        self.target_machine = target.create_target_machine()

        self.module = ir.Module(name=module_name)
        self.module.triple = binding.get_default_triple()
        self.module.data_layout = str(self.target_machine.target_data)

        self.i8 = ir.IntType(8)
        self.i32 = ir.IntType(32)
        self.i64 = ir.IntType(64)
        self.size_t = ir.IntType(64)

        self.obj_ty = self.module.context.get_identified_type("ath_obj")
        self.obj_ptr = self.obj_ty.as_pointer()
        self.obj_ty.set_body(self.i32, self.obj_ptr, self.obj_ptr)

        self.f_alloc = ir.Function(
            self.module, ir.FunctionType(self.obj_ptr, []), name="ath_alloc_alive"
        )
        self.f_compose = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, [self.obj_ptr, self.obj_ptr]),
            name="ath_compose",
        )
        self.f_decompose = ir.Function(
            self.module,
            ir.FunctionType(
                ir.VoidType(),
                [self.obj_ptr, self.obj_ptr.as_pointer(), self.obj_ptr.as_pointer()],
            ),
            name="ath_decompose",
        )
        self.f_die = ir.Function(
            self.module,
            ir.FunctionType(ir.VoidType(), [self.obj_ptr]),
            name="ath_die",
        )
        self.f_is_alive = ir.Function(
            self.module,
            ir.FunctionType(self.i32, [self.obj_ptr]),
            name="ath_is_alive",
        )
        self.f_print = ir.Function(
            self.module,
            ir.FunctionType(ir.VoidType(), [self.i8.as_pointer(), self.size_t]),
            name="ath_print",
        )
        self.f_input = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, []),
            name="ath_input_line",
        )
        self.f_print_obj = ir.Function(
            self.module,
            ir.FunctionType(ir.VoidType(), [self.obj_ptr]),
            name="ath_print_obj",
        )
        self.f_alloc_from_library = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, [self.i8.as_pointer()]),
            name="ath_alloc_from_library",
        )
        self.f_alloc_watching_file = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, [self.i8.as_pointer()]),
            name="ath_alloc_watching_file",
        )
        self.f_alloc_watching_signal_by_name = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, [self.i8.as_pointer()]),
            name="ath_alloc_watching_signal_by_name",
        )
        self.f_register_lifetime = ir.Function(
            self.module,
            ir.FunctionType(
                ir.VoidType(),
                [self.i8.as_pointer(), ir.DoubleType(), ir.DoubleType()],
            ),
            name="ath_register_lifetime",
        )
        self.f_alloc_number = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, [self.i64]),
            name="ath_alloc_number",
        )
        self.f_inherit_lifetime = ir.Function(
            self.module,
            ir.FunctionType(
                ir.VoidType(),
                [self.obj_ptr, self.obj_ptr, self.obj_ptr],
            ),
            name="ath_inherit_lifetime",
        )
        self.f_index = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, [self.obj_ptr, self.obj_ptr]),
            name="ath_index",
        )
        self.f_slice = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, [self.obj_ptr, self.obj_ptr]),
            name="ath_slice",
        )

        # Builtin C functions declared via `import builtin SYM as NAME;`.
        # Keyed by raw C symbol name to dedupe across files.
        self.c_builtin_fns: dict[str, ir.Function] = {}
        # Per-program (file) local builtin tables: name (lower) → ir.Function.
        # Populated in generate() before any FunctionEmitter runs.
        self.local_builtins: dict[int, dict[str, ir.Function]] = {}

        self.g_null = ir.GlobalVariable(self.module, self.obj_ptr, name="ath_NULL")
        self.g_null.linkage = "external"

        self.main_fn = ir.Function(
            self.module, ir.FunctionType(self.i32, []), name="main"
        )

        self.user_fns: dict[str, ir.Function] = {}
        for fname in self.function_table:
            self.user_fns[fname] = ir.Function(
                self.module,
                ir.FunctionType(self.obj_ptr, [self.obj_ptr]),
                name=f"ath_user_{fname}",
            )

        self._str_id = 0

        # Now that the module exists, register builtins for each program.
        # All builtins use the fixed (ath_obj*, ath_obj*) -> ath_obj* ABI
        # per SPEC §4.4.13.
        for prog in [self.main_program, *self.function_table.values()]:
            self._register_program_builtins(prog)

    def _ensure_c_builtin(self, symbol: str) -> ir.Function:
        if symbol in self.c_builtin_fns:
            return self.c_builtin_fns[symbol]
        fn = ir.Function(
            self.module,
            ir.FunctionType(self.obj_ptr, [self.obj_ptr, self.obj_ptr]),
            name=symbol,
        )
        self.c_builtin_fns[symbol] = fn
        return fn

    def _register_program_builtins(self, prog: Program) -> None:
        table: dict[str, ir.Function] = {}
        for s in prog.statements:
            if isinstance(s, ImportBuiltinStmt):
                fn = self._ensure_c_builtin(s.symbol)
                table[s.name.lower()] = fn
        self.local_builtins[id(prog)] = table

    def make_string_global(self, s: str):
        b = s.encode("utf-8")
        ty = ir.ArrayType(self.i8, len(b) if b else 1)
        name = f".str.{self._str_id}"
        self._str_id += 1
        g = ir.GlobalVariable(self.module, ty, name=name)
        g.linkage = "private"
        g.global_constant = True
        g.initializer = ir.Constant(ty, bytearray(b) if b else bytearray(b"\x00"))
        return g, len(b)

    def make_cstring_global(self, s: str) -> ir.GlobalVariable:
        """A NUL-terminated C string global, for runtime functions that take char*."""
        b = s.encode("utf-8") + b"\x00"
        ty = ir.ArrayType(self.i8, len(b))
        name = f".cstr.{self._str_id}"
        self._str_id += 1
        g = ir.GlobalVariable(self.module, ty, name=name)
        g.linkage = "private"
        g.global_constant = True
        g.initializer = ir.Constant(ty, bytearray(b))
        return g

    def generate(self) -> str:
        FunctionEmitter(
            self,
            self.main_fn,
            self.main_program,
            is_main=True,
            local_builtins=self.local_builtins[id(self.main_program)],
        ).emit()
        for fname, fprog in self.function_table.items():
            FunctionEmitter(
                self,
                self.user_fns[fname],
                fprog,
                is_main=False,
                local_builtins=self.local_builtins[id(fprog)],
            ).emit()
        return str(self.module)


class FunctionEmitter:
    def __init__(
        self,
        cg: Codegen,
        llvm_fn: ir.Function,
        program: Program,
        is_main: bool,
        local_builtins: dict[str, ir.Function] | None = None,
    ):
        self.cg = cg
        self.fn = llvm_fn
        self.program = program
        self.is_main = is_main
        self.local_builtins = local_builtins or {}
        self.slots: dict[str, ir.AllocaInstr] = {}
        self.return_slot: ir.AllocaInstr | None = None
        self.tmp_l: ir.AllocaInstr | None = None
        self.tmp_r: ir.AllocaInstr | None = None
        self._loop_id = 0
        self._dead_id = 0

    def emit(self) -> None:
        names: set[str] = set()
        _collect_names(self.program.statements, names)
        names.add("THIS")
        if not self.is_main:
            names.add("ARGS")
        names.discard("NULL")

        entry = self.fn.append_basic_block("entry")
        builder = ir.IRBuilder(entry)

        # Register user-defined library entries (--define-lifetime) before
        # any other code runs, so the first import sees them.
        if self.is_main:
            for name, min_s, max_s in self.cg.user_lifetimes:
                name_g = self.cg.make_cstring_global(name)
                zero = ir.Constant(self.cg.i32, 0)
                name_ptr = builder.gep(name_g, [zero, zero], inbounds=True)
                builder.call(
                    self.cg.f_register_lifetime,
                    [
                        name_ptr,
                        ir.Constant(ir.DoubleType(), min_s),
                        ir.Constant(ir.DoubleType(), max_s),
                    ],
                )

        for n in sorted(names):
            slot = builder.alloca(self.cg.obj_ptr, name=f"{n}_slot")
            self.slots[n] = slot
            builder.store(ir.Constant(self.cg.obj_ptr, None), slot)

        self.tmp_l = builder.alloca(self.cg.obj_ptr, name="tmp_l")
        self.tmp_r = builder.alloca(self.cg.obj_ptr, name="tmp_r")

        if not self.is_main:
            self.return_slot = builder.alloca(self.cg.obj_ptr, name="return_obj")
            builder.store(builder.load(self.cg.g_null), self.return_slot)

        this_obj = builder.call(self.cg.f_alloc, [])
        builder.store(this_obj, self.slots["THIS"])

        if not self.is_main:
            builder.store(self.fn.args[0], self.slots["ARGS"])

        self._emit_block(builder, self.program.statements)

        if not builder.block.is_terminated:
            self._emit_return(builder)

    def _emit_return(self, builder: ir.IRBuilder) -> None:
        if self.is_main:
            builder.ret(ir.Constant(self.cg.i32, 0))
        else:
            builder.ret(builder.load(self.return_slot))

    def _read_var(self, builder: ir.IRBuilder, name: str) -> ir.Value:
        if name == "NULL":
            return builder.load(self.cg.g_null, name="NULL_val")
        return builder.load(self.slots[name], name=f"{name}_val")

    def _write_var(self, builder: ir.IRBuilder, name: str, value: ir.Value) -> None:
        if name == "NULL":
            raise CodegenError("cannot bind the predefined name 'NULL'")
        builder.store(value, self.slots[name])

    def _ensure_open_block(self, builder: ir.IRBuilder) -> None:
        if builder.block.is_terminated:
            dead = builder.function.append_basic_block(f"dead_{self._dead_id}")
            self._dead_id += 1
            builder.position_at_start(dead)

    def _emit_block(self, builder: ir.IRBuilder, stmts: list) -> None:
        for stmt in stmts:
            self._ensure_open_block(builder)
            self._emit_stmt(builder, stmt)

    def _emit_stmt(self, builder: ir.IRBuilder, stmt) -> None:
        if isinstance(stmt, ImportStmt):
            self._emit_import(builder, stmt)
        elif isinstance(stmt, ImportNumberStmt):
            self._emit_import_number(builder, stmt)
        elif isinstance(stmt, ImportBuiltinStmt):
            pass  # declaration only; handled by Codegen at init time
        elif isinstance(stmt, DecomposeStmt):
            self._emit_decompose(builder, stmt)
        elif isinstance(stmt, ComposeStmt):
            self._emit_compose(builder, stmt)
        elif isinstance(stmt, AthLoop):
            self._emit_ath_loop(builder, stmt)
        elif isinstance(stmt, DieStmt):
            self._emit_die(builder, stmt)
        elif isinstance(stmt, PrintStmt):
            self._emit_print(builder, stmt)
        elif isinstance(stmt, InputStmt):
            self._emit_input(builder, stmt)
        elif isinstance(stmt, Print2Stmt):
            self._emit_print2(builder, stmt)
        elif isinstance(stmt, ImportFuncStmt):
            pass  # compile-time only; loader has registered the function
        elif isinstance(stmt, FuncCallComposeArg):
            self._emit_funcall_compose_arg(builder, stmt)
        elif isinstance(stmt, FuncCallDecomposeRet):
            self._emit_funcall_decompose_ret(builder, stmt)
        elif isinstance(stmt, WatchStmt):
            self._emit_watch(builder, stmt)
        elif isinstance(stmt, SubscriptStmt):
            self._emit_subscript(builder, stmt)
        elif isinstance(stmt, SliceStmt):
            self._emit_slice(builder, stmt)
        else:
            raise CodegenError(f"no codegen for {type(stmt).__name__}")

    def _emit_import(self, builder: ir.IRBuilder, stmt: ImportStmt) -> None:
        if stmt.var == "NULL":
            raise CodegenError("cannot import into the predefined name 'NULL'")
        slot = self.slots[stmt.var]
        cur = builder.load(slot)
        is_unbound = builder.icmp_unsigned(
            "==", cur, ir.Constant(self.cg.obj_ptr, None)
        )
        with builder.if_then(is_unbound):
            name_g = self.cg.make_cstring_global(stmt.name)
            zero = ir.Constant(self.cg.i32, 0)
            name_ptr = builder.gep(name_g, [zero, zero], inbounds=True)
            fresh = builder.call(self.cg.f_alloc_from_library, [name_ptr])
            builder.store(fresh, slot)

    def _emit_import_number(
        self, builder: ir.IRBuilder, stmt: ImportNumberStmt
    ) -> None:
        if stmt.var == "NULL":
            raise CodegenError("cannot import into the predefined name 'NULL'")
        slot = self.slots[stmt.var]
        cur = builder.load(slot)
        is_unbound = builder.icmp_unsigned(
            "==", cur, ir.Constant(self.cg.obj_ptr, None)
        )
        with builder.if_then(is_unbound):
            fresh = builder.call(
                self.cg.f_alloc_number,
                [ir.Constant(self.cg.i64, stmt.value)],
            )
            builder.store(fresh, slot)

    def _emit_watch(self, builder: ir.IRBuilder, stmt: WatchStmt) -> None:
        if stmt.var == "NULL":
            raise CodegenError("cannot watch into the predefined name 'NULL'")
        slot = self.slots[stmt.var]
        cur = builder.load(slot)
        is_unbound = builder.icmp_unsigned(
            "==", cur, ir.Constant(self.cg.obj_ptr, None)
        )
        with builder.if_then(is_unbound):
            zero = ir.Constant(self.cg.i32, 0)
            if stmt.path is not None:
                target_g = self.cg.make_cstring_global(stmt.path)
                target_ptr = builder.gep(target_g, [zero, zero], inbounds=True)
                fresh = builder.call(self.cg.f_alloc_watching_file, [target_ptr])
            elif stmt.signal_name is not None:
                name_g = self.cg.make_cstring_global(stmt.signal_name)
                name_ptr = builder.gep(name_g, [zero, zero], inbounds=True)
                fresh = builder.call(
                    self.cg.f_alloc_watching_signal_by_name, [name_ptr]
                )
            else:
                raise CodegenError(
                    "WatchStmt has neither path nor signal_name"
                )
            builder.store(fresh, slot)

    def _emit_decompose(self, builder: ir.IRBuilder, stmt: DecomposeStmt) -> None:
        src = self._read_var(builder, stmt.source)
        builder.call(self.cg.f_decompose, [src, self.tmp_l, self.tmp_r])
        l_val = builder.load(self.tmp_l)
        r_val = builder.load(self.tmp_r)
        self._write_var(builder, stmt.left, l_val)
        self._write_var(builder, stmt.right, r_val)

    def _emit_compose(self, builder: ir.IRBuilder, stmt: ComposeStmt) -> None:
        l_val = self._read_var(builder, stmt.left)
        r_val = self._read_var(builder, stmt.right)
        composed = builder.call(self.cg.f_compose, [l_val, r_val])
        self._write_var(builder, stmt.target, composed)

    def _emit_ath_loop(self, builder: ir.IRBuilder, stmt: AthLoop) -> None:
        fn = builder.function
        loop_id = self._loop_id
        self._loop_id += 1
        header = fn.append_basic_block(f"ath_header_{loop_id}")
        body = fn.append_basic_block(f"ath_body_{loop_id}")
        end = fn.append_basic_block(f"ath_end_{loop_id}")

        builder.branch(header)

        builder.position_at_start(header)
        v = self._read_var(builder, stmt.var)
        alive = builder.call(self.cg.f_is_alive, [v])
        op = "==" if stmt.inverted else "!="
        cond = builder.icmp_signed(op, alive, ir.Constant(self.cg.i32, 0))
        builder.cbranch(cond, body, end)

        builder.position_at_start(body)
        self._emit_block(builder, stmt.body)
        if not builder.block.is_terminated:
            builder.branch(header)

        builder.position_at_start(end)

    def _emit_die(self, builder: ir.IRBuilder, stmt: DieStmt) -> None:
        # Read the arg first (spec §4.4.5 step 1): so THIS.DIE(THIS) returns
        # the still-live THIS pointer before the kill.
        if stmt.arg is not None and not self.is_main:
            arg_val = self._read_var(builder, stmt.arg)
            builder.store(arg_val, self.return_slot)

        v = self._read_var(builder, stmt.var)
        builder.call(self.cg.f_die, [v])

        if stmt.var == "THIS":
            self._emit_return(builder)

    def _emit_print(self, builder: ir.IRBuilder, stmt: PrintStmt) -> None:
        g, length = self.cg.make_string_global(stmt.text)
        zero = ir.Constant(self.cg.i32, 0)
        ptr = builder.gep(g, [zero, zero], inbounds=True)
        builder.call(
            self.cg.f_print, [ptr, ir.Constant(self.cg.size_t, length)]
        )

    def _emit_input(self, builder: ir.IRBuilder, stmt: InputStmt) -> None:
        result = builder.call(self.cg.f_input, [])
        self._write_var(builder, stmt.var, result)

    def _emit_print2(self, builder: ir.IRBuilder, stmt: Print2Stmt) -> None:
        val = self._read_var(builder, stmt.var)
        builder.call(self.cg.f_print_obj, [val])

    def _emit_funcall_compose_arg(
        self, builder: ir.IRBuilder, stmt: FuncCallComposeArg
    ) -> None:
        fname = stmt.name.lower()
        l_val = self._read_var(builder, stmt.left)
        r_val = self._read_var(builder, stmt.right)
        if fname in self.local_builtins:
            # Direct C call: builtins take (l, r), no compose-and-decompose.
            result = builder.call(self.local_builtins[fname], [l_val, r_val])
        elif fname in self.cg.user_fns:
            arg = builder.call(self.cg.f_compose, [l_val, r_val])
            result = builder.call(self.cg.user_fns[fname], [arg])
        else:
            raise CodegenError(f"unknown function {stmt.name!r}")
        self._write_var(builder, stmt.target, result)

    def _emit_funcall_decompose_ret(
        self, builder: ir.IRBuilder, stmt: FuncCallDecomposeRet
    ) -> None:
        fname = stmt.name.lower()
        arg = self._read_var(builder, stmt.arg)
        if fname in self.local_builtins:
            # Builtins take two args; the second slot receives ath_NULL.
            null = builder.load(self.cg.g_null)
            result = builder.call(self.local_builtins[fname], [arg, null])
        elif fname in self.cg.user_fns:
            result = builder.call(self.cg.user_fns[fname], [arg])
        else:
            raise CodegenError(f"unknown function {stmt.name!r}")
        builder.call(self.cg.f_decompose, [result, self.tmp_l, self.tmp_r])
        l_val = builder.load(self.tmp_l)
        r_val = builder.load(self.tmp_r)
        self._write_var(builder, stmt.left, l_val)
        self._write_var(builder, stmt.right, r_val)

    def _emit_subscript(
        self, builder: ir.IRBuilder, stmt: SubscriptStmt
    ) -> None:
        s_val = self._read_var(builder, stmt.source)
        n_val = self._read_var(builder, stmt.index)
        result = builder.call(self.cg.f_index, [s_val, n_val])
        self._write_var(builder, stmt.target, result)

    def _emit_slice(self, builder: ir.IRBuilder, stmt: SliceStmt) -> None:
        s_val = self._read_var(builder, stmt.source)
        i_val = self._read_var(builder, stmt.start)
        j_val = self._read_var(builder, stmt.end)
        # Bundle the indices into a composite the slice runtime can
        # decompose, then explicitly install I and J as deps of the pair
        # so that killing either endpoint kills the slice on next
        # observation (§4.4.16 step 2).
        range_pair = builder.call(self.cg.f_compose, [i_val, j_val])
        builder.call(self.cg.f_inherit_lifetime, [range_pair, i_val, j_val])
        result = builder.call(self.cg.f_slice, [s_val, range_pair])
        self._write_var(builder, stmt.target, result)


def generate_ir(
    program: Program,
    function_table: dict[str, Program] | None = None,
    module_name: str = "ath",
    user_lifetimes: list[tuple[str, float, float]] | None = None,
) -> str:
    return Codegen(
        program, function_table, module_name, user_lifetimes
    ).generate()


def emit_object(ir_text: str) -> bytes:
    target = binding.Target.from_default_triple()
    tm = target.create_target_machine()
    mod = binding.parse_assembly(ir_text)
    mod.verify()
    return tm.emit_object(mod)
