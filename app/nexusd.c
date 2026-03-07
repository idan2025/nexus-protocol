/*
 * NEXUS Protocol -- Linux Daemon (nexusd)
 *
 * Standalone NEXUS node with full transport bridging:
 * - UDP multicast on ALL interfaces (auto-discovery, zero-config)
 * - TCP Internet transport (configured remote peers)
 *
 * Every interface is used: Ethernet, WiFi, VPN, USB, bridges.
 * Packets received on any transport are bridged to all others.
 *
 * Usage:
 *   nexusd [-l port] [-p host:port] [-i identity_file] [-r role] [-nv]
 *
 * Examples:
 *   nexusd                                    # Auto-discover on LAN + listen TCP
 *   nexusd -p server.example.com:4242         # Also connect to remote peer
 *   nexusd -n                                 # Disable multicast (TCP only)
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
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>

/* ── Configuration ───────────────────────────────────────────────────── */

#define MAX_PEERS 16
#define DEFAULT_TCP_PORT 4242
#define DEFAULT_BEACON_MS 30000

typedef struct {
    uint16_t listen_port;
    struct { char host[64]; uint16_t port; } peers[MAX_PEERS];
    int      peer_count;
    char     identity_file[256];
    int      role;
    int      verbose;
    int      no_multicast;
} daemon_config_t;

/* ── Globals ─────────────────────────────────────────────────────────── */

static volatile sig_atomic_t g_running = 1;
static nx_node_t g_node;
static int g_verbose = 0;

/* ── Signal handler ──────────────────────────────────────────────────── */

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ── Identity persistence ────────────────────────────────────────────── */

static int load_identity(const char *path, nx_identity_t *id)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    size_t n = fread(id, 1, sizeof(nx_identity_t), f);
    fclose(f);

    if (n != sizeof(nx_identity_t)) return -1;

    uint8_t zeros[32] = {0};
    if (memcmp(id->sign_public, zeros, 32) == 0) return -1;

    return 0;
}

static int save_identity(const char *path, const nx_identity_t *id)
{
    char dir[256];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0700);
    }

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fchmod(fileno(f), 0600);

    size_t n = fwrite(id, 1, sizeof(nx_identity_t), f);
    fclose(f);

    return (n == sizeof(nx_identity_t)) ? 0 : -1;
}

/* ── Callbacks ───────────────────────────────────────────────────────── */

static void on_data(const nx_addr_short_t *src,
                    const uint8_t *data, size_t len, void *user)
{
    (void)user;
    printf("[DATA] From %02X%02X%02X%02X (%zu bytes)\n",
           src->bytes[0], src->bytes[1],
           src->bytes[2], src->bytes[3], len);

    if (g_verbose && len > 0) {
        printf("  ");
        for (size_t i = 0; i < len && i < 64; i++)
            printf("%02X ", data[i]);
        if (len > 64) printf("...");
        printf("\n");
    }
}

static void on_neighbor(const nx_addr_short_t *addr,
                        nx_role_t role, void *user)
{
    (void)user;
    const char *role_names[] = {"LEAF", "RELAY", "GATEWAY", "ANCHOR", "SENTINEL"};
    const char *rname = (role >= 0 && role <= 4) ? role_names[role] : "?";
    printf("[NEIGHBOR] %02X%02X%02X%02X role=%s\n",
           addr->bytes[0], addr->bytes[1],
           addr->bytes[2], addr->bytes[3], rname);
}

static void on_session(const nx_addr_short_t *src,
                       const uint8_t *data, size_t len, void *user)
{
    (void)user; (void)data;
    printf("[SESSION] From %02X%02X%02X%02X (%zu bytes)\n",
           src->bytes[0], src->bytes[1],
           src->bytes[2], src->bytes[3], len);
}

/* ── Usage ───────────────────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Transports (all enabled by default for maximum discovery):\n"
        "  UDP multicast  Auto-discovers peers on every LAN interface\n"
        "  TCP inet       Persistent connections to configured Internet peers\n"
        "\n"
        "Options:\n"
        "  -l PORT        TCP listen port (default: %d)\n"
        "  -p HOST:PORT   Connect to a remote TCP peer (repeatable, max %d)\n"
        "  -i FILE        Identity file (default: ~/.nexus/identity)\n"
        "  -r ROLE        Node role: 0=leaf 1=relay 2=gateway 3=anchor (default: 2)\n"
        "  -n             Disable UDP multicast (TCP only)\n"
        "  -v             Verbose output\n"
        "  -h             Show this help\n"
        "\n"
        "Examples:\n"
        "  %s                                    Zero-config LAN discovery\n"
        "  %s -p server.example.com:4242          LAN + Internet peer\n"
        "  %s -n -p 10.0.0.1:4242                TCP only, no multicast\n"
        "\n", prog, DEFAULT_TCP_PORT, MAX_PEERS, prog, prog, prog);
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    daemon_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.listen_port = DEFAULT_TCP_PORT;
    cfg.role = NX_ROLE_GATEWAY;  /* Gateway: bridges across transports */

    const char *home = getenv("HOME");
    if (home) {
        snprintf(cfg.identity_file, sizeof(cfg.identity_file),
                 "%s/.nexus/identity", home);
    } else {
        snprintf(cfg.identity_file, sizeof(cfg.identity_file),
                 "/tmp/nexus_identity");
    }

    int opt;
    while ((opt = getopt(argc, argv, "l:p:i:r:nvh")) != -1) {
        switch (opt) {
        case 'l':
            cfg.listen_port = (uint16_t)atoi(optarg);
            break;
        case 'p':
            if (cfg.peer_count >= MAX_PEERS) {
                fprintf(stderr, "Too many peers (max %d)\n", MAX_PEERS);
                return 1;
            }
            {
                char *colon = strrchr(optarg, ':');
                if (!colon) {
                    fprintf(stderr, "Invalid peer: %s (expected host:port)\n", optarg);
                    return 1;
                }
                size_t hlen = (size_t)(colon - optarg);
                if (hlen >= 64) hlen = 63;
                memcpy(cfg.peers[cfg.peer_count].host, optarg, hlen);
                cfg.peers[cfg.peer_count].host[hlen] = '\0';
                cfg.peers[cfg.peer_count].port = (uint16_t)atoi(colon + 1);
                cfg.peer_count++;
            }
            break;
        case 'i':
            strncpy(cfg.identity_file, optarg, sizeof(cfg.identity_file) - 1);
            break;
        case 'r':
            cfg.role = atoi(optarg);
            if (cfg.role < 0 || cfg.role > 4) {
                fprintf(stderr, "Invalid role: %d\n", cfg.role);
                return 1;
            }
            break;
        case 'n':
            cfg.no_multicast = 1;
            break;
        case 'v':
            cfg.verbose = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    g_verbose = cfg.verbose;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    printf("NEXUS Daemon starting...\n");
    nx_transport_registry_init();

    nx_transport_t *udp = NULL;
    nx_transport_t *tcp = NULL;

    /* ── UDP Multicast: auto-discover on ALL interfaces ────────────── */

    if (!cfg.no_multicast) {
        udp = nx_udp_mcast_transport_create();
        if (udp) {
            nx_udp_mcast_config_t udp_cfg = { .group = NULL, .port = 0 };
            nx_err_t err = udp->ops->init(udp, &udp_cfg);
            if (err == NX_OK) {
                nx_transport_register(udp);
                printf("UDP multicast: 224.0.77.88:4243 on all interfaces\n");
            } else {
                fprintf(stderr, "Warning: UDP multicast init failed (%d)\n", err);
                nx_platform_free(udp);
                udp = NULL;
            }
        }
    }

    /* ── TCP Internet: persistent connections to remote peers ──────── */

    tcp = nx_tcp_inet_transport_create();
    if (!tcp) {
        fprintf(stderr, "Failed to create TCP transport\n");
        return 1;
    }

    nx_tcp_inet_config_t tcp_cfg;
    memset(&tcp_cfg, 0, sizeof(tcp_cfg));
    tcp_cfg.listen_port = cfg.listen_port;
    tcp_cfg.listen_host = "0.0.0.0";
    tcp_cfg.reconnect_interval_ms = 5000;

    for (int i = 0; i < cfg.peer_count; i++) {
        tcp_cfg.peers[i].host = cfg.peers[i].host;
        tcp_cfg.peers[i].port = cfg.peers[i].port;
    }
    tcp_cfg.peer_count = cfg.peer_count;

    nx_err_t err = tcp->ops->init(tcp, &tcp_cfg);
    if (err != NX_OK) {
        fprintf(stderr, "TCP init failed: %d\n", err);
        return 1;
    }
    nx_transport_register(tcp);

    printf("TCP inet: listening on :%u, %d outbound peer(s)\n",
           cfg.listen_port, cfg.peer_count);
    for (int i = 0; i < cfg.peer_count; i++)
        printf("  peer: %s:%u\n", cfg.peers[i].host, cfg.peers[i].port);

    printf("Transports: %d active", nx_transport_count());
    if (udp) printf(" [UDP-multicast]");
    printf(" [TCP-inet]\n");

    /* ── Identity ────────────────────────────────────────────────────── */

    nx_identity_t id;
    if (load_identity(cfg.identity_file, &id) == 0) {
        printf("Identity loaded from %s\n", cfg.identity_file);
    } else {
        printf("Generating new identity...\n");
        nx_identity_generate(&id);
        if (save_identity(cfg.identity_file, &id) == 0) {
            printf("Identity saved to %s\n", cfg.identity_file);
        } else {
            fprintf(stderr, "Warning: could not save identity to %s\n",
                    cfg.identity_file);
        }
    }

    /* ── Node ────────────────────────────────────────────────────────── */

    nx_node_config_t node_cfg;
    memset(&node_cfg, 0, sizeof(node_cfg));
    node_cfg.role = (nx_role_t)cfg.role;
    node_cfg.default_ttl = 7;
    node_cfg.beacon_interval_ms = DEFAULT_BEACON_MS;
    node_cfg.on_data = on_data;
    node_cfg.on_neighbor = on_neighbor;
    node_cfg.on_session = on_session;

    err = nx_node_init_with_identity(&g_node, &node_cfg, &id);
    if (err != NX_OK) {
        fprintf(stderr, "Node init failed: %d\n", err);
        return 1;
    }

    const nx_identity_t *nid = nx_node_identity(&g_node);
    const char *role_names[] = {"LEAF", "RELAY", "GATEWAY", "ANCHOR", "SENTINEL"};
    printf("Node: %02X%02X%02X%02X role=%s\n",
           nid->short_addr.bytes[0], nid->short_addr.bytes[1],
           nid->short_addr.bytes[2], nid->short_addr.bytes[3],
           role_names[cfg.role]);

    nx_node_announce(&g_node);
    printf("Ready. Press Ctrl+C to stop.\n");

    /* ── Event loop ──────────────────────────────────────────────────── */

    uint64_t last_status = nx_platform_time_ms();

    while (g_running) {
        nx_node_poll(&g_node, 50);

        uint64_t now = nx_platform_time_ms();
        if (cfg.verbose && now - last_status >= 60000) {
            last_status = now;
            const nx_route_table_t *rt = nx_node_route_table(&g_node);
            int n_neighbors = 0, n_routes = 0;
            for (int i = 0; i < NX_MAX_NEIGHBORS; i++)
                if (rt->neighbors[i].valid) n_neighbors++;
            for (int i = 0; i < NX_MAX_ROUTES; i++)
                if (rt->routes[i].valid) n_routes++;
            printf("[STATUS] neighbors=%d routes=%d transports=%d\n",
                   n_neighbors, n_routes, nx_transport_count());
        }
    }

    printf("\nShutting down...\n");
    nx_node_stop(&g_node);
    nx_transport_destroy(tcp);
    nx_platform_free(tcp);
    if (udp) {
        nx_transport_destroy(udp);
        nx_platform_free(udp);
    }
    printf("Bye.\n");
    return 0;
}
