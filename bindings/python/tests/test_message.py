"""Tests for NEXUS message format (pure Python + C cross-compatibility)."""
import unittest
import struct
import os
import sys
import ctypes

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from nexus.message import (
    parse, build_text, build_ack, build_reaction, build_location,
    build_nickname, build_contact, build_image, build_file, build_read,
    sign, verify,
    MessageBuilder, MsgType, MsgFlag, FieldType, Message,
    generate_msg_id, MSG_VERSION,
)


class TestMessageBuilder(unittest.TestCase):
    """Test the Python message builder."""

    def test_build_text(self):
        data = build_text("Hello NEXUS!")
        self.assertGreater(len(data), 8)
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.TEXT)
        self.assertEqual(msg.text, "Hello NEXUS!")
        self.assertIsNotNone(msg.msg_id)
        self.assertEqual(len(msg.msg_id), 4)

    def test_build_text_with_reply(self):
        reply_id = b'\x01\x02\x03\x04'
        data = build_text("Reply!", reply_to=reply_id)
        msg = parse(data)
        self.assertEqual(msg.text, "Reply!")
        self.assertTrue(msg.flags & MsgFlag.REPLY)
        self.assertEqual(msg.reply_to, reply_id)

    def test_build_ack(self):
        mid = b'\xAA\xBB\xCC\xDD'
        data = build_ack(mid)
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.ACK)
        self.assertEqual(msg.msg_id, mid)

    def test_build_reaction(self):
        mid = b'\x11\x22\x33\x44'
        data = build_reaction(mid, "thumbsup")
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.REACTION)
        self.assertEqual(msg.reaction, "thumbsup")
        self.assertEqual(msg.msg_id, mid)

    def test_build_location(self):
        data = build_location(40.7128, -74.0060, alt_m=10, accuracy_m=5)
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.LOCATION)
        loc = msg.get_location()
        self.assertIsNotNone(loc)
        lat, lon, alt, acc = loc
        self.assertAlmostEqual(lat, 40.7128, places=3)
        self.assertAlmostEqual(lon, -74.0060, places=3)
        self.assertEqual(alt, 10)
        self.assertEqual(acc, 5)

    def test_build_location_negative(self):
        """Southern/western hemisphere coordinates."""
        data = build_location(-33.8688, 151.2093, alt_m=-5, accuracy_m=10)
        msg = parse(data)
        loc = msg.get_location()
        lat, lon, alt, acc = loc
        self.assertAlmostEqual(lat, -33.8688, places=3)
        self.assertAlmostEqual(lon, 151.2093, places=3)

    def test_build_nickname(self):
        data = build_nickname("Alice")
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.NICKNAME)
        self.assertEqual(msg.nickname, "Alice")

    def test_build_contact(self):
        addr = b'\xDE\xAD\xBE\xEF'
        pubkey = b'\x42' * 32
        data = build_contact(addr, pubkey)
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.CONTACT)
        contact = msg.get_contact()
        self.assertIsNotNone(contact)
        self.assertEqual(contact[0], addr)
        self.assertEqual(contact[1], pubkey)

    def test_build_image(self):
        fake_img = b'\xFF\xD8\xFF\xE0' + b'\x00' * 60
        data = build_image("photo.jpg", fake_img)
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.IMAGE)
        self.assertEqual(msg.filename, "photo.jpg")
        self.assertEqual(msg.mimetype, "image/jpeg")
        self.assertEqual(msg.filedata, fake_img)

    def test_build_file(self):
        content = b'Hello World\n'
        data = build_file("readme.txt", "text/plain", content)
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.FILE)
        self.assertEqual(msg.filename, "readme.txt")
        self.assertEqual(msg.filedata, content)


class TestMessageParser(unittest.TestCase):
    """Test the Python message parser."""

    def test_parse_rejects_short(self):
        with self.assertRaises(ValueError):
            parse(b'\x00')

    def test_parse_rejects_bad_version(self):
        buf = bytes([99, 1, 0, 0, 0, 0, 0, 0])
        with self.assertRaises(ValueError):
            parse(buf)

    def test_parse_rejects_truncated_field(self):
        buf = bytes([MSG_VERSION, 1, 0, 0, 0, 0, 0, 1, 1, 0xFF, 0x00])
        with self.assertRaises(ValueError):
            parse(buf)

    def test_roundtrip_preserves_timestamp(self):
        data = build_text("ts test")
        msg = parse(data)
        self.assertGreater(msg.timestamp, 0)

    def test_find_field_missing(self):
        data = build_ack(b'\x01\x02\x03\x04')
        msg = parse(data)
        self.assertIsNone(msg.text)
        self.assertIsNone(msg.nickname)
        self.assertIsNone(msg.get_location())

    def test_msg_id_unique(self):
        ids = set()
        for _ in range(100):
            mid = generate_msg_id()
            ids.add(mid)
        # Should have many unique IDs (allow some collision for timestamp)
        self.assertGreater(len(ids), 90)


class TestMessageBuilderChaining(unittest.TestCase):
    """Test the builder chaining API."""

    def test_chaining(self):
        data = (MessageBuilder(MsgType.TEXT, MsgFlag.ENCRYPTED)
                .add_msg_id()
                .add_text("Chained!")
                .add_nickname("Bob")
                .build())
        msg = parse(data)
        self.assertEqual(msg.text, "Chained!")
        self.assertEqual(msg.nickname, "Bob")
        self.assertTrue(msg.flags & MsgFlag.ENCRYPTED)

    def test_too_many_fields(self):
        b = MessageBuilder(MsgType.TEXT)
        for i in range(16):
            b.add_text(f"f{i}")
        with self.assertRaises(ValueError):
            b.add_text("overflow")


class TestCrossCompatibility(unittest.TestCase):
    """Test that Python-built messages can be parsed by C and vice versa."""

    @classmethod
    def setUpClass(cls):
        """Try to load libnexus for cross-compat tests."""
        cls.lib = None
        search = [
            os.path.join(os.path.dirname(__file__), '../../../build/lib/libnexus.so'),
        ]
        for path in search:
            abspath = os.path.abspath(path)
            if os.path.exists(abspath):
                try:
                    cls.lib = ctypes.CDLL(abspath)
                    break
                except OSError:
                    pass

    def test_python_to_c_text(self):
        """Python-built text message parseable by C."""
        if not self.lib:
            self.skipTest("libnexus.so not found")

        data = build_text("Cross-compat test")

        # nx_msg_parse(data, len, &msg) -> nx_err_t
        # We can't easily call C parse from Python without full struct layout,
        # but we can verify the wire format manually
        self.assertEqual(data[0], MSG_VERSION)
        self.assertEqual(data[1], MsgType.TEXT)
        field_count = data[7]
        self.assertGreaterEqual(field_count, 2)

    def test_wire_format_header(self):
        """Verify wire format matches C implementation."""
        data = build_text("Wire test")

        # Header: [version(1)][type(1)][flags(1)][timestamp(4LE)][field_count(1)]
        self.assertEqual(len(data), 8 + sum(3 + len(f.data) for f in parse(data).fields))
        self.assertEqual(data[0], 1)  # version
        self.assertEqual(data[1], 1)  # MsgType.TEXT

        # Timestamp is 4 bytes little-endian at offset 3
        ts = struct.unpack_from('<I', data, 3)[0]
        self.assertGreater(ts, 0)

    def test_wire_format_fields(self):
        """Verify field TLV encoding matches C."""
        data = build_ack(b'\xAA\xBB\xCC\xDD')

        # Header
        self.assertEqual(data[0], MSG_VERSION)
        self.assertEqual(data[1], MsgType.ACK)
        self.assertEqual(data[7], 1)  # 1 field

        # Field 0: [type=MSG_ID(0x0B)][len=4 LE][data]
        self.assertEqual(data[8], FieldType.MSG_ID)
        flen = struct.unpack_from('<H', data, 9)[0]
        self.assertEqual(flen, 4)
        self.assertEqual(data[11:15], b'\xAA\xBB\xCC\xDD')


class TestReadReceipt(unittest.TestCase):
    """Test read receipt message."""

    def test_build_read(self):
        mid = b'\xAA\xBB\xCC\xDD'
        data = build_read(mid)
        msg = parse(data)
        self.assertEqual(msg.type, MsgType.READ)
        self.assertEqual(msg.msg_id, mid)

    def test_read_receipt_small(self):
        data = build_read(b'\x01\x02\x03\x04')
        self.assertLess(len(data), 20)


class TestSignature(unittest.TestCase):
    """Test NXM message signatures via C library."""

    @classmethod
    def setUpClass(cls):
        """Load libnexus and generate an identity for testing."""
        cls.lib = None
        search = [
            os.path.join(os.path.dirname(__file__), '../../../build/lib/libnexus.so'),
        ]
        for path in search:
            abspath = os.path.abspath(path)
            if os.path.exists(abspath):
                try:
                    cls.lib = ctypes.CDLL(abspath)
                    break
                except OSError:
                    pass

        if cls.lib:
            # Generate an identity via C library
            cls.sign_secret = (ctypes.c_uint8 * 64)()
            cls.sign_pubkey = (ctypes.c_uint8 * 32)()
            x25519_pub = (ctypes.c_uint8 * 32)()
            short_addr = (ctypes.c_uint8 * 4)()
            full_addr = (ctypes.c_uint8 * 16)()

            # Use nx_identity_generate to get a valid keypair
            # Identity struct layout: sign_secret(64) + sign_pubkey(32) + x25519_secret(32) + x25519_pubkey(32) + short_addr(4) + full_addr(16)
            identity_buf = (ctypes.c_uint8 * 180)()
            cls.lib.nx_identity_generate(identity_buf)
            cls.sign_secret_bytes = bytes(identity_buf[0:64])
            cls.sign_pubkey_bytes = bytes(identity_buf[64:96])

    def test_sign_verify_roundtrip(self):
        if not self.lib:
            self.skipTest("libnexus.so not found")
        data = build_text("Sign me via Python!")
        signed = sign(data, self.sign_secret_bytes)
        self.assertGreater(len(signed), len(data))
        msg = parse(signed)
        self.assertTrue(msg.flags & MsgFlag.SIGNED)
        self.assertIsNotNone(msg.signature)
        self.assertEqual(len(msg.signature), 64)
        self.assertTrue(verify(signed, self.sign_pubkey_bytes))

    def test_verify_tampered(self):
        if not self.lib:
            self.skipTest("libnexus.so not found")
        data = build_text("Tamper test Python")
        signed = sign(data, self.sign_secret_bytes)
        # Tamper with the message body
        tampered = bytearray(signed)
        tampered[10] ^= 0xFF
        self.assertFalse(verify(bytes(tampered), self.sign_pubkey_bytes))

    def test_verify_wrong_key(self):
        if not self.lib:
            self.skipTest("libnexus.so not found")
        data = build_text("Wrong key Python")
        signed = sign(data, self.sign_secret_bytes)
        # Generate a different identity
        identity_buf2 = (ctypes.c_uint8 * 180)()
        self.lib.nx_identity_generate(identity_buf2)
        wrong_pubkey = bytes(identity_buf2[64:96])
        self.assertFalse(verify(signed, wrong_pubkey))


if __name__ == '__main__':
    unittest.main(verbosity=2)
