"""Identity tests (4 tests)."""

import os
import tempfile
import unittest

from nexus import Identity


class TestIdentity(unittest.TestCase):
    def test_generate(self):
        ident = Identity()
        self.assertEqual(len(ident.short_addr), 4)
        self.assertNotEqual(ident.short_addr, b"\x00\x00\x00\x00")
        ident.wipe()

    def test_unique(self):
        a = Identity()
        b = Identity()
        self.assertNotEqual(a.short_addr, b.short_addr)
        a.wipe()
        b.wipe()

    def test_wipe(self):
        ident = Identity()
        ident.wipe()
        self.assertEqual(bytes(ident._raw.sign_secret), b"\x00" * 64)

    def test_save_load(self):
        ident = Identity()
        addr = ident.short_addr
        pub = ident.sign_public
        fd, path = tempfile.mkstemp()
        os.close(fd)
        try:
            ident.save(path)
            loaded = Identity.load(path)
            self.assertEqual(loaded.short_addr, addr)
            self.assertEqual(loaded.sign_public, pub)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
