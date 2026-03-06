#!/usr/bin/env python3
"""NEXUS Protocol -- CLI tool for identity management."""

import argparse
import sys

from nexus import Identity


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

    args = parser.parse_args()

    if args.command == "identity":
        if args.action == "generate":
            cmd_generate(args)
        elif args.action == "save":
            cmd_save(args)
        elif args.action == "show":
            cmd_show(args)
        else:
            id_parser.print_help()
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
