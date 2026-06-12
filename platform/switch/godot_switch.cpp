/**************************************************************************/
/*  godot_switch.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "os_switch.h"
#include "switch_logger.h"
#include "switch_wrapper.h"

#include "main/main.h"

#include <climits>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>

// Show a message through the system error applet, so startup failures are
// visible on screen. The tail of the boot log is appended so the actual
// engine error is readable on screen.
static void show_error_applet(const char *p_message) {
	char details[1900];
	int written = snprintf(details, sizeof(details), "%s\n--- log tail ---\n", p_message);

	switch_log_flush();
	FILE *log = fopen("sdmc:/godot_boot.log", "rb");
	if (log && written > 0 && (size_t)written < sizeof(details) - 1) {
		fseek(log, 0, SEEK_END);
		long size = ftell(log);
		long avail = (long)(sizeof(details) - 1 - written);
		long start = size > avail ? size - avail : 0;
		fseek(log, start, SEEK_SET);
		size_t n = fread(details + written, 1, avail, log);
		details[written + n] = '\0';
	}
	if (log) {
		fclose(log);
	}

	ErrorSystemConfig config;
	errorSystemCreate(&config, "Godot failed to start", details);
	errorSystemShow(&config);
}

// Route raw stdout/stderr (entry-point prints, thirdparty libraries) into the
// shared buffered boot log. Engine output goes there directly through
// SwitchLogger; neither path syncs the SD card per write.
static ssize_t log_write(struct _reent *r, void *fd, const char *ptr, size_t len) {
	FILE *log = switch_log_get_file();
	if (log) {
		fwrite(ptr, 1, len, log);
	}
	return len;
}

static const devoptab_t log_devoptab = {
	.name = "log",
	.write_r = log_write,
};

static void setup_stdio() {
	devoptab_list[STD_OUT] = &log_devoptab;
	devoptab_list[STD_ERR] = &log_devoptab;
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
}

// NVK runtime environment; must run before Main::setup brings up the display.
static void setup_nvk_env() {
	// Upstream NVK only exposes Turing+ GPUs; the Tegra X1's GM20B (Maxwell)
	// is gated behind this opt-in. Without it vkEnumeratePhysicalDevices
	// reports zero devices.
	setenv("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);

	// The Switch uses a freestanding Rust target where std::thread bypasses libnx's
	// pthread_create and calls svcCreateThread directly. This leaves tpidr_el0 (TLS)
	// uninitialized, causing a crash when NAK tries to use thread_local variables.
	// Force NAK/Mesa to run single-threaded on the current thread (which has valid TLS).
	setenv("RAYON_NUM_THREADS", "1", 1);
	setenv("MESA_SHADER_COMPILER_THREADS", "1", 1);

#ifdef DEBUG_ENABLED
	// GPU-hang bring-up aid: make NVK sync after EVERY queue submit and, when
	// one fails, dump that command buffer to stderr as decoded methods
	// (nvk_queue.c push_sync + nvk_cmd_buffer_dump). Serializes submission, so
	// it costs performance; remove once the renderer is stable on hardware.
	//setenv("NVK_DEBUG", "push_sync", 1);
	// Hang-point bisection: the drm shim splits each push into chunks of this
	// many dwords at method boundaries with a syncpoint increment in between,
	// so the drain diagnostic's `current` value names the exact chunk (= dword
	// range in the push_sync dump) the GPU died in. 64 dwords ≈ a dozen
	// methods per chunk.
	//setenv("DRM_SHIM_BISECT", "64", 1);
#endif

	// Point Mesa's on-disk shader cache at the SD card. Without a writable
	// HOME/XDG dir, Mesa keeps its cache disabled on Horizon and NVK's NAK
	// compiler recompiles every pipeline shader from SPIR-V each boot.
	mkdir("sdmc:/switch/godot", 0777);
	setenv("MESA_SHADER_CACHE_DIR", "sdmc:/switch/godot", 1);
	setenv("MESA_SHADER_CACHE_MAX_SIZE", "256M", 1);
}

int main(int argc, char *argv[]) {
	socketInitializeDefault();
	setup_stdio();
	
	setup_nvk_env();
	Result romfs_res = romfsInit();
	printf("godot_switch: boot, applet_type=%d romfs=0x%x\n", (int)appletGetAppletType(), (unsigned int)romfs_res);

	OS_Switch os;
	if (argc > 0) {
		os.set_executable_path(argv[0]);
	}

	setlocale(LC_CTYPE, "");

	// Force the Forward Mobile renderer on Vulkan/NVK unless the user passed
	// an explicit choice; it is the rendering method sized for the Tegra X1.
	// (Forward+ also runs on NVK and can be requested with
	// --rendering-method forward_plus.)
	List<String> args;
	bool has_rendering_method = false;
	bool has_main_pack = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--rendering-method") == 0) {
			has_rendering_method = true;
		}
		if (strcmp(argv[i], "--main-pack") == 0) {
			has_main_pack = true;
		}
		args.push_back(String::utf8(argv[i]));
	}
	if (!has_rendering_method) {
		args.push_back("--rendering-method");
		args.push_back("mobile");
	}

#ifdef DEBUG_ENABLED
	// Verbose stdout makes Godot register a VK_EXT_debug_utils messenger,
	// which is the only way Mesa's vk_errorf() diagnostics (e.g. the precise
	// reason a vkCreateDevice fails) reach the console on a release Mesa
	// build; without it they are silently dropped.
	//.push_back("--verbose");
	// Sync after every breadcrumb so a GPU hang names the exact render pass
	// instead of a window of them (pairs with NVK_DEBUG=push_sync above).
	//args.push_back("--accurate-breadcrumbs");
#endif

	// Fused build: the editor's Switch exporter embeds the game pack in the
	// NRO's RomFS as romfs:/game.pck. When RomFS mounted and no pack was given
	// explicitly, load it so a single self-contained .nro just works.
	if (R_SUCCEEDED(romfs_res) && !has_main_pack) {
		FILE *pack = fopen("romfs:/game.pck", "rb");
		if (pack) {
			fclose(pack);
			args.push_back("--main-pack");
			args.push_back("romfs:/game.pck");
		}
	}

	// The game pack is expected next to the executable with the same
	// basename (e.g. sdmc:/switch/game.nro + sdmc:/switch/game.pck), which
	// ProjectSettings::setup() resolves from the executable path.
	printf("godot_switch: argv[0]=%s\n", argc > 0 ? argv[0] : "(none)");

	int final_argc = args.size();
	char **final_argv = (char **)malloc(sizeof(char *) * final_argc);
	int i = 0;
	for (const String &arg : args) {
		final_argv[i++] = strdup(arg.utf8().get_data());
	}

	printf("godot_switch: calling Main::setup\n");
	Error err = Main::setup(argv[0], final_argc, final_argv);

	if (err != OK) {
		printf("godot_switch: Main::setup failed, error %d\n", (int)err);
		if (err != ERR_HELP) {
			char msg[256];
			snprintf(msg, sizeof(msg), "Main::setup failed with error %d.\nSee sdmc:/godot_boot.log for the full engine output.", (int)err);
			show_error_applet(msg);
		}
		romfsExit();
		socketExit();
		if (err == ERR_HELP) {
			return EXIT_SUCCESS;
		}
		return EXIT_FAILURE;
	}

	printf("godot_switch: Main::setup OK, calling Main::start\n");
	if (Main::start() == EXIT_SUCCESS) {
		os.run();
	} else {
		printf("godot_switch: Main::start failed\n");
		show_error_applet("Main::start failed.\nSee sdmc:/godot_boot.log for the full engine output.");
		os.set_exit_code(EXIT_FAILURE);
	}
	Main::cleanup();

	for (int j = 0; j < final_argc; j++) {
		free(final_argv[j]);
	}
	free(final_argv);

	romfsExit();
	socketExit();
	switch_log_flush();
	return os.get_exit_code();
}
