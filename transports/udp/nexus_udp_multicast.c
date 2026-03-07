/*
 * NEXUS Protocol -- UDP Multicast Transport (AutoInterface)
 *
 * Zero-config LAN discovery on ALL network interfaces.
 * Joins a multicast group on every interface and exchanges
 * NEXUS packets directly via UDP -- no connection setup needed.
 *
 * This covers: Ethernet, WiFi, VPN tunnels, bridges, Docker
 * networks, USB Ethernet, and any other IP interface.
 *
 * Multicast group: 224.0.77.88 port 4243 (NEXUS default)
 *
 * Send = multicast to group on all interfaces
 * Recv = receive from any interface
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "nexus/transport.h"
#include "nexus/platform.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>

#define NX_MCAST_GROUP  "224.0.77.88"
#define NX_MCAST_PORT   4243
#define NX_UDP_MAX_IFACES 16

/* ── State ───────────────────────────────────────────────────────────── */

typedef struct {
    int           fd;                             /* UDP socket */
    uint16_t      port;
    char          group[16];
    struct sockaddr_in mcast_addr;                /* Dest for sending */
    struct in_addr iface_addrs[NX_UDP_MAX_IFACES]; /* Local IPs for multicast send */
    int           iface_count;
    uint64_t      last_iface_scan_ms;
} udp_mcast_state_t;

/* ── Enumerate all IPv4 interfaces and join multicast on each ────────── */

static void scan_interfaces(udp_mcast_state_t *s)
{
    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) < 0) return;

    /* Drop all existing memberships first */
    for (int i = 0; i < s->iface_count; i++) {
        struct ip_mreq mreq;
        inet_pton(AF_INET, s->group, &mreq.imr_multiaddr);
        mreq.imr_interface = s->iface_addrs[i];
        setsockopt(s->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
    }
    s->iface_count = 0;

    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        /* Skip loopback */
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        /* Must be up and support multicast */
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (!(ifa->ifa_flags & IFF_MULTICAST)) continue;

        if (s->iface_count >= NX_UDP_MAX_IFACES) break;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        s->iface_addrs[s->iface_count] = sa->sin_addr;

        /* Join multicast group on this interface */
        struct ip_mreq mreq;
        inet_pton(AF_INET, s->group, &mreq.imr_multiaddr);
        mreq.imr_interface = sa->sin_addr;
        setsockopt(s->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

        s->iface_count++;
    }

    freeifaddrs(ifap);
    s->last_iface_scan_ms = nx_platform_time_ms();
}

/* ── Init ────────────────────────────────────────────────────────────── */

static nx_err_t udp_mcast_init(nx_transport_t *t, const void *config)
{
    const nx_udp_mcast_config_t *cfg = (const nx_udp_mcast_config_t *)config;

    udp_mcast_state_t *s = (udp_mcast_state_t *)nx_platform_alloc(sizeof(udp_mcast_state_t));
    if (!s) return NX_ERR_NO_MEMORY;
    memset(s, 0, sizeof(*s));

    s->port = (cfg && cfg->port) ? cfg->port : NX_MCAST_PORT;

    if (cfg && cfg->group && cfg->group[0]) {
        strncpy(s->group, cfg->group, sizeof(s->group) - 1);
    } else {
        strncpy(s->group, NX_MCAST_GROUP, sizeof(s->group) - 1);
    }

    /* Create UDP socket */
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd < 0) goto fail;

    /* Allow multiple processes on same port (co-located nodes) */
    int reuse = 1;
    setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(s->fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    /* Bind to INADDR_ANY to receive multicast on all interfaces */
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(s->port);

    if (bind(s->fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
        goto fail;

    /* Set multicast TTL to 1 (link-local only) */
    int ttl = 1;
    setsockopt(s->fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    /* Disable loopback so we don't receive our own packets */
    int loop = 0;
    setsockopt(s->fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    /* Prepare destination address */
    memset(&s->mcast_addr, 0, sizeof(s->mcast_addr));
    s->mcast_addr.sin_family = AF_INET;
    inet_pton(AF_INET, s->group, &s->mcast_addr.sin_addr);
    s->mcast_addr.sin_port = htons(s->port);

    /* Scan interfaces and join multicast on all of them */
    scan_interfaces(s);

    t->state = s;
    t->active = true;
    return NX_OK;

fail:
    if (s->fd >= 0) close(s->fd);
    nx_platform_free(s);
    return NX_ERR_IO;
}

/* ── Send (multicast on every interface) ─────────────────────────────── */

static nx_err_t udp_mcast_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    udp_mcast_state_t *s = (udp_mcast_state_t *)t->state;
    if (!s) return NX_ERR_TRANSPORT;
    if (len > NX_MAX_PACKET) return NX_ERR_INVALID_ARG;

    /* Rescan interfaces every 30s to pick up new ones */
    uint64_t now = nx_platform_time_ms();
    if (now - s->last_iface_scan_ms > 30000) {
        scan_interfaces(s);
    }

    bool any_sent = false;

    for (int i = 0; i < s->iface_count; i++) {
        /* Set outgoing interface for this send */
        setsockopt(s->fd, IPPROTO_IP, IP_MULTICAST_IF,
                   &s->iface_addrs[i], sizeof(s->iface_addrs[i]));

        ssize_t n = sendto(s->fd, data, len, 0,
                           (struct sockaddr *)&s->mcast_addr,
                           sizeof(s->mcast_addr));
        if (n == (ssize_t)len)
            any_sent = true;
    }

    return any_sent ? NX_OK : NX_ERR_IO;
}

/* ── Recv (from any interface) ───────────────────────────────────────── */

static nx_err_t udp_mcast_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                                size_t *out_len, uint32_t timeout_ms)
{
    udp_mcast_state_t *s = (udp_mcast_state_t *)t->state;
    if (!s) return NX_ERR_TRANSPORT;

    /* Periodic rescan */
    uint64_t now = nx_platform_time_ms();
    if (now - s->last_iface_scan_ms > 30000)
        scan_interfaces(s);

    struct pollfd pfd = { .fd = s->fd, .events = POLLIN };
    int pr = poll(&pfd, 1, (int)timeout_ms);
    if (pr < 0) {
        if (errno == EINTR) return NX_ERR_TIMEOUT;
        return NX_ERR_IO;
    }
    if (pr == 0) return NX_ERR_TIMEOUT;

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(s->fd, buf, buf_len, 0,
                         (struct sockaddr *)&from, &from_len);
    if (n <= 0) return NX_ERR_IO;

    *out_len = (size_t)n;
    return NX_OK;
}

/* ── Destroy ─────────────────────────────────────────────────────────── */

static void udp_mcast_destroy(nx_transport_t *t)
{
    udp_mcast_state_t *s = (udp_mcast_state_t *)t->state;
    if (s) {
        /* Leave all multicast groups */
        for (int i = 0; i < s->iface_count; i++) {
            struct ip_mreq mreq;
            inet_pton(AF_INET, s->group, &mreq.imr_multiaddr);
            mreq.imr_interface = s->iface_addrs[i];
            setsockopt(s->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
        }
        if (s->fd >= 0) close(s->fd);
        nx_platform_free(s);
    }
    t->state = NULL;
    t->active = false;
}

/* ── Vtable & Constructor ────────────────────────────────────────────── */

static const nx_transport_ops_t udp_mcast_ops = {
    .init    = udp_mcast_init,
    .send    = udp_mcast_send,
    .recv    = udp_mcast_recv,
    .destroy = udp_mcast_destroy,
};

nx_transport_t *nx_udp_mcast_transport_create(void)
{
    nx_transport_t *t = (nx_transport_t *)nx_platform_alloc(sizeof(nx_transport_t));
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));
    t->type   = NX_TRANSPORT_UDP;
    t->name   = "udp_mcast";
    t->ops    = &udp_mcast_ops;
    t->active = false;
    return t;
}
