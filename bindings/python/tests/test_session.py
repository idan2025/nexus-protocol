"""Session tests (4 tests).

Uses direct neighbor injection (like the C tests) instead of announce
exchange to avoid flaky interactions with the shared transport registry.
"""

import unittest

from nexus import Node, NexusError, registry_init, create_pipe_pair, set_active

ROLE = 0  # LEAF -- no flood forwarding


def _setup_pair():
    """Create two nodes that know each other as neighbors."""
    registry_init()
    pipe_a, pipe_b = create_pipe_pair()

    session_msgs = []

    def on_session_bob(src, data):
        session_msgs.append(("bob", src, data))

    def on_session_alice(src, data):
        session_msgs.append(("alice", src, data))

    alice = Node(role=ROLE, on_session=on_session_alice)
    bob = Node(role=ROLE, on_session=on_session_bob)

    # Inject each other as neighbors (avoids unreliable announce exchange)
    alice.inject_neighbor(bob)
    bob.inject_neighbor(alice)

    return alice, bob, session_msgs, pipe_a, pipe_b


def _handshake():
    """Set up two nodes with an established session."""
    alice, bob, session_msgs, pipe_a, pipe_b = _setup_pair()

    # Alice starts session -> INIT
    set_active(pipe_a, True); set_active(pipe_b, False)
    alice.session_start(bob.short_addr)
    # Bob receives INIT, sends ACK
    set_active(pipe_a, False); set_active(pipe_b, True)
    bob.drain()
    # Alice receives ACK, completes handshake
    set_active(pipe_a, True); set_active(pipe_b, False)
    alice.drain()

    # Re-activate both
    set_active(pipe_a, True); set_active(pipe_b, True)

    return alice, bob, session_msgs, pipe_a, pipe_b


class TestSession(unittest.TestCase):
    def test_session_handshake(self):
        alice, bob, _, pipe_a, pipe_b = _handshake()
        alice.stop()
        bob.stop()

    def test_session_send_recv(self):
        alice, bob, session_msgs, pipe_a, pipe_b = _handshake()

        set_active(pipe_a, True); set_active(pipe_b, False)
        alice.send_session(bob.short_addr, b"secret message")
        set_active(pipe_a, False); set_active(pipe_b, True)
        bob.drain()

        self.assertEqual(len(session_msgs), 1)
        self.assertEqual(session_msgs[0][0], "bob")
        self.assertEqual(session_msgs[0][1], alice.short_addr)
        self.assertEqual(session_msgs[0][2], b"secret message")

        alice.stop()
        bob.stop()

    def test_session_bidirectional(self):
        alice, bob, session_msgs, pipe_a, pipe_b = _handshake()

        set_active(pipe_a, True); set_active(pipe_b, False)
        alice.send_session(bob.short_addr, b"from alice")
        set_active(pipe_a, False); set_active(pipe_b, True)
        bob.drain()

        set_active(pipe_a, False); set_active(pipe_b, True)
        bob.send_session(alice.short_addr, b"from bob")
        set_active(pipe_a, True); set_active(pipe_b, False)
        alice.drain()

        self.assertEqual(len(session_msgs), 2)
        self.assertEqual(session_msgs[0][2], b"from alice")
        self.assertEqual(session_msgs[1][2], b"from bob")

        alice.stop()
        bob.stop()

    def test_session_no_neighbor(self):
        registry_init()
        create_pipe_pair()
        alice = Node(role=ROLE)
        bob = Node(role=ROLE)

        with self.assertRaises(NexusError) as ctx:
            alice.session_start(bob.short_addr)
        self.assertEqual(ctx.exception.code, -6)

        alice.stop()
        bob.stop()


if __name__ == "__main__":
    unittest.main()
