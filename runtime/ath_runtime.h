#ifndef ATH_RUNTIME_H
#define ATH_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "bigint.h"

// Numeric payload discriminant: NONE=no payload, INT=num.i, FLOAT=num.f, BIG reserved
typedef enum {
    ATH_NUM_NONE = 0,
    ATH_NUM_INT,
    ATH_NUM_FLOAT,
    // reserved
    ATH_NUM_BIG
} ath_num_kind;

typedef struct ath_obj {
    int alive;
    struct ath_obj *left;
    struct ath_obj *right;
    // Monotonic seconds at which object dies; 0.0 = no deadline
    double deadline_s;
    // Filesystem path watched via access(); NULL = no watch; dies when access() fails
    const char *watch_path;
    // If nonzero, ath_is_alive returns 1 exactly once then sets alive=0 (`once` entry)
    int is_oneshot;
    // If nonzero, liveness tied to pending POSIX signal of that number
    int awaiting_signal;
    // Set only by ath_alloc_read_file; explicit ath_die on live owner unlinks watch_path first; passive deaths do not; not cloned, cleared by ath_close
    int owns_path;
    // Tagged payload; selects active union member; small BIG values normalized to INT; test via ath_has_value()
    ath_num_kind num_kind;
    union { int64_t i; double f; ath_bigint *b; } num;
    // Dependency tracking: ath_is_alive returns 0 if any non-null dep dead; installed by ath_inherit_lifetime only
    struct ath_obj *dep1;
    struct ath_obj *dep2;
    // Dep eval mode: DEP_AND (default) dead if any dep dead; DEP_OR alive until both dead; set only by ath_or
    int dep_mode;
    // Character identity: nonzero iff carries char_code (0..255); copied by ath_clone so snapshots of atoms (S[N]) stay recognizable without mutating the shared atom
    int is_char;
    int char_code;
    // Extended watch: pid>0 dies when kill(pid,0) reports ESRCH; both monotonic. Appended after codegen-modeled {alive,left,right} prefix
    int watch_pid;
    // Non-NULL ties liveness to file mtime captured at alloc; dies on mtime change or file gone
    const char *mtime_path;
    int64_t mtime_sec;
    int64_t mtime_nsec;
    // Concurrency (scheduler.c): FIFO mailbox shared by actors and channels; actor back-pointer
    // (NULL for channels/universes); live-child count for universes. All zero on a plain object,
    // so non-spawning programs are unaffected. Appended after the codegen-modeled prefix.
    struct ath_msg   *mbox_head;
    struct ath_msg   *mbox_tail;
    void             *actor;
    int               universe_pending;
    // Networking (net.c). All zero on a non-socket object, so existing programs and codegen's
    // struct-prefix model are unaffected. A handle is EITHER a socket (sock_fd>0) OR a mailbox
    // (sock_fd==0); ath_send/ath_recv_from branch on sock_fd. Appended at the struct end.
    int    sock_fd;          // 0 = not a socket; >0 = open listener/connection fd (fd 0 is stdin)
    int    sock_is_listener; // 1 = accept target; 0 = stream connection
    int    sock_eof;         // latched on EOF/error; ath_observe reads this (no syscall)
    char  *sock_rbuf;        // recv line-buffer: bytes read from fd but not yet returned as a line
    size_t sock_rlen;        // valid bytes in sock_rbuf
    size_t sock_rcap;        // capacity of sock_rbuf
    struct ath_obj *sock_next; // intrusive link in the open-socket registry (net.c sweep)
} ath_obj;

#define ATH_DEP_AND 0
#define ATH_DEP_OR  1

// True iff o carries numeric payload (INT or FLOAT); null-safe
static inline int ath_has_value(const ath_obj *o) {
    return o != NULL && o->num_kind != ATH_NUM_NONE;
}

extern ath_obj *ath_NULL;

// Null-safety contract: every function below treats C null as ath_NULL
// ath_decompose/ath_die short-circuit on the ath_NULL singleton; decomposing it yields (ath_NULL, ath_NULL)
// ath_compose with null/ath_NULL operands builds a fresh composite of whatever was passed
ath_obj *ath_alloc_alive(void);
ath_obj *ath_compose(ath_obj *l, ath_obj *r);
void     ath_decompose(ath_obj *v, ath_obj **l_out, ath_obj **r_out);
void     ath_die(ath_obj *v);
int      ath_is_alive(ath_obj *v);
// Non-mutating liveness: same verdict as ath_is_alive but never flips the cached bit or consumes
// a one-shot. For inspection tools that must observe without disturbing the object.
int      ath_observe_alive(ath_obj *v);
void     ath_print(const char *text, size_t len);

// Newline-free print primitives; ath_print/ath_print_obj are these plus a line feed
void     ath_print_bytes(const char *text, size_t len);
void     ath_print_obj_raw(ath_obj *s);

// String I/O; strings are cons-lists of character atoms
ath_obj *ath_input_line(void);
ath_obj *ath_input_char(void);
void     ath_print_obj(ath_obj *s);
void     ath_flush(void);
ath_obj *ath_char_atom(int c);

// Build fresh right-nested cons-list from len bytes, terminated with ath_NULL; empty returns ath_NULL
ath_obj *ath_string_from_bytes(const char *bytes, size_t len);

// Build right-nested cons-list of string objects from argv[1..argc-1].
// Returns ath_NULL when argc <= 1 (no user args).
ath_obj *ath_build_argv(int argc, char **argv);

// Slurp a string-like object into a heap byte buffer (caller frees); 0 on success, -1 on malformed
// string or alloc failure. NULL/dead/empty s yields a successful empty buffer. Used by net.c send.
int      ath_string_to_bytes(ath_obj *s, char **out_buf, size_t *out_len);

// Coerce to string-like for `text` interpolation: payload operands via ath_to_string, else returned unchanged
ath_obj *ath_coerce_string(ath_obj *v);

// Lifetime allocators
ath_obj *ath_alloc_with_lifetime(double min_s, double max_s);
ath_obj *ath_alloc_watching_file(const char *path);
ath_obj *ath_alloc_watching_signal(int signum);
ath_obj *ath_alloc_watching_signal_by_name(const char *name);
// Ties liveness to running process (n's payload is pid); born dead if pid absent, non-positive, or out of range
ath_obj *ath_alloc_watching_pid(ath_obj *n);
// Ties liveness to file mtime captured at alloc; born dead if path missing
ath_obj *ath_alloc_watching_mtime(const char *path);
ath_obj *ath_alloc_oneshot(void);
ath_obj *ath_alloc_from_library(const char *name);
int      ath_library_lookup(const char *name, double *min_out, double *max_out);

// Register user-defined library entry; overrides built-in of same name; called from main's prologue for --define-lifetime; name must outlive program
void     ath_register_lifetime(const char *name, double min_s, double max_s);

// Arithmetic helpers return fresh object inheriting operand lifetimes; born dead on overflow, div-by-zero, dead operand, or missing payload
// ATH_NUM_INT payload
ath_obj *ath_alloc_number(int64_t v);
// ATH_NUM_FLOAT payload
ath_obj *ath_alloc_float(double v);
// Wrap bigint as sticky BIG payload (no demotion to INT)
ath_obj *ath_alloc_bignum(ath_bigint *b);
// Parse decimal literal too large for int64 into BIG; born dead on malformed input
ath_obj *ath_alloc_bignum_from_decimal(const char *s);
void     ath_inherit_lifetime(ath_obj *result, ath_obj *a, ath_obj *b);
ath_obj *ath_add(ath_obj *x, ath_obj *y);
ath_obj *ath_sub(ath_obj *x, ath_obj *y);
ath_obj *ath_mul(ath_obj *x, ath_obj *y);
ath_obj *ath_div(ath_obj *x, ath_obj *y);
ath_obj *ath_mod(ath_obj *x, ath_obj *y);
ath_obj *ath_to_string(ath_obj *x, ath_obj *unused);
ath_obj *ath_parse(ath_obj *s, ath_obj *unused);

// Comparisons as verdicts: result alive iff comparison holds, lifetime inherited from both operands
ath_obj *ath_lt(ath_obj *x, ath_obj *y);
ath_obj *ath_eq(ath_obj *x, ath_obj *y);
ath_obj *ath_gt(ath_obj *x, ath_obj *y);
ath_obj *ath_le(ath_obj *x, ath_obj *y);
ath_obj *ath_ge(ath_obj *x, ath_obj *y);
ath_obj *ath_ne(ath_obj *x, ath_obj *y);

// Logical combinators: born dead if both (AND: either) operands dead. OR sets dep_mode=ATH_DEP_OR (alive until both deps dead). NOT absent: would need forbidden dead->alive; negate at observation via `~ATH(!V)` or `BRANCH(!V)`
ath_obj *ath_and(ath_obj *x, ath_obj *y);
ath_obj *ath_or(ath_obj *x, ath_obj *y);

// Compose with dep propagation: ath_compose then ath_inherit_lifetime; builds carriers (e.g. (needle,replacement) pair) tracking operand lifetimes
ath_obj *ath_entangle(ath_obj *x, ath_obj *y);

// String ops on cons-lists; ath_index/ath_slice also drive subscript codegen; all install operand deps on results
ath_obj *ath_length(ath_obj *s, ath_obj *unused);
ath_obj *ath_concat(ath_obj *a, ath_obj *b);
ath_obj *ath_index(ath_obj *s, ath_obj *n);
ath_obj *ath_slice(ath_obj *s, ath_obj *range);
// Search and replace; replace ops decompose pair into (needle, replacement); all install operand deps
ath_obj *ath_find(ath_obj *hay, ath_obj *needle);
ath_obj *ath_replace(ath_obj *s, ath_obj *pair);
ath_obj *ath_replace_all(ath_obj *s, ath_obj *pair);

// Verdicts (streq/startswith/endswith/strlt/strgt): alive iff relation holds, operand deps installed; born dead on malformed string; empty prefix/suffix alive
// Transforms (lower/upper/trim/lstrip/rstrip): fresh cons-list, operand as dep; strip whitespace is space/tab/LF/CR
// SPLIT: cons-list of strings split by sep; empty sep born dead; trailing sep yields trailing empty element
// JOIN: concat LIST's right-spine interleaved with sep; empty LIST returns NULL
ath_obj *ath_streq(ath_obj *a, ath_obj *b);
ath_obj *ath_startswith(ath_obj *hay, ath_obj *prefix);
ath_obj *ath_endswith(ath_obj *hay, ath_obj *suffix);
ath_obj *ath_strlt(ath_obj *a, ath_obj *b);
ath_obj *ath_strgt(ath_obj *a, ath_obj *b);
ath_obj *ath_lower(ath_obj *s, ath_obj *unused);
ath_obj *ath_upper(ath_obj *s, ath_obj *unused);
ath_obj *ath_trim(ath_obj *s, ath_obj *unused);
ath_obj *ath_lstrip(ath_obj *s, ath_obj *unused);
ath_obj *ath_rstrip(ath_obj *s, ath_obj *unused);
ath_obj *ath_split(ath_obj *s, ath_obj *sep);
ath_obj *ath_join(ath_obj *list, ath_obj *sep);

// Search (contains/count/rfind): CONTAINS verdict, COUNT/RFIND carry int64. Empty needle: CONTAINS alive, COUNT born dead, RFIND matches at len(HAY)
// Construct (repeat/reverse/pad_left/pad_right): fresh cons-list, operand deps; REPEAT/PAD take number 2nd operand, N<0 or no payload dead; pad uses space, no-op past len(S)
// Atom bridge: ORD maps char atom to 0..255 code; CHR maps 0..255 payload to length-1 string; out-of-range/malformed born dead
ath_obj *ath_contains(ath_obj *hay, ath_obj *needle);
ath_obj *ath_count(ath_obj *hay, ath_obj *needle);
ath_obj *ath_rfind(ath_obj *hay, ath_obj *needle);
ath_obj *ath_repeat(ath_obj *s, ath_obj *n);
ath_obj *ath_reverse(ath_obj *s, ath_obj *unused);
ath_obj *ath_pad_left(ath_obj *s, ath_obj *n);
ath_obj *ath_pad_right(ath_obj *s, ath_obj *n);
ath_obj *ath_ord(ath_obj *a, ath_obj *unused);
ath_obj *ath_chr(ath_obj *n, ath_obj *unused);

// int64-payload builtins, born dead on non-payload/dead operand. POW rejects negative exponents; ABS/NEG/GCD reject INT64_MIN; SHL/SHR require 0..63 shift; CLAMP packs (LO,HI) pair, rejects LO>HI
ath_obj *ath_pow(ath_obj *x, ath_obj *y);
ath_obj *ath_abs(ath_obj *x, ath_obj *unused);
ath_obj *ath_neg(ath_obj *x, ath_obj *unused);
ath_obj *ath_min(ath_obj *x, ath_obj *y);
ath_obj *ath_max(ath_obj *x, ath_obj *y);
ath_obj *ath_gcd(ath_obj *x, ath_obj *y);
ath_obj *ath_sign(ath_obj *x, ath_obj *unused);
ath_obj *ath_band(ath_obj *x, ath_obj *y);
ath_obj *ath_bor(ath_obj *x, ath_obj *y);
ath_obj *ath_bxor(ath_obj *x, ath_obj *y);
ath_obj *ath_bnot(ath_obj *x, ath_obj *unused);
ath_obj *ath_shl(ath_obj *x, ath_obj *y);
ath_obj *ath_shr(ath_obj *x, ath_obj *y);
ath_obj *ath_clamp(ath_obj *x, ath_obj *pair);

// Numeric conversions and rounding
// sticky BIG
ath_obj *ath_int_to_bignum(ath_obj *x, ath_obj *unused);
ath_obj *ath_int_to_float(ath_obj *x, ath_obj *unused);
ath_obj *ath_float_to_int(ath_obj *x, ath_obj *unused);
ath_obj *ath_floor(ath_obj *x, ath_obj *unused);
ath_obj *ath_ceil(ath_obj *x, ath_obj *unused);
ath_obj *ath_round(ath_obj *x, ath_obj *unused);

// Float transcendentals: each returns FLOAT; out-of-domain inputs yield live nan/inf
ath_obj *ath_sqrt(ath_obj *x, ath_obj *unused);
ath_obj *ath_cbrt(ath_obj *x, ath_obj *unused);
ath_obj *ath_exp(ath_obj *x, ath_obj *unused);
ath_obj *ath_log(ath_obj *x, ath_obj *unused);
ath_obj *ath_log2(ath_obj *x, ath_obj *unused);
ath_obj *ath_log10(ath_obj *x, ath_obj *unused);
ath_obj *ath_sin(ath_obj *x, ath_obj *unused);
ath_obj *ath_cos(ath_obj *x, ath_obj *unused);
ath_obj *ath_tan(ath_obj *x, ath_obj *unused);
ath_obj *ath_asin(ath_obj *x, ath_obj *unused);
ath_obj *ath_acos(ath_obj *x, ath_obj *unused);
ath_obj *ath_atan(ath_obj *x, ath_obj *unused);
ath_obj *ath_atan2(ath_obj *y, ath_obj *x);
ath_obj *ath_hypot(ath_obj *x, ath_obj *y);

// COMPARE: three-way (-1/0/1) string verdict. CHAR_AT returns length-1 string (vs S[N]'s bare atom). FIND_FROM packs (NEEDLE,START). STRIP_CHARS strips custom set; PAD_*_WITH pad with fill packed as (WIDTH,FILL)
ath_obj *ath_compare(ath_obj *a, ath_obj *b);
ath_obj *ath_char_at(ath_obj *s, ath_obj *n);
ath_obj *ath_find_from(ath_obj *s, ath_obj *pair);
ath_obj *ath_capitalize(ath_obj *s, ath_obj *unused);
ath_obj *ath_title(ath_obj *s, ath_obj *unused);
ath_obj *ath_strip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_lstrip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_rstrip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_pad_left_with(ath_obj *s, ath_obj *pair);
ath_obj *ath_pad_right_with(ath_obj *s, ath_obj *pair);

// Walk list right-spine reading each element's int64 payload. SUM/PRODUCT fold (identity 0/1, empty yields identity); MAXIMUM/MINIMUM born-die on empty; MEMBER verdict over payload equality; TAKE/DROP return fresh sublist. Non-payload/dead element born-dies aggregates (rejects strings)
ath_obj *ath_sum(ath_obj *list, ath_obj *unused);
ath_obj *ath_product(ath_obj *list, ath_obj *unused);
ath_obj *ath_maximum(ath_obj *list, ath_obj *unused);
ath_obj *ath_minimum(ath_obj *list, ath_obj *unused);
ath_obj *ath_member(ath_obj *list, ath_obj *x);
ath_obj *ath_take(ath_obj *list, ath_obj *n);
ath_obj *ath_drop(ath_obj *list, ath_obj *n);

// n-ary lifetime combinators: ALL_OF alive iff every element alive (empty->alive); ANY_OF iff some alive (empty->dead). Fold ath_and/ath_or into dep-tracked tree so element deaths propagate
ath_obj *ath_all_of(ath_obj *list, ath_obj *unused);
ath_obj *ath_any_of(ath_obj *list, ath_obj *unused);

// Iteration count for `repeat N`: N's non-negative int64 payload, or 0 if dead/payload-less/negative
int64_t ath_count_of(ath_obj *n);

// Shallow snapshot clone: copies every field except dep1/dep2/owns_path (zeroed). Independent identity; clone never owns the file
ath_obj *ath_clone(ath_obj *v);

// File I/O. read_file slurps into cons-list whose head is non-interned wrapper with watch_path and owns_path=1. write/append return fresh verdict (alive on success). ath_close disowns+kills without unlinking; ath_die unlinks watch_path on still-alive owner
ath_obj *ath_alloc_read_file(const char *path);
ath_obj *ath_write_file(ath_obj *s, const char *path);
ath_obj *ath_append_file(ath_obj *s, const char *path);
void     ath_close(ath_obj *v);

// Dynamic-path variants: extract C string from path_obj, delegate to char* version
ath_obj *ath_alloc_read_file_obj(ath_obj *path_obj);
ath_obj *ath_write_file_obj(ath_obj *s, ath_obj *path_obj);
ath_obj *ath_append_file_obj(ath_obj *s, ath_obj *path_obj);

// Directory operations
int      ath_mkdir(const char *path);
int      ath_mkdir_obj(ath_obj *path_obj);
ath_obj *ath_listdir(const char *path);
ath_obj *ath_listdir_obj(ath_obj *path_obj);
ath_obj *ath_exists(const char *path);
ath_obj *ath_exists_obj(ath_obj *path_obj);

// Time and randomness; durations are int64 milliseconds. ath_sleep_ms no-op on dead/no-payload n. ath_alloc_timer_ms binds fresh alive object with deadline; duration not dep-tracked on result
void     ath_sleep_ms(ath_obj *n);
ath_obj *ath_alloc_timer_ms(ath_obj *n);
ath_obj *ath_now(ath_obj *a, ath_obj *b);
ath_obj *ath_random_range(ath_obj *lo, ath_obj *hi);

_Noreturn void ath_halt(void);

// Cooperative single-thread coroutine scheduler (scheduler.c). Compose-agnostic: linked into
// both archives. Lazily initialized on first ath_spawn; programs that never spawn are unaffected.
// Determinism: FIFO run queue + round-robin, FIFO mailboxes, plain handles -> output identical
// under fresh and intern. Cancellation is cooperative (an actor unwinds at its next recv/yield).
struct ath_msg; /* opaque mailbox node */
// Start FN on a fresh coroutine; returns a live handle, dead once the actor finishes/cancels.
ath_obj *ath_spawn(ath_obj *(*fn)(ath_obj *), ath_obj *arg);
// Like ath_spawn but the child is scoped to a universe: dies if the universe dies (cancel), and
// the universe stays alive while any child is (join).
ath_obj *ath_spawn_into(ath_obj *(*fn)(ath_obj *), ath_obj *arg, ath_obj *universe);
// FIFO-enqueue msg onto dest's mailbox (actor or channel); non-blocking; no-op if dest is dead.
void     ath_send(ath_obj *dest, ath_obj *msg);
// Dequeue from the running actor's own mailbox; blocks (yields) until a message or cancellation.
ath_obj *ath_recv(void);
// Dequeue from src's mailbox; blocks while src is alive and empty; returns ath_NULL (EOF) once
// src is dead and drained. Buffered messages are delivered FIFO even after src dies.
ath_obj *ath_recv_from(ath_obj *src);
// Cooperative yield to the scheduler.
void     ath_yield(void);
// Drive the scheduler until handle is dead (joins an actor or a universe). Works at top level too.
void     ath_join_handle(ath_obj *handle);
// Fresh closeable channel: a live handle with an empty mailbox. Close via `close C;` or C.DIE().
ath_obj *ath_channel(void);
// Fresh universe handle: live while any scoped child is; DIE() cancels the whole subtree.
ath_obj *ath_universe_new(void);
// Run the scheduler until no live actor remains. Emitted once at end of main when SPAWN is used.
void     ath_scheduler_drain(void);
// True iff a coroutine is currently running (used by ath_sleep_ms to park instead of block).
int      ath_in_actor(void);
// True iff a coroutine is running and its handle has died (cancelled); net.c bails on it.
int      ath_self_dead(void);
// Park the current actor until the monotonic deadline, yielding the scheduler meanwhile.
void     ath_park_until(double deadline_s);
// Park the current actor until fd is ready for events (POLLIN/POLLOUT), yielding meanwhile.
// No-op at top level (net.c does a blocking poll there instead). Declared for net.c.
void     ath_park_io(int fd, short events);

// Networking (net.c). A connection is a channel that carries a socket fd: the handle is alive
// iff the socket is open. Peer close, socket error, or .DIE()/close kills it, ending ~ATH(C).
// Transports: TCP (spec NULL, AF_INET, host+port) and Unix-domain (spec "unix:/path", AF_UNIX).
// All null-safe (C NULL => ath_NULL). Setup errors yield a born-dead handle.
// Bind+listen; spec NULL => TCP on port's int64 payload; "unix:/p" => AF_UNIX (port ignored).
ath_obj *ath_listen(const char *spec, ath_obj *port);
// Accept one connection from a listener; fresh connection handle, ath_NULL if dead/not a listener.
ath_obj *ath_accept(ath_obj *listener);
// Connect out; host "unix:/p" => AF_UNIX, else TCP to host:port (port's int64 payload).
ath_obj *ath_connect(const char *host, ath_obj *port);
ath_obj *ath_connect_obj(ath_obj *host_obj, ath_obj *port);
// Socket-backed send/recv, reached from ath_send/ath_recv_from when the handle has sock_fd>0.
// recv returns one newline-framed line as a string; EOF/error => sock_eof latched, handle dies, NULL.
void     ath_sock_send(ath_obj *c, ath_obj *msg);
ath_obj *ath_sock_recv_line(ath_obj *c);
// Idempotent: close(sock_fd), free recv buffer, latch sock_eof. Called from ath_die/ath_close.
void     ath_sock_teardown(ath_obj *v);
// Close any still-open registered sockets whose handle is dead; called at scheduler drain/atexit.
void     ath_sock_sweep(void);

#endif
