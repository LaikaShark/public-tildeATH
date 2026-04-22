from llvmlite import binding, ir

from athc.ast import (
    AthLoop,
    ComposeStmt,
    DecomposeStmt,
    DieStmt,
    ImportStmt,
    PrintStmt,
    Program,
)

binding.initialize_native_target()
binding.initialize_native_asmprinter()


class CodegenError(Exception):
    pass


def _collect_names(stmts, names: set):
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


class Codegen:
    def __init__(self, program: Program, module_name: str = "ath"):
        self.program = program

        target = binding.Target.from_default_triple()
        self.target_machine = target.create_target_machine()

        self.module = ir.Module(name=module_name)
        self.module.triple = binding.get_default_triple()
        self.module.data_layout = str(self.target_machine.target_data)

        self.i8 = ir.IntType(8)
        self.i32 = ir.IntType(32)
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
        self.f_halt = ir.Function(
            self.module, ir.FunctionType(ir.VoidType(), []), name="ath_halt"
        )
        self.f_halt.attributes.add("noreturn")

        self.g_null = ir.GlobalVariable(self.module, self.obj_ptr, name="ath_NULL")
        self.g_null.linkage = "external"

        self.main_fn = ir.Function(
            self.module, ir.FunctionType(self.i32, []), name="main"
        )

        self.slots: dict[str, ir.AllocaInstr] = {}
        self.tmp_l = None
        self.tmp_r = None
        self._loop_id = 0
        self._str_id = 0
        self._dead_id = 0

    def _read_var(self, builder: ir.IRBuilder, name: str) -> ir.Value:
        if name == "NULL":
            return builder.load(self.g_null, name="NULL_val")
        return builder.load(self.slots[name], name=f"{name}_val")

    def _write_var(self, builder: ir.IRBuilder, name: str, value: ir.Value) -> None:
        if name == "NULL":
            raise CodegenError("cannot bind the predefined name 'NULL'")
        builder.store(value, self.slots[name])

    def _string_global(self, s: str):
        b = s.encode("utf-8")
        ty = ir.ArrayType(self.i8, len(b) if b else 1)
        name = f".str.{self._str_id}"
        self._str_id += 1
        g = ir.GlobalVariable(self.module, ty, name=name)
        g.linkage = "private"
        g.global_constant = True
        g.initializer = ir.Constant(ty, bytearray(b) if b else bytearray(b"\x00"))
        return g, len(b)

    def generate(self) -> str:
        names: set[str] = set()
        _collect_names(self.program.statements, names)
        names.add("THIS")
        names.discard("NULL")

        entry = self.main_fn.append_basic_block("entry")
        builder = ir.IRBuilder(entry)

        for name in sorted(names):
            slot = builder.alloca(self.obj_ptr, name=f"{name}_slot")
            self.slots[name] = slot
            builder.store(ir.Constant(self.obj_ptr, None), slot)

        self.tmp_l = builder.alloca(self.obj_ptr, name="tmp_l")
        self.tmp_r = builder.alloca(self.obj_ptr, name="tmp_r")

        this_obj = builder.call(self.f_alloc, [])
        builder.store(this_obj, self.slots["THIS"])

        self._emit_block(builder, self.program.statements)

        if not builder.block.is_terminated:
            builder.ret(ir.Constant(self.i32, 0))

        return str(self.module)

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
        else:
            raise CodegenError(f"no codegen for {type(stmt).__name__}")

    def _emit_import(self, builder: ir.IRBuilder, stmt: ImportStmt) -> None:
        if stmt.var == "NULL":
            raise CodegenError("cannot import into the predefined name 'NULL'")
        slot = self.slots[stmt.var]
        cur = builder.load(slot)
        is_unbound = builder.icmp_unsigned(
            "==", cur, ir.Constant(self.obj_ptr, None)
        )
        with builder.if_then(is_unbound):
            fresh = builder.call(self.f_alloc, [])
            builder.store(fresh, slot)

    def _emit_decompose(self, builder: ir.IRBuilder, stmt: DecomposeStmt) -> None:
        src = self._read_var(builder, stmt.source)
        builder.call(self.f_decompose, [src, self.tmp_l, self.tmp_r])
        l_val = builder.load(self.tmp_l)
        r_val = builder.load(self.tmp_r)
        self._write_var(builder, stmt.left, l_val)
        self._write_var(builder, stmt.right, r_val)

    def _emit_compose(self, builder: ir.IRBuilder, stmt: ComposeStmt) -> None:
        l_val = self._read_var(builder, stmt.left)
        r_val = self._read_var(builder, stmt.right)
        composed = builder.call(self.f_compose, [l_val, r_val])
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
        alive = builder.call(self.f_is_alive, [v])
        cond = builder.icmp_signed("!=", alive, ir.Constant(self.i32, 0))
        builder.cbranch(cond, body, end)

        builder.position_at_start(body)
        self._emit_block(builder, stmt.body)
        if not builder.block.is_terminated:
            builder.branch(header)

        builder.position_at_start(end)

    def _emit_die(self, builder: ir.IRBuilder, stmt: DieStmt) -> None:
        v = self._read_var(builder, stmt.var)
        builder.call(self.f_die, [v])
        if stmt.var == "THIS":
            builder.call(self.f_halt, [])
            builder.unreachable()

    def _emit_print(self, builder: ir.IRBuilder, stmt: PrintStmt) -> None:
        g, length = self._string_global(stmt.text)
        zero = ir.Constant(self.i32, 0)
        ptr = builder.gep(g, [zero, zero], inbounds=True)
        builder.call(
            self.f_print, [ptr, ir.Constant(self.size_t, length)]
        )


def generate_ir(program: Program, module_name: str = "ath") -> str:
    return Codegen(program, module_name).generate()


def emit_object(ir_text: str) -> bytes:
    target = binding.Target.from_default_triple()
    tm = target.create_target_machine()
    mod = binding.parse_assembly(ir_text)
    mod.verify()
    return tm.emit_object(mod)
