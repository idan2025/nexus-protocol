"""
PlatformIO pre-build script: symlinks libnexus C sources into lib/nexus_lib/
and common C++ files into src/ so PlatformIO can find them.
"""
Import("env")
import os, shutil, json

project_dir = env.get("PROJECT_DIR")
repo_root = os.path.abspath(os.path.join(project_dir, "..", ".."))
common_dir = os.path.join(project_dir, "..", "common")
lib_dir = os.path.join(project_dir, "lib", "nexus_lib")
src_dir = os.path.join(project_dir, "src")

# libnexus C sources -> lib/nexus_lib/
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
    "transports/lora/nexus_lora.c",
    "transports/lora/nexus_lora_asf.c",
]

# Add platform-specific source based on build flags
build_flags = env.get("BUILD_FLAGS", [])
flags_str = " ".join(str(f) for f in build_flags)
if "NX_PLATFORM_ESP32" in flags_str:
    c_sources.append("lib/src/platform/platform_esp32.c")
elif "NX_PLATFORM_NRF52" in flags_str:
    c_sources.append("lib/src/platform/platform_nrf52.c")

os.makedirs(lib_dir, exist_ok=True)

# library.json for PlatformIO library manager
lib_json = {
    "name": "nexus_lib",
    "version": "0.1.0",
    "build": {
        "flags": [
            "-I" + os.path.join(repo_root, "lib", "include"),
            "-I" + os.path.join(repo_root, "lib", "vendor"),
        ]
    }
}
with open(os.path.join(lib_dir, "library.json"), "w") as f:
    json.dump(lib_json, f)

def link_file(src_path, dst_path):
    if os.path.exists(dst_path) or os.path.islink(dst_path):
        os.remove(dst_path)
    try:
        os.symlink(src_path, dst_path)
    except OSError:
        shutil.copy2(src_path, dst_path)

# Symlink C sources into lib/nexus_lib/
for src in c_sources:
    link_file(
        os.path.join(repo_root, src),
        os.path.join(lib_dir, os.path.basename(src))
    )

# Symlink common C++ files into src/ (alongside main.cpp)
common_files = ["radiolib_hal.cpp", "radiolib_hal.h",
                "ble_bridge.cpp", "ble_bridge.h",
                "identity_store.cpp", "identity_store.h"]
for f in common_files:
    src_path = os.path.join(common_dir, f)
    if os.path.exists(src_path):
        link_file(src_path, os.path.join(src_dir, f))
