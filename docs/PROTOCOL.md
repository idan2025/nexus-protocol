# NEXUS Protocol

This document specifies the on-the-wire behaviour of NEXUS so a second
implementation can interoperate with libnexus. It is a snapshot of what the C
reference in `lib/` currently produces — if the code and this document
disagree, the code is authoritative.

All multi-byte integers are **big-endian** unless stated otherwise.

## 1. Identity and addressing

- Signing keys: Ed25519 (Monocypher).
- Key-exchange keys: X25519 (Monocypher).
- Symmetric cipher: XChaCha20-Poly1305 (24-byte nonce, 16-byte MAC).
- Hash / MAC: BLAKE2b, also used keyed as a PRF.
- Address derivation:
  - Full address (16 bytes) = first 16 bytes of `BLAKE2b(ed25519_pubkey)`.
  - Short address (4 bytes) = first 4 bytes of the full address.

Short addresses are the routing key on the wire. Full addresses appear in
announces only, bound to the Ed25519 signature.

## 2. Packet header

Every packet begins with the 13-byte compact header:

| Offset | Size | Field        | Notes                                     |
|--------|------|--------------|-------------------------------------------|
| 0      | 1    | `version`    | 2 high bits; must be 0 today              |
|        |      | `ptype`      | 3 bits; see §3                            |
|        |      | `flags`      | 3 bits; bit 0 = has extended header       |
| 1      | 4    | `src`        | short address of origin                   |
| 5      | 4    | `dst`        | short address of destination, or broadcast|
| 9      | 1    | `hop`        | hop count (decremented on forward)        |
| 10     | 2    | `msg_id`     | random, 16-bit dedup key                  |
| 12     | 1    | `payload_len`| 0–242                                     |

Broadcast destination is `0xFFFFFFFF`. If the `has-exthdr` flag is set, the
payload begins with a single type byte (§4) that dispatches further parsing.

`NX_MAX_PACKET = 255`, `NX_MAX_PAYLOAD = 242`.

## 3. Packet types (`ptype`)

| Value | Name            | Summary                                        |
|-------|-----------------|------------------------------------------------|
| 0     | `PTYPE_DATA`    | Direct or broadcast payload                    |
| 1     | `PTYPE_ANNOUNCE`| Identity announcement                          |
| 2     | `PTYPE_ROUTE`   | PRISM routing control (sub-type in payload[0]) |
| 3     | `PTYPE_LINK`    | Reserved for future Link protocol              |

## 4. Extended headers (data packets)

When `has-exthdr` is set, `payload[0]` is the type:

| Type  | Name              | Payload shape                                             |
|-------|-------------------|-----------------------------------------------------------|
| 0x01  | `FRAGMENT`        | `[frag_id(2)][idx_total(1)][data]` (§6)                    |
| 0x10  | `SESSION_INIT`    | X3DH-lite handshake init (§5)                              |
| 0x11  | `SESSION_ACK`     | X3DH-lite handshake ack                                    |
| 0x12  | `SESSION_MSG`     | Ratchet-encrypted message (§5)                             |
| 0x13  | `TITLE`           | Inline NXM field (rare, usually inside SESSION_MSG)        |
| 0x20  | `GROUP_MSG`       | Sender-key encrypted group message (§7)                    |
| 0x30  | `INBOX_REQ`       | Request mailbox flush to this address                      |

## 5. Sessions (Double Ratchet)

Handshake is a simplified X3DH with two Diffie-Hellman outputs
(identity×identity and ephemeral×identity) fed into BLAKE2b to derive the
initial root key. Each subsequent message runs a symmetric BLAKE2b-keyed
chain ratchet; when the peer's DH public changes, a fresh root-key DH ratchet
step is performed.

`SESSION_MSG` wire format inside the payload:

```
[msg_num(4)][prev_n(4)][dh_pub(32)][nonce(24)][mac(16)][ciphertext]
```

Total session overhead: 80 bytes. Max plaintext inside one session packet:
`NX_SESSION_MAX_PLAINTEXT = 161` bytes (use fragmentation for larger).

## 6. Fragmentation

Fragment exthdr `0x01` wraps a chunk:

- `frag_id` (uint16): random per-message identifier.
- `idx_total`: upper 4 bits = fragment index (0–15), lower 4 bits = total
  fragments (1–16).
- Remaining payload: up to 238 bytes per fragment.

Max reassembled size: 16 × 238 = 3808 bytes. Reassembly buffer holds up to 8
concurrent messages with a 30 s timeout; LRU evicts on overflow.

## 7. Groups (sender-keys)

Each group has a 32-byte group key. A per-member send key is derived as
`BLAKE2b-keyed(group_key, member_addr)` and chain-ratcheted per message
with BLAKE2b-keyed. `GROUP_MSG` wire format:

```
[group_id(4)][msg_num(4)][nonce(24)][mac(16)][ciphertext]
```

48-byte overhead, 193-byte max plaintext per packet.

## 8. Announces

`PTYPE_ANNOUNCE` payload (130 bytes):

```
[sign_pub(32)][x25519_pub(32)][role(1)][flags(1)][signature(64)]
```

Signature is Ed25519 over `sign_pub || x25519_pub || role || flags`. Role
values: 0=LEAF, 1=RELAY, 2=GATEWAY, 3=ANCHOR, 4=SENTINEL, 5=PILLAR,
6=VAULT.

## 9. Routing (PRISM)

`PTYPE_ROUTE` packets carry a 1-byte sub-type followed by a fixed payload:

| Sub-type | Name   | Size | Notes                                       |
|----------|--------|------|---------------------------------------------|
| 0        | RREQ   | 12 B | Route request                                |
| 1        | RREP   | 13 B | Route reply with metric                      |
| 2        | RERR   | 5 B  | Route error / link break                     |
| 3        | BEACON | 3 B  | Cheap neighbour liveness                     |

LEAF nodes never forward. RELAY and higher roles forward per the route
table (64 entries, 128-entry dedup window). GATEWAY bridges across
transports that share a different `domain_id`.

## 10. Transports

- **Serial / TCP / TCP-inet**: `[0x7E][LEN_HI][LEN_LO][payload][0x7E]`
  framing. TCP-inet optionally adds a 72-byte PSK challenge–response
  (`"NXAUTH\0\0" || 32-byte nonce` each way, then
  `BLAKE2b-keyed(psk, peer_nonce || my_nonce)` tag each way).
- **UDP multicast**: no framing (datagrams self-delimit). Default group
  `224.0.77.88:4243`, TTL 1.
- **BLE / LoRa / HaLow**: no extra framing; one PDU per packet.

## 11. NXM message envelope

NXM is an LXMF-inspired structured payload carried inside the session or
group ciphertext:

```
[version(1)][type(1)][flags(1)][timestamp(4 LE)][field_count(1)]
[fields...]   field = [type(1)][len(2 LE)][value]
```

Known field types: 0x01 TEXT, 0x02 FILE, 0x03 IMAGE, 0x04 LOCATION,
0x05 VOICE_NOTE, 0x06 REACTION, 0x07 ACK, 0x08 TYPING, 0x09 READ_RECEIPT,
0x0A DELETE, 0x0B NICKNAME, 0x0C CONTACT, 0x10 MIMETYPE, 0x13 TITLE.

Up to 16 fields per message, 3800 bytes total (fits fragmentation).

## 12. Storage-and-forward (anchor mailbox)

All RELAY-or-higher nodes buffer unroutable packets:

- RELAY: 8 slots, 30 min TTL.
- ANCHOR: 32 slots, 1 h TTL.
- VAULT: 32 slots static (256 planned), 24 h TTL.

Delivery is triggered by seeing the destination's announce or by an
explicit `INBOX_REQ` exthdr packet.
