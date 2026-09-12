import os
import sys
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
            "mesa_sdk_path",
            "Path to the unified Mesa Horizon SDK prefix (containing lib/libvulkan.a, lib/libEGL.a and include/)",
            os.environ.get(
                "SWITCH_MESA_SDK",
                "/workspaces/godot-switch/mesa-switch-sdk/opt/devkitpro/portlibs/switch",
            ),
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
        # Forward+ and Mobile render through RenderingDevice on Vulkan over
        # the statically linked NVK driver (Mesa's open-source driver for the
        # Tegra X1 GM20B, with a VK_NN_vi_surface WSI over the libnx nwindow).
        "vulkan": True,
        # The SDK's libvulkan.a is a loaderless ICD that exports the full set
        # of vk* entry points; Godot links them directly. volk must stay off:
        # its writable vk* global function pointers would collide with those
        # exported trampolines at link time.
        "use_volk": False,
        # The Compatibility renderer is GLES3 over the SDK's EGL stack,
        # backed by zink (GL over the same NVK driver) by default.
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

    # Unified Mesa Horizon SDK: one prefix carries the whole graphics stack as
    # static archives. libvulkan.a is the self-contained NVK driver (NAK, NIL,
    # the nouveau_horizon backend and a loaderless shim exporting every vk*
    # entry point); libEGL.a carries the EGL frontend, the GL/GLES state
    # tracker and two gallium backends (zink over NVK, and the native nvc0
    # driver), presenting through the libnx nwindow.
    mesa_sdk = env["mesa_sdk_path"]
    if not os.path.isfile(os.path.join(mesa_sdk, "lib", "libvulkan.a")):
        print_error("mesa_sdk_path does not contain lib/libvulkan.a: " + mesa_sdk)
        sys.exit(255)
    env.Prepend(CPPPATH=[os.path.join(mesa_sdk, "include")])
    env.Prepend(LIBPATH=[os.path.join(mesa_sdk, "lib")])

    # Static-archive link order: the GL stack pulls symbols from libvulkan.a
    # (zink) and the mesa util archives, so those must come after it.
    mesa_libs = []

    if env["vulkan"]:
        env.Append(CPPDEFINES=["VULKAN_ENABLED", "RD_ENABLED", "VK_USE_PLATFORM_VI_NN"])

    if env["opengl3"]:
        env.Append(CPPDEFINES=["GLES3_ENABLED"])
        mesa_libs += ["EGL", "GLESv2", "glapi"]

    if env["vulkan"] or env["opengl3"]:
        mesa_libs += ["vulkan"]

    if env["opengl3"]:
        # zink (inside libEGL.a) uses Mesa's vk_format_*/vk_*_to_str helpers,
        # which the localized ozuba libvulkan.a no longer exports; take them
        # from the GL stack's own vintage of vulkan_util.
        mesa_libs += ["vulkan_util"]

    if env["opengl3"]:
        mesa_libs += ["mesa_util", "mesa_util_c11", "mesa_util_simd", "blake3"]

    if mesa_libs:
        # expat (drirc), zlib + zstd (shader disk cache) come from the stock
        # devkitPro portlibs.
        env.Append(LIBS=mesa_libs + ["expat", "z", "zstd"])

    env.Append(LIBS=["nx", "m"])
