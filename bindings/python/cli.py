#!/usr/bin/env python3
"""NEXUS Protocol -- CLI tool for identity and daemon control."""

import argparse
import socket
import sys

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
    else:
        parser.print_help()
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()
