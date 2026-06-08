// Cooperative single-OS-thread coroutine scheduler for ~ATH.
//
// Actors are stackful `ucontext` coroutines. Only one runs at a time and context switches
// happen only at explicit yield points (YIELD / RECV / JOIN / actor-context SLEEP), so the
// rest of the (not-thread-safe) runtime needs no locking and stays byte-for-byte correct.
//
// The object liveness machinery *is* the scheduler's signal: an actor is scheduled while its
// handle is alive and reaped when it dies; a channel/nursery is just a handle with a mailbox /
// child-count. Determinism: a FIFO run queue scanned in order, FIFO mailboxes, and plain
// handles (never interned, one-shot, or deadline-bearing) make output identical under `fresh`
// and `intern`.
//
// Compose-agnostic: this file is linked into both libath_fresh and libath_intern.

#define _XOPEN_SOURCE 700

#include "ath_runtime.h"

#include <stdlib.h>
#include <time.h>
#include <ucontext.h>

#define ATH_ACTOR_STACK_SIZE (1u << 20) /* 1 MiB per actor */

enum { ACT_READY = 0, ACT_BLOCKED_RECV, ACT_BLOCKED_SLEEP, ACT_DONE };

struct ath_msg {
    ath_obj        *value;
    struct ath_msg *next;
};

typedef struct ath_actor {
    ucontext_t          ctx;
    void               *stack;
    ath_obj            *handle;       // this actor's liveness handle (also its own mailbox)
    ath_obj            *parent;       // nursery this actor is scoped to, or NULL
    ath_obj            *(*fn)(ath_obj *);
    ath_obj            *arg;
    int                 state;
    ath_obj            *wait_on;      // channel/handle this actor is blocked receiving from
    double              park_deadline;
    struct ath_actor   *qnext;        // run-queue link
} ath_actor;

// ---- scheduler state (single thread) --------------------------------------------------------

static ath_actor  *g_current;     // running actor, or NULL at top level / in the scheduler
static ath_actor  *g_runq_head;
static ath_actor  *g_runq_tail;
static size_t      g_runq_len;
static ucontext_t  g_sched_ctx;   // where actors swapcontext back to

static double sched_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// ---- run queue (intrusive FIFO) -------------------------------------------------------------

static void runq_push(ath_actor *a) {
    a->qnext = NULL;
    if (g_runq_tail) g_runq_tail->qnext = a;
    else g_runq_head = a;
    g_runq_tail = a;
    g_runq_len++;
}

static ath_actor *runq_pop(void) {
    ath_actor *a = g_runq_head;
    if (!a) return NULL;
    g_runq_head = a->qnext;
    if (!g_runq_head) g_runq_tail = NULL;
    g_runq_len--;
    a->qnext = NULL;
    return a;
}

// ---- mailboxes (shared by actors and channels) ----------------------------------------------

static void mbox_push(ath_obj *h, ath_obj *msg) {
    struct ath_msg *node = (struct ath_msg *)malloc(sizeof *node);
    if (!node) { return; } // drop on OOM; messaging is best-effort
    node->value = msg;
    node->next = NULL;
    if (h->mbox_tail) h->mbox_tail->next = node;
    else h->mbox_head = node;
    h->mbox_tail = node;
}

static ath_obj *mbox_pop(ath_obj *h) {
    struct ath_msg *node = h ? h->mbox_head : NULL;
    if (!node) return NULL;
    h->mbox_head = node->next;
    if (!h->mbox_head) h->mbox_tail = NULL;
    ath_obj *v = node->value;
    free(node);
    return v;
}

static int mbox_nonempty(ath_obj *h) {
    return h != NULL && h->mbox_head != NULL;
}

// ---- coroutine entry / teardown -------------------------------------------------------------

static void ath_trampoline(void) {
    ath_actor *self = g_current;
    self->fn(self->arg); // runs to normal completion, or unwinds cooperatively on cancellation
    self->state = ACT_DONE;
    ath_die(self->handle);
    if (self->parent) {
        if (--self->parent->nursery_pending <= 0) ath_die(self->parent);
    }
    // Hand control back to the scheduler; this context is never resumed.
    swapcontext(&self->ctx, &g_sched_ctx);
}

static ath_obj *spawn_common(ath_obj *(*fn)(ath_obj *), ath_obj *arg, ath_obj *nursery) {
    ath_actor *a = (ath_actor *)calloc(1, sizeof *a);
    if (!a) { return ath_NULL; }
    a->stack = malloc(ATH_ACTOR_STACK_SIZE);
    if (!a->stack) { free(a); return ath_NULL; }
    a->handle = ath_alloc_alive();
    a->handle->actor = a;
    a->fn = fn;
    a->arg = arg;
    a->state = ACT_READY;
    a->parent = nursery;
    if (nursery) {
        // Child dies when the nursery dies (cancel); nursery stays alive while children run (join).
        a->handle->dep1 = nursery;
        nursery->nursery_pending++;
    }
    getcontext(&a->ctx);
    a->ctx.uc_stack.ss_sp = a->stack;
    a->ctx.uc_stack.ss_size = ATH_ACTOR_STACK_SIZE;
    a->ctx.uc_link = NULL; // trampoline swaps back explicitly
    makecontext(&a->ctx, ath_trampoline, 0);
    runq_push(a);
    return a->handle;
}

ath_obj *ath_spawn(ath_obj *(*fn)(ath_obj *), ath_obj *arg) {
    return spawn_common(fn, arg, NULL);
}

ath_obj *ath_spawn_into(ath_obj *(*fn)(ath_obj *), ath_obj *arg, ath_obj *nursery) {
    if (nursery == NULL || nursery == ath_NULL) return spawn_common(fn, arg, NULL);
    return spawn_common(fn, arg, nursery);
}

// ---- scheduling core ------------------------------------------------------------------------

static int actor_runnable(ath_actor *a) {
    // A dead handle (finished or cancelled) is always runnable: we resume it so it can unwind.
    if (!ath_is_alive(a->handle)) {
        a->state = ACT_READY;
        return 1;
    }
    switch (a->state) {
        case ACT_READY:
            return 1;
        case ACT_BLOCKED_RECV:
            return mbox_nonempty(a->wait_on) || !ath_is_alive(a->wait_on);
        case ACT_BLOCKED_SLEEP:
            return sched_now() >= a->park_deadline;
        default:
            return 0;
    }
}

static void actor_run(ath_actor *a) {
    g_current = a;
    swapcontext(&g_sched_ctx, &a->ctx);
    g_current = NULL;
}

static void actor_reap(ath_actor *a) {
    free(a->stack);
    free(a);
}

// Earliest deadline among sleeping actors with a live handle; 0.0 if none.
static double earliest_sleep_deadline(void) {
    double best = 0.0;
    for (ath_actor *a = g_runq_head; a; a = a->qnext) {
        if (a->state == ACT_BLOCKED_SLEEP && ath_is_alive(a->handle)) {
            if (best == 0.0 || a->park_deadline < best) best = a->park_deadline;
        }
    }
    return best;
}

// Run a single runnable actor. Returns 1 if work advanced (an actor ran, or we slept toward a
// deadline), 0 if nothing is runnable (queue empty or a pure deadlock).
static int sched_step(void) {
    size_t n = g_runq_len;
    for (size_t i = 0; i < n; i++) {
        ath_actor *a = runq_pop();
        if (actor_runnable(a)) {
            actor_run(a);
            if (a->state == ACT_DONE) actor_reap(a);
            else runq_push(a);
            return 1;
        }
        runq_push(a);
    }
    // Full sweep, nobody runnable: wait for the nearest sleeper, else give up.
    double dl = earliest_sleep_deadline();
    if (dl > 0.0) {
        double now = sched_now();
        if (dl > now) {
            double rem = dl - now;
            struct timespec ts;
            ts.tv_sec = (time_t)rem;
            ts.tv_nsec = (long)((rem - (double)ts.tv_sec) * 1e9);
            nanosleep(&ts, NULL);
        }
        return 1;
    }
    return 0;
}

void ath_scheduler_drain(void) {
    while (g_runq_len > 0 && sched_step()) {
        /* keep stepping */
    }
}

// ---- cooperative primitives -----------------------------------------------------------------

void ath_yield(void) {
    ath_actor *self = g_current;
    if (!self) return; // top level: nothing to yield to
    self->state = ACT_READY;
    swapcontext(&self->ctx, &g_sched_ctx);
}

void ath_park_until(double deadline_s) {
    ath_actor *self = g_current;
    if (!self) return;
    self->park_deadline = deadline_s;
    self->state = ACT_BLOCKED_SLEEP;
    swapcontext(&self->ctx, &g_sched_ctx);
    self->park_deadline = 0.0;
}

ath_obj *ath_recv_from(ath_obj *src) {
    ath_actor *self = g_current;
    for (;;) {
        // Cancelled actor: stop waiting and unwind (EOF).
        if (self && !ath_is_alive(self->handle)) return ath_NULL;
        ath_obj *m = mbox_pop(src);
        if (m) return m;
        if (!ath_is_alive(src)) return ath_NULL; // closed + drained -> EOF
        if (!self) {
            // Top-level receive on a live, empty channel: drive the scheduler to make progress.
            if (!sched_step()) return ath_NULL;
            continue;
        }
        self->wait_on = src;
        self->state = ACT_BLOCKED_RECV;
        swapcontext(&self->ctx, &g_sched_ctx);
        self->wait_on = NULL;
    }
}

ath_obj *ath_recv(void) {
    ath_actor *self = g_current;
    if (!self) return ath_NULL; // no own mailbox at top level
    return ath_recv_from(self->handle);
}

void ath_send(ath_obj *dest, ath_obj *msg) {
    if (dest == NULL || dest == ath_NULL || !ath_is_alive(dest)) return; // drop to a dead target
    mbox_push(dest, msg);
    // No explicit wakeup needed: blocked receivers are re-checked by actor_runnable each sweep.
}

void ath_join_handle(ath_obj *handle) {
    if (g_current) {
        while (ath_is_alive(handle)) ath_yield();
        return;
    }
    while (ath_is_alive(handle)) {
        if (!sched_step()) break;
    }
}

ath_obj *ath_channel(void) {
    return ath_alloc_alive(); // a live handle with an empty mailbox
}

ath_obj *ath_nursery_new(void) {
    return ath_alloc_alive(); // nursery_pending starts at 0; born alive
}

int ath_in_actor(void) {
    return g_current != NULL;
}
