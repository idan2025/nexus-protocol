"""NEXUS Protocol -- Node class (high-level API)."""

import ctypes

from . import _ffi
from .errors import check


class Node:
    def __init__(self, role=0, identity=None, on_data=None, on_neighbor=None,
                 on_session=None, on_group=None):
        self._state = _ffi.NodeState()
        self._config = _ffi.NodeConfig()
        self._config.role = role
        self._config.default_ttl = 7
        self._config.beacon_interval_ms = 999999999
        self._cb_refs = []  # prevent GC of CFUNCTYPE instances

        if on_data:
            def _on_data(src, data, length, user):
                on_data(bytes(src.contents.bytes), bytes(data[:length]))
            cb = _ffi.OnDataFn(_on_data)
            self._cb_refs.append(cb)
            self._config.on_data = cb

        if on_neighbor:
            def _on_neighbor(addr, role_val, user):
                on_neighbor(bytes(addr.contents.bytes), role_val)
            cb = _ffi.OnNeighborFn(_on_neighbor)
            self._cb_refs.append(cb)
            self._config.on_neighbor = cb

        if on_session:
            def _on_session(src, data, length, user):
                on_session(bytes(src.contents.bytes), bytes(data[:length]))
            cb = _ffi.OnSessionFn(_on_session)
            self._cb_refs.append(cb)
            self._config.on_session = cb

        if on_group:
            def _on_group(gid, src, data, length, user):
                on_group(
                    bytes(gid.contents.bytes),
                    bytes(src.contents.bytes),
                    bytes(data[:length]),
                )
            cb = _ffi.OnGroupFn(_on_group)
            self._cb_refs.append(cb)
            self._config.on_group = cb

        if identity:
            check(_ffi.lib.nx_node_init_with_identity(
                ctypes.byref(self._state),
                ctypes.byref(self._config),
                ctypes.byref(identity._raw),
            ))
        else:
            check(_ffi.lib.nx_node_init(
                ctypes.byref(self._state),
                ctypes.byref(self._config),
            ))

    @property
    def short_addr(self):
        id_ptr = _ffi.lib.nx_node_identity(ctypes.byref(self._state))
        return bytes(id_ptr.contents.short_addr.bytes)

    def poll(self, timeout_ms=0):
        return _ffi.lib.nx_node_poll(ctypes.byref(self._state), timeout_ms)

    def drain(self, rounds=8, timeout_ms=0):
        """Poll multiple rounds to drain all queued messages."""
        for _ in range(rounds):
            self.poll(timeout_ms)

    def announce(self):
        check(_ffi.lib.nx_node_announce(ctypes.byref(self._state)))

    def send_raw(self, dest, data):
        dst = _ffi.AddrShort()
        dst.bytes[:] = dest[:4]
        buf = (ctypes.c_uint8 * len(data))(*data)
        check(_ffi.lib.nx_node_send_raw(
            ctypes.byref(self._state), ctypes.byref(dst), buf, len(data),
        ))

    def send(self, dest, data):
        dst = _ffi.AddrShort()
        dst.bytes[:] = dest[:4]
        buf = (ctypes.c_uint8 * len(data))(*data)
        check(_ffi.lib.nx_node_send(
            ctypes.byref(self._state), ctypes.byref(dst), buf, len(data),
        ))

    def session_start(self, dest):
        dst = _ffi.AddrShort()
        dst.bytes[:] = dest[:4]
        check(_ffi.lib.nx_node_session_start(
            ctypes.byref(self._state), ctypes.byref(dst),
        ))

    def send_session(self, dest, data):
        dst = _ffi.AddrShort()
        dst.bytes[:] = dest[:4]
        buf = (ctypes.c_uint8 * len(data))(*data)
        check(_ffi.lib.nx_node_send_session(
            ctypes.byref(self._state), ctypes.byref(dst), buf, len(data),
        ))

    def group_create(self, group_id, group_key):
        gid = _ffi.AddrShort()
        gid.bytes[:] = group_id[:4]
        key = (ctypes.c_uint8 * 32)(*group_key[:32])
        check(_ffi.lib.nx_node_group_create(
            ctypes.byref(self._state), ctypes.byref(gid), key,
        ))

    def group_add_member(self, group_id, member):
        gid = _ffi.AddrShort()
        gid.bytes[:] = group_id[:4]
        mem = _ffi.AddrShort()
        mem.bytes[:] = member[:4]
        check(_ffi.lib.nx_node_group_add_member(
            ctypes.byref(self._state), ctypes.byref(gid), ctypes.byref(mem),
        ))

    def group_send(self, group_id, data):
        gid = _ffi.AddrShort()
        gid.bytes[:] = group_id[:4]
        buf = (ctypes.c_uint8 * len(data))(*data)
        check(_ffi.lib.nx_node_group_send(
            ctypes.byref(self._state), ctypes.byref(gid), buf, len(data),
        ))

    def inject_neighbor(self, other):
        """Directly inject another node as a neighbor (for testing)."""
        other_id = _ffi.lib.nx_node_identity(ctypes.byref(other._state))
        now = _ffi.lib.nx_platform_time_ms()
        _ffi.lib.nx_neighbor_update(
            ctypes.byref(self._state.route_table),
            ctypes.byref(other_id.contents.short_addr),
            ctypes.byref(other_id.contents.full_addr),
            other_id.contents.sign_public,
            other_id.contents.x25519_public,
            0, 0, now,
        )
        _ffi.lib.nx_route_update(
            ctypes.byref(self._state.route_table),
            ctypes.byref(other_id.contents.short_addr),
            ctypes.byref(other_id.contents.short_addr),
            1, 1, now,
        )

    def stop(self):
        _ffi.lib.nx_node_stop(ctypes.byref(self._state))

    def __del__(self):
        if hasattr(self, "_state"):
            self.stop()
