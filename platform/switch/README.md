# Switch platform port (libnx homebrew)

Unofficial Godot 4.7 port to the Nintendo Switch using devkitPro's libnx
toolchain. Rendering uses the **gl_compatibility** (GLES3) renderer through
the Mesa/nouveau OpenGL ES driver shipped in devkitPro portlibs. A Vulkan
(NVK) backend can be added later once Mesa NVK support lands for Horizon.

This is a homebrew port; it runs on consoles with custom firmware via
hbmenu/nxlink and is not affiliated with or endorsed by Nintendo.

## Requirements

- devkitPro with the `switch-dev` group installed (`DEVKITPRO` must be set,
  with `devkitA64` and `libnx` present).
- Mesa GLES/EGL portlibs (`switch-mesa`, `switch-libdrm_nouveau`), normally
  pulled in by `switch-dev`.

## Building

```sh
scons platform=switch target=template_debug
scons platform=switch target=template_release
```

Outputs in `bin/`:

- `godot.switch.<target>.arm64` — the ELF (useful for `nxlink -s` debugging).
- `godot.switch.<target>.arm64.nro` — the homebrew executable.

Options:

- `nxlink_stdio=yes|no` (default yes) — redirect stdout/stderr to the nxlink
  host. Build templates with `nxlink_stdio=no` for release distribution.

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
   scons platform=switch target=template_debug nxlink_stdio=yes
   scons platform=switch target=template_release nxlink_stdio=no
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

The port only registers the `opengl3` rendering driver and forces
`--rendering-method gl_compatibility` unless one is passed explicitly, so
projects do not need to be pre-configured for the compatibility renderer
(though textures must be imported with a format the GPU supports — S3TC/BPTC
work via Mesa on the Tegra X1; ETC2 is decompressed in software by Mesa).

Typical homebrew layout on the SD card:

```
sdmc:/switch/mygame/godot.nro   (renamed template .nro)
sdmc:/switch/mygame/game.pck
```

Launch via hbmenu, passing the pack with the usual Godot arguments in the
`.nro` hbmenu config, or run over nxlink during development:

```sh
nxlink -s bin/godot.switch.template_debug.arm64.nro --args --main-pack sdmc:/switch/mygame/game.pck
```

User data (`user://`) is stored under `sdmc:/switch/godot/app_userdata/<name>`.

## Implementation notes

- `OS_Switch` implements `OS` directly (newlib lacks `dlfcn.h`, `sys/mman.h`,
  `getifaddrs()`, so `OS_Unix` is not usable), reusing the unix FileAccess /
  DirAccess / NetSocket / IP drivers, which were given `HORIZON_ENABLED`
  guards.
- `DisplayServerSwitch` owns the EGL display/surface/context on the default
  `NWindow` (`eglGetDisplay(EGL_DEFAULT_DISPLAY)` — Mesa's platform-extension
  path is not available on Horizon, so `EGLManager` is bypassed) and calls
  `RasterizerGLES3::make_current(false)` (native GLES, not GL-over-GLES).
  Touch input is polled from HID each frame.
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

- Software keyboard (swkbd) is not wired up yet (`virtual_keyboard_show`).
- Docked/handheld mode switches do not resize the window at runtime yet
  (the EGL surface size is fixed at startup).
- Vulkan/NVK rendering driver pending upstream Mesa support.
