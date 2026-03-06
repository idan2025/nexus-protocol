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


def set_active(transport_ptr, active):
    """Set a transport's active flag via C function (avoids ctypes proxy GC)."""
    _ffi.lib.nx_transport_set_active(transport_ptr, active)
