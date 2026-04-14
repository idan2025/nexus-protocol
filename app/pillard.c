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
#include <sys/stat.h>
#include <sys/types.h>

#define PILLARD_DEFAULT_PORT   4242
#define PILLARD_DEFAULT_IDENT  "/var/lib/nexus/pillar.identity"
#define PILLARD_FALLBACK_IDENT_DIR ".nexus"
#define PILLARD_FALLBACK_IDENT "pillar.identity"
#define PILLARD_MAX_PEERS      NX_TCP_INET_MAX_PEERS
#define PILLARD_POLL_MS        200
#define PILLARD_STATS_EVERY_S  300   /* 5 min */
#define PILLARD_REANNOUNCE_S   600   /* 10 min */

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
} pillard_config_t;

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

static void on_neighbor(const nx_addr_short_t *addr, nx_role_t role, void *user)
{
    (void)user;
    g_announces++;
    static const char *role_names[] = {
        "LEAF", "RELAY", "GATEWAY", "ANCHOR", "SENTINEL", "PILLAR", "VAULT"
    };
    const char *rn = ((int)role >= 0 && (int)role <= 6) ? role_names[role] : "?";
    LOG_INFO("peer: %02X%02X%02X%02X role=%s",
             addr->bytes[0], addr->bytes[1], addr->bytes[2], addr->bytes[3], rn);
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
             "rx_data=%llu rx_session=%llu rx_group=%llu announces=%llu",
             n_neighbors, n_routes, nx_transport_count(),
             (unsigned long long)g_msgs_data,
             (unsigned long long)g_msgs_session,
             (unsigned long long)g_msgs_group,
             (unsigned long long)g_announces);
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

    int opt;
    while ((opt = getopt(argc, argv, "p:i:c:mVfvh")) != -1) {
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

    /* ── Announce + event loop ───────────────────────────────────────── */

    nx_node_announce(&g_node);
    LOG_INFO("ready");

    time_t last_stats = time(NULL);
    time_t last_announce = time(NULL);

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
        if ((now - last_stats) >= PILLARD_STATS_EVERY_S) {
            last_stats = now;
            dump_stats();
        }
    }

    LOG_INFO("shutting down");
    dump_stats();
    nx_node_stop(&g_node);
    LOG_INFO("stopped");
    return 0;
}
