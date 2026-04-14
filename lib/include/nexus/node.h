/*
 * NEXUS Protocol -- Node Lifecycle & Message Dispatch
 *
 * A node ties together identity, routing, transports, and crypto into
 * a running mesh participant. The main loop polls transports for incoming
 * packets, dispatches them (announce / route / data), and handles
 * periodic tasks (beacons, expiry).
 */
#ifndef NEXUS_NODE_H
#define NEXUS_NODE_H

#include "types.h"
#include "route.h"
#include "fragment.h"
#include "anchor.h"
#include "session.h"
#include "group.h"

/* ── Callback types ──────────────────────────────────────────────────── */

/* Called when a data packet arrives addressed to us. */
typedef void (*nx_on_data_fn)(const nx_addr_short_t *src,
                              const uint8_t *data, size_t len,
                              void *user);

/* Called when a new neighbor is discovered via announcement. */
typedef void (*nx_on_neighbor_fn)(const nx_addr_short_t *addr,
                                  nx_role_t role, void *user);

/* Called when a session-encrypted message is received. */
typedef void (*nx_on_session_fn)(const nx_addr_short_t *src,
                                  const uint8_t *data, size_t len,
                                  void *user);

/* Called when a group message is received. */
typedef void (*nx_on_group_fn)(const nx_addr_short_t *group_id,
                                const nx_addr_short_t *src,
                                const uint8_t *data, size_t len,
                                void *user);

/* ── Node Configuration ──────────────────────────────────────────────── */

typedef struct {
    nx_role_t     role;
    uint8_t       default_ttl;     /* Default TTL for outgoing packets */
    uint32_t      beacon_interval_ms;
    nx_on_data_fn     on_data;
    nx_on_neighbor_fn on_neighbor;
    nx_on_session_fn  on_session;
    nx_on_group_fn    on_group;
    void             *user_ctx;    /* Passed to callbacks */
} nx_node_config_t;

/* ── Node State ──────────────────────────────────────────────────────── */

typedef struct {
    nx_identity_t      identity;
    nx_node_config_t   config;
    nx_route_table_t   route_table;
    nx_frag_buffer_t   frag_buffer;
    nx_anchor_t        anchor;       /* Active only when role >= ANCHOR */
    nx_session_store_t sessions;
    nx_group_store_t   groups;
    uint16_t           next_seq_id;
    bool               running;
} nx_node_t;

/* ── Lifecycle ───────────────────────────────────────────────────────── */

/* Initialize a node with a new random identity. */
nx_err_t nx_node_init(nx_node_t *node, const nx_node_config_t *config);

/* Initialize a node with an existing identity. */
nx_err_t nx_node_init_with_identity(nx_node_t *node,
                                    const nx_node_config_t *config,
                                    const nx_identity_t *id);

/* Shut down a node, wipe keys. */
void nx_node_stop(nx_node_t *node);

/* ── Event Loop ──────────────────────────────────────────────────────── */

/*
 * Run one iteration of the event loop:
 * - Poll all registered transports for incoming packets
 * - Dispatch received packets (announce / route / data)
 * - Send beacons if timer expired
 * - Expire stale routing entries
 *
 * poll_timeout_ms: how long to wait on each transport recv
 * Returns NX_OK, or error if something critical fails.
 */
nx_err_t nx_node_poll(nx_node_t *node, uint32_t poll_timeout_ms);

/* ── Sending ─────────────────────────────────────────────────────────── */

/*
 * Send a data packet to a destination.
 * Encrypts with ephemeral mode using recipient's X25519 pubkey
 * (looked up from neighbor/announce table).
 * Falls back to flood if no route exists.
 */
nx_err_t nx_node_send(nx_node_t *node,
                      const nx_addr_short_t *dest,
                      const uint8_t *data, size_t len);

/*
 * Send raw plaintext (unencrypted) data packet.
 * Used for testing or when caller handles encryption externally.
 */
nx_err_t nx_node_send_raw(nx_node_t *node,
                          const nx_addr_short_t *dest,
                          const uint8_t *data, size_t len);

/*
 * Send a large raw message, auto-fragmenting if needed.
 * Up to NX_FRAG_MAX_MESSAGE bytes (3808).
 */
nx_err_t nx_node_send_large(nx_node_t *node,
                            const nx_addr_short_t *dest,
                            const uint8_t *data, size_t len);

/* Broadcast an announcement on all transports. */
nx_err_t nx_node_announce(nx_node_t *node);

/*
 * Ask a pillar/anchor to replay any stored-and-forward packets it is
 * holding for this node. Sends an EXTHDR_INBOX_REQ packet to [target].
 * Targets with role < RELAY or an empty mailbox will silently ignore it.
 */
nx_err_t nx_node_request_inbox(nx_node_t *node,
                               const nx_addr_short_t *target);

/* ── Session API ─────────────────────────────────────────────────────── */

/* Max plaintext per session message: 242 - 1(type) - 80(overhead) = 161 */
#define NX_SESSION_MAX_PLAINTEXT (NX_MAX_PAYLOAD - 1 - NX_SESSION_OVERHEAD)

/*
 * Start a session with a peer (sends SESSION_INIT).
 * Peer must be in the neighbor table.
 */
nx_err_t nx_node_session_start(nx_node_t *node, const nx_addr_short_t *dest);

/*
 * Send a session-encrypted message to a peer.
 * Session must be established first via session_start + handshake.
 */
nx_err_t nx_node_send_session(nx_node_t *node, const nx_addr_short_t *dest,
                               const uint8_t *data, size_t len);

/* ── Group API ───────────────────────────────────────────────────────── */

/* Create a group on this node. */
nx_err_t nx_node_group_create(nx_node_t *node, const nx_addr_short_t *group_id,
                               const uint8_t group_key[NX_SYMMETRIC_KEY_SIZE]);

/* Add a member to a group. */
nx_err_t nx_node_group_add_member(nx_node_t *node,
                                   const nx_addr_short_t *group_id,
                                   const nx_addr_short_t *member);

/* Send a group-encrypted message (broadcast). */
nx_err_t nx_node_group_send(nx_node_t *node, const nx_addr_short_t *group_id,
                             const uint8_t *data, size_t len);

/* ── Accessors ───────────────────────────────────────────────────────── */

const nx_identity_t    *nx_node_identity(const nx_node_t *node);
const nx_route_table_t *nx_node_route_table(const nx_node_t *node);

#endif /* NEXUS_NODE_H */
