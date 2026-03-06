/*
 * NEXUS Protocol -- Node Lifecycle & Message Dispatch
 */
#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/packet.h"
#include "nexus/crypto.h"
#include "nexus/announce.h"
#include "nexus/fragment.h"
#include "nexus/anchor.h"
#include "nexus/session.h"
#include "nexus/group.h"
#include "nexus/transport.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"

#include <string.h>

/* ── Lifecycle ───────────────────────────────────────────────────────── */

nx_err_t nx_node_init(nx_node_t *node, const nx_node_config_t *config)
{
    if (!node || !config) return NX_ERR_INVALID_ARG;

    nx_identity_t id;
    nx_err_t err = nx_identity_generate(&id);
    if (err != NX_OK) return err;

    return nx_node_init_with_identity(node, config, &id);
}

nx_err_t nx_node_init_with_identity(nx_node_t *node,
                                    const nx_node_config_t *config,
                                    const nx_identity_t *id)
{
    if (!node || !config || !id) return NX_ERR_INVALID_ARG;

    memset(node, 0, sizeof(*node));
    node->identity = *id;
    node->config   = *config;

    if (node->config.default_ttl == 0)
        node->config.default_ttl = 7;
    if (node->config.beacon_interval_ms == 0)
        node->config.beacon_interval_ms = NX_BEACON_INTERVAL_MS;

    nx_route_init(&node->route_table);
    nx_frag_init(&node->frag_buffer);
    nx_anchor_init(&node->anchor);
    nx_session_store_init(&node->sessions);
    nx_group_store_init(&node->groups);
    node->next_seq_id = 0;
    node->running = true;

    return NX_OK;
}

void nx_node_stop(nx_node_t *node)
{
    if (!node) return;
    node->running = false;
    /* Wipe all session keys */
    for (int i = 0; i < NX_SESSION_MAX; i++) {
        if (node->sessions.sessions[i].valid) {
            crypto_wipe(&node->sessions.sessions[i], sizeof(nx_session_t));
        }
    }
    /* Wipe all group keys */
    for (int i = 0; i < NX_GROUP_MAX; i++) {
        if (node->groups.groups[i].valid) {
            crypto_wipe(&node->groups.groups[i], sizeof(nx_group_t));
        }
    }
    nx_identity_wipe(&node->identity);
}

/* ── Internal: transmit packet on all active transports ──────────────── */

static nx_err_t transmit_all(const nx_packet_t *pkt)
{
    uint8_t wire[NX_MAX_PACKET];
    int n = nx_packet_serialize(pkt, wire, sizeof(wire));
    if (n < 0) return (nx_err_t)n;

    int count = nx_transport_count();
    nx_err_t last_err = NX_OK;
    bool any_sent = false;

    for (int i = 0; i < count; i++) {
        nx_transport_t *t = nx_transport_get(i);
        if (t && t->active) {
            nx_err_t err = nx_transport_send(t, wire, (size_t)n);
            if (err == NX_OK) {
                any_sent = true;
            } else {
                last_err = err;
            }
        }
    }

    return any_sent ? NX_OK : last_err;
}

/* ── Internal: bridge packet to all transports except ingress ────────── */

static nx_err_t transmit_bridge(const nx_packet_t *pkt, int exclude_idx)
{
    uint8_t wire[NX_MAX_PACKET];
    int n = nx_packet_serialize(pkt, wire, sizeof(wire));
    if (n < 0) return (nx_err_t)n;

    int count = nx_transport_count();
    nx_err_t last_err = NX_OK;
    bool any_sent = false;

    for (int i = 0; i < count; i++) {
        if (i == exclude_idx) continue;  /* Skip ingress transport */
        nx_transport_t *t = nx_transport_get(i);
        if (t && t->active) {
            nx_err_t err = nx_transport_send(t, wire, (size_t)n);
            if (err == NX_OK) {
                any_sent = true;
            } else {
                last_err = err;
            }
        }
    }

    return any_sent ? NX_OK : last_err;
}

/* ── Internal: handle an incoming announcement ───────────────────────── */

static void handle_announce(nx_node_t *node, const nx_packet_t *pkt)
{
    nx_announce_t ann;
    if (nx_announce_parse(pkt->payload, pkt->header.payload_len, &ann) != NX_OK)
        return;

    /* Don't add ourselves */
    if (nx_addr_short_cmp(&ann.short_addr, &node->identity.short_addr) == 0)
        return;

    uint64_t now = nx_platform_time_ms();
    nx_neighbor_update(&node->route_table,
                       &ann.short_addr, &ann.full_addr,
                       ann.sign_pubkey, ann.x25519_pubkey,
                       ann.role, 0, now);

    /* Also install a direct route to this neighbor */
    nx_route_update(&node->route_table,
                    &ann.short_addr, &ann.short_addr,
                    1, 1, now);

    if (node->config.on_neighbor) {
        node->config.on_neighbor(&ann.short_addr, ann.role,
                                 node->config.user_ctx);
    }

    /* ANCHOR: deliver any stored messages for this neighbor */
    if (node->config.role >= NX_ROLE_ANCHOR) {
        nx_packet_t stored[NX_ANCHOR_MAX_PER_DEST];
        int n_stored = nx_anchor_retrieve(&node->anchor, &ann.short_addr,
                                          stored, NX_ANCHOR_MAX_PER_DEST);
        for (int i = 0; i < n_stored; i++) {
            (void)transmit_all(&stored[i]);
        }
    }
}

/* ── Internal: handle a routing control packet ───────────────────────── */

static void handle_route(nx_node_t *node, const nx_packet_t *pkt)
{
    nx_route_subtype_t sub;
    uint64_t now = nx_platform_time_ms();

    nx_route_process(&node->route_table, &pkt->header.src,
                     pkt->payload, pkt->header.payload_len,
                     &sub, now);

    /* If this is an RREQ for us, generate RREP */
    if (sub == NX_ROUTE_SUB_RREQ && pkt->header.payload_len >= NX_RREQ_PAYLOAD_LEN) {
        nx_addr_short_t rreq_dest;
        memcpy(rreq_dest.bytes, &pkt->payload[7], NX_SHORT_ADDR_SIZE);

        if (nx_addr_short_cmp(&rreq_dest, &node->identity.short_addr) == 0) {
            /* We are the target -- send RREP back */
            uint16_t rreq_id = (uint16_t)((uint16_t)pkt->payload[1] << 8 |
                                           (uint16_t)pkt->payload[2]);
            nx_addr_short_t origin;
            memcpy(origin.bytes, &pkt->payload[3], NX_SHORT_ADDR_SIZE);

            nx_packet_t rrep_pkt;
            memset(&rrep_pkt, 0, sizeof(rrep_pkt));
            rrep_pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_HIGH,
                                                    NX_PTYPE_ROUTE, NX_RTYPE_ROUTED);
            rrep_pkt.header.ttl = node->config.default_ttl;
            rrep_pkt.header.dst = origin;
            rrep_pkt.header.src = node->identity.short_addr;
            rrep_pkt.header.seq_id = node->next_seq_id++;

            int rlen = nx_route_build_rrep(rreq_id, &origin,
                                           &node->identity.short_addr,
                                           1, 1,
                                           rrep_pkt.payload,
                                           sizeof(rrep_pkt.payload));
            if (rlen > 0 && rlen <= NX_MAX_PAYLOAD) {
                rrep_pkt.header.payload_len = (uint8_t)rlen;
                transmit_all(&rrep_pkt);
            }
        }
    }
}

/* ── Internal: deliver data to application ───────────────────────────── */

static void deliver_data(nx_node_t *node, const nx_addr_short_t *src,
                         const uint8_t *data, size_t len)
{
    if (node->config.on_data) {
        node->config.on_data(src, data, len, node->config.user_ctx);
    }
}

/* ── Internal: session handlers ──────────────────────────────────────── */

static void handle_session_init(nx_node_t *node, const nx_packet_t *pkt)
{
    /* Payload: [type(1)][eph_pub(32)] */
    if (pkt->header.payload_len < 1 + NX_SESSION_INIT_LEN) return;

    /* Look up sender's X25519 pubkey from neighbor table */
    const nx_neighbor_t *nb = nx_neighbor_find(&node->route_table,
                                                &pkt->header.src);
    if (!nb) return; /* Unknown peer */

    /* Allocate session slot */
    nx_session_t *s = nx_session_alloc(&node->sessions, &pkt->header.src);
    if (!s) return; /* Full */

    /* Accept the session */
    uint8_t ack_payload[NX_SESSION_ACK_LEN];
    nx_err_t err = nx_session_accept(s,
                                      node->identity.x25519_secret,
                                      node->identity.x25519_public,
                                      nb->x25519_pubkey,
                                      pkt->payload + 1, /* skip type byte */
                                      NX_SESSION_INIT_LEN,
                                      ack_payload, sizeof(ack_payload));
    if (err != NX_OK) {
        nx_session_remove(&node->sessions, &pkt->header.src);
        return;
    }

    /* Build SESSION_ACK packet: PTYPE_DATA + EXTHDR, payload = [0x11][eph_pub(32)] */
    nx_packet_t ack_pkt;
    memset(&ack_pkt, 0, sizeof(ack_pkt));
    ack_pkt.header.flags = nx_packet_flags(false, true, NX_PRIO_NORMAL,
                                            NX_PTYPE_DATA, NX_RTYPE_DIRECT);
    ack_pkt.header.ttl = node->config.default_ttl;
    ack_pkt.header.dst = pkt->header.src;
    ack_pkt.header.src = node->identity.short_addr;
    ack_pkt.header.seq_id = node->next_seq_id++;
    ack_pkt.payload[0] = NX_SESSION_SUB_ACK;
    memcpy(ack_pkt.payload + 1, ack_payload, NX_SESSION_ACK_LEN);
    ack_pkt.header.payload_len = 1 + NX_SESSION_ACK_LEN;

    transmit_all(&ack_pkt);
}

static void handle_session_ack(nx_node_t *node, const nx_packet_t *pkt)
{
    /* Payload: [type(1)][eph_pub(32)] */
    if (pkt->header.payload_len < 1 + NX_SESSION_ACK_LEN) return;

    /* Find existing session for sender */
    nx_session_t *s = nx_session_find(&node->sessions, &pkt->header.src);
    if (!s) return;

    /* Complete the handshake */
    nx_session_complete(s, pkt->payload + 1, NX_SESSION_ACK_LEN);
}

static void handle_session_msg(nx_node_t *node, const nx_packet_t *pkt)
{
    /* Payload: [type(1)][session_ciphertext] */
    if (pkt->header.payload_len < 1 + NX_SESSION_OVERHEAD) return;

    /* Find established session for sender */
    nx_session_t *s = nx_session_find(&node->sessions, &pkt->header.src);
    if (!s || !s->established) return;

    /* Decrypt */
    size_t ct_len = pkt->header.payload_len - 1; /* skip type byte */
    uint8_t plaintext[NX_MAX_PAYLOAD];
    size_t plaintext_len = 0;

    nx_err_t err = nx_session_decrypt(s, pkt->payload + 1, ct_len,
                                       plaintext, sizeof(plaintext),
                                       &plaintext_len);
    if (err != NX_OK) return;

    /* Deliver via callback */
    if (node->config.on_session) {
        node->config.on_session(&pkt->header.src, plaintext, plaintext_len,
                                 node->config.user_ctx);
    }
}

/* ── Internal: handle a group message ────────────────────────────────── */

static void handle_group_msg(nx_node_t *node, const nx_packet_t *pkt)
{
    /* Payload: [type(1)][group_id(4)][msg_num(4)][nonce(24)][MAC(16)][ct] */
    if (pkt->header.payload_len < 1 + NX_GROUP_OVERHEAD) return;

    /* Extract group_id from payload+1 */
    nx_addr_short_t group_id;
    memcpy(group_id.bytes, pkt->payload + 1, NX_SHORT_ADDR_SIZE);

    nx_group_t *g = nx_group_find(&node->groups, &group_id);
    if (!g) return; /* Not a member */

    /* Decrypt: pass payload+1 (skip type byte) */
    size_t ct_len = pkt->header.payload_len - 1;
    uint8_t plaintext[NX_MAX_PAYLOAD];
    size_t plaintext_len = 0;

    nx_err_t err = nx_group_decrypt(g, &pkt->header.src,
                                     pkt->payload + 1, ct_len,
                                     plaintext, sizeof(plaintext),
                                     &plaintext_len);
    if (err != NX_OK) return;

    /* Deliver via group callback */
    if (node->config.on_group) {
        node->config.on_group(&group_id, &pkt->header.src,
                               plaintext, plaintext_len,
                               node->config.user_ctx);
    }
}

/* ── Internal: handle a data packet ──────────────────────────────────── */

static void handle_data(nx_node_t *node, const nx_packet_t *pkt,
                         int ingress_transport)
{
    /* Is it for us? */
    bool for_us = (nx_addr_short_cmp(&pkt->header.dst,
                                     &node->identity.short_addr) == 0);

    /* Broadcast? */
    nx_addr_short_t bcast = NX_ADDR_BROADCAST_SHORT;
    bool is_broadcast = (nx_addr_short_cmp(&pkt->header.dst, &bcast) == 0);

    if (for_us || is_broadcast) {
        bool has_exthdr = nx_packet_flag_exthdr(pkt->header.flags);

        if (has_exthdr) {
            if (pkt->header.payload_len < 1) return;
            uint8_t type = pkt->payload[0];
            switch (type) {
            case NX_EXTHDR_FRAGMENT: {
                /* Fragmented packet -- reassemble */
                uint8_t reassembled[NX_FRAG_MAX_MESSAGE];
                size_t  reassembled_len = 0;
                uint64_t now = nx_platform_time_ms();

                nx_err_t err = nx_frag_receive(&node->frag_buffer, pkt,
                                               reassembled, sizeof(reassembled),
                                               &reassembled_len, now);
                if (err == NX_OK && reassembled_len > 0) {
                    deliver_data(node, &pkt->header.src,
                                 reassembled, reassembled_len);
                }
                break;
            }
            case NX_SESSION_SUB_INIT:
                handle_session_init(node, pkt);
                break;
            case NX_SESSION_SUB_ACK:
                handle_session_ack(node, pkt);
                break;
            case NX_SESSION_SUB_MSG:
                handle_session_msg(node, pkt);
                break;
            case NX_EXTHDR_GROUP_MSG:
                handle_group_msg(node, pkt);
                break;
            }
        } else {
            deliver_data(node, &pkt->header.src,
                         pkt->payload, pkt->header.payload_len);
        }
    }

    /* Forward if we're a relay-capable node and TTL allows */
    if (!for_us && !is_broadcast &&
        node->config.role >= NX_ROLE_RELAY &&
        pkt->header.ttl > 0) {

        /* Look up route */
        const nx_route_t *route = nx_route_lookup(&node->route_table,
                                                  &pkt->header.dst);

        nx_packet_t fwd = *pkt;
        fwd.header.hop_count++;
        fwd.header.ttl--;

        if (route || nx_packet_flag_rtype(pkt->header.flags) == NX_RTYPE_FLOOD
            || nx_packet_flag_rtype(pkt->header.flags) == NX_RTYPE_DOMAIN) {
            /* GATEWAY: bridge cross-transport (exclude ingress) */
            if (node->config.role >= NX_ROLE_GATEWAY) {
                (void)transmit_bridge(&fwd, ingress_transport);
            } else {
                (void)transmit_all(&fwd);
            }
        } else if (node->config.role >= NX_ROLE_ANCHOR) {
            /* ANCHOR: store for offline destination */
            (void)nx_anchor_store(&node->anchor, pkt, nx_platform_time_ms());
        }
    }
}

/* ── Internal: dispatch a received packet ────────────────────────────── */

static void dispatch_packet(nx_node_t *node, const nx_packet_t *pkt,
                             int ingress_transport)
{
    /* Dedup check */
    uint64_t now = nx_platform_time_ms();
    if (nx_dedup_check(&node->route_table, &pkt->header.src,
                       pkt->header.seq_id, now)) {
        return; /* Already seen */
    }

    nx_ptype_t ptype = nx_packet_flag_ptype(pkt->header.flags);

    switch (ptype) {
    case NX_PTYPE_ANNOUNCE:
        handle_announce(node, pkt);
        break;
    case NX_PTYPE_ROUTE:
        handle_route(node, pkt);
        break;
    case NX_PTYPE_DATA:
        handle_data(node, pkt, ingress_transport);
        break;
    case NX_PTYPE_ACK:
        /* ACK handling -- Phase 3+ */
        break;
    }

    /* If this is a flooded packet and we relay, forward with decremented TTL */
    if (ptype == NX_PTYPE_ANNOUNCE || ptype == NX_PTYPE_ROUTE) {
        if (node->config.role >= NX_ROLE_RELAY &&
            nx_packet_flag_rtype(pkt->header.flags) == NX_RTYPE_FLOOD &&
            pkt->header.ttl > 0) {
            nx_packet_t fwd = *pkt;
            fwd.header.hop_count++;
            fwd.header.ttl--;
            /* GATEWAY: bridge cross-transport for flood forwarding too */
            if (node->config.role >= NX_ROLE_GATEWAY) {
                (void)transmit_bridge(&fwd, ingress_transport);
            } else {
                (void)transmit_all(&fwd);
            }
        }
    }
}

/* ── Event Loop ──────────────────────────────────────────────────────── */

nx_err_t nx_node_poll(nx_node_t *node, uint32_t poll_timeout_ms)
{
    if (!node || !node->running) return NX_ERR_INVALID_ARG;

    uint64_t now = nx_platform_time_ms();

    /* Poll all transports for incoming packets */
    int count = nx_transport_count();
    for (int i = 0; i < count; i++) {
        nx_transport_t *t = nx_transport_get(i);
        if (!t || !t->active) continue;

        uint8_t wire[NX_MAX_PACKET];
        size_t wire_len = 0;

        nx_err_t err = nx_transport_recv(t, wire, sizeof(wire), &wire_len,
                                         poll_timeout_ms);
        if (err == NX_OK && wire_len >= NX_HEADER_SIZE) {
            nx_packet_t pkt;
            if (nx_packet_deserialize(wire, wire_len, &pkt) == NX_OK) {
                dispatch_packet(node, &pkt, i);
            }
        }
    }

    /* Periodic beacon */
    if (now - node->route_table.last_beacon_ms >=
        node->config.beacon_interval_ms) {
        nx_node_announce(node);
        node->route_table.last_beacon_ms = now;
    }

    /* Expire stale entries */
    nx_route_expire(&node->route_table, now);
    nx_frag_expire(&node->frag_buffer, now);
    if (node->config.role >= NX_ROLE_ANCHOR)
        nx_anchor_expire(&node->anchor, now);

    return NX_OK;
}

/* ── Sending ─────────────────────────────────────────────────────────── */

nx_err_t nx_node_send(nx_node_t *node,
                      const nx_addr_short_t *dest,
                      const uint8_t *data, size_t len)
{
    if (!node || !dest || !data || len == 0) return NX_ERR_INVALID_ARG;

    /* Look up recipient's X25519 pubkey from neighbor table */
    const nx_neighbor_t *nb = nx_neighbor_find(&node->route_table, dest);
    if (!nb) {
        /* No known pubkey -- can't encrypt. Try route discovery first. */
        /* For now, check routes and send raw if we have a route but no key */
        return NX_ERR_NOT_FOUND;
    }

    /* Build packet header */
    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    const nx_route_t *route = nx_route_lookup(&node->route_table, dest);
    nx_rtype_t rtype = route ? NX_RTYPE_ROUTED : NX_RTYPE_DIRECT;

    pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                       NX_PTYPE_DATA, rtype);
    pkt.header.hop_count = 0;
    pkt.header.ttl = node->config.default_ttl;
    pkt.header.dst = *dest;
    pkt.header.src = node->identity.short_addr;
    pkt.header.seq_id = node->next_seq_id++;

    /* Serialize header as AD for AEAD */
    uint8_t ad[NX_HEADER_SIZE];
    nx_packet_serialize(&pkt, ad, sizeof(ad));
    /* AD is just the header portion */

    /* Ephemeral encrypt */
    size_t enc_len = 0;
    nx_err_t err = nx_crypto_ephemeral_encrypt(
        nb->x25519_pubkey, ad, NX_HEADER_SIZE,
        data, len,
        pkt.payload, sizeof(pkt.payload), &enc_len);
    if (err != NX_OK) return err;
    if (enc_len > 255) return NX_ERR_BUFFER_TOO_SMALL;

    pkt.header.payload_len = (uint8_t)enc_len;

    return transmit_all(&pkt);
}

nx_err_t nx_node_send_raw(nx_node_t *node,
                          const nx_addr_short_t *dest,
                          const uint8_t *data, size_t len)
{
    if (!node || !dest || !data) return NX_ERR_INVALID_ARG;
    if (len > NX_MAX_PAYLOAD) return NX_ERR_BUFFER_TOO_SMALL;

    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    const nx_route_t *route = nx_route_lookup(&node->route_table, dest);
    nx_rtype_t rtype = route ? NX_RTYPE_ROUTED : NX_RTYPE_FLOOD;

    pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                       NX_PTYPE_DATA, rtype);
    pkt.header.hop_count = 0;
    pkt.header.ttl = node->config.default_ttl;
    pkt.header.dst = *dest;
    pkt.header.src = node->identity.short_addr;
    pkt.header.seq_id = node->next_seq_id++;
    pkt.header.payload_len = (uint8_t)len;

    memcpy(pkt.payload, data, len);

    return transmit_all(&pkt);
}

nx_err_t nx_node_send_large(nx_node_t *node,
                            const nx_addr_short_t *dest,
                            const uint8_t *data, size_t len)
{
    if (!node || !dest || !data) return NX_ERR_INVALID_ARG;
    if (len == 0) return NX_ERR_INVALID_ARG;

    /* Small enough for a single packet? */
    if (len <= NX_MAX_PAYLOAD)
        return nx_node_send_raw(node, dest, data, len);

    if (len > NX_FRAG_MAX_MESSAGE) return NX_ERR_BUFFER_TOO_SMALL;

    /* Build base header */
    nx_header_t base;
    memset(&base, 0, sizeof(base));

    const nx_route_t *route = nx_route_lookup(&node->route_table, dest);
    nx_rtype_t rtype = route ? NX_RTYPE_ROUTED : NX_RTYPE_FLOOD;

    base.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                 NX_PTYPE_DATA, rtype);
    base.hop_count = 0;
    base.ttl = node->config.default_ttl;
    base.dst = *dest;
    base.src = node->identity.short_addr;
    base.seq_id = node->next_seq_id++;

    /* Fragment */
    nx_packet_t frags[NX_FRAG_MAX_COUNT];
    int frag_count = 0;
    nx_err_t err = nx_frag_split(&node->frag_buffer, &base, data, len,
                                 frags, &frag_count);
    if (err != NX_OK) return err;

    /* Transmit each fragment */
    for (int i = 0; i < frag_count; i++) {
        err = transmit_all(&frags[i]);
        if (err != NX_OK) return err;
    }

    return NX_OK;
}

nx_err_t nx_node_announce(nx_node_t *node)
{
    if (!node) return NX_ERR_INVALID_ARG;

    nx_packet_t pkt;
    nx_err_t err = nx_announce_build_packet(&node->identity,
                                            node->config.role,
                                            node->config.default_ttl,
                                            &pkt);
    if (err != NX_OK) return err;

    pkt.header.seq_id = node->next_seq_id++;
    return transmit_all(&pkt);
}

/* ── Session Sending ─────────────────────────────────────────────────── */

nx_err_t nx_node_session_start(nx_node_t *node, const nx_addr_short_t *dest)
{
    if (!node || !dest) return NX_ERR_INVALID_ARG;

    /* Look up peer's X25519 pubkey from neighbor table */
    const nx_neighbor_t *nb = nx_neighbor_find(&node->route_table, dest);
    if (!nb) return NX_ERR_NOT_FOUND;

    /* Allocate session slot */
    nx_session_t *s = nx_session_alloc(&node->sessions, dest);
    if (!s) return NX_ERR_FULL;

    /* Initiate session */
    uint8_t init_payload[NX_SESSION_INIT_LEN];
    nx_err_t err = nx_session_initiate(s,
                                        node->identity.x25519_secret,
                                        node->identity.x25519_public,
                                        nb->x25519_pubkey,
                                        init_payload, sizeof(init_payload));
    if (err != NX_OK) {
        nx_session_remove(&node->sessions, dest);
        return err;
    }

    /* Build SESSION_INIT packet: PTYPE_DATA + EXTHDR, payload = [0x10][eph_pub(32)] */
    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.flags = nx_packet_flags(false, true, NX_PRIO_NORMAL,
                                        NX_PTYPE_DATA, NX_RTYPE_DIRECT);
    pkt.header.ttl = node->config.default_ttl;
    pkt.header.dst = *dest;
    pkt.header.src = node->identity.short_addr;
    pkt.header.seq_id = node->next_seq_id++;
    pkt.payload[0] = NX_SESSION_SUB_INIT;
    memcpy(pkt.payload + 1, init_payload, NX_SESSION_INIT_LEN);
    pkt.header.payload_len = 1 + NX_SESSION_INIT_LEN;

    return transmit_all(&pkt);
}

nx_err_t nx_node_send_session(nx_node_t *node, const nx_addr_short_t *dest,
                               const uint8_t *data, size_t len)
{
    if (!node || !dest || !data) return NX_ERR_INVALID_ARG;
    if (len > NX_SESSION_MAX_PLAINTEXT) return NX_ERR_BUFFER_TOO_SMALL;

    /* Find established session for dest */
    nx_session_t *s = nx_session_find(&node->sessions, dest);
    if (!s || !s->established) return NX_ERR_NOT_FOUND;

    /* Encrypt */
    uint8_t session_ct[NX_MAX_PAYLOAD];
    size_t session_ct_len = 0;
    nx_err_t err = nx_session_encrypt(s, data, len,
                                       session_ct, sizeof(session_ct),
                                       &session_ct_len);
    if (err != NX_OK) return err;

    /* Build packet: PTYPE_DATA + EXTHDR, payload = [0x12][session_ciphertext] */
    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    const nx_route_t *route = nx_route_lookup(&node->route_table, dest);
    nx_rtype_t rtype = route ? NX_RTYPE_ROUTED : NX_RTYPE_DIRECT;

    pkt.header.flags = nx_packet_flags(false, true, NX_PRIO_NORMAL,
                                        NX_PTYPE_DATA, rtype);
    pkt.header.ttl = node->config.default_ttl;
    pkt.header.dst = *dest;
    pkt.header.src = node->identity.short_addr;
    pkt.header.seq_id = node->next_seq_id++;
    pkt.payload[0] = NX_SESSION_SUB_MSG;
    memcpy(pkt.payload + 1, session_ct, session_ct_len);
    pkt.header.payload_len = (uint8_t)(1 + session_ct_len);

    return transmit_all(&pkt);
}

/* ── Group API ───────────────────────────────────────────────────────── */

nx_err_t nx_node_group_create(nx_node_t *node, const nx_addr_short_t *group_id,
                               const uint8_t group_key[NX_SYMMETRIC_KEY_SIZE])
{
    if (!node || !group_id || !group_key) return NX_ERR_INVALID_ARG;

    nx_group_t *g = nx_group_create(&node->groups, group_id, group_key,
                                     &node->identity.short_addr);
    return g ? NX_OK : NX_ERR_FULL;
}

nx_err_t nx_node_group_add_member(nx_node_t *node,
                                   const nx_addr_short_t *group_id,
                                   const nx_addr_short_t *member)
{
    if (!node || !group_id || !member) return NX_ERR_INVALID_ARG;

    nx_group_t *g = nx_group_find(&node->groups, group_id);
    if (!g) return NX_ERR_NOT_FOUND;

    return nx_group_add_member(g, member, g->group_key);
}

nx_err_t nx_node_group_send(nx_node_t *node, const nx_addr_short_t *group_id,
                             const uint8_t *data, size_t len)
{
    if (!node || !group_id || !data) return NX_ERR_INVALID_ARG;
    if (len > NX_GROUP_MAX_PLAINTEXT) return NX_ERR_BUFFER_TOO_SMALL;

    nx_group_t *g = nx_group_find(&node->groups, group_id);
    if (!g) return NX_ERR_NOT_FOUND;

    /* Encrypt */
    uint8_t group_ct[NX_MAX_PAYLOAD];
    size_t group_ct_len = 0;
    nx_err_t err = nx_group_encrypt(g, data, len,
                                     group_ct, sizeof(group_ct),
                                     &group_ct_len);
    if (err != NX_OK) return err;

    /* Build packet: PTYPE_DATA + EXTHDR, dst=BROADCAST, rtype=FLOOD */
    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.flags = nx_packet_flags(false, true, NX_PRIO_NORMAL,
                                        NX_PTYPE_DATA, NX_RTYPE_FLOOD);
    pkt.header.ttl = node->config.default_ttl;
    pkt.header.dst = NX_ADDR_BROADCAST_SHORT;
    pkt.header.src = node->identity.short_addr;
    pkt.header.seq_id = node->next_seq_id++;
    pkt.payload[0] = NX_EXTHDR_GROUP_MSG;
    memcpy(pkt.payload + 1, group_ct, group_ct_len);
    pkt.header.payload_len = (uint8_t)(1 + group_ct_len);

    return transmit_all(&pkt);
}

/* ── Accessors ───────────────────────────────────────────────────────── */

const nx_identity_t *nx_node_identity(const nx_node_t *node)
{
    return node ? &node->identity : NULL;
}

const nx_route_table_t *nx_node_route_table(const nx_node_t *node)
{
    return node ? &node->route_table : NULL;
}
