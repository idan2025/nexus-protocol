/*
 * pillard -- NEXUS Pillar Server
 *
 * A dedicated public Internet relay for the NEXUS mesh network.
 *
 * A Pillar is a publicly-reachable NEXUS node that:
 *   - Accepts inbound TCP connections from other NEXUS nodes (phones, gateways,
 *     other pillars) that sit behind NAT / mobile networks with no port forward.
 *   - Forwards mesh traffic between all its connected peers (Reticulum-style
 *     many-to-many TCP hub).
 *   - Stores-and-forwards messages for offline destinations via the VAULT-tier
 *     anchor mailbox (256 slots, 24h TTL).
 *   - Optionally peers with other pillars for a redundant public backbone.
 *
 * This is purpose-built for 24/7 unattended operation on a VPS. Unlike
 * `nexusd` (the multi-role dev daemon), pillard:
 *   - Defaults to role=PILLAR and binds 0.0.0.0:4242
 *   - Disables UDP multicast by default (no LAN around a public host)
 *   - Logs with timestamps to stdout (systemd-journald friendly)
 *   - Handles SIGTERM cleanly, SIGUSR1 dumps stats, SIGHUP re-announces
 *   - Persists identity at a stable path (/var/lib/nexus/pillar.identity)
 *   - Supports optional foreground mode (--foreground) for systemd Type=simple
 *
 * Build:   cmake --build build --target pillard
 * Usage:   pillard [-p PORT] [-i IDENTITY] [-c peer:port]... [-m] [-f]
 * Systemd: see scripts/nexus-pillar.service
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/transport.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PILLARD_DEFAULT_PORT   4242
#define PILLARD_DEFAULT_IDENT  "/var/lib/nexus/pillar.identity"
#define PILLARD_FALLBACK_IDENT_DIR ".nexus"
#define PILLARD_FALLBACK_IDENT "pillar.identity"
#define PILLARD_MAX_PEERS      NX_TCP_INET_MAX_PEERS
#define PILLARD_POLL_MS        200
#define PILLARD_STATS_EVERY_S  300   /* 5 min */
#define PILLARD_REANNOUNCE_S   600   /* 10 min */
#define PILLARD_FED_GOSSIP_S   30    /* digest cadence to peer pillars */

typedef struct {
    uint16_t listen_port;
    char     identity_file[512];
    struct {
        char     host[128];
        uint16_t port;
    } peers[PILLARD_MAX_PEERS];
    int      peer_count;
    int      enable_multicast;   /* off by default on a public VPS */
    int      foreground;         /* systemd Type=simple friendly */
    int      verbose;
    int      vault_mode;         /* use VAULT role (larger mailbox) */
    uint16_t metrics_port;       /* 0 = off */
    uint32_t peer_rate_window_s; /* announces-per-window threshold window */
    uint32_t peer_rate_limit;    /* warn when a peer exceeds this in window */
    char     admin_socket[256];  /* UDS path for admin commands; "" disables */
} pillard_config_t;

/* Per-peer rate-limit tracker. Keyed by 4-byte short address. Not a hard
 * drop (forwarding happens deep in the routing layer) -- logged as a WARN
 * so operators notice noisy peers and can act. */
#define PILLARD_RATE_SLOTS  64
typedef struct {
    uint8_t  addr[NX_SHORT_ADDR_SIZE];
    uint32_t count;
    time_t   window_start;
    int      used;
    int      warned;             /* one WARN per window */
} peer_rate_slot_t;

/* ── Globals ─────────────────────────────────────────────────────────── */

static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_reannounce = 0;
static volatile sig_atomic_t g_dump_stats = 0;
static nx_node_t g_node;
static pillard_config_t g_cfg;

static uint64_t g_msgs_data    = 0;
static uint64_t g_msgs_session = 0;
static uint64_t g_msgs_group   = 0;
static uint64_t g_announces    = 0;
static uint64_t g_rate_warnings = 0;
static uint64_t g_fed_digests_sent = 0;
static uint64_t g_fed_fetches_sent = 0;
static uint64_t g_fed_pkts_served  = 0;
static time_t   g_start_time    = 0;

static peer_rate_slot_t g_rate_slots[PILLARD_RATE_SLOTS];
static pthread_mutex_t  g_rate_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_t g_metrics_thread;
static int       g_metrics_running = 0;
static int       g_metrics_fd = -1;

static pthread_t g_admin_thread;
static int       g_admin_running = 0;
static int       g_admin_fd = -1;
static char      g_admin_bound_path[256] = {0}; /* so we know what to unlink */

/* ── Logging ─────────────────────────────────────────────────────────── */

static void logmsg(const char *level, const char *fmt, ...)
{
    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", &tm);

    fprintf(stdout, "%s %s ", ts, level);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);

    fputc('\n', stdout);
    fflush(stdout);
}

#define LOG_INFO(...)  logmsg("INFO ", __VA_ARGS__)
#define LOG_WARN(...)  logmsg("WARN ", __VA_ARGS__)
#define LOG_ERROR(...) logmsg("ERROR", __VA_ARGS__)
#define LOG_DEBUG(...) do { if (g_cfg.verbose) logmsg("DEBUG", __VA_ARGS__); } while (0)

/* ── Signal handlers ─────────────────────────────────────────────────── */

static void sig_term(int sig)
{
    (void)sig;
    g_running = 0;
}

static void sig_usr1(int sig)
{
    (void)sig;
    g_dump_stats = 1;
}

static void sig_hup(int sig)
{
    (void)sig;
    g_reannounce = 1;
}

/* ── Identity persistence ────────────────────────────────────────────── */

static int ensure_parent_dir(const char *path)
{
    char buf[512];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *slash = strrchr(buf, '/');
    if (!slash) return 0;
    *slash = '\0';
    if (!*buf) return 0;
    /* mkdir -p (one level; /var/lib and /var exist already) */
    if (mkdir(buf, 0700) < 0 && errno != EEXIST) {
        LOG_WARN("mkdir %s: %s", buf, strerror(errno));
        return -1;
    }
    return 0;
}

static int load_identity(const char *path, nx_identity_t *id)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(id, 1, sizeof(*id), f);
    fclose(f);
    return (n == sizeof(*id)) ? 0 : -1;
}

static int save_identity(const char *path, const nx_identity_t *id)
{
    ensure_parent_dir(path);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t n = write(fd, id, sizeof(*id));
    close(fd);
    return (n == (ssize_t)sizeof(*id)) ? 0 : -1;
}

static void resolve_default_identity_path(char *out, size_t out_len)
{
    /* Prefer /var/lib/nexus/pillar.identity when writable. Otherwise fall
     * back to $HOME/.nexus/pillar.identity so non-root dev runs work. */
    if (access("/var/lib/nexus", W_OK) == 0 || access("/var/lib", W_OK) == 0) {
        snprintf(out, out_len, "%s", PILLARD_DEFAULT_IDENT);
        return;
    }
    const char *home = getenv("HOME");
    if (home) {
        snprintf(out, out_len, "%s/%s/%s", home,
                 PILLARD_FALLBACK_IDENT_DIR, PILLARD_FALLBACK_IDENT);
        return;
    }
    snprintf(out, out_len, "/tmp/%s", PILLARD_FALLBACK_IDENT);
}

/* ── Node callbacks ──────────────────────────────────────────────────── */

static void on_data(const nx_addr_short_t *src,
                    const uint8_t *data, size_t len, void *user)
{
    (void)data; (void)user;
    g_msgs_data++;
    LOG_DEBUG("data: from=%02X%02X%02X%02X len=%zu",
              src->bytes[0], src->bytes[1], src->bytes[2], src->bytes[3], len);
}

static void on_session(const nx_addr_short_t *src,
                       const uint8_t *data, size_t len, void *user)
{
    (void)data; (void)user;
    g_msgs_session++;
    LOG_DEBUG("session: from=%02X%02X%02X%02X len=%zu",
              src->bytes[0], src->bytes[1], src->bytes[2], src->bytes[3], len);
}

static void on_group(const nx_addr_short_t *gid,
                     const nx_addr_short_t *src,
                     const uint8_t *data, size_t len, void *user)
{
    (void)data; (void)user;
    g_msgs_group++;
    LOG_DEBUG("group: gid=%02X%02X%02X%02X from=%02X%02X%02X%02X len=%zu",
              gid->bytes[0], gid->bytes[1], gid->bytes[2], gid->bytes[3],
              src->bytes[0], src->bytes[1], src->bytes[2], src->bytes[3], len);
}

/* Peer pillar is telling us what ids it holds. Reply with a FETCH for
 * everything we don't already store. */
static void on_fed_digest(const nx_addr_short_t *src,
                          const uint8_t *ids, int count, void *user)
{
    (void)user;
    if (!ids || count <= 0) return;

    uint8_t wanted[NX_FED_MAX_IDS_PER_PKT][NX_ANCHOR_MSG_ID_SIZE];
    int want = 0;
    for (int i = 0; i < count && want < NX_FED_MAX_IDS_PER_PKT; i++) {
        const uint8_t *id = ids + (size_t)i * NX_ANCHOR_MSG_ID_SIZE;
        if (!nx_anchor_has_id(&g_node.anchor, id)) {
            memcpy(wanted[want++], id, NX_ANCHOR_MSG_ID_SIZE);
        }
    }
    if (want == 0) {
        LOG_DEBUG("fed: digest from %02X%02X%02X%02X (%d ids, all known)",
                  src->bytes[0], src->bytes[1], src->bytes[2], src->bytes[3],
                  count);
        return;
    }
    LOG_DEBUG("fed: digest from %02X%02X%02X%02X (%d ids, %d missing)",
              src->bytes[0], src->bytes[1], src->bytes[2], src->bytes[3],
              count, want);
    if (nx_node_send_federation_fetch(&g_node, src,
                                      (const uint8_t *)wanted, want) == NX_OK) {
        g_fed_fetches_sent++;
    }
}

/* Peer pillar is asking us for packets behind specific msg-ids. Retransmit
 * each one verbatim; peer's node layer will anchor-store via the normal
 * offline-destination forwarding path. */
static void on_fed_fetch(const nx_addr_short_t *src,
                         const uint8_t *ids, int count, void *user)
{
    (void)src; (void)user;
    if (!ids || count <= 0) return;
    int served = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t *id = ids + (size_t)i * NX_ANCHOR_MSG_ID_SIZE;
        const nx_packet_t *p = nx_anchor_find_by_id(&g_node.anchor, id);
        if (p && nx_node_retransmit_packet(&g_node, p) == NX_OK) {
            served++;
        }
    }
    g_fed_pkts_served += (uint64_t)served;
    LOG_DEBUG("fed: fetch from %02X%02X%02X%02X (%d requested, %d served)",
              src->bytes[0], src->bytes[1], src->bytes[2], src->bytes[3],
              count, served);
}

/* Broadcast a DIGEST to every known peer pillar (role >= PILLAR). Called
 * periodically from the main loop. */
static void federation_gossip_tick(void)
{
    if (g_node.anchor.max_slots <= 0) return;

    uint8_t ids[NX_FED_MAX_IDS_PER_PKT][NX_ANCHOR_MSG_ID_SIZE];
    int have = nx_anchor_list_ids(&g_node.anchor, ids, NX_FED_MAX_IDS_PER_PKT);
    if (have <= 0) return;

    const nx_route_table_t *rt = nx_node_route_table(&g_node);
    if (!rt) return;

    int sent = 0;
    for (int i = 0; i < NX_MAX_NEIGHBORS; i++) {
        const nx_neighbor_t *n = &rt->neighbors[i];
        if (!n->valid) continue;
        if (n->role < NX_ROLE_PILLAR) continue;  /* only federate PILLAR/VAULT */
        if (nx_node_send_federation_digest(&g_node, &n->addr,
                                           (const uint8_t *)ids, have) == NX_OK) {
            sent++;
            g_fed_digests_sent++;
        }
    }
    if (sent > 0) {
        LOG_DEBUG("fed: digest -> %d peer pillar(s), %d id(s)", sent, have);
    }
}

/* Returns 1 if this tick pushed the peer over the per-window threshold
 * (first crossing triggers a single WARN; further hits are silent until
 * the window rolls). */
static int rate_record(const uint8_t addr[NX_SHORT_ADDR_SIZE])
{
    if (g_cfg.peer_rate_limit == 0 || g_cfg.peer_rate_window_s == 0) return 0;
    time_t now = time(NULL);
    int crossed = 0;

    pthread_mutex_lock(&g_rate_mutex);

    int free_slot = -1;
    int oldest = -1;
    time_t oldest_ts = now;
    for (int i = 0; i < PILLARD_RATE_SLOTS; i++) {
        if (!g_rate_slots[i].used) { if (free_slot < 0) free_slot = i; continue; }
        if (memcmp(g_rate_slots[i].addr, addr, NX_SHORT_ADDR_SIZE) == 0) {
            if ((uint32_t)(now - g_rate_slots[i].window_start)
                    >= g_cfg.peer_rate_window_s) {
                g_rate_slots[i].window_start = now;
                g_rate_slots[i].count = 0;
                g_rate_slots[i].warned = 0;
            }
            g_rate_slots[i].count++;
            if (!g_rate_slots[i].warned &&
                g_rate_slots[i].count > g_cfg.peer_rate_limit) {
                g_rate_slots[i].warned = 1;
                g_rate_warnings++;
                crossed = 1;
            }
            pthread_mutex_unlock(&g_rate_mutex);
            return crossed;
        }
        if (g_rate_slots[i].window_start < oldest_ts) {
            oldest_ts = g_rate_slots[i].window_start;
            oldest = i;
        }
    }

    int slot = (free_slot >= 0) ? free_slot : (oldest >= 0 ? oldest : 0);
    memcpy(g_rate_slots[slot].addr, addr, NX_SHORT_ADDR_SIZE);
    g_rate_slots[slot].count = 1;
    g_rate_slots[slot].window_start = now;
    g_rate_slots[slot].used = 1;
    g_rate_slots[slot].warned = 0;

    pthread_mutex_unlock(&g_rate_mutex);
    return 0;
}

static void on_neighbor(const nx_addr_short_t *addr, nx_role_t role, void *user)
{
    (void)user;
    g_announces++;
    if (rate_record(addr->bytes)) {
        LOG_WARN("peer %02X%02X%02X%02X exceeded %u announces/%us -- noisy",
                 addr->bytes[0], addr->bytes[1], addr->bytes[2], addr->bytes[3],
                 g_cfg.peer_rate_limit, g_cfg.peer_rate_window_s);
    }
    static const char *role_names[] = {
        "LEAF", "RELAY", "GATEWAY", "ANCHOR", "SENTINEL", "PILLAR", "VAULT"
    };
    const char *rn = ((int)role >= 0 && (int)role <= 6) ? role_names[role] : "?";
    LOG_INFO("peer: %02X%02X%02X%02X role=%s",
             addr->bytes[0], addr->bytes[1], addr->bytes[2], addr->bytes[3], rn);
}

/* ── Metrics HTTP (Prometheus text format) ───────────────────────────── */

static int count_neighbors(void)
{
    const nx_route_table_t *rt = nx_node_route_table(&g_node);
    if (!rt) return 0;
    int n = 0;
    for (int i = 0; i < NX_MAX_NEIGHBORS; i++)
        if (rt->neighbors[i].valid) n++;
    return n;
}

static int count_routes(void)
{
    const nx_route_table_t *rt = nx_node_route_table(&g_node);
    if (!rt) return 0;
    int n = 0;
    for (int i = 0; i < NX_MAX_ROUTES; i++)
        if (rt->routes[i].valid) n++;
    return n;
}

static size_t metrics_render(char *buf, size_t buflen)
{
    uint64_t uptime = (uint64_t)(time(NULL) - g_start_time);
    return (size_t)snprintf(buf, buflen,
        "# HELP pillard_uptime_seconds Process uptime\n"
        "# TYPE pillard_uptime_seconds counter\n"
        "pillard_uptime_seconds %llu\n"
        "# HELP pillard_neighbors Current neighbor count\n"
        "# TYPE pillard_neighbors gauge\n"
        "pillard_neighbors %d\n"
        "# HELP pillard_routes Current route count\n"
        "# TYPE pillard_routes gauge\n"
        "pillard_routes %d\n"
        "# HELP pillard_transports Registered transports\n"
        "# TYPE pillard_transports gauge\n"
        "pillard_transports %d\n"
        "# HELP pillard_rx_total Received application packets\n"
        "# TYPE pillard_rx_total counter\n"
        "pillard_rx_total{kind=\"data\"} %llu\n"
        "pillard_rx_total{kind=\"session\"} %llu\n"
        "pillard_rx_total{kind=\"group\"} %llu\n"
        "# HELP pillard_announces_total Announce callbacks fired\n"
        "# TYPE pillard_announces_total counter\n"
        "pillard_announces_total %llu\n"
        "# HELP pillard_rate_warnings_total Peers that crossed the rate threshold\n"
        "# TYPE pillard_rate_warnings_total counter\n"
        "pillard_rate_warnings_total %llu\n"
        "# HELP pillard_mailbox_depth Stored packets awaiting delivery\n"
        "# TYPE pillard_mailbox_depth gauge\n"
        "pillard_mailbox_depth %d\n"
        "# HELP pillard_fed_digests_sent_total DIGEST exthdrs broadcast to peer pillars\n"
        "# TYPE pillard_fed_digests_sent_total counter\n"
        "pillard_fed_digests_sent_total %llu\n"
        "# HELP pillard_fed_fetches_sent_total FETCH exthdrs sent in reply to DIGESTs\n"
        "# TYPE pillard_fed_fetches_sent_total counter\n"
        "pillard_fed_fetches_sent_total %llu\n"
        "# HELP pillard_fed_packets_served_total Stored packets retransmitted in reply to FETCH\n"
        "# TYPE pillard_fed_packets_served_total counter\n"
        "pillard_fed_packets_served_total %llu\n",
        (unsigned long long)uptime,
        count_neighbors(), count_routes(), nx_transport_count(),
        (unsigned long long)g_msgs_data,
        (unsigned long long)g_msgs_session,
        (unsigned long long)g_msgs_group,
        (unsigned long long)g_announces,
        (unsigned long long)g_rate_warnings,
        nx_anchor_count(&g_node.anchor),
        (unsigned long long)g_fed_digests_sent,
        (unsigned long long)g_fed_fetches_sent,
        (unsigned long long)g_fed_pkts_served);
}

static void metrics_handle_client(int cfd)
{
    char req[512];
    ssize_t n = recv(cfd, req, sizeof(req) - 1, 0);
    if (n <= 0) { close(cfd); return; }
    req[n] = '\0';

    /* Accept only "GET /metrics" (plus "/" returns same). Everything else = 404. */
    int is_metrics = (strncmp(req, "GET /metrics", 12) == 0) ||
                     (strncmp(req, "GET / ", 6) == 0);

    if (!is_metrics) {
        const char *nf = "HTTP/1.0 404 Not Found\r\n"
                         "Content-Length: 0\r\n\r\n";
        (void)send(cfd, nf, strlen(nf), MSG_NOSIGNAL);
        close(cfd);
        return;
    }

    char body[2048];
    size_t blen = metrics_render(body, sizeof(body));
    char hdr[128];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/plain; version=0.0.4\r\n"
        "Content-Length: %zu\r\n\r\n", blen);
    (void)send(cfd, hdr, (size_t)hlen, MSG_NOSIGNAL);
    (void)send(cfd, body, blen, MSG_NOSIGNAL);
    close(cfd);
}

static void *metrics_thread_main(void *arg)
{
    (void)arg;
    while (g_metrics_running && g_metrics_fd >= 0) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int cfd = accept(g_metrics_fd, (struct sockaddr *)&cli, &clen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (!g_metrics_running) break;
            usleep(100000);
            continue;
        }
        metrics_handle_client(cfd);
    }
    return NULL;
}

static int metrics_start(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }
    g_metrics_fd = fd;
    g_metrics_running = 1;
    if (pthread_create(&g_metrics_thread, NULL, metrics_thread_main, NULL) != 0) {
        close(fd);
        g_metrics_fd = -1;
        g_metrics_running = 0;
        return -1;
    }
    return 0;
}

static void metrics_stop(void)
{
    if (!g_metrics_running) return;
    g_metrics_running = 0;
    if (g_metrics_fd >= 0) {
        shutdown(g_metrics_fd, SHUT_RDWR);
        close(g_metrics_fd);
        g_metrics_fd = -1;
    }
    pthread_join(g_metrics_thread, NULL);
}

/* ── Admin UDS ───────────────────────────────────────────────────────────
 *
 * A tiny Unix-domain control socket. Line-based protocol, one command per
 * connection. Intended for local operators and systemd ExecReload (which
 * can just `echo reload | socat - UNIX-CONNECT:/run/nexus/pillard.sock`).
 *
 * Commands:
 *   ping           -> pong
 *   stats          -> logs stats, returns a one-line summary
 *   reload         -> same as SIGHUP (re-announce on all transports)
 *   shutdown|quit  -> graceful exit
 *   help           -> command list
 *
 * Access control is filesystem-based: the socket is created with mode 0660
 * so only processes in the owner+group (typically the systemd unit's user
 * and a nexus-admin group) can open it. No auth beyond that.
 */

static void admin_reply(int cfd, const char *s)
{
    (void)send(cfd, s, strlen(s), MSG_NOSIGNAL);
}

static void admin_handle_client(int cfd)
{
    char buf[128];
    ssize_t n = recv(cfd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(cfd);
        return;
    }
    buf[n] = '\0';
    /* strip trailing newline/CR/whitespace */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' '  || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }

    if (strcmp(buf, "ping") == 0) {
        admin_reply(cfd, "pong\n");
    } else if (strcmp(buf, "reload") == 0) {
        g_reannounce = 1;
        admin_reply(cfd, "ok reload\n");
        LOG_INFO("admin: reload requested");
    } else if (strcmp(buf, "stats") == 0) {
        g_dump_stats = 1;
        char line[160];
        snprintf(line, sizeof(line),
                 "ok stats rx_data=%llu rx_session=%llu rx_group=%llu "
                 "announces=%llu rate_warn=%llu\n",
                 (unsigned long long)g_msgs_data,
                 (unsigned long long)g_msgs_session,
                 (unsigned long long)g_msgs_group,
                 (unsigned long long)g_announces,
                 (unsigned long long)g_rate_warnings);
        admin_reply(cfd, line);
    } else if (strcmp(buf, "shutdown") == 0 || strcmp(buf, "quit") == 0) {
        g_running = 0;
        admin_reply(cfd, "ok shutdown\n");
        LOG_INFO("admin: shutdown requested");
    } else if (strcmp(buf, "help") == 0 || buf[0] == '\0') {
        admin_reply(cfd,
            "commands: ping reload stats shutdown help\n");
    } else {
        admin_reply(cfd, "error unknown-command\n");
    }
    close(cfd);
}

static void *admin_thread_main(void *arg)
{
    (void)arg;
    while (g_admin_running && g_admin_fd >= 0) {
        int cfd = accept(g_admin_fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (!g_admin_running) break;
            usleep(100000);
            continue;
        }
        admin_handle_client(cfd);
    }
    return NULL;
}

static int admin_start(const char *path)
{
    if (!path || !*path) return -1;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(sa.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);

    /* ensure parent dir exists (best effort) */
    ensure_parent_dir(path);

    /* Stale socket from a crashed previous run would refuse to bind;
     * unlink only if it's actually a socket so we don't nuke arbitrary files. */
    struct stat st;
    if (lstat(path, &st) == 0 && S_ISSOCK(st.st_mode)) {
        unlink(path);
    }

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(fd);
        return -1;
    }
    /* 0660: owner + group. Group membership gates operator access. */
    (void)chmod(path, 0660);

    if (listen(fd, 4) < 0) {
        close(fd);
        unlink(path);
        return -1;
    }

    g_admin_fd = fd;
    g_admin_running = 1;
    strncpy(g_admin_bound_path, path, sizeof(g_admin_bound_path) - 1);

    if (pthread_create(&g_admin_thread, NULL, admin_thread_main, NULL) != 0) {
        close(fd);
        unlink(path);
        g_admin_fd = -1;
        g_admin_running = 0;
        g_admin_bound_path[0] = '\0';
        return -1;
    }
    return 0;
}

static void admin_stop(void)
{
    if (!g_admin_running) return;
    g_admin_running = 0;
    if (g_admin_fd >= 0) {
        shutdown(g_admin_fd, SHUT_RDWR);
        close(g_admin_fd);
        g_admin_fd = -1;
    }
    pthread_join(g_admin_thread, NULL);
    if (g_admin_bound_path[0]) {
        unlink(g_admin_bound_path);
        g_admin_bound_path[0] = '\0';
    }
}

/* ── Stats ───────────────────────────────────────────────────────────── */

static void dump_stats(void)
{
    const nx_route_table_t *rt = nx_node_route_table(&g_node);
    int n_neighbors = 0, n_routes = 0;
    if (rt) {
        for (int i = 0; i < NX_MAX_NEIGHBORS; i++)
            if (rt->neighbors[i].valid) n_neighbors++;
        for (int i = 0; i < NX_MAX_ROUTES; i++)
            if (rt->routes[i].valid) n_routes++;
    }

    LOG_INFO("stats neighbors=%d routes=%d transports=%d "
             "rx_data=%llu rx_session=%llu rx_group=%llu announces=%llu "
             "fed_digest=%llu fed_fetch=%llu fed_served=%llu mailbox=%d",
             n_neighbors, n_routes, nx_transport_count(),
             (unsigned long long)g_msgs_data,
             (unsigned long long)g_msgs_session,
             (unsigned long long)g_msgs_group,
             (unsigned long long)g_announces,
             (unsigned long long)g_fed_digests_sent,
             (unsigned long long)g_fed_fetches_sent,
             (unsigned long long)g_fed_pkts_served,
             nx_anchor_count(&g_node.anchor));
}

/* ── Usage ───────────────────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "pillard -- NEXUS Pillar Server (public Internet relay)\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -p PORT        TCP listen port (default: %d)\n"
        "  -i FILE        Identity file (default: %s)\n"
        "  -c HOST:PORT   Peer with another pillar (repeatable, max %d)\n"
        "  -m             Also enable UDP multicast (default: off; LAN-only use)\n"
        "  -V             Run as VAULT (role=6) instead of PILLAR (role=5)\n"
        "  -f             Foreground (don't double-fork); use under systemd\n"
        "  -M PORT        Expose Prometheus /metrics on this TCP port (default: off)\n"
        "  -r N/WINDOW    Warn when a peer exceeds N announces per WINDOW seconds\n"
        "                 (e.g. '-r 60/60' = 60/min). 0 disables (default).\n"
        "  -u PATH        Admin control socket (default: /run/nexus/pillard.sock,\n"
        "                 fallback $HOME/.nexus/pillard.sock). 'off' disables.\n"
        "                 Commands: ping reload stats shutdown (newline terminated).\n"
        "  -v             Verbose debug logging\n"
        "  -h             Show this help\n"
        "\n"
        "Signals:\n"
        "  SIGTERM/SIGINT  Graceful shutdown\n"
        "  SIGHUP          Re-announce on all transports\n"
        "  SIGUSR1         Dump stats to log\n"
        "\n"
        "Examples:\n"
        "  %s                              # listen on 0.0.0.0:%d, auto identity\n"
        "  %s -p 4242 -c peer.example:4242 # pair with another pillar\n"
        "  %s -f                           # foreground for systemd Type=simple\n"
        "\n",
        prog, PILLARD_DEFAULT_PORT, PILLARD_DEFAULT_IDENT, PILLARD_MAX_PEERS,
        prog, PILLARD_DEFAULT_PORT, prog, prog);
}

/* ── Daemonize ───────────────────────────────────────────────────────── */

static int daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);
    if (setsid() < 0) return -1;
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    if (chdir("/") < 0) { /* best effort */ }
    umask(0027);

    /* stdout/stderr go to journald via systemd; when self-daemonized,
     * redirect them to /dev/null to avoid writing to a dead tty. */
    int nullfd = open("/dev/null", O_RDWR);
    if (nullfd >= 0) {
        dup2(nullfd, STDIN_FILENO);
        dup2(nullfd, STDOUT_FILENO);
        dup2(nullfd, STDERR_FILENO);
        if (nullfd > 2) close(nullfd);
    }
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.listen_port = PILLARD_DEFAULT_PORT;
    g_cfg.enable_multicast = 0;
    g_cfg.foreground = 0;
    resolve_default_identity_path(g_cfg.identity_file, sizeof(g_cfg.identity_file));

    /* Default admin socket: /run/nexus/pillard.sock when writable, else
     * $HOME/.nexus/pillard.sock for unprivileged dev runs. */
    if (access("/run/nexus", W_OK) == 0 || access("/run", W_OK) == 0) {
        snprintf(g_cfg.admin_socket, sizeof(g_cfg.admin_socket),
                 "/run/nexus/pillard.sock");
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(g_cfg.admin_socket, sizeof(g_cfg.admin_socket),
                     "%s/.nexus/pillard.sock", home);
        } else {
            snprintf(g_cfg.admin_socket, sizeof(g_cfg.admin_socket),
                     "/tmp/pillard.sock");
        }
    }

    int opt;
    while ((opt = getopt(argc, argv, "p:i:c:mVfM:r:u:vh")) != -1) {
        switch (opt) {
        case 'p':
            g_cfg.listen_port = (uint16_t)atoi(optarg);
            break;
        case 'i':
            strncpy(g_cfg.identity_file, optarg,
                    sizeof(g_cfg.identity_file) - 1);
            g_cfg.identity_file[sizeof(g_cfg.identity_file) - 1] = '\0';
            break;
        case 'c': {
            if (g_cfg.peer_count >= PILLARD_MAX_PEERS) {
                fprintf(stderr, "Too many peers (max %d)\n", PILLARD_MAX_PEERS);
                return 1;
            }
            char *colon = strrchr(optarg, ':');
            if (!colon) {
                fprintf(stderr, "Invalid peer %s (want HOST:PORT)\n", optarg);
                return 1;
            }
            size_t hlen = (size_t)(colon - optarg);
            if (hlen >= sizeof(g_cfg.peers[0].host))
                hlen = sizeof(g_cfg.peers[0].host) - 1;
            memcpy(g_cfg.peers[g_cfg.peer_count].host, optarg, hlen);
            g_cfg.peers[g_cfg.peer_count].host[hlen] = '\0';
            g_cfg.peers[g_cfg.peer_count].port = (uint16_t)atoi(colon + 1);
            g_cfg.peer_count++;
            break;
        }
        case 'm': g_cfg.enable_multicast = 1; break;
        case 'V': g_cfg.vault_mode = 1; break;
        case 'f': g_cfg.foreground = 1; break;
        case 'M': g_cfg.metrics_port = (uint16_t)atoi(optarg); break;
        case 'r': {
            char *slash = strchr(optarg, '/');
            if (!slash) {
                fprintf(stderr, "Invalid -r %s (want N/WINDOW)\n", optarg);
                return 1;
            }
            g_cfg.peer_rate_limit = (uint32_t)atoi(optarg);
            g_cfg.peer_rate_window_s = (uint32_t)atoi(slash + 1);
            break;
        }
        case 'u':
            if (strcmp(optarg, "off") == 0) {
                g_cfg.admin_socket[0] = '\0';
            } else {
                strncpy(g_cfg.admin_socket, optarg,
                        sizeof(g_cfg.admin_socket) - 1);
                g_cfg.admin_socket[sizeof(g_cfg.admin_socket) - 1] = '\0';
            }
            break;
        case 'v': g_cfg.verbose = 1; break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    if (!g_cfg.foreground) {
        if (daemonize() < 0) {
            fprintf(stderr, "daemonize failed: %s\n", strerror(errno));
            return 1;
        }
    }

    /* Install signal handlers. Use sigaction so we can mask re-entries. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sa.sa_handler = sig_usr1; sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = sig_hup;  sigaction(SIGHUP,  &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    LOG_INFO("pillard starting (port=%u peers=%d multicast=%s role=%s)",
             g_cfg.listen_port, g_cfg.peer_count,
             g_cfg.enable_multicast ? "on" : "off",
             g_cfg.vault_mode ? "VAULT" : "PILLAR");

    /* ── Identity ─────────────────────────────────────────────────────── */

    nx_identity_t id;
    int loaded = 0;
    if (load_identity(g_cfg.identity_file, &id) == 0) {
        LOG_INFO("identity loaded from %s", g_cfg.identity_file);
        loaded = 1;
    } else {
        if (nx_identity_generate(&id) != NX_OK) {
            LOG_ERROR("identity generation failed");
            return 1;
        }
        if (save_identity(g_cfg.identity_file, &id) == 0) {
            LOG_INFO("identity generated and saved to %s", g_cfg.identity_file);
        } else {
            LOG_WARN("identity generated but could not save to %s (%s)",
                     g_cfg.identity_file, strerror(errno));
        }
    }
    (void)loaded;

    /* ── Node ─────────────────────────────────────────────────────────── */

    nx_node_config_t ncfg;
    memset(&ncfg, 0, sizeof(ncfg));
    ncfg.role = g_cfg.vault_mode ? NX_ROLE_VAULT : NX_ROLE_PILLAR;
    ncfg.default_ttl = 7;
    ncfg.beacon_interval_ms = 30000;
    ncfg.on_data     = on_data;
    ncfg.on_neighbor = on_neighbor;
    ncfg.on_session  = on_session;
    ncfg.on_group    = on_group;
    ncfg.on_fed_digest = on_fed_digest;
    ncfg.on_fed_fetch  = on_fed_fetch;

    if (nx_node_init_with_identity(&g_node, &ncfg, &id) != NX_OK) {
        LOG_ERROR("node init failed");
        return 1;
    }

    const nx_identity_t *nid = nx_node_identity(&g_node);
    LOG_INFO("node address: %02X%02X%02X%02X",
             nid->short_addr.bytes[0], nid->short_addr.bytes[1],
             nid->short_addr.bytes[2], nid->short_addr.bytes[3]);

    /* ── TCP inet (public listener + optional peer clients) ──────────── */

    nx_tcp_inet_config_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    tcp.listen_host = "0.0.0.0";
    tcp.listen_port = g_cfg.listen_port;
    tcp.reconnect_interval_ms = 5000;
    for (int i = 0; i < g_cfg.peer_count; i++) {
        tcp.peers[i].host = g_cfg.peers[i].host;
        tcp.peers[i].port = g_cfg.peers[i].port;
    }
    tcp.peer_count = g_cfg.peer_count;

    nx_transport_t *tcp_t = nx_tcp_inet_transport_create();
    if (!tcp_t || tcp_t->ops->init(tcp_t, &tcp) != NX_OK) {
        LOG_ERROR("TCP inet init failed on :%u", g_cfg.listen_port);
        return 1;
    }
    nx_transport_set_active(tcp_t, true);
    nx_transport_register(tcp_t);
    LOG_INFO("TCP inet listening :%u (%d outbound peer(s))",
             g_cfg.listen_port, g_cfg.peer_count);

    /* ── Optional UDP multicast ──────────────────────────────────────── */

    if (g_cfg.enable_multicast) {
        nx_udp_mcast_config_t uc;
        memset(&uc, 0, sizeof(uc));
        nx_transport_t *udp_t = nx_udp_mcast_transport_create();
        if (udp_t && udp_t->ops->init(udp_t, &uc) == NX_OK) {
            nx_transport_set_active(udp_t, true);
            nx_transport_register(udp_t);
            LOG_INFO("UDP multicast enabled (LAN discovery)");
        } else {
            LOG_WARN("UDP multicast init failed");
        }
    }

    /* ── Metrics HTTP (optional) ─────────────────────────────────────── */

    if (g_cfg.metrics_port > 0) {
        if (metrics_start(g_cfg.metrics_port) == 0) {
            LOG_INFO("metrics listening on :%u /metrics", g_cfg.metrics_port);
        } else {
            LOG_WARN("metrics bind on :%u failed: %s",
                     g_cfg.metrics_port, strerror(errno));
        }
    }

    if (g_cfg.peer_rate_limit > 0) {
        LOG_INFO("peer rate-warn threshold: %u announces / %us",
                 g_cfg.peer_rate_limit, g_cfg.peer_rate_window_s);
    }

    /* ── Admin UDS (optional) ────────────────────────────────────────── */

    if (g_cfg.admin_socket[0]) {
        if (admin_start(g_cfg.admin_socket) == 0) {
            LOG_INFO("admin socket at %s (commands: ping reload stats shutdown)",
                     g_cfg.admin_socket);
        } else {
            LOG_WARN("admin socket %s bind failed: %s",
                     g_cfg.admin_socket, strerror(errno));
        }
    }

    /* ── Announce + event loop ───────────────────────────────────────── */

    g_start_time = time(NULL);
    nx_node_announce(&g_node);
    LOG_INFO("ready");

    time_t last_stats = time(NULL);
    time_t last_announce = time(NULL);
    time_t last_fed = time(NULL);

    while (g_running) {
        nx_node_poll(&g_node, PILLARD_POLL_MS);

        time_t now = time(NULL);
        if (g_dump_stats) {
            g_dump_stats = 0;
            dump_stats();
        }
        if (g_reannounce || (now - last_announce) >= PILLARD_REANNOUNCE_S) {
            g_reannounce = 0;
            last_announce = now;
            nx_node_announce(&g_node);
            LOG_DEBUG("announced");
        }
        if ((now - last_fed) >= PILLARD_FED_GOSSIP_S) {
            last_fed = now;
            federation_gossip_tick();
        }
        if ((now - last_stats) >= PILLARD_STATS_EVERY_S) {
            last_stats = now;
            dump_stats();
        }
    }

    LOG_INFO("shutting down");
    dump_stats();
    admin_stop();
    metrics_stop();
    nx_node_stop(&g_node);
    LOG_INFO("stopped");
    return 0;
}
