"""
PlatformIO pre-build script: symlinks all NEXUS sources into src/
so PlatformIO compiles them alongside main.cpp.
Also installs custom board variants into the Adafruit BSP if needed.
"""
Import("env")
import os, shutil

project_dir = env.get("PROJECT_DIR")
repo_root = os.path.abspath(os.path.join(project_dir, "..", ".."))
common_dir = os.path.join(project_dir, "..", "common")
src_dir = os.path.join(project_dir, "src")

# Install custom variant into Adafruit BSP if project has one
local_variant_dir = os.path.join(project_dir, "variant")
if os.path.isdir(local_variant_dir):
    variant_name = env.BoardConfig().get("build.variant", "")
    if variant_name:
        framework_dir = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")
        bsp_variant_dir = os.path.join(framework_dir, "variants", variant_name)
        if not os.path.exists(bsp_variant_dir):
            try:
                os.symlink(os.path.abspath(local_variant_dir), bsp_variant_dir)
                print("Installed variant '%s' -> %s" % (variant_name, bsp_variant_dir))
            except OSError:
                shutil.copytree(local_variant_dir, bsp_variant_dir)
                print("Copied variant '%s' -> %s" % (variant_name, bsp_variant_dir))
        # Also install custom linker scripts into BSP linker directory
        linker_dir = os.path.join(framework_dir, "cores", "nRF5", "linker")
        for f in os.listdir(local_variant_dir):
            if f.endswith(".ld"):
                ld_src = os.path.join(local_variant_dir, f)
                ld_dst = os.path.join(linker_dir, f)
                if not os.path.exists(ld_dst):
                    try:
                        os.symlink(os.path.abspath(ld_src), ld_dst)
                    except OSError:
                        shutil.copy2(ld_src, ld_dst)
                    print("Installed linker script '%s'" % f)

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
