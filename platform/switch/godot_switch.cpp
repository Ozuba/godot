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
#include "switch_wrapper.h"

#include "main/main.h"

#include <climits>
#include <clocale>
#include <cstdlib>
#include <unistd.h>

int main(int argc, char *argv[]) {
	socketInitializeDefault();
#ifdef NXLINK_STDIO_ENABLED
	nxlinkStdio();
#endif
	romfsInit();

	OS_Switch os;
	if (argc > 0) {
		os.set_executable_path(argv[0]);
	}

	setlocale(LC_CTYPE, "");

	// Force the GL compatibility renderer unless the user passed an explicit
	// choice; the Switch port only registers the opengl3 driver.
	List<String> args;
	bool has_rendering_method = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--rendering-method") == 0) {
			has_rendering_method = true;
		}
		args.push_back(String::utf8(argv[i]));
	}
	if (!has_rendering_method) {
		args.push_back("--rendering-method");
		args.push_back("gl_compatibility");
	}

	int final_argc = args.size();
	char **final_argv = (char **)malloc(sizeof(char *) * final_argc);
	int i = 0;
	for (const String &arg : args) {
		final_argv[i++] = strdup(arg.utf8().get_data());
	}

	Error err = Main::setup(argv[0], final_argc, final_argv);

	if (err != OK) {
		romfsExit();
		socketExit();
		if (err == ERR_HELP) {
			return EXIT_SUCCESS;
		}
		return EXIT_FAILURE;
	}

	if (Main::start() == EXIT_SUCCESS) {
		os.run();
	} else {
		os.set_exit_code(EXIT_FAILURE);
	}
	Main::cleanup();

	for (int j = 0; j < final_argc; j++) {
		free(final_argv[j]);
	}
	free(final_argv);

	romfsExit();
	socketExit();
	return os.get_exit_code();
}
