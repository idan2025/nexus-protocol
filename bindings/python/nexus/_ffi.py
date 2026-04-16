"""NEXUS Protocol -- ctypes FFI bindings to libnexus."""

import ctypes
import ctypes.util
import os
import pathlib

# ── Constants ──────────────────────────────────────────────────────────

NX_SHORT_ADDR_SIZE = 4
NX_FULL_ADDR_SIZE = 16
NX_PUBKEY_SIZE = 32
NX_PRIVKEY_SIZE = 32
NX_SIGN_SECRET_SIZE = 64
NX_SIGNATURE_SIZE = 64
NX_SYMMETRIC_KEY_SIZE = 32
NX_NONCE_SIZE = 24
NX_MAC_SIZE = 16
NX_HEADER_SIZE = 13
NX_MAX_PAYLOAD = 242
NX_MAX_PACKET = NX_HEADER_SIZE + NX_MAX_PAYLOAD + NX_MAC_SIZE

NX_MAX_NEIGHBORS = 32
NX_MAX_ROUTES = 64
NX_MAX_DEDUP = 128
NX_MAX_PENDING_RREQ = 16

NX_FRAG_MAX_COUNT = 16
NX_FRAG_PAYLOAD_CAP = NX_MAX_PAYLOAD - 4
NX_FRAG_MAX_MESSAGE = NX_FRAG_MAX_COUNT * NX_FRAG_PAYLOAD_CAP
NX_FRAG_REASSEMBLY_SLOTS = 8

NX_ANCHOR_MAX_STORED = 32

NX_SESSION_MAX = 16
NX_SESSION_OVERHEAD = 80
NX_SESSION_MAX_SKIP = 32

NX_GROUP_MAX = 8
NX_GROUP_MAX_MEMBERS = 16

# ── Library loading ───────────────────────────────────────────────────

def _find_lib():
    # 1. NEXUS_LIB_PATH env var
    env_path = os.environ.get("NEXUS_LIB_PATH")
    if env_path and os.path.isfile(env_path):
        return env_path

    # 2. Relative to this package: ../../../../build/lib/libnexus.so
    pkg_dir = pathlib.Path(__file__).parent
    rel_path = pkg_dir / ".." / ".." / ".." / "build" / "lib" / "libnexus.so"
    rel_path = rel_path.resolve()
    if rel_path.is_file():
        return str(rel_path)

    # 3. System library
    sys_path = ctypes.util.find_library("nexus")
    if sys_path:
        return sys_path

    raise OSError(
        "Cannot find libnexus.so. Set NEXUS_LIB_PATH or build with cmake."
    )


lib = ctypes.CDLL(_find_lib())

# ── Struct definitions ────────────────────────────────────────────────
# All structs match exact C layout verified via offsetof/sizeof.


class AddrShort(ctypes.Structure):
    _fields_ = [("bytes", ctypes.c_uint8 * NX_SHORT_ADDR_SIZE)]


class AddrFull(ctypes.Structure):
    _fields_ = [("bytes", ctypes.c_uint8 * NX_FULL_ADDR_SIZE)]


class CIdentity(ctypes.Structure):
    _fields_ = [
        ("sign_secret", ctypes.c_uint8 * NX_SIGN_SECRET_SIZE),   # off=0
        ("sign_public", ctypes.c_uint8 * NX_PUBKEY_SIZE),         # off=64
        ("x25519_secret", ctypes.c_uint8 * NX_PRIVKEY_SIZE),      # off=96
        ("x25519_public", ctypes.c_uint8 * NX_PUBKEY_SIZE),       # off=128
        ("full_addr", AddrFull),                                    # off=160
        ("short_addr", AddrShort),                                  # off=176
    ]


assert ctypes.sizeof(CIdentity) == 180


class Header(ctypes.Structure):
    # C layout: flags@0 hop@1 ttl@2 dst@3 src@7 [pad]@11 seq_id@12 payload_len@14 [pad]@15
    _pack_ = 1
    _fields_ = [
        ("flags", ctypes.c_uint8),
        ("hop_count", ctypes.c_uint8),
        ("ttl", ctypes.c_uint8),
        ("dst", AddrShort),
        ("src", AddrShort),
        ("_pad0", ctypes.c_uint8),
        ("seq_id", ctypes.c_uint16),
        ("payload_len", ctypes.c_uint8),
        ("_pad1", ctypes.c_uint8),
    ]


assert ctypes.sizeof(Header) == 16


class Packet(ctypes.Structure):
    _fields_ = [
        ("header", Header),                                          # off=0, size=16
        ("payload", ctypes.c_uint8 * (NX_MAX_PAYLOAD + NX_MAC_SIZE)),  # off=16, size=258
    ]
    _pack_ = 1


assert ctypes.sizeof(Packet) == 274


# Transport ops -- function pointer types
INIT_FN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
SEND_FN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t)
RECV_FN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8),
                            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t), ctypes.c_uint32)
DESTROY_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p)


class TransportOps(ctypes.Structure):
    _fields_ = [
        ("init", INIT_FN),
        ("send", SEND_FN),
        ("recv", RECV_FN),
        ("destroy", DESTROY_FN),
    ]


class Transport(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),              # off=0, nx_transport_type_t (enum = int)
        ("_pad0", ctypes.c_uint8 * 4),       # padding to align name pointer
        ("name", ctypes.c_char_p),           # off=8
        ("ops", ctypes.POINTER(TransportOps)),  # off=16
        ("state", ctypes.c_void_p),          # off=24
        ("active", ctypes.c_bool),           # off=32
        ("domain_id", ctypes.c_uint8),       # off=33
    ]


assert ctypes.sizeof(Transport) == 40


# Node config -- callback types
OnDataFn = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.c_void_p,
)

OnNeighborFn = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(AddrShort),
    ctypes.c_int,
    ctypes.c_void_p,
)

OnSessionFn = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.c_void_p,
)

OnGroupFn = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.c_void_p,
)


class NodeConfig(ctypes.Structure):
    _fields_ = [
        ("role", ctypes.c_int),                  # off=0 (enum)
        ("default_ttl", ctypes.c_uint8),         # off=4
        ("_pad0", ctypes.c_uint8 * 3),           # padding to align beacon_interval_ms
        ("beacon_interval_ms", ctypes.c_uint32), # off=8
        ("_pad1", ctypes.c_uint8 * 4),           # padding to align fn pointers
        ("on_data", OnDataFn),                   # off=16
        ("on_neighbor", OnNeighborFn),           # off=24
        ("on_session", OnSessionFn),             # off=32
        ("on_group", OnGroupFn),                 # off=40
        ("user_ctx", ctypes.c_void_p),           # off=48
    ]


assert ctypes.sizeof(NodeConfig) == 56


class Neighbor(ctypes.Structure):
    _fields_ = [
        ("addr", AddrShort),                          # off=0
        ("full_addr", AddrFull),                       # off=4
        ("sign_pubkey", ctypes.c_uint8 * NX_PUBKEY_SIZE),   # off=20
        ("x25519_pubkey", ctypes.c_uint8 * NX_PUBKEY_SIZE), # off=52
        ("role", ctypes.c_int),                        # off=84
        ("rssi", ctypes.c_int8),                       # off=88
        ("link_quality", ctypes.c_uint8),              # off=89
        ("_pad0", ctypes.c_uint8 * 6),                 # padding to align last_seen_ms
        ("last_seen_ms", ctypes.c_uint64),             # off=96
        ("valid", ctypes.c_bool),                      # off=104
        ("_pad1", ctypes.c_uint8 * 7),                 # padding to 112
    ]


assert ctypes.sizeof(Neighbor) == 112


class Route(ctypes.Structure):
    _fields_ = [
        ("dest", AddrShort),            # off=0
        ("next_hop", AddrShort),        # off=4
        ("hop_count", ctypes.c_uint8),  # off=8
        ("metric", ctypes.c_uint8),     # off=9
        ("via_transport", ctypes.c_uint8),  # off=10
        ("_pad0", ctypes.c_uint8 * 5),  # padding to align expires_ms
        ("expires_ms", ctypes.c_uint64),  # off=16
        ("valid", ctypes.c_bool),       # off=24
        ("_pad1", ctypes.c_uint8 * 7),  # padding to 32
    ]


assert ctypes.sizeof(Route) == 32


class Dedup(ctypes.Structure):
    _fields_ = [
        ("src", AddrShort),              # off=0
        ("seq_id", ctypes.c_uint16),     # off=4
        ("_pad0", ctypes.c_uint8 * 2),   # padding
        ("expires_ms", ctypes.c_uint64), # off=8
        ("valid", ctypes.c_bool),        # off=16
        ("_pad1", ctypes.c_uint8 * 7),   # padding to 24
    ]


assert ctypes.sizeof(Dedup) == 24


class PendingRreq(ctypes.Structure):
    _fields_ = [
        ("dest", AddrShort),              # off=0
        ("rreq_id", ctypes.c_uint16),     # off=4
        ("_pad0", ctypes.c_uint8 * 2),    # padding
        ("expires_ms", ctypes.c_uint64),  # off=8
        ("valid", ctypes.c_bool),         # off=16
        ("_pad1", ctypes.c_uint8 * 7),    # padding to 24
    ]


assert ctypes.sizeof(PendingRreq) == 24


class RouteTable(ctypes.Structure):
    _fields_ = [
        ("neighbors", Neighbor * NX_MAX_NEIGHBORS),       # off=0
        ("routes", Route * NX_MAX_ROUTES),                 # off=3584
        ("dedup", Dedup * NX_MAX_DEDUP),                   # off=5632
        ("pending_rreq", PendingRreq * NX_MAX_PENDING_RREQ),  # off=8704
        ("next_rreq_id", ctypes.c_uint16),                 # off=9088
        ("_pad0", ctypes.c_uint8 * 6),                     # padding
        ("last_beacon_ms", ctypes.c_uint64),               # off=9096
    ]


assert ctypes.sizeof(RouteTable) == 9104


class FragHeader(ctypes.Structure):
    _fields_ = [
        ("frag_id", ctypes.c_uint16),     # off=0
        ("frag_index", ctypes.c_uint8),   # off=2
        ("frag_total", ctypes.c_uint8),   # off=3
    ]


assert ctypes.sizeof(FragHeader) == 4


class Reassembly(ctypes.Structure):
    _fields_ = [
        ("src", AddrShort),                               # off=0
        ("frag_id", ctypes.c_uint16),                     # off=4
        ("frag_total", ctypes.c_uint8),                   # off=6
        ("_pad0", ctypes.c_uint8),                        # padding
        ("received_mask", ctypes.c_uint16),               # off=8
        ("data", ctypes.c_uint8 * NX_FRAG_MAX_MESSAGE),  # off=10
        ("_pad1", ctypes.c_uint8 * 6),                    # padding to align frag_sizes
        ("frag_sizes", ctypes.c_size_t * NX_FRAG_MAX_COUNT),  # off=3824
        ("started_ms", ctypes.c_uint64),                  # off=3952
        ("valid", ctypes.c_bool),                         # off=3960
        ("_pad2", ctypes.c_uint8 * 7),                    # padding to 3968
    ]


assert ctypes.sizeof(Reassembly) == 3968


class FragBuffer(ctypes.Structure):
    _fields_ = [
        ("slots", Reassembly * NX_FRAG_REASSEMBLY_SLOTS),  # off=0
        ("next_frag_id", ctypes.c_uint16),                  # last
        ("_pad0", ctypes.c_uint8 * 6),                      # padding
    ]


assert ctypes.sizeof(FragBuffer) == 31752


class AnchorMsg(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("pkt", Packet),                    # off=0, size=274
        ("dest", AddrShort),                # off=274
        ("_pad0", ctypes.c_uint8 * 2),      # padding to align stored_ms
        ("stored_ms", ctypes.c_uint64),     # off=280
        ("valid", ctypes.c_bool),           # off=288
        ("_pad1", ctypes.c_uint8 * 7),      # padding to 296
    ]


assert ctypes.sizeof(AnchorMsg) == 296


class Anchor(ctypes.Structure):
    _fields_ = [
        ("msgs", AnchorMsg * NX_ANCHOR_MAX_STORED),  # off=0
        ("msg_ttl_ms", ctypes.c_uint32),              # last
        ("_pad0", ctypes.c_uint8 * 4),                # padding
    ]


assert ctypes.sizeof(Anchor) == 9480


class Chain(ctypes.Structure):
    _fields_ = [
        ("key", ctypes.c_uint8 * NX_SYMMETRIC_KEY_SIZE),  # off=0
        ("n", ctypes.c_uint32),                             # off=32
    ]


assert ctypes.sizeof(Chain) == 36


class SkippedKey(ctypes.Structure):
    _fields_ = [
        ("dh_pub", ctypes.c_uint8 * NX_PUBKEY_SIZE),       # off=0
        ("n", ctypes.c_uint32),                              # off=32
        ("msg_key", ctypes.c_uint8 * NX_SYMMETRIC_KEY_SIZE),  # off=36
        ("valid", ctypes.c_bool),                            # off=68
        ("_pad0", ctypes.c_uint8 * 3),                       # padding to 72
    ]


assert ctypes.sizeof(SkippedKey) == 72


class Session(ctypes.Structure):
    _fields_ = [
        ("peer_addr", AddrShort),                               # off=0
        ("peer_x25519_pub", ctypes.c_uint8 * NX_PUBKEY_SIZE),  # off=4
        ("dh_secret", ctypes.c_uint8 * NX_PRIVKEY_SIZE),       # off=36
        ("dh_public", ctypes.c_uint8 * NX_PUBKEY_SIZE),        # off=68
        ("dh_remote", ctypes.c_uint8 * NX_PUBKEY_SIZE),        # off=100
        ("root_key", ctypes.c_uint8 * NX_SYMMETRIC_KEY_SIZE),  # off=132
        ("send_chain", Chain),                                   # off=164
        ("recv_chain", Chain),                                   # off=200
        ("prev_send_n", ctypes.c_uint32),                       # off=236
        ("skipped", SkippedKey * NX_SESSION_MAX_SKIP),          # off=240
        ("established", ctypes.c_bool),                         # off=2544
        ("valid", ctypes.c_bool),                               # off=2545
        ("_pad0", ctypes.c_uint8 * 2),                          # padding to 2548
    ]


assert ctypes.sizeof(Session) == 2548


class SessionStore(ctypes.Structure):
    _fields_ = [
        ("sessions", Session * NX_SESSION_MAX),
    ]


assert ctypes.sizeof(SessionStore) == 40768


class GroupMember(ctypes.Structure):
    _fields_ = [
        ("addr", AddrShort),                                    # off=0
        ("chain_key", ctypes.c_uint8 * NX_SYMMETRIC_KEY_SIZE),  # off=4
        ("msg_num", ctypes.c_uint32),                            # off=36
        ("valid", ctypes.c_bool),                                # off=40
        ("_pad0", ctypes.c_uint8 * 3),                           # padding to 44
    ]


assert ctypes.sizeof(GroupMember) == 44


class Group(ctypes.Structure):
    _fields_ = [
        ("group_id", AddrShort),                                     # off=0
        ("group_key", ctypes.c_uint8 * NX_SYMMETRIC_KEY_SIZE),       # off=4
        ("send_chain_key", ctypes.c_uint8 * NX_SYMMETRIC_KEY_SIZE),  # off=36
        ("send_msg_num", ctypes.c_uint32),                           # off=68
        ("members", GroupMember * NX_GROUP_MAX_MEMBERS),             # off=72
        ("valid", ctypes.c_bool),                                    # off=776
        ("_pad0", ctypes.c_uint8 * 3),                               # padding to 780
    ]


assert ctypes.sizeof(Group) == 780


class GroupStore(ctypes.Structure):
    _fields_ = [
        ("groups", Group * NX_GROUP_MAX),
    ]


assert ctypes.sizeof(GroupStore) == 6240


class NodeState(ctypes.Structure):
    _fields_ = [
        ("identity", CIdentity),            # off=0
        ("_pad0", ctypes.c_uint8 * 4),      # padding (180 -> 184)
        ("config", NodeConfig),              # off=184
        ("route_table", RouteTable),         # off=240
        ("frag_buffer", FragBuffer),         # off=9344
        ("anchor", Anchor),                  # off=41096
        ("sessions", SessionStore),          # off=50576
        ("groups", GroupStore),              # off=91344
        ("next_seq_id", ctypes.c_uint16),   # off=97584
        ("running", ctypes.c_bool),          # off=97586
        ("has_telemetry", ctypes.c_bool),    # off=97587
        ("telemetry", ctypes.c_uint8 * 4),   # off=97588  (nx_announce_telemetry_t)
        ("msgring", ctypes.c_uint8 * 8200),  # off=97592  (nx_msgring_t)
    ]


assert ctypes.sizeof(NodeState) == 105792

# ── Function signatures ──────────────────────────────────────────────

# Identity
lib.nx_identity_generate.argtypes = [ctypes.POINTER(CIdentity)]
lib.nx_identity_generate.restype = ctypes.c_int

lib.nx_identity_wipe.argtypes = [ctypes.POINTER(CIdentity)]
lib.nx_identity_wipe.restype = None

lib.nx_addr_short_cmp.argtypes = [
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(AddrShort),
]
lib.nx_addr_short_cmp.restype = ctypes.c_int

# Transport registry
lib.nx_transport_registry_init.argtypes = []
lib.nx_transport_registry_init.restype = None

lib.nx_transport_register.argtypes = [ctypes.POINTER(Transport)]
lib.nx_transport_register.restype = ctypes.c_int

lib.nx_transport_count.argtypes = []
lib.nx_transport_count.restype = ctypes.c_int

lib.nx_transport_get.argtypes = [ctypes.c_int]
lib.nx_transport_get.restype = ctypes.POINTER(Transport)

lib.nx_transport_set_active.argtypes = [ctypes.POINTER(Transport), ctypes.c_bool]
lib.nx_transport_set_active.restype = None

lib.nx_transport_destroy.argtypes = [ctypes.POINTER(Transport)]
lib.nx_transport_destroy.restype = None

# Pipe transport
lib.nx_pipe_transport_create.argtypes = []
lib.nx_pipe_transport_create.restype = ctypes.POINTER(Transport)

lib.nx_pipe_transport_link.argtypes = [
    ctypes.POINTER(Transport),
    ctypes.POINTER(Transport),
]
lib.nx_pipe_transport_link.restype = None

# TCP Internet transport
lib.nx_tcp_inet_transport_create.argtypes = []
lib.nx_tcp_inet_transport_create.restype = ctypes.POINTER(Transport)

# UDP Multicast transport
lib.nx_udp_mcast_transport_create.argtypes = []
lib.nx_udp_mcast_transport_create.restype = ctypes.POINTER(Transport)

# Node lifecycle
lib.nx_node_init.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(NodeConfig),
]
lib.nx_node_init.restype = ctypes.c_int

lib.nx_node_init_with_identity.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(NodeConfig),
    ctypes.POINTER(CIdentity),
]
lib.nx_node_init_with_identity.restype = ctypes.c_int

lib.nx_node_stop.argtypes = [ctypes.POINTER(NodeState)]
lib.nx_node_stop.restype = None

# Node event loop
lib.nx_node_poll.argtypes = [ctypes.POINTER(NodeState), ctypes.c_uint32]
lib.nx_node_poll.restype = ctypes.c_int

# Node sending
lib.nx_node_send.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
]
lib.nx_node_send.restype = ctypes.c_int

lib.nx_node_send_raw.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
]
lib.nx_node_send_raw.restype = ctypes.c_int

lib.nx_node_send_large.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
]
lib.nx_node_send_large.restype = ctypes.c_int

lib.nx_node_announce.argtypes = [ctypes.POINTER(NodeState)]
lib.nx_node_announce.restype = ctypes.c_int

# Node session
lib.nx_node_session_start.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(AddrShort),
]
lib.nx_node_session_start.restype = ctypes.c_int

lib.nx_node_send_session.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
]
lib.nx_node_send_session.restype = ctypes.c_int

# Node group
lib.nx_node_group_create.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
]
lib.nx_node_group_create.restype = ctypes.c_int

lib.nx_node_group_add_member.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(AddrShort),
]
lib.nx_node_group_add_member.restype = ctypes.c_int

lib.nx_node_group_send.argtypes = [
    ctypes.POINTER(NodeState),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
]
lib.nx_node_group_send.restype = ctypes.c_int

# Neighbor / route injection (for testing)
lib.nx_neighbor_update.argtypes = [
    ctypes.POINTER(RouteTable),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(AddrFull),
    ctypes.POINTER(ctypes.c_uint8),   # sign_pubkey[32]
    ctypes.POINTER(ctypes.c_uint8),   # x25519_pubkey[32]
    ctypes.c_int,                      # role
    ctypes.c_int8,                     # rssi
    ctypes.c_uint64,                   # now_ms
]
lib.nx_neighbor_update.restype = ctypes.c_int

lib.nx_route_update.argtypes = [
    ctypes.POINTER(RouteTable),
    ctypes.POINTER(AddrShort),
    ctypes.POINTER(AddrShort),
    ctypes.c_uint8,
    ctypes.c_uint8,
    ctypes.c_uint64,
]
lib.nx_route_update.restype = ctypes.c_int

lib.nx_platform_time_ms.argtypes = []
lib.nx_platform_time_ms.restype = ctypes.c_uint64

# Node accessors
lib.nx_node_identity.argtypes = [ctypes.POINTER(NodeState)]
lib.nx_node_identity.restype = ctypes.POINTER(CIdentity)
