#!/usr/bin/env python3
"""NEXUS Protocol -- CLI tool for identity and daemon control."""

import argparse
import socket
import sys
import time

from nexus import Identity

DEFAULT_SOCK = "/tmp/nexusd.sock"


def _uds_cmd(sock_path, line, timeout=5.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(sock_path)
    except (FileNotFoundError, ConnectionRefusedError) as e:
        print(f"error: cannot connect to {sock_path}: {e}", file=sys.stderr)
        return 1
    try:
        s.sendall((line.rstrip("\n") + "\n").encode())
        buf = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
        sys.stdout.write(buf.decode("utf-8", errors="replace"))
        return 0
    finally:
        s.close()


def cmd_generate(args):
    ident = Identity()
    print(f"short_addr: {ident.short_addr_hex}")
    print(f"sign_pub:   {ident.sign_public.hex()}")
    print(f"x25519_pub: {ident.x25519_public.hex()}")
    ident.wipe()


def cmd_save(args):
    ident = Identity()
    ident.save(args.file)
    print(f"Identity saved to {args.file}")
    print(f"short_addr: {ident.short_addr_hex}")
    ident.wipe()


def cmd_show(args):
    ident = Identity.load(args.file)
    print(f"short_addr: {ident.short_addr_hex}")
    print(f"sign_pub:   {ident.sign_public.hex()}")
    print(f"x25519_pub: {ident.x25519_public.hex()}")


def cmd_send(args):
    peer = args.peer.strip().lower()
    if len(peer) != 8 or not all(c in "0123456789abcdef" for c in peer):
        print("error: peer must be 8 hex chars", file=sys.stderr)
        return 1
    text = args.text
    return _uds_cmd(args.sock, f"send {peer} {text}")


def cmd_inbox(args):
    return _uds_cmd(args.sock, "inbox")


def cmd_status(args):
    return _uds_cmd(args.sock, "status")


def cmd_announce(args):
    return _uds_cmd(args.sock, "announce")


def cmd_pillar_probe(args):
    """TCP probe of a pillard endpoint: measures RTT and reads version/peer info."""
    target = args.target
    if ":" not in target:
        print(f"error: target must be host:port", file=sys.stderr)
        return 1
    host, _, port_str = target.rpartition(":")
    try:
        port = int(port_str)
    except ValueError:
        print(f"error: invalid port '{port_str}'", file=sys.stderr)
        return 1

    # Measure TCP connect RTT
    t0 = time.monotonic()
    try:
        s = socket.create_connection((host, port), timeout=args.timeout)
    except OSError as e:
        print(f"error: cannot connect to {host}:{port}: {e}", file=sys.stderr)
        return 1
    rtt_ms = (time.monotonic() - t0) * 1000

    # Send NEXUS probe: a raw NEXUS ANNOUNCE request is complex, so we use
    # the pillard admin HTTP interface if -M port is known, or just read the
    # banner.  pillard closes unrecognised connections immediately; we read
    # whatever it sends back (typically nothing) and report connectivity.
    try:
        s.settimeout(args.timeout)
        # pillard speaks raw NXM framing over TCP, not a text banner.
        # Send a minimal ping by writing a zero-length frame and reading
        # back. If the server drops us that still confirms reachability.
        s.sendall(b"\x00\x00")  # zero-length NXM frame
        banner = b""
        try:
            chunk = s.recv(64)
            if chunk:
                banner = chunk
        except (socket.timeout, OSError):
            pass
    finally:
        s.close()

    print(f"host:    {host}")
    print(f"port:    {port}")
    print(f"rtt_ms:  {rtt_ms:.1f}")
    print(f"status:  reachable")
    if banner:
        print(f"banner:  {banner[:32].hex()}")

    # If an admin HTTP port was provided, query /admin for peer count / version
    if args.admin_port:
        try:
            hs = socket.create_connection((host, args.admin_port), timeout=args.timeout)
            hs.sendall(b"GET /admin HTTP/1.0\r\nHost: " +
                       host.encode() + b"\r\n\r\n")
            resp = b""
            hs.settimeout(2.0)
            try:
                while True:
                    chunk = hs.recv(4096)
                    if not chunk:
                        break
                    resp += chunk
                    if len(resp) > 32768:
                        break
            except (socket.timeout, OSError):
                pass
            hs.close()
            # extract key fields from the HTML
            text = resp.decode("utf-8", errors="replace")
            for line in text.splitlines():
                if "Neighbors" in line or "Mailbox" in line or "Uptime" in line:
                    # strip HTML tags for a rough plain-text extract
                    import re
                    clean = re.sub(r"<[^>]+>", " ", line).split()
                    if clean:
                        print(f"admin:   {' '.join(clean)}")
        except OSError as e:
            print(f"admin:   unreachable ({e})")

    return 0


def main():
    parser = argparse.ArgumentParser(
        prog="nexus-cli",
        description="NEXUS Protocol CLI",
    )
    sub = parser.add_subparsers(dest="command")

    # identity subcommands
    id_parser = sub.add_parser("identity", help="Identity management")
    id_sub = id_parser.add_subparsers(dest="action")

    id_sub.add_parser("generate", help="Generate and print a new identity")

    save_parser = id_sub.add_parser("save", help="Generate and save identity to file")
    save_parser.add_argument("file", help="Output file path")

    show_parser = id_sub.add_parser("show", help="Load and display identity from file")
    show_parser.add_argument("file", help="Identity file path")

    # daemon control subcommands (talk to nexusd over UDS)
    send_parser = sub.add_parser("send", help="Send a text message via nexusd")
    send_parser.add_argument("--sock", default=DEFAULT_SOCK, help="nexusd UDS path")
    send_parser.add_argument("peer", help="Peer 8-hex short address")
    send_parser.add_argument("text", help="Message text")

    inbox_parser = sub.add_parser("inbox", help="List stored anchor mailbox slots")
    inbox_parser.add_argument("--sock", default=DEFAULT_SOCK, help="nexusd UDS path")

    status_parser = sub.add_parser("status", help="Show nexusd node status")
    status_parser.add_argument("--sock", default=DEFAULT_SOCK, help="nexusd UDS path")

    announce_parser = sub.add_parser("announce", help="Send an announce now")
    announce_parser.add_argument("--sock", default=DEFAULT_SOCK, help="nexusd UDS path")

    probe_parser = sub.add_parser(
        "pillar-probe",
        help="Probe a pillard endpoint: TCP RTT + reachability",
    )
    probe_parser.add_argument("target", help="Pillar address as host:port")
    probe_parser.add_argument(
        "--timeout", type=float, default=5.0, help="Connection timeout (seconds)"
    )
    probe_parser.add_argument(
        "--admin-port", type=int, default=0, metavar="PORT",
        help="pillard -A port for peer/uptime info from /admin",
    )

    args = parser.parse_args()

    rc = 0
    if args.command == "identity":
        if args.action == "generate":
            cmd_generate(args)
        elif args.action == "save":
            cmd_save(args)
        elif args.action == "show":
            cmd_show(args)
        else:
            id_parser.print_help()
    elif args.command == "send":
        rc = cmd_send(args)
    elif args.command == "inbox":
        rc = cmd_inbox(args)
    elif args.command == "status":
        rc = cmd_status(args)
    elif args.command == "announce":
        rc = cmd_announce(args)
    elif args.command == "pillar-probe":
        rc = cmd_pillar_probe(args)
    else:
        parser.print_help()
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()
