"""
PlatformIO pre-build script: symlinks all NEXUS sources into src/
so PlatformIO compiles them alongside main.cpp.
"""
Import("env")
import os, shutil

project_dir = env.get("PROJECT_DIR")
repo_root = os.path.abspath(os.path.join(project_dir, "..", ".."))
common_dir = os.path.join(project_dir, "..", "common")
src_dir = os.path.join(project_dir, "src")

# libnexus C sources
c_sources = [
    "lib/src/identity.c",
    "lib/src/packet.c",
    "lib/src/crypto.c",
    "lib/src/transport.c",
    "lib/src/announce.c",
    "lib/src/route.c",
    "lib/src/fragment.c",
    "lib/src/anchor.c",
    "lib/src/session.c",
    "lib/src/group.c",
    "lib/src/node.c",
    "lib/vendor/monocypher/monocypher.c",
    "lib/vendor/monocypher/monocypher.h",
    "transports/lora/nexus_lora.c",
    "transports/lora/nexus_lora_asf.c",
    "transports/tcp/nexus_tcp_inet.c",
    "transports/udp/nexus_udp_multicast.c",
]

# Detect platform from build flags
build_flags = " ".join(str(f) for f in env.get("BUILD_FLAGS", []))
if "NX_PLATFORM_ESP32" in build_flags:
    c_sources.append("lib/src/platform/platform_esp32.c")
elif "NX_PLATFORM_NRF52" in build_flags:
    c_sources.append("lib/src/platform/platform_nrf52_arduino.c")

def link_file(src_path, dst_path):
    """Create symlink, or copy if symlinks not supported."""
    if os.path.exists(dst_path) or os.path.islink(dst_path):
        os.remove(dst_path)
    try:
        os.symlink(os.path.abspath(src_path), dst_path)
    except OSError:
        shutil.copy2(src_path, dst_path)

# Symlink all C sources into src/
for src in c_sources:
    link_file(
        os.path.join(repo_root, src),
        os.path.join(src_dir, os.path.basename(src))
    )

# Symlink common C++ and header files into src/
for f in os.listdir(common_dir):
    if f.endswith((".cpp", ".h")) and f != "link_sources.py":
        link_file(
            os.path.join(common_dir, f),
            os.path.join(src_dir, f)
        )
