/*
 * NEXUS Protocol -- Transport Abstraction Interface
 */
#ifndef NEXUS_TRANSPORT_H
#define NEXUS_TRANSPORT_H

#include "types.h"

/* Maximum registered transports */
#define NX_MAX_TRANSPORTS  12

/* Transport types */
typedef enum {
    NX_TRANSPORT_SERIAL = 0,
    NX_TRANSPORT_TCP    = 1,
    NX_TRANSPORT_LORA   = 2,
    NX_TRANSPORT_BLE    = 3,
    NX_TRANSPORT_WIFI   = 4,
    NX_TRANSPORT_PIPE   = 5,
    NX_TRANSPORT_UDP    = 6,
} nx_transport_type_t;

/* Forward declaration */
typedef struct nx_transport nx_transport_t;

/* Transport operations vtable */
typedef struct {
    nx_err_t (*init)(nx_transport_t *t, const void *config);
    nx_err_t (*send)(nx_transport_t *t, const uint8_t *data, size_t len);
    nx_err_t (*recv)(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                     size_t *out_len, uint32_t timeout_ms);
    void     (*destroy)(nx_transport_t *t);
} nx_transport_ops_t;

/* Transport instance */
struct nx_transport {
    nx_transport_type_t  type;
    const char          *name;
    const nx_transport_ops_t *ops;
    void                *state;   /* Transport-specific state */
    bool                 active;
    uint8_t              domain_id;  /* 0 = default, >0 = domain tag */
};

/* ── Transport Registry ──────────────────────────────────────────────── */

/* Initialize the transport registry. Call once at startup. */
void nx_transport_registry_init(void);

/* Register a transport. Returns NX_ERR_FULL if registry is full. */
nx_err_t nx_transport_register(nx_transport_t *t);

/* Get number of registered transports. */
int nx_transport_count(void);

/* Get transport by index (0..count-1). Returns NULL if invalid. */
nx_transport_t *nx_transport_get(int index);

/* Send raw bytes on a specific transport. */
nx_err_t nx_transport_send(nx_transport_t *t, const uint8_t *data, size_t len);

/* Receive raw bytes from a specific transport. */
nx_err_t nx_transport_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                           size_t *out_len, uint32_t timeout_ms);

/* Set a transport's active flag. */
void nx_transport_set_active(nx_transport_t *t, bool active);

/* Destroy a transport and free its resources. */
void nx_transport_destroy(nx_transport_t *t);

/* ── Serial Transport ────────────────────────────────────────────────── */

typedef struct {
    const char *device;    /* e.g., "/dev/ttyUSB0" or "/dev/pts/3" */
    uint32_t    baud_rate; /* e.g., 115200 */
} nx_serial_config_t;

/* Create a serial transport instance. Caller must free with nx_transport_destroy. */
nx_transport_t *nx_serial_transport_create(void);

/* ── TCP Transport ───────────────────────────────────────────────────── */

typedef struct {
    const char *host;    /* Bind address (server) or target (client) */
    uint16_t    port;
    bool        server;  /* true = listen, false = connect */
} nx_tcp_config_t;

/* Create a TCP transport instance. Caller must free with nx_transport_destroy. */
nx_transport_t *nx_tcp_transport_create(void);

/* ── TCP Internet Transport (Reticulum-style) ────────────────────────── */

#define NX_TCP_INET_MAX_PEERS 16

typedef struct {
    const char *listen_host;       /* Bind address, NULL = "0.0.0.0" */
    uint16_t    listen_port;       /* Server port, 0 = no server */

    struct {
        const char *host;
        uint16_t    port;
    } peers[NX_TCP_INET_MAX_PEERS]; /* Outbound peers to connect to */
    int peer_count;

    uint32_t    reconnect_interval_ms; /* Auto-reconnect delay (default 5000) */
} nx_tcp_inet_config_t;

/* Create a TCP Internet transport (multi-peer, auto-reconnect). */
nx_transport_t *nx_tcp_inet_transport_create(void);

/* ── LoRa Transport ──────────────────────────────────────────────────── */

/* Create a LoRa transport instance.
 * Config passed to init() must be a pointer to nx_lora_radio_t*.
 * The radio must be initialized before the transport is used. */
nx_transport_t *nx_lora_transport_create(void);

/* ── BLE Transport ───────────────────────────────────────────────────── */

/* Create a BLE transport instance.
 * Config passed to init() must be a pointer to nx_ble_radio_t*.
 * The radio must be initialized before the transport is used. */
nx_transport_t *nx_ble_transport_create(void);

/* ── Pipe Transport ──────────────────────────────────────────────────── */

/* Create a pipe transport instance (in-memory, for testing). */
nx_transport_t *nx_pipe_transport_create(void);

/* Link two pipe transports so A's send goes to B's recv and vice versa. */
void nx_pipe_transport_link(nx_transport_t *a, nx_transport_t *b);

/* ── UDP Multicast Transport (AutoInterface) ─────────────────────────── */

typedef struct {
    const char *group;   /* Multicast group (default "224.0.77.88") */
    uint16_t    port;    /* Port (default 4243) */
} nx_udp_mcast_config_t;

/* Create a UDP multicast transport.
 * Joins multicast on ALL network interfaces for zero-config LAN discovery.
 * Periodically rescans interfaces to pick up new ones (hotplug, VPN, etc). */
nx_transport_t *nx_udp_mcast_transport_create(void);

#endif /* NEXUS_TRANSPORT_H */
