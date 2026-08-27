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

#include <atomic>
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

// Route raw stdout/stderr (entry-point prints, thirdparty libraries) into a
// SEPARATE log from the engine's (sdmc:/godot_stdout.log vs godot_boot.log),
// so garbage on one stream immediately attributes to its producer. The write
// path is serialized with a TLS-free spinlock (a thread with broken tpidr --
// e.g. a stray Rust std::thread -- cannot take a libnx mutex) and each write
// is length-capped: a wild "%s" on a bad pointer otherwise floods the card
// with gigabytes of memory contents.
static ssize_t log_write(struct _reent *r, void *fd, const char *ptr, size_t len) {
	static FILE *out_file = nullptr;
	static char out_buffer[16 * 1024];
	static std::atomic_flag out_lock = ATOMIC_FLAG_INIT;

	while (out_lock.test_and_set(std::memory_order_acquire)) {
	}
	if (!out_file) {
		out_file = fopen("sdmc:/godot_stdout.log", "w");
		if (out_file) {
			setvbuf(out_file, out_buffer, _IOFBF, sizeof(out_buffer));
		}
	}
	if (out_file) {
		size_t n = len;
		if (n > 64 * 1024) {
			fprintf(out_file, "\n[log_write: absurd len=%zu, truncating]\n", len);
			n = 64 * 1024;
		}
		fwrite(ptr, 1, n, out_file);
		fflush(out_file);
	}
	out_lock.clear(std::memory_order_release);
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

// Mesa/NVK runtime environment; must run before Main::setup brings up the
// display. Applies to all three rendering methods: Forward+ and Mobile talk
// to NVK directly, the Compatibility renderer reaches it through zink.
static void setup_nvk_env() {
	// The Compatibility renderer runs GLES3 through Mesa's EGL stack; default
	// its gallium backend to zink so every rendering method shares the one
	// NVK driver. The native nvc0 gallium driver remains available with
	// MESA_SWITCH_GL_DRIVER=nouveau (no overwrite: a user-set value wins).
	setenv("MESA_SWITCH_GL_DRIVER", "zink", 0);

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

	// Nothing on Horizon manages comptags, so framebuffer compression must
	// stay off on GM20B. The ozuba driver already reports
	// has_compression = false; keep the debug flag as a guard so a driver
	// swap (e.g. back to a 26.2.1 libvulkan.a, which force-enabled it and
	// wedged the first 3D opaque pass) cannot silently re-enable it.
	setenv("NVK_DEBUG", "no_compression", 1);

	// Point Mesa's on-disk shader cache at the SD card. Without a writable
	// HOME/XDG dir, Mesa keeps its cache disabled on Horizon and NVK's NAK
	// compiler recompiles every pipeline shader from SPIR-V each boot.
	mkdir("sdmc:/switch/godot", 0777);
	setenv("MESA_SHADER_CACHE_DIR", "sdmc:/switch/godot", 1);
	setenv("MESA_SHADER_CACHE_MAX_SIZE", "256M", 1);
}

// Optional per-run overrides read from sdmc:/godot_env.cfg, so driver knobs
// (NVK_DEBUG=..., MESA_*=...) and engine arguments (--rendering-method ...)
// can be flipped between runs over FTP without rebuilding the NRO. NAME=VALUE
// lines override the environment set above; lines starting with "--" are
// appended to the engine argument list; '#' starts a comment.
static List<String> env_cfg_args;

static void apply_env_cfg() {
	FILE *f = fopen("sdmc:/godot_env.cfg", "rb");
	if (!f) {
		return;
	}
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *s = line;
		while (*s == ' ' || *s == '\t') {
			s++;
		}
		char *end = s + strlen(s);
		while (end > s && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) {
			*--end = '\0';
		}
		if (*s == '\0' || *s == '#') {
			continue;
		}
		if (s[0] == '-' && s[1] == '-') {
			// Argument line; a space separates an argument from its value.
			char *sp = strchr(s, ' ');
			if (sp) {
				*sp = '\0';
				env_cfg_args.push_back(String::utf8(s));
				env_cfg_args.push_back(String::utf8(sp + 1));
			} else {
				env_cfg_args.push_back(String::utf8(s));
			}
			printf("godot_env.cfg: arg %s\n", s);
		} else {
			char *eq = strchr(s, '=');
			if (eq) {
				*eq = '\0';
				setenv(s, eq + 1, 1);
				printf("godot_env.cfg: setenv %s=%s\n", s, eq + 1);
			}
		}
	}
	fclose(f);
}

int main(int argc, char *argv[]) {
	socketInitializeDefault();
	setup_stdio();
	
	setup_nvk_env();
	apply_env_cfg();
	Result romfs_res = romfsInit();
	printf("godot_switch: boot, applet_type=%d romfs=0x%x\n", (int)appletGetAppletType(), (unsigned int)romfs_res);

	OS_Switch os;
	if (argc > 0) {
		os.set_executable_path(argv[0]);
	}

	setlocale(LC_CTYPE, "");

	// All three rendering methods work on this driver stack: forward_plus and
	// mobile over Vulkan/NVK, gl_compatibility over GLES3/zink. The project's
	// rendering/renderer/rendering_method setting (or an explicit
	// --rendering-method argument) picks one; nothing is forced here so the
	// exported project's choice is respected.
	List<String> args;
	bool has_main_pack = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--main-pack") == 0) {
			has_main_pack = true;
		}
		args.push_back(String::utf8(argv[i]));
	}
	// Per-run overrides from sdmc:/godot_env.cfg (see apply_env_cfg). Later
	// arguments win, so these override both argv and the project settings.
	for (const String &arg : env_cfg_args) {
		args.push_back(arg);
	}

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
