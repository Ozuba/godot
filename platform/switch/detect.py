import os
from typing import TYPE_CHECKING

from methods import print_error
from platform_methods import validate_arch

if TYPE_CHECKING:
    from SCons.Script.SConscript import SConsEnvironment


def get_name():
    return "Switch"


def can_build():
    if "DEVKITPRO" not in os.environ:
        return False

    dkp = os.environ["DEVKITPRO"]

    if not os.path.exists(os.path.join(dkp, "devkitA64")):
        print_error("devkitA64 not found in DEVKITPRO. Switch platform disabled.")
        return False

    if not os.path.exists(os.path.join(dkp, "libnx")):
        print_error("libnx not found in DEVKITPRO. Switch platform disabled.")
        return False

    return True


def get_opts():
    return [
        (
            "switch_nvk_path",
            "Path to the switch-nvk package (containing lib/libvulkan.a and include/vulkan)",
            os.environ.get("SWITCH_NVK_PATH", ""),
        ),
    ]


def get_doc_classes():
    return []


def get_doc_path():
    return "doc_classes"


def get_flags():
    return {
        "arch": "arm64",
        "target": "template_debug",
        # The renderer is Vulkan over the statically linked NVK driver from the
        # switch-nvk project (Mesa's open-source driver for the Tegra X1 GM20B,
        # with a VK_NN_vi_surface WSI over the libnx nwindow).
        "vulkan": True,
        "use_volk": True,
        # DEPRECATED: the old GLES3 compatibility renderer over the Mesa
        # portlibs (libEGL/libGLESv2/libglapi). Build with opengl3=yes to get
        # it back; it is scheduled for removal.
        "opengl3": False,
        "sdl": False,
        "accesskit": False,
        # PCRE2 JIT works through libnx jitCreate() (CodeMemory backend);
        # it degrades gracefully to the interpreter when unavailable.
        "builtin_pcre2_with_jit": True,
        # Embree is heavy and not ported to Horizon.
        "module_raycast_enabled": False,
        # miniupnpc requires getifaddrs() and other APIs missing from newlib.
        "module_upnp_enabled": False,
        # No camera or MIDI access.
        "module_camera_enabled": False,
        # Enable OVERRIDE_PATH_ENABLED even in export templates so the entry
        # point can pass `--main-pack romfs:/game.pck` for fused single-file
        # .nro builds. Without this, --main-pack aborts Main::setup with
        # ERR_INVALID_PARAMETER (homebrew has no other way to point at the pack).
        "disable_path_overrides": False,
        "supported": [],
    }


def configure(env: "SConsEnvironment"):
    # Validate arch.
    supported_arches = ["arm64"]
    validate_arch(env["arch"], get_name(), supported_arches)

    dkp = os.environ["DEVKITPRO"]
    env["ENV"]["DEVKITPRO"] = dkp

    tool_prefix = os.path.join(dkp, "devkitA64", "bin", "aarch64-none-elf-")

    env["CC"] = tool_prefix + "gcc"
    env["CXX"] = tool_prefix + "g++"
    env["LINK"] = tool_prefix + "g++"
    env["AR"] = tool_prefix + "gcc-ar"
    env["RANLIB"] = tool_prefix + "gcc-ranlib"
    env["AS"] = tool_prefix + "as"
    env["OBJCOPY"] = tool_prefix + "objcopy"
    env["STRIP"] = tool_prefix + "strip"

    env["ENV"]["PATH"] = os.path.join(dkp, "devkitA64", "bin") + os.pathsep + os.environ["PATH"]

    # Standard devkitA64 flags for Horizon userland (see ${DEVKITPRO}/switchvars.sh).
    arch_flags = [
        "-march=armv8-a+crc+crypto",
        "-mtune=cortex-a57",
        "-mtp=soft",
        "-fPIE",
        "-ftls-model=local-exec",
    ]

    env.Append(CCFLAGS=arch_flags + ["-ffunction-sections", "-fdata-sections"])

    env.Append(
        CPPDEFINES=[
            "__SWITCH__",
            "HORIZON_ENABLED",
            "POSH_COMPILER_GCC",
            "POSH_OS_HORIZON",
            ("POSH_OS_STRING", '\\"horizon\\"'),
            # newlib has no pthread_setname_np().
            "PTHREAD_NO_RENAME",
        ]
    )

    env.Append(CPPPATH=["#platform/switch"])
    env.Prepend(CPPPATH=[os.path.join(dkp, "portlibs", "switch", "include")])
    env.Append(CPPFLAGS=["-isystem", os.path.join(dkp, "libnx", "include")])

    env.Append(LINKFLAGS=arch_flags + ["-specs=" + os.path.join(dkp, "libnx", "switch.specs"), "-Wl,--gc-sections"])
    env.Append(
        LIBPATH=[
            os.path.join(dkp, "portlibs", "switch", "lib"),
            os.path.join(dkp, "libnx", "lib"),
        ]
    )

    if env["vulkan"]:
        # NVK (Mesa's open-source Vulkan driver) ported to the Tegra X1 by the
        # Bypass switch-nvk and use externally built Vulkan
        env.Append(LIBPATH=["/workspaces/godot-switch/builddir-switch/builddir-switch/src/nouveau/vulkan"])
        env.Append(CPPPATH=["/workspaces/godot-switch/builddir-switch/builddir-switch/include"])
        env.Append(CPPDEFINES=["VULKAN_ENABLED", "RD_ENABLED", "VK_USE_PLATFORM_VI_NN"])
        
        # Link normal vulkan, but force whole-archive only on nak_rs to preserve its TLS sections
        nak_rs_path = "/workspaces/godot-switch/builddir-switch/builddir-switch/src/nouveau/compiler/libnak_rs.a"
        env.Append(LINKFLAGS=["-Wl,--whole-archive", nak_rs_path, "-Wl,--no-whole-archive"])
        env.Append(LIBS=["vulkan", "expat"])

    if env["opengl3"]:
        # DEPRECATED: GLES3 compatibility renderer over the Mesa GL portlibs.
        print("WARNING: the GLES3/EGL renderer on the Mesa portlibs is deprecated; the supported renderer is Vulkan (NVK).")
        env.Append(CPPDEFINES=["GLES3_ENABLED"])
        env.Append(LIBS=["EGL", "GLESv2", "glapi", "drm_nouveau"])

    env.Append(LIBS=["nx", "m"])
