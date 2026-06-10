/**************************************************************************/
/*  switch_logger.cpp                                                     */
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

#include "switch_logger.h"

#include "switch_wrapper.h"

#include <cstdarg>

static FILE *log_file = nullptr;
static char log_buffer[64 * 1024];

FILE *switch_log_get_file() {
	if (!log_file) {
		log_file = fopen("sdmc:/godot_boot.log", "w");
		if (log_file) {
			setvbuf(log_file, log_buffer, _IOFBF, sizeof(log_buffer));
		}
	}
	return log_file;
}

void switch_log_flush() {
	if (log_file) {
		fflush(log_file);
	}
}

void SwitchLogger::logv(const char *p_format, va_list p_list, bool p_err) {
	if (!should_log(p_err)) {
		return;
	}

	FILE *file = switch_log_get_file();
	if (file) {
		va_list list_copy;
		va_copy(list_copy, p_list);
		vfprintf(file, p_format, list_copy);
		va_end(list_copy);

		if (p_err || _flush_stdout_on_print) {
			fflush(file);
		}
	}

#ifdef DEBUG_ENABLED
	char line[2048];
	int len = vsnprintf(line, sizeof(line), p_format, p_list);
	if (len > 0) {
		svcOutputDebugString(line, (size_t)len < sizeof(line) ? (size_t)len : sizeof(line) - 1);
	}
#endif
}
