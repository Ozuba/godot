/**************************************************************************/
/*  platform_mbedtls_config.h                                             */
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

#pragma once

// mbedTLS configuration for the Horizon OS (Nintendo Switch homebrew).
// Mirrors the defaults from godot_module_mbedtls_config.h, plus entropy
// sourced from the system CSRNG since there is no /dev/urandom.

#include <mbedtls/mbedtls_config.h>

// Disable weak cryptography.
#undef MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED
#undef MBEDTLS_DES_C
#undef MBEDTLS_DHM_C

#ifdef THREADS_ENABLED
// In mbedTLS 3, the PSA subsystem has an implicit shared context, MBEDTLS_THREADING_C is required to make it thread safe.
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT
#define GODOT_MBEDTLS_THREADING_ALT
#endif

// ARMv8 hardware AES operations; runtime detection is not possible on Horizon.
#undef MBEDTLS_AESCE_C

// No POSIX entropy sources; use the hardware poll backed by randomGet().
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

// Godot uses its own sockets; mbedTLS's BSD socket layer does not build on Horizon.
#undef MBEDTLS_NET_C

// newlib does not advertise _POSIX_VERSION; provide mbedtls_ms_time() ourselves.
#define MBEDTLS_PLATFORM_MS_TIME_ALT


// Disable deprecated
#define MBEDTLS_DEPRECATED_REMOVED
