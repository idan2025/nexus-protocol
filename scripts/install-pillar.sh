#!/usr/bin/env bash
# install-pillar.sh -- one-shot installer for the NEXUS Pillar Server.
#
# Downloads the latest GitHub release tarball, installs the `pillard`
# binary to /usr/local/bin, sets up a dedicated `nexus` system user with
# data dir under /var/lib/nexus, and (optionally) enables a systemd
# service that listens on TCP/4242 by default.
#
# Usage (must run as root):
#   curl -fsSL https://raw.githubusercontent.com/idan2025/nexus-protocol/main/scripts/install-pillar.sh | sudo bash
#
# Environment overrides:
#   PILLAR_PORT=4242             TCP port the pillar listens on
#   PILLAR_TAG=v0.6.8            Pin a specific release (default: latest)
#   PILLAR_NO_SERVICE=1          Skip systemd unit install/enable
#   PILLAR_REPO=idan2025/nexus-protocol
#
# Idempotent: re-running upgrades the binary in place.

set -euo pipefail

REPO="${PILLAR_REPO:-idan2025/nexus-protocol}"
TAG="${PILLAR_TAG:-}"
PORT="${PILLAR_PORT:-4242}"
NO_SERVICE="${PILLAR_NO_SERVICE:-0}"

if [ "$(id -u)" -ne 0 ]; then
    echo "error: this installer must run as root (try: sudo bash $0)" >&2
    exit 1
fi

# --- Detect arch ---
arch="$(uname -m)"
case "$arch" in
    x86_64|amd64) asset_arch="x64" ;;
    *)
        echo "error: unsupported architecture '$arch' (only x86_64 release tarballs exist today)" >&2
        echo "       you can build from source: cmake --build build --target pillard" >&2
        exit 1
        ;;
esac

# --- Resolve release tag ---
if [ -z "$TAG" ]; then
    echo ">> Resolving latest release of $REPO..."
    TAG="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
           | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -1)"
    if [ -z "$TAG" ]; then
        echo "error: could not resolve latest release tag" >&2
        exit 1
    fi
fi
echo ">> Installing $REPO $TAG ($asset_arch)"

# --- Download tarball ---
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
ASSET="nexus-linux-${asset_arch}.tar.gz"
URL="https://github.com/$REPO/releases/download/$TAG/$ASSET"
echo ">> Fetching $URL"
curl -fsSL "$URL" -o "$TMP/$ASSET"

# --- Extract ---
mkdir -p "$TMP/x"
tar -xzf "$TMP/$ASSET" -C "$TMP/x"
if [ ! -x "$TMP/x/bin/pillard" ]; then
    echo "error: tarball does not contain bin/pillard" >&2
    exit 1
fi

# --- Install binary ---
install -Dm0755 "$TMP/x/bin/pillard" /usr/local/bin/pillard
echo ">> Installed /usr/local/bin/pillard"

# --- Create system user + data dir ---
if ! id -u nexus >/dev/null 2>&1; then
    useradd --system --home /var/lib/nexus --shell /usr/sbin/nologin nexus
    echo ">> Created system user 'nexus'"
fi
install -d -o nexus -g nexus -m 0700 /var/lib/nexus

# --- Optional systemd service ---
if [ "$NO_SERVICE" != "1" ] && [ -d /etc/systemd/system ]; then
    UNIT_SRC=""
    if [ -f "$TMP/x/scripts/nexus-pillar.service" ]; then
        UNIT_SRC="$TMP/x/scripts/nexus-pillar.service"
    else
        # Fallback: fetch the unit straight from the repo.
        echo ">> Fetching nexus-pillar.service from main"
        curl -fsSL "https://raw.githubusercontent.com/$REPO/main/scripts/nexus-pillar.service" \
            -o "$TMP/nexus-pillar.service"
        UNIT_SRC="$TMP/nexus-pillar.service"
    fi

    install -m0644 "$UNIT_SRC" /etc/systemd/system/nexus-pillar.service

    if [ "$PORT" != "4242" ]; then
        # Patch the port in the installed unit.
        sed -i "s|-p 4242|-p $PORT|" /etc/systemd/system/nexus-pillar.service
    fi

    systemctl daemon-reload
    systemctl enable --now nexus-pillar.service
    sleep 1
    systemctl --no-pager status nexus-pillar.service | head -15 || true
    echo
    echo ">> Pillar is up. Tail logs with:"
    echo "     journalctl -u nexus-pillar -f"
else
    echo ">> Skipped systemd setup (PILLAR_NO_SERVICE=$NO_SERVICE)"
    echo "   Run manually with:  pillard -f -p $PORT"
fi

echo
echo ">> Open inbound TCP/$PORT in your firewall and cloud security group"
echo "   so phones / nodes can dial in. UDP multicast (LAN-only) needs no"
echo "   public exposure."
