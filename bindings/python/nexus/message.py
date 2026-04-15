"""
NEXUS Protocol -- Structured Message Format (NXM) Python bindings.

Pure-Python implementation of the NXM wire format for building and
parsing NEXUS structured messages. Compatible with the C library's
nx_msg_build_*/nx_msg_parse functions.

Wire format (little-endian):
  [version(1)][type(1)][flags(1)][timestamp(4)][field_count(1)][fields...]

Each field:
  [field_type(1)][field_len(2LE)][field_data(field_len)]
"""

import struct
import time
import os
from enum import IntEnum
from typing import Optional, List, Tuple, Dict, Any


# ── Constants ──────────────────────────────────────────────────────────

MSG_VERSION = 1
MSG_HEADER_SIZE = 8
FIELD_HEADER_SIZE = 3
MAX_FIELDS = 16
MAX_SIZE = 3800
MAX_TEXT = 200
MAX_NICKNAME = 32
MAX_TITLE = 128


# ── Message types ──────────────────────────────────────────────────────

class MsgType(IntEnum):
    TEXT = 0x01
    FILE = 0x02
    IMAGE = 0x03
    LOCATION = 0x04
    VOICE_NOTE = 0x05
    REACTION = 0x06
    ACK = 0x07
    TYPING = 0x08
    READ = 0x09
    DELETE = 0x0A
    NICKNAME = 0x0B
    CONTACT = 0x0C


# ── Message flags ──────────────────────────────────────────────────────

class MsgFlag:
    ENCRYPTED = 0x01
    SIGNED = 0x02
    PROPAGATE = 0x04
    URGENT = 0x08
    REPLY = 0x10
    GROUP = 0x20


# ── Field types ────────────────────────────────────────────────────────

class FieldType(IntEnum):
    TEXT = 0x01
    FILENAME = 0x02
    MIMETYPE = 0x03
    FILEDATA = 0x04
    LATITUDE = 0x05
    LONGITUDE = 0x06
    ALTITUDE = 0x07
    ACCURACY = 0x08
    REPLY_TO = 0x09
    REACTION = 0x0A
    MSG_ID = 0x0B
    NICKNAME = 0x0C
    DURATION = 0x0D
    THUMBNAIL = 0x0E
    CONTACT_ADDR = 0x0F
    CONTACT_PUB = 0x10
    CODEC = 0x11
    SIGNATURE = 0x12
    TITLE = 0x13


# ── Field data class ──────────────────────────────────────────────────

class Field:
    """A single TLV field within a message."""
    __slots__ = ('type', 'data')

    def __init__(self, field_type: FieldType, data: bytes):
        self.type = field_type
        self.data = data

    def __repr__(self):
        return f"Field({self.type.name}, {len(self.data)}B)"


# ── Message data class ────────────────────────────────────────────────

class Message:
    """A parsed NEXUS message."""

    def __init__(self):
        self.version: int = MSG_VERSION
        self.type: MsgType = MsgType.TEXT
        self.flags: int = 0
        self.timestamp: int = 0
        self.fields: List[Field] = []

    def find_field(self, field_type: FieldType) -> Optional[Field]:
        """Find the first field of the given type."""
        for f in self.fields:
            if f.type == field_type:
                return f
        return None

    @property
    def text(self) -> Optional[str]:
        """Get text content, or None if not present."""
        f = self.find_field(FieldType.TEXT)
        return f.data.decode('utf-8', errors='replace') if f else None

    @property
    def msg_id(self) -> Optional[bytes]:
        """Get 4-byte message ID, or None."""
        f = self.find_field(FieldType.MSG_ID)
        return f.data if f else None

    @property
    def reply_to(self) -> Optional[bytes]:
        """Get 4-byte reply-to message ID, or None."""
        f = self.find_field(FieldType.REPLY_TO)
        return f.data if f else None

    @property
    def nickname(self) -> Optional[str]:
        """Get nickname, or None."""
        f = self.find_field(FieldType.NICKNAME)
        return f.data.decode('utf-8', errors='replace') if f else None

    @property
    def title(self) -> Optional[str]:
        """Get title / subject (LXMF parity), or None."""
        f = self.find_field(FieldType.TITLE)
        return f.data.decode('utf-8', errors='replace') if f else None

    @property
    def mimetype(self) -> Optional[str]:
        """Get MIME type string, or None."""
        f = self.find_field(FieldType.MIMETYPE)
        return f.data.decode('utf-8', errors='replace') if f else None

    @property
    def reaction(self) -> Optional[str]:
        """Get reaction text, or None."""
        f = self.find_field(FieldType.REACTION)
        return f.data.decode('utf-8', errors='replace') if f else None

    def get_location(self) -> Optional[Tuple[float, float, int, int]]:
        """Get (lat, lon, alt_m, accuracy_m) or None."""
        flat = self.find_field(FieldType.LATITUDE)
        flon = self.find_field(FieldType.LONGITUDE)
        if not flat or not flon:
            return None

        lat = struct.unpack('<i', flat.data[:4])[0] / 1e7
        lon = struct.unpack('<i', flon.data[:4])[0] / 1e7

        falt = self.find_field(FieldType.ALTITUDE)
        alt = struct.unpack('<h', falt.data[:2])[0] if falt else 0

        facc = self.find_field(FieldType.ACCURACY)
        acc = facc.data[0] if facc else 0

        return (lat, lon, alt, acc)

    def get_contact(self) -> Optional[Tuple[bytes, bytes]]:
        """Get (addr_4bytes, pubkey_32bytes) or None."""
        fa = self.find_field(FieldType.CONTACT_ADDR)
        fp = self.find_field(FieldType.CONTACT_PUB)
        if not fa or not fp:
            return None
        return (fa.data, fp.data)

    @property
    def filename(self) -> Optional[str]:
        """Get filename for file/image messages."""
        f = self.find_field(FieldType.FILENAME)
        return f.data.decode('utf-8', errors='replace') if f else None

    @property
    def mimetype(self) -> Optional[str]:
        """Get MIME type."""
        f = self.find_field(FieldType.MIMETYPE)
        return f.data.decode('utf-8', errors='replace') if f else None

    @property
    def filedata(self) -> Optional[bytes]:
        """Get file data."""
        f = self.find_field(FieldType.FILEDATA)
        return f.data if f else None

    @property
    def signature(self) -> Optional[bytes]:
        """Get 64-byte signature, or None."""
        f = self.find_field(FieldType.SIGNATURE)
        return f.data if f else None

    def __repr__(self):
        return (f"Message(type={self.type.name}, flags=0x{self.flags:02x}, "
                f"ts={self.timestamp}, fields={len(self.fields)})")


# ── Parser ─────────────────────────────────────────────────────────────

def parse(data: bytes) -> Message:
    """Parse a serialized NXM message.

    Args:
        data: Raw message bytes.

    Returns:
        Parsed Message object.

    Raises:
        ValueError: If data is malformed.
    """
    if len(data) < MSG_HEADER_SIZE:
        raise ValueError(f"Message too short ({len(data)} < {MSG_HEADER_SIZE})")

    msg = Message()
    msg.version = data[0]
    if msg.version != MSG_VERSION:
        raise ValueError(f"Unknown message version {msg.version}")

    msg.type = MsgType(data[1])
    msg.flags = data[2]
    msg.timestamp = struct.unpack_from('<I', data, 3)[0]
    field_count = data[7]

    if field_count > MAX_FIELDS:
        raise ValueError(f"Too many fields ({field_count} > {MAX_FIELDS})")

    pos = MSG_HEADER_SIZE
    for _ in range(field_count):
        if pos + FIELD_HEADER_SIZE > len(data):
            raise ValueError("Truncated field header")

        ftype = FieldType(data[pos])
        flen = struct.unpack_from('<H', data, pos + 1)[0]
        pos += FIELD_HEADER_SIZE

        if pos + flen > len(data):
            raise ValueError(f"Truncated field data (need {flen}, have {len(data) - pos})")

        msg.fields.append(Field(ftype, data[pos:pos + flen]))
        pos += flen

    return msg


# ── Builder ────────────────────────────────────────────────────────────

class MessageBuilder:
    """Build a NEXUS message by adding fields."""

    def __init__(self, msg_type: MsgType, flags: int = 0):
        self._type = msg_type
        self._flags = flags
        self._fields: List[Tuple[FieldType, bytes]] = []

    def add(self, field_type: FieldType, data: bytes) -> 'MessageBuilder':
        """Add a raw field. Returns self for chaining."""
        if len(self._fields) >= MAX_FIELDS:
            raise ValueError("Too many fields")
        self._fields.append((field_type, data))
        return self

    def add_text(self, text: str) -> 'MessageBuilder':
        """Add a text field."""
        return self.add(FieldType.TEXT, text.encode('utf-8'))

    def add_msg_id(self, msg_id: Optional[bytes] = None) -> 'MessageBuilder':
        """Add a message ID (auto-generated if None)."""
        if msg_id is None:
            msg_id = generate_msg_id()
        return self.add(FieldType.MSG_ID, msg_id)

    def add_reply_to(self, reply_id: bytes) -> 'MessageBuilder':
        """Add a reply-to reference."""
        self._flags |= MsgFlag.REPLY
        return self.add(FieldType.REPLY_TO, reply_id)

    def add_nickname(self, name: str) -> 'MessageBuilder':
        """Add a nickname field."""
        encoded = name.encode('utf-8')[:MAX_NICKNAME]
        return self.add(FieldType.NICKNAME, encoded)

    def add_title(self, title: str) -> 'MessageBuilder':
        """Add a subject/title field (LXMF parity)."""
        encoded = title.encode('utf-8')[:MAX_TITLE]
        return self.add(FieldType.TITLE, encoded)

    def add_location(self, lat: float, lon: float,
                     alt_m: int = 0, accuracy_m: int = 0) -> 'MessageBuilder':
        """Add location fields (lat/lon/alt/accuracy)."""
        ilat = int(lat * 1e7)
        ilon = int(lon * 1e7)
        self.add(FieldType.LATITUDE, struct.pack('<i', ilat))
        self.add(FieldType.LONGITUDE, struct.pack('<i', ilon))
        self.add(FieldType.ALTITUDE, struct.pack('<h', alt_m))
        self.add(FieldType.ACCURACY, bytes([accuracy_m & 0xFF]))
        return self

    def add_reaction(self, text: str) -> 'MessageBuilder':
        """Add a reaction field."""
        return self.add(FieldType.REACTION, text.encode('utf-8')[:64])

    def add_file(self, filename: str, mimetype: str, data: bytes,
                 thumbnail: Optional[bytes] = None) -> 'MessageBuilder':
        """Add file fields (filename, mimetype, data, optional thumbnail)."""
        self.add(FieldType.FILENAME, filename.encode('utf-8'))
        self.add(FieldType.MIMETYPE, mimetype.encode('utf-8'))
        self.add(FieldType.FILEDATA, data)
        if thumbnail:
            self.add(FieldType.THUMBNAIL, thumbnail)
        return self

    def add_contact(self, addr: bytes, pubkey: bytes) -> 'MessageBuilder':
        """Add a contact share (4-byte addr + 32-byte pubkey)."""
        self.add(FieldType.CONTACT_ADDR, addr[:4])
        self.add(FieldType.CONTACT_PUB, pubkey[:32])
        return self

    def build(self) -> bytes:
        """Serialize the message to bytes."""
        ts = int(time.time()) & 0xFFFFFFFF
        header = struct.pack('<BBBI',
                             MSG_VERSION,
                             int(self._type),
                             self._flags,
                             ts)
        header += bytes([len(self._fields)])

        body = b''
        for ftype, fdata in self._fields:
            body += struct.pack('<BH', int(ftype), len(fdata))
            body += fdata

        result = header + body
        if len(result) > MAX_SIZE:
            raise ValueError(f"Message too large ({len(result)} > {MAX_SIZE})")
        return result


# ── Convenience builders ───────────────────────────────────────────────

def generate_msg_id() -> bytes:
    """Generate a 4-byte unique message ID."""
    ts = int(time.time()) & 0xFFFF
    rnd = os.urandom(2)
    return struct.pack('<H', ts) + rnd


def build_text(text: str, reply_to: Optional[bytes] = None) -> bytes:
    """Build a text message."""
    b = MessageBuilder(MsgType.TEXT)
    b.add_msg_id()
    b.add_text(text)
    if reply_to:
        b.add_reply_to(reply_to)
    return b.build()


def build_ack(msg_id: bytes) -> bytes:
    """Build a delivery ACK."""
    return MessageBuilder(MsgType.ACK).add(FieldType.MSG_ID, msg_id).build()


def build_reaction(target_id: bytes, reaction: str) -> bytes:
    """Build a reaction message."""
    return (MessageBuilder(MsgType.REACTION)
            .add(FieldType.MSG_ID, target_id)
            .add_reaction(reaction)
            .build())


def build_location(lat: float, lon: float,
                   alt_m: int = 0, accuracy_m: int = 0) -> bytes:
    """Build a location share message."""
    return (MessageBuilder(MsgType.LOCATION)
            .add_msg_id()
            .add_location(lat, lon, alt_m, accuracy_m)
            .build())


def build_nickname(name: str) -> bytes:
    """Build a nickname announcement."""
    return MessageBuilder(MsgType.NICKNAME).add_nickname(name).build()


def build_contact(addr: bytes, pubkey: bytes) -> bytes:
    """Build a contact share."""
    return (MessageBuilder(MsgType.CONTACT)
            .add_contact(addr, pubkey)
            .build())


def build_image(filename: str, data: bytes,
                thumbnail: Optional[bytes] = None) -> bytes:
    """Build an image message."""
    return (MessageBuilder(MsgType.IMAGE)
            .add_msg_id()
            .add_file(filename, "image/jpeg", data, thumbnail)
            .build())


def build_read(msg_id: bytes) -> bytes:
    """Build a read receipt."""
    return MessageBuilder(MsgType.READ).add(FieldType.MSG_ID, msg_id).build()


def build_file(filename: str, mimetype: str, data: bytes) -> bytes:
    """Build a file transfer message."""
    return (MessageBuilder(MsgType.FILE)
            .add_msg_id()
            .add_file(filename, mimetype, data)
            .build())


# ── Signature operations (via C library) ──────────────────────────────

def sign(data: bytes, sign_secret: bytes) -> bytes:
    """Sign an NXM message using Ed25519 via libnexus.

    Args:
        data: Serialized NXM message bytes.
        sign_secret: 64-byte Ed25519 secret key.

    Returns:
        Signed message bytes (original + signature field).

    Raises:
        ValueError: If signing fails.
    """
    import ctypes
    from ._ffi import lib

    buf_cap = len(data) + 67  # 3-byte header + 64-byte sig
    buf = ctypes.create_string_buffer(data, buf_cap)
    secret = (ctypes.c_uint8 * 64)(*sign_secret)

    lib.nx_msg_sign.restype = ctypes.c_size_t
    result_len = lib.nx_msg_sign(buf, ctypes.c_size_t(len(data)),
                                 ctypes.c_size_t(buf_cap), secret)
    if result_len == 0:
        raise ValueError("nx_msg_sign failed")
    return buf.raw[:result_len]


def verify(data: bytes, sign_pubkey: bytes) -> bool:
    """Verify an Ed25519 signature on an NXM message via libnexus.

    Args:
        data: Signed NXM message bytes.
        sign_pubkey: 32-byte Ed25519 public key.

    Returns:
        True if valid, False if invalid or not signed.
    """
    import ctypes
    from ._ffi import lib

    buf = (ctypes.c_uint8 * len(data))(*data)
    pubkey = (ctypes.c_uint8 * 32)(*sign_pubkey)

    lib.nx_msg_verify.restype = ctypes.c_int
    ret = lib.nx_msg_verify(buf, ctypes.c_size_t(len(data)), pubkey)
    return ret == 0  # NX_OK
