# NEXUS Pillar Server

`pillard` is a dedicated public-internet relay for the NEXUS mesh. A Pillar is
a NEXUS node with a reachable public IP that:

- Accepts inbound TCP from NAT-bound clients (phones, gateways, home nodes)
- Bridges mesh traffic between every connected peer (Reticulum-style hub)
- Stores-and-forwards messages for offline destinations (VAULT-tier mailbox)
- Optionally peers with other Pillars to form a redundant public backbone

## Build

```
cmake --build build --target pillard
```

Binary lands at `build/app/pillard`.

## Quick start

```
./build/app/pillard -f -p 4242
```

First run generates `~/.nexus/pillar.identity` (or `/var/lib/nexus/pillar.identity`
if writable). Subsequent runs reuse it, so the node's short address is stable.

## Flags

| Flag | Meaning |
|------|---------|
| `-p PORT` | TCP listen port (default 4242) |
| `-i FILE` | Identity path |
| `-c HOST:PORT` | Peer with another Pillar (repeatable, up to 16) |
| `-m` | Also enable UDP multicast (LAN-only; off by default) |
| `-V` | Run as VAULT (bigger mailbox) instead of PILLAR |
| `-f` | Foreground -- required under systemd Type=simple |
| `-v` | Verbose debug logging |

## Signals

- `SIGTERM` / `SIGINT` -- graceful shutdown
- `SIGHUP` -- re-announce on all transports
- `SIGUSR1` -- dump stats line to the log

## Systemd

Install the unit:

```
sudo cp scripts/nexus-pillar.service /etc/systemd/system/
sudo cp build/app/pillard /usr/local/bin/
sudo useradd --system --home /var/lib/nexus --shell /usr/sbin/nologin nexus
sudo install -d -o nexus -g nexus -m 0700 /var/lib/nexus
sudo systemctl daemon-reload
sudo systemctl enable --now nexus-pillar.service
journalctl -u nexus-pillar -f
```

Tune `ExecStart=` in the unit to add peers or change port.

## Federating Pillars

To form a backbone between two Pillars, point them at each other:

```
# On pillar-a.example:
pillard -f -p 4242 -c pillar-b.example:4242

# On pillar-b.example:
pillard -f -p 4242 -c pillar-a.example:4242
```

Messages stored on either node are re-announced via mesh routing to the other,
so an offline recipient can connect to whichever Pillar their client prefers.

## Client config (Android)

In the Android app, Settings -> Pillar Nodes -> add `your-pillar.example:4242`.
The client dials out, so the Pillar only needs an inbound TCP port open.
