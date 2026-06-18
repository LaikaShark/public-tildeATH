// Networking for ~ATH. A connection is a channel that carries a socket fd: the handle is alive
// iff the socket is open. Peer close, socket error, or .DIE()/close kills it, which ends a
// ~ATH(C) loop with no new control-flow concept.
//
// Transports: TCP (AF_INET stream) and Unix-domain (AF_UNIX stream) — both stream sockets sharing
// one accept/connect/send/recv path. Payloads are newline-framed strings (cons-lists of char
// atoms), matching ath_input_line. EOF (peer closed + buffer drained) makes recv return ath_NULL
// and the handle dead.
//
// Concurrency: fds are non-blocking. A would-block accept/recv/connect parks the current actor on
// the fd (ath_park_io -> ACT_BLOCKED_IO) and yields; the scheduler's idle poll() rewakes it. At
// top level (no actor) the same wait is a blocking poll(). So one-actor-per-connection servers run
// concurrently while plain top-level scripts still work.
//
// Compose-agnostic: linked into both libath_fresh and libath_intern. All entry points are
// null-safe (C NULL => ath_NULL); setup failures yield a born-dead handle.

#define _GNU_SOURCE

#include "ath_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>

// ---- open-socket registry (for the scheduler-drain sweep) -----------------------------------

// Intrusive singly-linked list through ath_obj::sock_next. Single-threaded; never shrinks (a
// closed entry has sock_fd==0 and is skipped). The node is the ath_obj itself, so no extra alloc.
static ath_obj *g_open_socks;

static void sock_register(ath_obj *o) {
    o->sock_next = g_open_socks;
    g_open_socks = o;
}

void ath_sock_sweep(void) {
    for (ath_obj *o = g_open_socks; o; o = o->sock_next) {
        if (o->sock_fd > 0 && !ath_is_alive(o)) ath_sock_teardown(o);
    }
}

// ---- helpers --------------------------------------------------------------------------------

static ath_obj *net_born_dead(void) {
    ath_obj *o = ath_alloc_alive();
    o->alive = 0;
    return o;
}

static ath_obj *net_make_socket(int fd, int is_listener) {
    ath_obj *o = ath_alloc_alive();
    o->sock_fd = fd;
    o->sock_is_listener = is_listener;
    sock_register(o);
    return o;
}

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

// Wait until fd is ready for events. In an actor: park and yield, returning 0 if the actor was
// cancelled while parked. At top level: blocking poll(), returning 0 on poll error.
static int net_wait(int fd, short events) {
    if (ath_in_actor()) {
        ath_park_io(fd, events);
        return !ath_self_dead();
    }
    struct pollfd p = { fd, events, 0 };
    int r;
    do { r = poll(&p, 1, -1); } while (r < 0 && errno == EINTR);
    return r > 0;
}

// Write all len bytes (looping on partial writes / EAGAIN). 1 on success, 0 on error/cancel.
static int sock_write_all(ath_obj *c, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = send(c->sock_fd, buf + off, len - off, MSG_NOSIGNAL);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!net_wait(c->sock_fd, POLLOUT)) return 0;
            continue;
        }
        return 0; // EPIPE / ECONNRESET / other hard error
    }
    return 1;
}

// Finish a non-blocking connect: wait for writability, then check SO_ERROR. 1 ok, 0 failed.
static int finish_connect(int fd, int rc) {
    if (rc == 0) return 1;
    if (errno != EINPROGRESS) return 0;
    if (!net_wait(fd, POLLOUT)) return 0;
    int err = 0;
    socklen_t el = sizeof err;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err != 0) return 0;
    return 1;
}

// ---- listen ---------------------------------------------------------------------------------

static ath_obj *net_listen_tcp(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return net_born_dead();
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 16) != 0) {
        close(fd);
        return net_born_dead();
    }
    set_nonblock(fd);
    return net_make_socket(fd, 1);
}

static ath_obj *net_listen_unix(const char *path) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    if (path == NULL || strlen(path) >= sizeof addr.sun_path) return net_born_dead();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return net_born_dead();
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof addr.sun_path - 1);
    unlink(path); // clear a stale socket file from a previous run
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 16) != 0) {
        close(fd);
        return net_born_dead();
    }
    set_nonblock(fd);
    return net_make_socket(fd, 1);
}

ath_obj *ath_listen(const char *spec, ath_obj *port) {
    if (spec != NULL && strncmp(spec, "unix:", 5) == 0) return net_listen_unix(spec + 5);
    if (port == NULL || !ath_is_alive(port) || port->num_kind != ATH_NUM_INT
        || port->num.i < 1 || port->num.i > 65535) {
        return net_born_dead();
    }
    return net_listen_tcp((int)port->num.i);
}

// ---- accept ---------------------------------------------------------------------------------

ath_obj *ath_accept(ath_obj *listener) {
    if (listener == NULL || listener == ath_NULL || !ath_is_alive(listener)
        || listener->sock_fd <= 0 || !listener->sock_is_listener) {
        return ath_NULL;
    }
    for (;;) {
        int fd = accept(listener->sock_fd, NULL, NULL);
        if (fd >= 0) {
            set_nonblock(fd);
            return net_make_socket(fd, 0);
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!net_wait(listener->sock_fd, POLLIN)) return ath_NULL;
            continue;
        }
        return net_born_dead(); // hard accept error; listener stays alive
    }
}

// ---- connect --------------------------------------------------------------------------------

static ath_obj *net_connect_tcp(const char *host, int port) {
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || res == NULL) return net_born_dead();
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return net_born_dead();
    }
    set_nonblock(fd);
    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (!finish_connect(fd, rc)) {
        close(fd);
        return net_born_dead();
    }
    return net_make_socket(fd, 0);
}

static ath_obj *net_connect_unix(const char *path) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    if (path == NULL || strlen(path) >= sizeof addr.sun_path) return net_born_dead();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return net_born_dead();
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof addr.sun_path - 1);
    set_nonblock(fd);
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof addr);
    if (!finish_connect(fd, rc)) {
        close(fd);
        return net_born_dead();
    }
    return net_make_socket(fd, 0);
}

ath_obj *ath_connect(const char *host, ath_obj *port) {
    if (host != NULL && strncmp(host, "unix:", 5) == 0) return net_connect_unix(host + 5);
    if (host == NULL || port == NULL || !ath_is_alive(port) || port->num_kind != ATH_NUM_INT
        || port->num.i < 1 || port->num.i > 65535) {
        return net_born_dead();
    }
    return net_connect_tcp(host, (int)port->num.i);
}

// ---- send / recv (reached from ath_send / ath_recv_from on sock_fd>0) -----------------------

void ath_sock_send(ath_obj *c, ath_obj *msg) {
    if (c == NULL || c == ath_NULL || c->sock_fd <= 0 || c->sock_is_listener) return;
    char *bytes = NULL;
    size_t len = 0;
    ath_obj *s = ath_coerce_string(msg);
    if (ath_string_to_bytes(s, &bytes, &len) != 0) return; // malformed payload: drop
    int ok = sock_write_all(c, bytes, len) && sock_write_all(c, "\n", 1);
    free(bytes);
    if (!ok) ath_die(c); // peer gone / write error: connection dies
}

// Emit one line from the buffer at byte offset nl (the newline index), stripping a trailing '\r',
// shifting the remainder down. Returns the line as a fresh string.
static ath_obj *take_line(ath_obj *c, size_t nl) {
    size_t linelen = nl;
    if (linelen > 0 && c->sock_rbuf[linelen - 1] == '\r') linelen--;
    ath_obj *s = ath_string_from_bytes(c->sock_rbuf, linelen);
    size_t rest = c->sock_rlen - (nl + 1);
    memmove(c->sock_rbuf, c->sock_rbuf + nl + 1, rest);
    c->sock_rlen = rest;
    return s;
}

ath_obj *ath_sock_recv_line(ath_obj *c) {
    if (c == NULL || c == ath_NULL || c->sock_fd <= 0 || c->sock_is_listener) return ath_NULL;
    for (;;) {
        for (size_t k = 0; k < c->sock_rlen; k++) {
            if (c->sock_rbuf[k] == '\n') return take_line(c, k);
        }
        if (c->sock_rlen == c->sock_rcap) {
            size_t newcap = c->sock_rcap ? c->sock_rcap * 2 : 1024;
            char *nb = (char *)realloc(c->sock_rbuf, newcap);
            if (!nb) { ath_die(c); return ath_NULL; }
            c->sock_rbuf = nb;
            c->sock_rcap = newcap;
        }
        ssize_t got = recv(c->sock_fd, c->sock_rbuf + c->sock_rlen,
                           c->sock_rcap - c->sock_rlen, 0);
        if (got > 0) {
            c->sock_rlen += (size_t)got;
            continue;
        }
        if (got == 0) {
            // Peer closed. Deliver any unterminated remainder as a final line, then die.
            if (c->sock_rlen > 0) {
                size_t linelen = c->sock_rlen;
                if (c->sock_rbuf[linelen - 1] == '\r') linelen--;
                ath_obj *s = ath_string_from_bytes(c->sock_rbuf, linelen);
                c->sock_rlen = 0;
                ath_die(c);
                return s;
            }
            ath_die(c);
            return ath_NULL;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!net_wait(c->sock_fd, POLLIN)) { ath_die(c); return ath_NULL; }
            continue;
        }
        ath_die(c); // hard error
        return ath_NULL;
    }
}

ath_obj *ath_connect_obj(ath_obj *host_obj, ath_obj *port) {
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_to_bytes(host_obj, &buf, &len) != 0) return net_born_dead();
    ath_obj *r = ath_connect(buf, port);
    free(buf);
    return r;
}

// ---- teardown -------------------------------------------------------------------------------

void ath_sock_teardown(ath_obj *v) {
    if (v == NULL || v == ath_NULL || v->sock_fd <= 0) return;
    close(v->sock_fd);
    v->sock_fd = 0; // makes this idempotent
    free(v->sock_rbuf);
    v->sock_rbuf = NULL;
    v->sock_rlen = 0;
    v->sock_rcap = 0;
    v->sock_eof = 1;
}
