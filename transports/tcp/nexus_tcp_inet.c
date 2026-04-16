/*
 * NEXUS Protocol -- TCP Internet Transport (Reticulum-style)
 *
 * Multi-peer TCP transport for Internet connectivity.
 * Unlike the point-to-point nexus_tcp.c, this transport:
 * - Accepts multiple inbound connections (server mode)
 * - Connects to multiple remote peers (client mode) with auto-reconnect
 * - Can run server + client simultaneously
 * - Send broadcasts to all connected peers
 * - Recv polls all connections, returns first available packet
 *
 * Uses the same framing as serial/TCP: [0x7E][LEN_HI][LEN_LO][PAYLOAD][0x7E]
 */
#define _POSIX_C_SOURCE 200809L

#include "nexus/transport.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define TCP_FRAME_DELIM   0x7E
#define TCP_MAX_FRAME     (NX_MAX_PACKET + 4)

static const uint8_t AUTH_MAGIC[8] = { 'N','X','A','U','T','H','\0','\0' };

typedef enum {
    AUTH_NONE = 0,     /* No auth required; treat as authed */
    AUTH_SEND_HELLO,   /* Need to send our hello */
    AUTH_RECV_HELLO,   /* Hello sent; waiting for peer hello */
    AUTH_SEND_TAG,     /* Peer hello received; need to send response tag */
    AUTH_RECV_TAG,     /* Tag sent; waiting for peer tag */
    AUTH_DONE,         /* Mutually authenticated */
    AUTH_FAIL,         /* Authentication failed; connection will be closed */
} auth_state_t;

/* ── Per-connection receive state machine ────────────────────────────── */

typedef enum {
    RX_WAIT_START,
    RX_READ_LEN_HI,
    RX_READ_LEN_LO,
    RX_READ_PAYLOAD,
    RX_WAIT_END,
} rx_state_t;

typedef struct {
    rx_state_t state;
    uint16_t   frame_len;
    size_t     payload_pos;
    uint8_t    buf[NX_MAX_PACKET];
} rx_ctx_t;

/* ── Connection slot ─────────────────────────────────────────────────── */

typedef struct {
    int         fd;
    bool        active;
    bool        is_outbound;       /* true = we initiated this connection */
    char        peer_host[64];     /* For outbound: reconnect target */
    uint16_t    peer_port;
    uint64_t    last_reconnect_ms; /* When we last tried to reconnect */
    rx_ctx_t    rx;                /* Per-connection framing state */

    auth_state_t auth_state;
    uint64_t     auth_deadline_ms;
    uint8_t      my_nonce[32];
    uint8_t      peer_nonce[32];
    uint8_t      auth_rx[NX_TCP_INET_AUTH_HELLO_SIZE];
    size_t       auth_rx_pos;
    size_t       auth_rx_need;
} tcp_inet_conn_t;

/* ── Transport state ─────────────────────────────────────────────────── */

typedef struct {
    int                listen_fd;
    uint16_t           listen_port;
    char               listen_host[64];

    tcp_inet_conn_t    conns[NX_TCP_INET_MAX_PEERS];
    int                conn_count;

    uint32_t           reconnect_interval_ms;

    uint8_t            psk[NX_TCP_INET_PSK_SIZE];
    size_t             psk_len;

    char               allow_list[NX_TCP_INET_MAX_ALLOW][64];
    int                allow_count;

    /* Failover mode: try one outbound peer at a time */
    bool               failover;
    uint32_t           failover_timeout_ms;
    int                failover_current;       /* Index into conns of next peer to try */
    int                failover_peer_count;    /* How many outbound peers configured */
    int                failover_first_slot;    /* conns index where outbound peers start */
    uint64_t           failover_attempt_ms;    /* When we started trying current peer */
} tcp_inet_state_t;

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void set_nodelay(int fd)
{
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

static void set_keepalive(int fd)
{
    int flag = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
}

static void conn_init(tcp_inet_conn_t *c)
{
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    c->active = false;
    c->rx.state = RX_WAIT_START;
    c->auth_state = AUTH_NONE;
}

static void auth_compute_tag(const uint8_t psk[NX_TCP_INET_PSK_SIZE], size_t psk_len,
                             const uint8_t first[32], const uint8_t second[32],
                             uint8_t out_tag[NX_TCP_INET_AUTH_TAG_SIZE])
{
    uint8_t input[64];
    memcpy(input, first, 32);
    memcpy(input + 32, second, 32);
    crypto_blake2b_keyed(out_tag, NX_TCP_INET_AUTH_TAG_SIZE, psk, psk_len,
                         input, sizeof(input));
    crypto_wipe(input, sizeof(input));
}

static bool auth_write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { .fd = fd, .events = POLLOUT };
                if (poll(&pfd, 1, 500) <= 0) return false;
                continue;
            }
            return false;
        }
        written += (size_t)n;
    }
    return true;
}

static void conn_auth_arm(tcp_inet_conn_t *c, size_t psk_len)
{
    if (psk_len == 0) {
        c->auth_state = AUTH_NONE;
        return;
    }
    c->auth_state = AUTH_SEND_HELLO;
    c->auth_rx_pos = 0;
    c->auth_rx_need = NX_TCP_INET_AUTH_HELLO_SIZE;
    c->auth_deadline_ms = nx_platform_time_ms() + NX_TCP_INET_AUTH_TIMEOUT_MS;
    nx_platform_random(c->my_nonce, sizeof(c->my_nonce));
}

static bool conn_is_authed(const tcp_inet_conn_t *c)
{
    return c->auth_state == AUTH_NONE || c->auth_state == AUTH_DONE;
}

static bool ip_allowed(const tcp_inet_state_t *s, const char *ip)
{
    if (s->allow_count == 0) return true;
    for (int i = 0; i < s->allow_count; i++) {
        if (strcmp(s->allow_list[i], ip) == 0) return true;
    }
    return false;
}

static void conn_close(tcp_inet_conn_t *c)
{
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    c->active = false;
    c->rx.state = RX_WAIT_START;
    c->rx.frame_len = 0;
    c->rx.payload_pos = 0;
}

static tcp_inet_conn_t *find_free_slot(tcp_inet_state_t *s)
{
    for (int i = 0; i < NX_TCP_INET_MAX_PEERS; i++) {
        if (!s->conns[i].active && s->conns[i].fd < 0)
            return &s->conns[i];
    }
    return NULL;
}

static int try_connect(const char *host, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    /* Set non-blocking before connect for non-blocking connect */
    set_nonblocking(fd);

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    if (ret < 0) {
        /* EINPROGRESS: poll for write-ready to complete connect */
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        int pr = poll(&pfd, 1, 3000); /* 3s timeout for connect */
        if (pr <= 0) {
            close(fd);
            return -1;
        }
        int err = 0;
        socklen_t errlen = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
        if (err != 0) {
            close(fd);
            return -1;
        }
    }

    set_nodelay(fd);
    set_keepalive(fd);
    return fd;
}

/* ── Init ────────────────────────────────────────────────────────────── */

static nx_err_t tcp_inet_init(nx_transport_t *t, const void *config)
{
    const nx_tcp_inet_config_t *cfg = (const nx_tcp_inet_config_t *)config;
    if (!cfg) return NX_ERR_INVALID_ARG;

    tcp_inet_state_t *s = (tcp_inet_state_t *)nx_platform_alloc(sizeof(tcp_inet_state_t));
    if (!s) return NX_ERR_NO_MEMORY;
    memset(s, 0, sizeof(*s));

    s->listen_fd = -1;
    s->reconnect_interval_ms = cfg->reconnect_interval_ms;
    if (s->reconnect_interval_ms == 0)
        s->reconnect_interval_ms = 5000;

    if (cfg->psk_len > 0) {
        if (cfg->psk_len > NX_TCP_INET_PSK_SIZE) {
            nx_platform_free(s);
            return NX_ERR_INVALID_ARG;
        }
        memcpy(s->psk, cfg->psk, cfg->psk_len);
        s->psk_len = cfg->psk_len;
    }

    for (int i = 0; i < cfg->allow_count && i < NX_TCP_INET_MAX_ALLOW; i++) {
        if (!cfg->allow_list[i]) continue;
        size_t alen = strlen(cfg->allow_list[i]);
        if (alen >= sizeof(s->allow_list[0])) alen = sizeof(s->allow_list[0]) - 1;
        memcpy(s->allow_list[s->allow_count], cfg->allow_list[i], alen);
        s->allow_list[s->allow_count][alen] = '\0';
        s->allow_count++;
    }

    for (int i = 0; i < NX_TCP_INET_MAX_PEERS; i++)
        conn_init(&s->conns[i]);

    /* ── Server: set up listening socket ─────────────────────────────── */
    if (cfg->listen_port > 0) {
        s->listen_port = cfg->listen_port;

        if (cfg->listen_host) {
            size_t hlen = strlen(cfg->listen_host);
            if (hlen >= sizeof(s->listen_host)) hlen = sizeof(s->listen_host) - 1;
            memcpy(s->listen_host, cfg->listen_host, hlen);
            s->listen_host[hlen] = '\0';
        } else {
            memcpy(s->listen_host, "0.0.0.0", 8);
        }

        s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (s->listen_fd < 0) goto fail;

        int opt = 1;
        setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(cfg->listen_port);
        if (inet_pton(AF_INET, s->listen_host, &addr.sin_addr) != 1) goto fail;

        if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            goto fail;
        if (listen(s->listen_fd, NX_TCP_INET_MAX_PEERS) < 0)
            goto fail;

        set_nonblocking(s->listen_fd);
    }

    /* ── Failover mode setup ────────────────────────────────────────── */
    s->failover = cfg->failover;
    s->failover_timeout_ms = cfg->failover_timeout_ms;
    if (s->failover_timeout_ms == 0)
        s->failover_timeout_ms = 10000;
    s->failover_current = 0;
    s->failover_peer_count = 0;
    s->failover_first_slot = -1;

    /* ── Client: connect to configured peers ─────────────────────────── */
    for (int i = 0; i < cfg->peer_count && i < NX_TCP_INET_MAX_PEERS; i++) {
        tcp_inet_conn_t *c = find_free_slot(s);
        if (!c) break;

        if (s->failover_first_slot < 0)
            s->failover_first_slot = (int)(c - s->conns);
        s->failover_peer_count++;

        size_t hlen = strlen(cfg->peers[i].host);
        if (hlen >= sizeof(c->peer_host)) hlen = sizeof(c->peer_host) - 1;
        memcpy(c->peer_host, cfg->peers[i].host, hlen);
        c->peer_host[hlen] = '\0';
        c->peer_port = cfg->peers[i].port;
        c->is_outbound = true;

        /* In failover mode, only connect to the first peer initially */
        if (s->failover && i > 0) continue;

        int fd = try_connect(c->peer_host, c->peer_port);
        if (fd >= 0) {
            c->fd = fd;
            c->active = true;
            conn_auth_arm(c, s->psk_len);
            s->conn_count++;
            if (s->failover)
                s->failover_attempt_ms = nx_platform_time_ms();
        } else {
            /* Will auto-reconnect later */
            c->last_reconnect_ms = nx_platform_time_ms();
            if (s->failover)
                s->failover_attempt_ms = nx_platform_time_ms();
        }
    }

    t->state = s;
    t->active = true;
    return NX_OK;

fail:
    if (s->listen_fd >= 0) close(s->listen_fd);
    nx_platform_free(s);
    return NX_ERR_IO;
}

/* ── Accept new inbound connections ──────────────────────────────────── */

static void accept_pending(tcp_inet_state_t *s)
{
    if (s->listen_fd < 0) return;

    for (;;) {
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        int fd = accept(s->listen_fd, (struct sockaddr *)&peer_addr, &peer_len);
        if (fd < 0) break; /* EAGAIN or error */

        char ip_str[64];
        inet_ntop(AF_INET, &peer_addr.sin_addr, ip_str, sizeof(ip_str));

        if (!ip_allowed(s, ip_str)) {
            close(fd);
            continue;
        }

        tcp_inet_conn_t *c = find_free_slot(s);
        if (!c) {
            /* No room, reject */
            close(fd);
            break;
        }

        set_nonblocking(fd);
        set_nodelay(fd);
        set_keepalive(fd);

        c->fd = fd;
        c->active = true;
        c->is_outbound = false;

        /* Store peer address for logging/debugging */
        memcpy(c->peer_host, ip_str, sizeof(c->peer_host));
        c->peer_port = ntohs(peer_addr.sin_port);

        conn_auth_arm(c, s->psk_len);
        s->conn_count++;
    }
}

/* ── Reconnect dropped outbound connections ──────────────────────────── */

/* Check whether any outbound peer is currently connected. */
static bool has_active_outbound(const tcp_inet_state_t *s)
{
    for (int i = 0; i < NX_TCP_INET_MAX_PEERS; i++) {
        const tcp_inet_conn_t *c = &s->conns[i];
        if (c->is_outbound && c->active)
            return true;
    }
    return false;
}

static void reconnect_outbound_parallel(tcp_inet_state_t *s, uint64_t now)
{
    for (int i = 0; i < NX_TCP_INET_MAX_PEERS; i++) {
        tcp_inet_conn_t *c = &s->conns[i];
        if (!c->is_outbound) continue;
        if (c->active) continue;
        if (c->peer_port == 0) continue;

        if (now - c->last_reconnect_ms < s->reconnect_interval_ms)
            continue;

        c->last_reconnect_ms = now;

        int fd = try_connect(c->peer_host, c->peer_port);
        if (fd >= 0) {
            c->fd = fd;
            c->active = true;
            c->rx.state = RX_WAIT_START;
            conn_auth_arm(c, s->psk_len);
            s->conn_count++;
        }
    }
}

static void reconnect_outbound_failover(tcp_inet_state_t *s, uint64_t now)
{
    if (s->failover_peer_count == 0 || s->failover_first_slot < 0)
        return;

    /* If already connected to a Pillar, nothing to do */
    if (has_active_outbound(s))
        return;

    /* Respect reconnect interval between attempts */
    int slot = s->failover_first_slot + s->failover_current;
    tcp_inet_conn_t *c = &s->conns[slot];

    if (now - c->last_reconnect_ms < s->reconnect_interval_ms)
        return;

    /* If we've been trying this peer too long, advance to the next */
    if (s->failover_attempt_ms > 0 &&
        now - s->failover_attempt_ms >= s->failover_timeout_ms) {
        s->failover_current = (s->failover_current + 1) % s->failover_peer_count;
        s->failover_attempt_ms = now;
        slot = s->failover_first_slot + s->failover_current;
        c = &s->conns[slot];
    }

    c->last_reconnect_ms = now;

    int fd = try_connect(c->peer_host, c->peer_port);
    if (fd >= 0) {
        c->fd = fd;
        c->active = true;
        c->rx.state = RX_WAIT_START;
        conn_auth_arm(c, s->psk_len);
        s->conn_count++;
        s->failover_attempt_ms = now;
    } else if (s->failover_attempt_ms == 0) {
        /* First attempt for this peer, start the timeout clock */
        s->failover_attempt_ms = now;
    }
}

static void reconnect_outbound(tcp_inet_state_t *s)
{
    uint64_t now = nx_platform_time_ms();

    if (s->failover)
        reconnect_outbound_failover(s, now);
    else
        reconnect_outbound_parallel(s, now);
}

/* Drive the PSK challenge-response state machine for one connection.
 * Returns true if connection is still usable, false if it should be dropped. */
static bool auth_pump(tcp_inet_state_t *s, tcp_inet_conn_t *c)
{
    if (conn_is_authed(c)) return true;
    if (c->auth_state == AUTH_FAIL) return false;

    if (nx_platform_time_ms() > c->auth_deadline_ms) {
        c->auth_state = AUTH_FAIL;
        return false;
    }

    /* Send hello once */
    if (c->auth_state == AUTH_SEND_HELLO) {
        uint8_t hello[NX_TCP_INET_AUTH_HELLO_SIZE];
        memcpy(hello, AUTH_MAGIC, 8);
        memcpy(hello + 8, c->my_nonce, 32);
        if (!auth_write_all(c->fd, hello, sizeof(hello))) {
            c->auth_state = AUTH_FAIL;
            return false;
        }
        c->auth_state = AUTH_RECV_HELLO;
        c->auth_rx_pos = 0;
        c->auth_rx_need = NX_TCP_INET_AUTH_HELLO_SIZE;
    }

    /* Read any available handshake bytes */
    while (c->auth_state == AUTH_RECV_HELLO || c->auth_state == AUTH_RECV_TAG) {
        ssize_t n = read(c->fd, c->auth_rx + c->auth_rx_pos,
                         c->auth_rx_need - c->auth_rx_pos);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            c->auth_state = AUTH_FAIL;
            return false;
        }
        if (n == 0) {
            c->auth_state = AUTH_FAIL;
            return false;
        }
        c->auth_rx_pos += (size_t)n;
        if (c->auth_rx_pos < c->auth_rx_need) return true;

        if (c->auth_state == AUTH_RECV_HELLO) {
            if (memcmp(c->auth_rx, AUTH_MAGIC, 8) != 0) {
                c->auth_state = AUTH_FAIL;
                return false;
            }
            memcpy(c->peer_nonce, c->auth_rx + 8, 32);

            uint8_t tag[NX_TCP_INET_AUTH_TAG_SIZE];
            auth_compute_tag(s->psk, s->psk_len, c->my_nonce, c->peer_nonce, tag);
            if (!auth_write_all(c->fd, tag, sizeof(tag))) {
                crypto_wipe(tag, sizeof(tag));
                c->auth_state = AUTH_FAIL;
                return false;
            }
            crypto_wipe(tag, sizeof(tag));

            c->auth_rx_pos = 0;
            c->auth_rx_need = NX_TCP_INET_AUTH_TAG_SIZE;
            c->auth_state = AUTH_RECV_TAG;
            continue;
        }

        /* AUTH_RECV_TAG: verify peer's tag */
        uint8_t expected[NX_TCP_INET_AUTH_TAG_SIZE];
        auth_compute_tag(s->psk, s->psk_len, c->peer_nonce, c->my_nonce, expected);
        int diff = crypto_verify32(expected, c->auth_rx);
        crypto_wipe(expected, sizeof(expected));
        if (diff != 0) {
            c->auth_state = AUTH_FAIL;
            return false;
        }
        c->auth_state = AUTH_DONE;
        return true;
    }

    return true;
}

/* ── Send (broadcast to all peers) ───────────────────────────────────── */

static nx_err_t tcp_inet_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    tcp_inet_state_t *s = (tcp_inet_state_t *)t->state;
    if (!s) return NX_ERR_TRANSPORT;

    if (len > NX_MAX_PACKET) return NX_ERR_INVALID_ARG;

    uint8_t frame[TCP_MAX_FRAME];
    size_t pos = 0;
    frame[pos++] = TCP_FRAME_DELIM;
    frame[pos++] = (uint8_t)(len >> 8);
    frame[pos++] = (uint8_t)(len);
    memcpy(&frame[pos], data, len);
    pos += len;
    frame[pos++] = TCP_FRAME_DELIM;

    bool any_sent = false;

    for (int i = 0; i < NX_TCP_INET_MAX_PEERS; i++) {
        tcp_inet_conn_t *c = &s->conns[i];
        if (!c->active || c->fd < 0) continue;
        if (!conn_is_authed(c)) continue;

        size_t written = 0;
        bool failed = false;

        while (written < pos) {
            ssize_t n = write(c->fd, frame + written, pos - written);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    /* Poll for write-ready briefly */
                    struct pollfd pfd = { .fd = c->fd, .events = POLLOUT };
                    if (poll(&pfd, 1, 100) <= 0) { failed = true; break; }
                    continue;
                }
                failed = true;
                break;
            }
            written += (size_t)n;
        }

        if (failed) {
            conn_close(c);
            s->conn_count--;
        } else {
            any_sent = true;
        }
    }

    return any_sent ? NX_OK : NX_ERR_TRANSPORT;
}

/* ── Recv (poll all connections, return first complete packet) ────────── */

static nx_err_t tcp_inet_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                               size_t *out_len, uint32_t timeout_ms)
{
    tcp_inet_state_t *s = (tcp_inet_state_t *)t->state;
    if (!s) return NX_ERR_TRANSPORT;

    uint64_t deadline = nx_platform_time_ms() + timeout_ms;

    for (;;) {
        /* Accept new connections + reconnect dropped ones */
        accept_pending(s);
        reconnect_outbound(s);

        /* Drive PSK handshake for any pending connections */
        for (int i = 0; i < NX_TCP_INET_MAX_PEERS; i++) {
            tcp_inet_conn_t *c = &s->conns[i];
            if (!c->active || c->fd < 0) continue;
            if (conn_is_authed(c)) continue;
            if (!auth_pump(s, c)) {
                conn_close(c);
                s->conn_count--;
            }
        }

        /* Build poll set: listen_fd + all active connections */
        struct pollfd pfds[NX_TCP_INET_MAX_PEERS + 1];
        int           fd_map[NX_TCP_INET_MAX_PEERS + 1]; /* maps pfd idx -> conn idx, -1 = listen */
        int nfds = 0;

        if (s->listen_fd >= 0) {
            pfds[nfds].fd = s->listen_fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            fd_map[nfds] = -1;
            nfds++;
        }

        for (int i = 0; i < NX_TCP_INET_MAX_PEERS; i++) {
            if (s->conns[i].active && s->conns[i].fd >= 0) {
                pfds[nfds].fd = s->conns[i].fd;
                pfds[nfds].events = POLLIN;
                pfds[nfds].revents = 0;
                fd_map[nfds] = i;
                nfds++;
            }
        }

        if (nfds == 0) {
            /* No connections and no listener - check timeout */
            uint64_t now = nx_platform_time_ms();
            if (now >= deadline) return NX_ERR_TIMEOUT;
            /* Brief sleep, then retry (reconnect may succeed) */
            struct pollfd dummy;
            dummy.fd = -1;
            dummy.events = 0;
            poll(&dummy, 0, 50);
            continue;
        }

        uint64_t now = nx_platform_time_ms();
        if (now >= deadline) return NX_ERR_TIMEOUT;

        int remaining = (int)(deadline - now);
        if (remaining < 1) remaining = 1;

        int pr = poll(pfds, (nfds_t)nfds, remaining);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return NX_ERR_IO;
        }
        if (pr == 0) return NX_ERR_TIMEOUT;

        /* Process events */
        for (int p = 0; p < nfds; p++) {
            if (!(pfds[p].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;

            if (fd_map[p] == -1) {
                /* Listener has a new connection */
                accept_pending(s);
                continue;
            }

            int ci = fd_map[p];
            tcp_inet_conn_t *c = &s->conns[ci];

            /* If not yet authenticated, route bytes through auth state machine */
            if (!conn_is_authed(c)) {
                if (!auth_pump(s, c)) {
                    conn_close(c);
                    s->conn_count--;
                }
                continue;
            }

            /* Read available bytes and run framing state machine */
            uint8_t chunk[256];
            ssize_t nr = read(c->fd, chunk, sizeof(chunk));
            if (nr <= 0) {
                /* Peer closed or error */
                conn_close(c);
                s->conn_count--;
                continue;
            }

            for (ssize_t b = 0; b < nr; b++) {
                uint8_t byte = chunk[b];

                switch (c->rx.state) {
                case RX_WAIT_START:
                    if (byte == TCP_FRAME_DELIM) c->rx.state = RX_READ_LEN_HI;
                    break;
                case RX_READ_LEN_HI:
                    c->rx.frame_len = (uint16_t)((uint16_t)byte << 8);
                    c->rx.state = RX_READ_LEN_LO;
                    break;
                case RX_READ_LEN_LO:
                    c->rx.frame_len |= byte;
                    if (c->rx.frame_len == 0 || c->rx.frame_len > NX_MAX_PACKET) {
                        c->rx.state = RX_WAIT_START;
                    } else {
                        c->rx.payload_pos = 0;
                        c->rx.state = RX_READ_PAYLOAD;
                    }
                    break;
                case RX_READ_PAYLOAD:
                    c->rx.buf[c->rx.payload_pos++] = byte;
                    if (c->rx.payload_pos >= c->rx.frame_len)
                        c->rx.state = RX_WAIT_END;
                    break;
                case RX_WAIT_END:
                    if (byte == TCP_FRAME_DELIM) {
                        /* Complete packet! */
                        if (c->rx.frame_len <= buf_len) {
                            memcpy(buf, c->rx.buf, c->rx.frame_len);
                            *out_len = c->rx.frame_len;
                            c->rx.state = RX_WAIT_START;
                            return NX_OK;
                        }
                        /* Buffer too small, drop */
                    }
                    c->rx.state = RX_WAIT_START;
                    break;
                }
            }
        }
        /* No complete packet yet, loop */
    }
}

/* ── Destroy ─────────────────────────────────────────────────────────── */

static void tcp_inet_destroy(nx_transport_t *t)
{
    tcp_inet_state_t *s = (tcp_inet_state_t *)t->state;
    if (s) {
        for (int i = 0; i < NX_TCP_INET_MAX_PEERS; i++) {
            if (s->conns[i].fd >= 0)
                close(s->conns[i].fd);
        }
        if (s->listen_fd >= 0)
            close(s->listen_fd);
        nx_platform_free(s);
    }
    t->state = NULL;
    t->active = false;
}

/* ── Vtable & Constructor ────────────────────────────────────────────── */

static const nx_transport_ops_t tcp_inet_ops = {
    .init    = tcp_inet_init,
    .send    = tcp_inet_send,
    .recv    = tcp_inet_recv,
    .destroy = tcp_inet_destroy,
};

nx_transport_t *nx_tcp_inet_transport_create(void)
{
    nx_transport_t *t = (nx_transport_t *)nx_platform_alloc(sizeof(nx_transport_t));
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));
    t->type   = NX_TRANSPORT_TCP;
    t->name   = "tcp_inet";
    t->ops    = &tcp_inet_ops;
    t->active = false;
    return t;
}
