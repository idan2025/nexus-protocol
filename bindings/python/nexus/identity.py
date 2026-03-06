"""NEXUS Protocol -- Identity management."""

import ctypes

from . import _ffi
from .errors import check


class Identity:
    def __init__(self):
        self._raw = _ffi.CIdentity()
        check(_ffi.lib.nx_identity_generate(ctypes.byref(self._raw)))

    @classmethod
    def from_raw(cls, raw):
        obj = object.__new__(cls)
        obj._raw = raw
        return obj

    @property
    def short_addr(self):
        return bytes(self._raw.short_addr.bytes)

    @property
    def short_addr_hex(self):
        return self.short_addr.hex()

    @property
    def sign_public(self):
        return bytes(self._raw.sign_public)

    @property
    def x25519_public(self):
        return bytes(self._raw.x25519_public)

    def wipe(self):
        _ffi.lib.nx_identity_wipe(ctypes.byref(self._raw))

    def save(self, path):
        with open(path, "wb") as f:
            f.write(bytes(self._raw))

    @classmethod
    def load(cls, path):
        obj = object.__new__(cls)
        obj._raw = _ffi.CIdentity()
        with open(path, "rb") as f:
            data = f.read(ctypes.sizeof(_ffi.CIdentity))
            ctypes.memmove(ctypes.byref(obj._raw), data, len(data))
        return obj
