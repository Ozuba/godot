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
    from SCons.Variables import BoolVariable

    return [
        BoolVariable("nxlink_stdio", "Redirect stdout/stderr over nxlink for debugging", True),
    ]


def get_doc_classes():
    return []


def get_doc_path():
    return "doc_classes"


def get_flags():
    return {
        "arch": "arm64",
        "target": "template_debug",
        "vulkan": False,
        "opengl3": True,
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

    if env["nxlink_stdio"]:
        env.Append(CPPDEFINES=["NXLINK_STDIO_ENABLED"])

    if env["opengl3"]:
        env.Append(CPPDEFINES=["GLES3_ENABLED"])
        env.Append(LIBS=["EGL", "GLESv2", "glapi", "drm_nouveau"])

    env.Append(LIBS=["nx", "m"])
