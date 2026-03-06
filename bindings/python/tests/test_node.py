"""Node tests (5 tests)."""

import unittest

from nexus import Node, registry_init, create_pipe_pair

# LEAF role (0) = no forwarding. Avoids flood-forward filling the ring buffer.
ROLE = 0


class TestNode(unittest.TestCase):
    def test_init_stop(self):
        registry_init()
        node = Node(role=ROLE)
        node.stop()

    def test_poll_empty(self):
        registry_init()
        node = Node(role=ROLE)
        rc = node.poll(0)
        self.assertEqual(rc, 0)
        node.stop()

    def test_send_raw_recv(self):
        registry_init()
        create_pipe_pair()

        received = []

        def on_data(src, data):
            received.append((src, data))

        alice = Node(role=ROLE)
        bob = Node(role=ROLE, on_data=on_data)

        alice.send_raw(bob.short_addr, b"hello")
        bob.drain()

        self.assertEqual(len(received), 1)
        self.assertEqual(received[0][0], alice.short_addr)
        self.assertEqual(received[0][1], b"hello")

        alice.stop()
        bob.stop()

    def test_announce_neighbor(self):
        registry_init()
        create_pipe_pair()

        neighbors = []

        def on_neighbor(addr, role):
            neighbors.append((addr, role))

        alice = Node(role=ROLE)
        bob = Node(role=ROLE, on_neighbor=on_neighbor)

        alice.announce()
        bob.drain()

        self.assertEqual(len(neighbors), 1)
        self.assertEqual(neighbors[0][0], alice.short_addr)

        alice.stop()
        bob.stop()

    def test_node_identity(self):
        registry_init()
        node = Node(role=ROLE)
        addr = node.short_addr
        self.assertEqual(len(addr), 4)
        self.assertNotEqual(addr, b"\x00\x00\x00\x00")
        node.stop()


if __name__ == "__main__":
    unittest.main()
