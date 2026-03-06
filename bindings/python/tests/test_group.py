"""Group tests (4 tests)."""

import unittest

from nexus import Node, NexusError, registry_init, create_pipe_pair

ROLE = 0  # LEAF — no flood forwarding
GROUP_ID = b"\x01\x02\x03\x04"
GROUP_KEY = bytes(range(32))


def _setup_group():
    """Set up two nodes with a shared group."""
    registry_init()
    create_pipe_pair()

    group_msgs = []

    def on_group_alice(gid, src, data):
        group_msgs.append(("alice", gid, src, data))

    def on_group_bob(gid, src, data):
        group_msgs.append(("bob", gid, src, data))

    alice = Node(role=ROLE, on_group=on_group_alice)
    bob = Node(role=ROLE, on_group=on_group_bob)

    # Exchange announcements
    alice.announce()
    bob.drain()
    bob.announce()
    alice.drain()

    # Both create the group
    alice.group_create(GROUP_ID, GROUP_KEY)
    bob.group_create(GROUP_ID, GROUP_KEY)

    # Add each other as members
    alice.group_add_member(GROUP_ID, bob.short_addr)
    bob.group_add_member(GROUP_ID, alice.short_addr)

    return alice, bob, group_msgs


class TestGroup(unittest.TestCase):
    def test_group_send_recv(self):
        alice, bob, group_msgs = _setup_group()

        alice.group_send(GROUP_ID, b"group hello")
        bob.drain()

        self.assertEqual(len(group_msgs), 1)
        self.assertEqual(group_msgs[0][0], "bob")
        self.assertEqual(group_msgs[0][3], b"group hello")

        alice.stop()
        bob.stop()

    def test_group_bidirectional(self):
        alice, bob, group_msgs = _setup_group()

        alice.group_send(GROUP_ID, b"from alice")
        bob.drain()

        bob.group_send(GROUP_ID, b"from bob")
        alice.drain()

        self.assertEqual(len(group_msgs), 2)
        self.assertEqual(group_msgs[0][3], b"from alice")
        self.assertEqual(group_msgs[1][3], b"from bob")

        alice.stop()
        bob.stop()

    def test_group_unknown(self):
        registry_init()
        create_pipe_pair()
        alice = Node(role=ROLE)

        with self.assertRaises(NexusError) as ctx:
            alice.group_send(b"\xff\xff\xff\xff", b"test")
        self.assertEqual(ctx.exception.code, -6)

        alice.stop()

    def test_group_non_member(self):
        registry_init()
        create_pipe_pair()

        group_msgs = []

        def on_group(gid, src, data):
            group_msgs.append(data)

        alice = Node(role=ROLE)
        bob = Node(role=ROLE, on_group=on_group)

        alice.announce()
        bob.drain()

        alice.group_create(GROUP_ID, GROUP_KEY)
        alice.group_add_member(GROUP_ID, bob.short_addr)

        alice.group_send(GROUP_ID, b"secret")
        bob.drain()

        self.assertEqual(len(group_msgs), 0)

        alice.stop()
        bob.stop()


if __name__ == "__main__":
    unittest.main()
