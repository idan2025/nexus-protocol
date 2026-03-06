/*
 * NEXUS Protocol -- TCP Transport
 *
 * For multi-node mesh simulation on localhost (or LAN).
 * Uses the same framing as serial: [0x7E][LEN_HI][LEN_LO][PAYLOAD][0x7E]
 *
 * Two modes:
 * - Server: Listens on a port, accepts one connection.
 * - Client: Connects to a server.
 *
 * This is a point-to-point TCP link (one peer). For a mesh, each node
 * creates multiple TCP transports (one per link).
 */
#define _POSIX_C_SOURCE 200809L

#include "nexus/transport.h"
#include "nexus/platform.h"

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

typedef struct {
    int       listen_fd;   /* Server only: listening socket (-1 if client) */
    int       conn_fd;     /* Connected peer socket */
    bool      is_server;
    uint16_t  port;
    char      host[64];
} tcp_state_t;

/* ── Init ────────────────────────────────────────────────────────────── */

static nx_err_t tcp_init(nx_transport_t *t, const void *config)
{
    const nx_tcp_config_t *cfg = (const nx_tcp_config_t *)config;
    if (!cfg) return NX_ERR_INVALID_ARG;

    tcp_state_t *s = (tcp_state_t *)nx_platform_alloc(sizeof(tcp_state_t));
    if (!s) return NX_ERR_NO_MEMORY;
    memset(s, 0, sizeof(*s));

    s->listen_fd = -1;
    s->conn_fd   = -1;
    s->is_server = cfg->server;
    s->port      = cfg->port;

    if (cfg->host) {
        size_t hlen = strlen(cfg->host);
        if (hlen >= sizeof(s->host)) hlen = sizeof(s->host) - 1;
        memcpy(s->host, cfg->host, hlen);
        s->host[hlen] = '\0';
    } else {
        memcpy(s->host, "0.0.0.0", 8);
    }

    if (cfg->server) {
        /* Create listening socket */
        s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (s->listen_fd < 0) goto fail;

        int opt = 1;
        setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(cfg->port);
        if (inet_pton(AF_INET, s->host, &addr.sin_addr) != 1) goto fail;

        if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            goto fail;
        if (listen(s->listen_fd, 1) < 0)
            goto fail;

        /* Accept is deferred to first recv/send to avoid blocking init */
    } else {
        /* Client: connect immediately */
        s->conn_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (s->conn_fd < 0) goto fail;

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(cfg->port);
        if (inet_pton(AF_INET, s->host, &addr.sin_addr) != 1) goto fail;

        if (connect(s->conn_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            goto fail;

        /* Disable Nagle for low-latency */
        int flag = 1;
        setsockopt(s->conn_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    }

    t->state  = s;
    t->active = true;
    return NX_OK;

fail:
    if (s->listen_fd >= 0) close(s->listen_fd);
    if (s->conn_fd >= 0) close(s->conn_fd);
    nx_platform_free(s);
    return NX_ERR_IO;
}

/* ── Ensure connected (server accepts lazily) ────────────────────────── */

static nx_err_t ensure_connected(tcp_state_t *s, uint32_t timeout_ms)
{
    if (s->conn_fd >= 0) return NX_OK;
    if (!s->is_server || s->listen_fd < 0) return NX_ERR_TRANSPORT;

    struct pollfd pfd = { .fd = s->listen_fd, .events = POLLIN };
    int pr = poll(&pfd, 1, (int)timeout_ms);
    if (pr < 0) return NX_ERR_IO;
    if (pr == 0) return NX_ERR_TIMEOUT;

    s->conn_fd = accept(s->listen_fd, NULL, NULL);
    if (s->conn_fd < 0) return NX_ERR_IO;

    int flag = 1;
    setsockopt(s->conn_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    return NX_OK;
}

/* ── Send ────────────────────────────────────────────────────────────── */

static nx_err_t tcp_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    tcp_state_t *s = (tcp_state_t *)t->state;
    if (!s) return NX_ERR_TRANSPORT;

    nx_err_t err = ensure_connected(s, 1000);
    if (err != NX_OK) return err;

    if (len > NX_MAX_PACKET) return NX_ERR_INVALID_ARG;

    uint8_t frame[TCP_MAX_FRAME];
    size_t pos = 0;

    frame[pos++] = TCP_FRAME_DELIM;
    frame[pos++] = (uint8_t)(len >> 8);
    frame[pos++] = (uint8_t)(len);
    memcpy(&frame[pos], data, len);
    pos += len;
    frame[pos++] = TCP_FRAME_DELIM;

    size_t written = 0;
    while (written < pos) {
        ssize_t n = write(s->conn_fd, frame + written, pos - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return NX_ERR_IO;
        }
        written += (size_t)n;
    }

    return NX_OK;
}

/* ── Recv ────────────────────────────────────────────────────────────── */

static nx_err_t tcp_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                         size_t *out_len, uint32_t timeout_ms)
{
    tcp_state_t *s = (tcp_state_t *)t->state;
    if (!s) return NX_ERR_TRANSPORT;

    nx_err_t err = ensure_connected(s, timeout_ms);
    if (err != NX_OK) return err;

    uint64_t deadline = nx_platform_time_ms() + timeout_ms;

    enum { WAIT_START, READ_LEN_HI, READ_LEN_LO, READ_PAYLOAD, WAIT_END } state = WAIT_START;
    uint16_t frame_len = 0;
    size_t   payload_pos = 0;

    for (;;) {
        uint64_t now = nx_platform_time_ms();
        if (now >= deadline) return NX_ERR_TIMEOUT;

        int remaining = (int)(deadline - now);
        struct pollfd pfd = { .fd = s->conn_fd, .events = POLLIN };
        int pr = poll(&pfd, 1, remaining);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return NX_ERR_IO;
        }
        if (pr == 0) return NX_ERR_TIMEOUT;

        uint8_t byte;
        ssize_t n = read(s->conn_fd, &byte, 1);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return NX_ERR_IO;
        }
        if (n == 0) return NX_ERR_IO; /* Peer closed */

        switch (state) {
        case WAIT_START:
            if (byte == TCP_FRAME_DELIM) state = READ_LEN_HI;
            break;
        case READ_LEN_HI:
            frame_len = (uint16_t)((uint16_t)byte << 8);
            state = READ_LEN_LO;
            break;
        case READ_LEN_LO:
            frame_len |= byte;
            if (frame_len == 0 || frame_len > NX_MAX_PACKET) {
                state = WAIT_START;
                break;
            }
            if (frame_len > buf_len) return NX_ERR_BUFFER_TOO_SMALL;
            payload_pos = 0;
            state = READ_PAYLOAD;
            break;
        case READ_PAYLOAD:
            buf[payload_pos++] = byte;
            if (payload_pos >= frame_len) state = WAIT_END;
            break;
        case WAIT_END:
            if (byte == TCP_FRAME_DELIM) {
                *out_len = frame_len;
                return NX_OK;
            }
            state = WAIT_START;
            break;
        }
    }
}

/* ── Destroy ─────────────────────────────────────────────────────────── */

static void tcp_destroy(nx_transport_t *t)
{
    tcp_state_t *s = (tcp_state_t *)t->state;
    if (s) {
        if (s->conn_fd >= 0) close(s->conn_fd);
        if (s->listen_fd >= 0) close(s->listen_fd);
        nx_platform_free(s);
    }
    t->state  = NULL;
    t->active = false;
}

/* ── Vtable & Constructor ────────────────────────────────────────────── */

static const nx_transport_ops_t tcp_ops = {
    .init    = tcp_init,
    .send    = tcp_send,
    .recv    = tcp_recv,
    .destroy = tcp_destroy,
};

nx_transport_t *nx_tcp_transport_create(void)
{
    nx_transport_t *t = (nx_transport_t *)nx_platform_alloc(sizeof(nx_transport_t));
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));
    t->type   = NX_TRANSPORT_TCP;
    t->name   = "tcp";
    t->ops    = &tcp_ops;
    t->active = false;
    return t;
}
