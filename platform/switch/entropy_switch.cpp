/**************************************************************************/
/*  entropy_switch.cpp                                                    */
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

#include "switch_wrapper.h"

#include <cstddef>
#include <cstdint>
#include <ctime>

// Entropy source for mbedTLS (MBEDTLS_ENTROPY_HARDWARE_ALT), backed by the
// Horizon CSRNG service. Only referenced when the mbedtls module is enabled.
extern "C" int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
	(void)data;
	randomGet(output, len);
	*olen = len;
	return 0;
}

// newlib declares posix_memalign() but does not implement it; astcenc and
// other thirdparty code link against it.
#include <cerrno>
#include <malloc.h>

extern "C" int posix_memalign(void **memptr, size_t alignment, size_t size) {
	if (alignment % sizeof(void *) != 0 || (alignment & (alignment - 1)) != 0) {
		return EINVAL;
	}
	void *mem = memalign(alignment, size);
	if (!mem) {
		return ENOMEM;
	}
	*memptr = mem;
	return 0;
}

// Monotonic millisecond clock for mbedTLS (MBEDTLS_PLATFORM_MS_TIME_ALT).
extern "C" int64_t mbedtls_ms_time(void) {
	struct timespec tv;
	if (clock_gettime(CLOCK_MONOTONIC, &tv) != 0) {
		return (int64_t)time(nullptr) * 1000;
	}
	return (int64_t)tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}
