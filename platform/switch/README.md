# Switch platform port (libnx homebrew)

Unofficial Godot 4.7 port to the Nintendo Switch using devkitPro's libnx
toolchain. All three rendering methods work, on one statically linked Mesa
graphics stack (the unified Mesa Horizon SDK, Mesa's drivers ported to the
Tegra X1/GM20B):

- **Forward+** (`forward_plus`) and **Mobile** (`mobile`) render through
  RenderingDevice on **Vulkan over NVK**, presenting through a
  `VK_NN_vi_surface` WSI with a zero-copy block-linear swapchain on the libnx
  `nwindow`.
- **Compatibility** (`gl_compatibility`) renders GLES3 through the SDK's EGL
  stack, backed by **zink** (GL over the same NVK driver) by default; the
  native `nvc0` gallium driver can be selected with
  `MESA_SWITCH_GL_DRIVER=nouveau`.

Everything is linked statically (Horizon has no `dlopen`, no Vulkan loader,
no GL dispatcher); the project's `rendering/renderer/rendering_method`
setting picks the renderer at boot.

This is a homebrew port; it runs on consoles with custom firmware via
hbmenu/nxlink and is not affiliated with or endorsed by Nintendo.

## Requirements

- devkitPro with the `switch-dev` group installed (`DEVKITPRO` must be set,
  with `devkitA64` and `libnx` present).
- The **unified Mesa Horizon SDK** prefix (`lib/libvulkan.a`, `lib/libEGL.a`,
  `lib/libGLESv2.a`, the mesa util archives and `include/`). Point
  `mesa_sdk_path=` (or `SWITCH_MESA_SDK`) at it; the default is the
  `mesa-26.2.1-switch-unified-horizon-sdk/opt/devkitpro/portlibs/switch`
  checkout in this workspace.
- Stock portlibs: `switch-expat` (Mesa's drirc/xmlconfig), `switch-zlib` and
  `switch-zstd` (shader disk cache).

## Building

```sh
scons platform=switch target=template_debug
scons platform=switch target=template_release
```

Outputs in `bin/`:

- `godot.switch.<target>.arm64` — the ELF (useful for symbolizing crash logs).
- `godot.switch.<target>.arm64.nro` — the homebrew executable.

Engine output is written to a buffered log at `sdmc:/godot_boot.log` (fetch it
with an FTP homebrew, e.g. ftpd). Errors flush immediately; run with
`--verbose` to flush every line. On a crash, `sdmc:/godot_crash.log` gets
registers and a backtrace, and the boot log is flushed first. Debug builds
also mirror engine output to the kernel debug log (`svcOutputDebugString`),
visible in emulators and debuggers.

## Exporting from the editor

The editor integration ships as a **GDScript addon**, not as compiled-in editor
code — so it works in a **stock, official Godot editor** (4.3+, which is when
`EditorExportPlatformExtension` was added). There is no need to build a custom
editor. See [`addons/switch_export/`](../../../addons/switch_export/) at the repo
root and its README for the addon itself.

To export, you need three things:

1. **The addon**, copied into your project's `res://addons/switch_export/` and
   enabled in **Project → Project Settings → Plugins**. It then adds "Switch" to
   **Project → Export → Add…**.

2. **Export templates** — the `.nro` built from this fork. These *must* be built
   from this fork (mainline Godot has no Switch engine); a stock editor cannot
   supply them. Build and install them under the editor's export-templates
   directory with the names the addon looks for:

   ```sh
   scons platform=switch target=template_debug
   scons platform=switch target=template_release
   # then, e.g. on Linux:
   cp bin/godot.switch.template_debug.arm64.nro   ~/.local/share/godot/export_templates/<version>/switch_nro_debug.nro
   cp bin/godot.switch.template_release.arm64.nro ~/.local/share/godot/export_templates/<version>/switch_nro_release.nro
   ```

   Or just point **Custom Template → Debug/Release** in the export preset
   directly at the built `.nro` files.

3. **devkitPro tools.** The addon shells out to `build_romfs` to pack the game
   and to `nxlink` to deploy. With `DEVKITPRO` set, both default to
   `$DEVKITPRO/tools/bin/...`; override in **Editor Settings → Export → Switch**.

Export produces a **single self-contained `.nro`** with the game `.pck` embedded
in the NRO RomFS (loaded at boot as `romfs:/game.pck`). Just copy it to the SD
card and launch it from hbmenu — no separate `.pck` required.

> **Note:** loading `romfs:/game.pck` requires the engine change in
> `godot_switch.cpp`, so the **export templates must be built from a tree that
> includes it**. The addon (which runs in any editor) and the templates (which
> must be this fork) are versioned independently — only the templates carry the
> engine behavior.

### One-click deploy

Put the Switch on the **hbmenu netloader** screen (so it listens for nxlink). It
then shows up in the export dialog's device dropdown; pressing the run/deploy
button builds a fused `.nro` and netloads it over `nxlink`, forwarding the
editor's remote-debug flags so the debugger can attach.

## Running a project manually

The port registers both the `vulkan` and `opengl3` rendering drivers. The
project's `rendering/renderer/rendering_method` setting (or an explicit
`--rendering-method forward_plus|mobile|gl_compatibility` argument) selects
the renderer; `mobile` is the method sized for the Tegra X1, `forward_plus`
runs but is heavier, and `gl_compatibility` goes through GLES3/zink.
Textures must be imported with a format the GPU supports — S3TC/BPTC work on
the Tegra X1; prefer those over ETC2.

> **Memory note:** NVK needs the full application memory pool to initialize
> its GPU channel. Launch the homebrew with **title takeover** ("boot as
> application"), not from the album applet.

Typical homebrew layout on the SD card:

```
sdmc:/switch/mygame/godot.nro   (renamed template .nro)
sdmc:/switch/mygame/game.pck
```

Launch via hbmenu, passing the pack with the usual Godot arguments in the
`.nro` hbmenu config, or run over nxlink during development:

```sh
nxlink bin/godot.switch.template_debug.arm64.nro --args --main-pack sdmc:/switch/mygame/game.pck
```

User data (`user://`) is stored under `sdmc:/switch/godot/app_userdata/<name>`.

## Implementation notes

- `OS_Switch` implements `OS` directly (newlib lacks `dlfcn.h`, `sys/mman.h`,
  `getifaddrs()`, so `OS_Unix` is not usable), reusing the unix FileAccess /
  DirAccess / NetSocket / IP drivers, which were given `HORIZON_ENABLED`
  guards.
- The Vulkan stack: the SDK's `libvulkan.a` is a loaderless ICD exporting the
  full set of `vk*` entry points, and Godot links them directly
  (`use_volk=no`; volk's writable `vk*` global function pointers would
  collide with those exported trampolines at link time).
  `RenderingContextDriverVulkanSwitch` creates the surface with
  `vkCreateViSurfaceNN` on `nwindowGetDefault()`.
- Docked/handheld switches recreate the surface + swapchain Android-style
  (`screen_free` → `window_destroy` → `nwindowSetDimensions` →
  `window_create` → `screen_create`); the WSI reads the new nwindow size.
- The Compatibility path owns an EGL display/surface/context on the default
  `NWindow` and calls `RasterizerGLES3::make_current(false)`. The gallium
  backend defaults to zink (set in `godot_switch.cpp`); override with
  `MESA_SWITCH_GL_DRIVER=nouveau` for the native nvc0 driver.
- Touch input is polled from HID each frame.
- Audio uses the `audren` renderer (stereo, 48 kHz device rate, mixed at the
  project mix rate) on a dedicated thread.
- Joypads: up to 8 standard-style npads; pad 1 doubles as handheld mode.
  Buttons are mapped positionally to Godot's SDL-style layout (Switch B =
  Godot A, etc.). ZL/ZR are exposed as trigger axes.
- mbedTLS uses a custom config (`platform_mbedtls_config.h`) with entropy
  from the Horizon CSRNG (`randomGet`) and no BSD-socket layer.
- No subprocesses, no GDExtension (`dlopen` does not exist on Horizon), no
  PCRE2 JIT (no executable pages for homebrew).

## Known limitations / TODO

- The NVK WSI is FIFO-only: vsync cannot be disabled.
- First boot compiles all pipelines through NAK; Mesa's disk cache
  (`sdmc:/switch/godot`) makes subsequent boots fast.
