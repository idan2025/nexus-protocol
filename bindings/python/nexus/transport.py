"""NEXUS Protocol -- Transport helpers."""

import ctypes

from . import _ffi
from .errors import check

# Module-level list prevents GC of ctypes transport pointers.
_transport_refs = []


def registry_init():
    """Reset the global transport registry and release tracked references."""
    _transport_refs.clear()
    _ffi.lib.nx_transport_registry_init()


def transport_count():
    return _ffi.lib.nx_transport_count()


def create_pipe_pair():
    """Create two linked pipe transports and register both."""
    a = _ffi.lib.nx_pipe_transport_create()
    b = _ffi.lib.nx_pipe_transport_create()
    _ffi.lib.nx_pipe_transport_link(a, b)
    check(_ffi.lib.nx_transport_register(a))
    check(_ffi.lib.nx_transport_register(b))
    _transport_refs.extend([a, b])
    return a, b


def create_tcp_inet(listen_port=0, listen_host="0.0.0.0", peers=None,
                    reconnect_ms=5000, failover=False, failover_timeout_ms=10000):
    """Create a TCP Internet transport (multi-peer, auto-reconnect).

    Args:
        listen_port: Port to listen on (0 = no server).
        listen_host: Bind address for server.
        peers: List of (host, port) tuples to connect to.
        reconnect_ms: Reconnect interval in ms.
        failover: If True, try one peer at a time instead of all in parallel.
        failover_timeout_ms: Max wait per peer before trying next (default 10s).

    Returns the transport pointer (already registered).
    """
    # Build the config struct in-memory matching nx_tcp_inet_config_t layout.
    # Layout: listen_host(ptr) listen_port(u16) pad(6) peers[16]*(ptr,u16,pad6)
    # peer_count(i32) pad(4) reconnect_interval_ms(u32) pad(4)
    # It's easier to just init via raw C: create, then poke config fields.
    t = _ffi.lib.nx_tcp_inet_transport_create()

    # We need to call init with a proper C config struct.
    # Define it inline to match the C struct.
    class PeerEntry(ctypes.Structure):
        _fields_ = [
            ("host", ctypes.c_char_p),
            ("port", ctypes.c_uint16),
        ]

    class TcpInetConfig(ctypes.Structure):
        _fields_ = [
            ("listen_host", ctypes.c_char_p),       # offset 0
            ("listen_port", ctypes.c_uint16),        # offset 8
            ("_pad0", ctypes.c_uint8 * 6),           # offset 10..16
            ("peers", PeerEntry * 16),               # offset 16..272
            ("peer_count", ctypes.c_int),            # offset 272
            ("reconnect_interval_ms", ctypes.c_uint32),  # offset 276
            ("failover", ctypes.c_bool),             # offset 280
            ("_pad1", ctypes.c_uint8 * 3),           # offset 281..284
            ("failover_timeout_ms", ctypes.c_uint32),    # offset 284
            ("psk", ctypes.c_uint8 * 32),            # offset 288
            ("psk_len", ctypes.c_size_t),            # offset 320
            ("allow_list", ctypes.c_char_p * 16),    # offset 328
            ("allow_count", ctypes.c_int),           # offset 456
        ]

    cfg = TcpInetConfig()
    cfg.listen_host = listen_host.encode() if listen_host else None
    cfg.listen_port = listen_port
    cfg.reconnect_interval_ms = reconnect_ms
    cfg.failover = failover
    cfg.failover_timeout_ms = failover_timeout_ms

    if peers:
        cfg.peer_count = min(len(peers), 16)
        for i, (host, port) in enumerate(peers[:16]):
            cfg.peers[i].host = host.encode() if isinstance(host, str) else host
            cfg.peers[i].port = port
    else:
        cfg.peer_count = 0

    # Call init
    ops = t.contents.ops.contents
    init_fn = ops.init
    rc = init_fn(ctypes.cast(t, ctypes.c_void_p), ctypes.byref(cfg))
    check(rc)

    check(_ffi.lib.nx_transport_register(t))
    _transport_refs.append(t)
    # Keep cfg alive to prevent GC of string pointers used during init
    _transport_refs.append(cfg)
    return t


def create_udp_multicast(group=None, port=0):
    """Create a UDP multicast transport for zero-config LAN discovery.

    Args:
        group: Multicast group address (default: 224.0.77.88).
        port: UDP port (default: 4243).

    Returns the transport pointer (already registered).
    """
    t = _ffi.lib.nx_udp_mcast_transport_create()

    class UdpMcastConfig(ctypes.Structure):
        _fields_ = [
            ("group", ctypes.c_char_p),
            ("port", ctypes.c_uint16),
        ]

    cfg = UdpMcastConfig()
    cfg.group = group.encode() if group else None
    cfg.port = port

    ops = t.contents.ops.contents
    rc = ops.init(ctypes.cast(t, ctypes.c_void_p), ctypes.byref(cfg))
    check(rc)

    check(_ffi.lib.nx_transport_register(t))
    _transport_refs.append(t)
    _transport_refs.append(cfg)
    return t


def set_active(transport_ptr, active):
    """Set a transport's active flag via C function (avoids ctypes proxy GC)."""
    _ffi.lib.nx_transport_set_active(transport_ptr, active)
